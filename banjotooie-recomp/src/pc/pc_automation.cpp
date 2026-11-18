#include "pc_automation.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <psapi.h>

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/input/test_gamepad.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include "pc_ingame_menu.h"
#include "pc_achievements.h"
#include "pc_display.h"
#include "pc_lang.h"
#include "pc_log.h"
#include "pc_menu.h"
#include "pc_progress.h"
#include "pc_save_format.h"
#include "pc_saves.h"
#include "pc_settings.h"
#include "pc_state.h"
#include "mechanics_values.h"
#include "png_write.h"
#include "scripted_input.h"

namespace pc::automation {
namespace {

// Textos esperados no idioma do jogo carregado (pedido de 2026-11-13: a versão global, em inglês,
// roda no mesmo port). Os do jogo vêm da própria tabela de textos (GetString); os do port seguem
// pc_lang. Assim os mesmos testes valem para a pasta PT-BR e para a global.
std::string GameText(uint32_t id) { return pc::lang::GameString(id); }
std::string PortLabel(const char* pt_latin1, const char* en) {
  return pc::lang::English() ? en : pt_latin1;
}
std::string Utf8ToLatin1(const std::string& u8) {
  std::string out;
  for (size_t i = 0; i < u8.size();) {
    const unsigned char c = static_cast<unsigned char>(u8[i]);
    if (c < 0x80) {
      out += char(c);
      ++i;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < u8.size()) {
      out += char(((c & 0x1F) << 6) | (static_cast<unsigned char>(u8[i + 1]) & 0x3F));
      i += 2;
    } else {
      out += '?';
      i += (c & 0xF0) == 0xE0 ? 3 : 4;
    }
  }
  return out;
}

std::atomic<bool> g_automation_running{false};

std::string JsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else o += c;
  }
  return o;
}

struct Step {
  std::string name;
  bool passed;
  std::string detail;
};

void WriteSelfTest(const std::filesystem::path& file, const std::vector<Step>& steps) {
  std::ostringstream o;
  o << "{\"version\": 1, \"steps\": [";
  for (size_t i = 0; i < steps.size(); ++i) {
    o << (i ? ", " : "") << "{\"step\": \"" << JsonEscape(steps[i].name) << "\", \"passed\": "
      << (steps[i].passed ? "true" : "false") << ", \"detail\": \"" << JsonEscape(steps[i].detail)
      << "\"}";
  }
  o << "]}\n";
  std::error_code ec;
  if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
  std::ofstream(file, std::ios::binary | std::ios::trunc) << o.str();
}

// ---------------------------------------------------------------- orçamento de tempo do --self-test
// A verificação do Craft Anything encerra o programa aos 90 s. Um auto-teste que passe disso
// não entrega relatório nenhum (e, antes da correção do lançador, deixava o jogo órfão
// segurando a saída da verificação). O cão de guarda grava o que já foi medido e encerra o
// jogo aos 80 s, contados a partir da criação do processo.
constexpr double kSelfTestBudgetSecs = 80.0;
std::mutex g_selftest_mu;
std::vector<Step> g_selftest_steps;  // o que já foi medido, para o cão de guarda
bool g_selftest_written = false;
bool g_saved_skip_intro_logos = false;

double SecondsSinceProcessStart() {
  FILETIME created{}, exited{}, kernel{}, user{}, now{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0.0;
  GetSystemTimeAsFileTime(&now);
  const auto to64 = [](const FILETIME& f) {
    return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime;
  };
  return double(to64(now) - to64(created)) / 1e7;
}

void PublishSteps(const std::vector<Step>& steps) {
  std::lock_guard<std::mutex> lk(g_selftest_mu);
  g_selftest_steps = steps;
}

// Grava o relatório uma única vez (o cão de guarda ou o fim normal, o que vier primeiro).
bool WriteSelfTestOnce(const std::filesystem::path& file, const std::vector<Step>& steps) {
  std::lock_guard<std::mutex> lk(g_selftest_mu);
  if (g_selftest_written) return false;
  WriteSelfTest(file, steps);
  g_selftest_written = true;
  return true;
}

// Reduz a imagem por média de blocos até no máximo 1920 px de largura (evidências em 4K
// ficariam com ~33 MB num PNG sem compressão).
void ReduceTo1920(rex::ui::RawImage& img) {
  if (img.width <= 1920) return;
  const uint32_t f = (img.width + 1919) / 1920;
  const uint32_t w = img.width / f, h = img.height / f;
  std::vector<uint8_t> reduced(size_t(w) * h * 4);
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x)
      for (int c = 0; c < 4; ++c) {
        uint32_t sum = 0;
        for (uint32_t dy = 0; dy < f; ++dy)
          for (uint32_t dx = 0; dx < f; ++dx)
            sum += img.data[size_t(y * f + dy) * img.stride + size_t(x * f + dx) * 4 + c];
        reduced[(size_t(y) * w + x) * 4 + c] = uint8_t(sum / (f * f));
      }
  img.width = w;
  img.height = h;
  img.stride = size_t(w) * 4;
  img.data = std::move(reduced);
}

// Cor dourada dos favos de mel (barra de vida) na tela de pausa.
bool LooksLikeHud(const rex::ui::RawImage& img) {
  if (img.width == 0 || img.height == 0) return false;
  // faixa superior esquerda onde fica a barra de favos, proporcional à imagem
  const uint32_t x0 = img.width * 7 / 100, x1 = img.width * 42 / 100;
  const uint32_t y0 = img.height * 5 / 100, y1 = img.height * 18 / 100;
  uint64_t gold = 0;
  for (uint32_t y = y0; y < y1; y += 2) {
    const uint8_t* row = img.data.data() + size_t(y) * img.stride;
    for (uint32_t x = x0; x < x1; x += 2) {
      const uint8_t* px = row + size_t(x) * 4;
      if (px[0] > 140 && px[1] > 90 && px[2] < 110 && px[0] > px[2] + 60) ++gold;
    }
  }
  return gold > 200;
}

// Imagem "em branco" = todos os pixels praticamente iguais.
// Captura a janela do jogo como aparece na tela (jogo + sobreposições de PC), lendo a área
// cliente da tela. Requer a janela visível; usada só como evidência dos menus de PC.
HWND FindGameWindow() {
  HWND hwnd = nullptr;
  EnumWindows(
      [](HWND h, LPARAM lp) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(h) &&
            GetWindow(h, GW_OWNER) == nullptr) {
          RECT r{};
          GetClientRect(h, &r);
          if (r.right - r.left > 320) {
            *reinterpret_cast<HWND*>(lp) = h;
            return FALSE;
          }
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&hwnd));
  return hwnd;
}

class PhysicalDpiScope {
 public:
  explicit PhysicalDpiScope(bool enable = true) {
    if (!enable) return;
    module_ = GetModuleHandleW(L"user32.dll");
    set_context_ = module_ ? reinterpret_cast<SetContextFn>(
                                  GetProcAddress(module_, "SetThreadDpiAwarenessContext"))
                           : nullptr;
    if (set_context_) {
      previous_ = set_context_(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
      active_ = previous_ != nullptr;
    }
  }
  ~PhysicalDpiScope() {
    if (active_) set_context_(previous_);
  }
  bool active() const { return active_; }

 private:
  using SetContextFn = HANDLE(WINAPI*)(HANDLE);
  HMODULE module_ = nullptr;
  SetContextFn set_context_ = nullptr;
  HANDLE previous_ = nullptr;
  bool active_ = false;
};

rex::ui::RawImage CaptureWindowOnScreen(bool physical_client = false) {
  rex::ui::RawImage img;
  PhysicalDpiScope dpi_scope(physical_client);
  // Tamanho lógico da janela (o mesmo DPI do processo): PrintWindow desenha a janela nesse
  // tamanho; pedir o tamanho físico deixaria a imagem num canto de um quadro maior.
  HWND hwnd = FindGameWindow();
  if (!hwnd) return img;
  if (IsIconic(hwnd)) return img;
  // Privacidade: só o conteúdo da JANELA DO JOGO, pedido ao próprio Windows (PrintWindow com
  // PW_RENDERFULLCONTENT funciona com a janela coberta). Nunca copia a tela do computador:
  // com outra janela na frente, um BitBlt da tela pegaria o que o usuário estiver vendo.
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int w = rc.right - rc.left, h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) return img;
  HDC screen = GetDC(hwnd);
  HDC mem = CreateCompatibleDC(screen);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits = nullptr;
  HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bmp && bits) {
    HGDIOBJ old = SelectObject(mem, bmp);
    constexpr UINT kClientOnlyFullContent = 0x1 | 0x2;  // PW_CLIENTONLY | PW_RENDERFULLCONTENT
    const BOOL printed = PrintWindow(hwnd, mem, kClientOnlyFullContent);
    SelectObject(mem, old);
    if (!printed) {
      DeleteObject(bmp);
      DeleteDC(mem);
      ReleaseDC(hwnd, screen);
      return img;
    }
    img.width = uint32_t(w);
    img.height = uint32_t(h);
    img.stride = size_t(w) * 4;
    img.data.resize(img.stride * h);
    const uint8_t* src = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < size_t(w) * h; ++i) {  // BGRA -> RGBX
      img.data[i * 4 + 0] = src[i * 4 + 2];
      img.data[i * 4 + 1] = src[i * 4 + 1];
      img.data[i * 4 + 2] = src[i * 4 + 0];
      img.data[i * 4 + 3] = 255;
    }
  }
  if (bmp) DeleteObject(bmp);
  DeleteDC(mem);
  ReleaseDC(hwnd, screen);
  return img;
}

bool LooksBlank(const rex::ui::RawImage& img) {
  if (img.width == 0 || img.height == 0) return true;
  const uint8_t* p0 = img.data.data();
  uint64_t differing = 0, samples = 0;
  for (uint32_t y = 0; y < img.height; y += 8) {
    const uint8_t* row = img.data.data() + size_t(y) * img.stride;
    for (uint32_t x = 0; x < img.width; x += 8) {
      const uint8_t* px = row + size_t(x) * 4;
      ++samples;
      if (std::abs(px[0] - p0[0]) + std::abs(px[1] - p0[1]) + std::abs(px[2] - p0[2]) > 24) ++differing;
    }
  }
  return differing * 200 < samples;  // < 0.5% diferente
}

struct WindowMetrics {
  bool valid = false;
  bool visible = false;
  bool minimized = false;
  bool physical_coordinates = false;
  bool fullscreen = false;
  UINT dpi = 0;
  LONG_PTR style = 0;
  LONG_PTR ex_style = 0;
  RECT client{};
  RECT outer{};
  RECT monitor{};
  RECT work{};
  std::string monitor_device;
};

std::string WideToUtf8(const wchar_t* value) {
  if (!value || !*value) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) return {};
  std::string result(size_t(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
  result.pop_back();
  return result;
}

WindowMetrics InspectWindow(HWND hwnd, bool fullscreen) {
  WindowMetrics metrics;
  if (!hwnd || !IsWindow(hwnd)) return metrics;
  PhysicalDpiScope dpi_scope;
  metrics.valid = GetClientRect(hwnd, &metrics.client) && GetWindowRect(hwnd, &metrics.outer);
  metrics.visible = IsWindowVisible(hwnd) != FALSE;
  metrics.minimized = IsIconic(hwnd) != FALSE;
  metrics.fullscreen = fullscreen;
  metrics.physical_coordinates = dpi_scope.active();
  metrics.style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  metrics.ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
    using GetDpiFn = UINT(WINAPI*)(HWND);
    auto get_dpi = reinterpret_cast<GetDpiFn>(GetProcAddress(user32, "GetDpiForWindow"));
    if (get_dpi) metrics.dpi = get_dpi(hwnd);
  }
  const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (monitor && GetMonitorInfoW(monitor, &info)) {
    metrics.monitor = info.rcMonitor;
    metrics.work = info.rcWork;
    metrics.monitor_device = WideToUtf8(info.szDevice);
  } else {
    metrics.valid = false;
  }
  return metrics;
}

bool RectNear(const RECT& a, const RECT& b, LONG tolerance = 1) {
  return std::abs(a.left - b.left) <= tolerance && std::abs(a.top - b.top) <= tolerance &&
         std::abs(a.right - b.right) <= tolerance && std::abs(a.bottom - b.bottom) <= tolerance;
}

bool ModeMatches(DisplayMode mode, const WindowMetrics& metrics) {
  if (!metrics.valid || !metrics.physical_coordinates || !metrics.visible || metrics.minimized)
    return false;
  switch (mode) {
    case DisplayMode::Janela:
      return !metrics.fullscreen && (metrics.style & WS_CAPTION) != 0;
    case DisplayMode::SemBordas:
      // M04 (C-25): sem bordas ocupa o monitor inteiro (antes: a área útil)
      return !metrics.fullscreen && (metrics.style & WS_CAPTION) == 0 &&
             RectNear(metrics.outer, metrics.monitor);
    case DisplayMode::TelaCheia:
      return metrics.fullscreen && (metrics.style & WS_CAPTION) == 0 &&
             RectNear(metrics.outer, metrics.monitor);
  }
  return false;
}

std::string RectJson(const RECT& rect) {
  std::ostringstream out;
  out << "{\"left\":" << rect.left << ",\"top\":" << rect.top << ",\"right\":"
      << rect.right << ",\"bottom\":" << rect.bottom << ",\"width\":"
      << rect.right - rect.left << ",\"height\":" << rect.bottom - rect.top << "}";
  return out.str();
}

struct ImageCorrelation {
  bool content_match = false;
  double mean_abs_diff = 0;
  double similar_samples = 0;
};

ImageCorrelation CompareGuestAndWindow(const rex::ui::RawImage& guest,
                                       const rex::ui::RawImage& window) {
  ImageCorrelation result;
  if (!guest.width || !guest.height || !window.width || !window.height) return result;
  constexpr uint32_t kSamplesX = 64;
  constexpr uint32_t kSamplesY = 36;
  constexpr double kAspect = 16.0 / 9.0;
  const double window_aspect = double(window.width) / window.height;
  double viewport_x = 0, viewport_y = 0;
  double viewport_w = window.width, viewport_h = window.height;
  if (window_aspect > kAspect) {
    viewport_w = window.height * kAspect;
    viewport_x = (window.width - viewport_w) / 2.0;
  } else if (window_aspect < kAspect) {
    viewport_h = window.width / kAspect;
    viewport_y = (window.height - viewport_h) / 2.0;
  }
  uint64_t diff_sum = 0, similar = 0, samples = 0;
  for (uint32_t y = 2; y + 2 < kSamplesY; ++y) {
    for (uint32_t x = 2; x + 2 < kSamplesX; ++x) {
      const uint32_t gx = std::min(guest.width - 1, x * guest.width / kSamplesX);
      const uint32_t gy = std::min(guest.height - 1, y * guest.height / kSamplesY);
      const uint32_t wx = std::min(window.width - 1, uint32_t(viewport_x + (x + 0.5) * viewport_w / kSamplesX));
      const uint32_t wy = std::min(window.height - 1, uint32_t(viewport_y + (y + 0.5) * viewport_h / kSamplesY));
      const uint8_t* gp = guest.data.data() + size_t(gy) * guest.stride + size_t(gx) * 4;
      const uint8_t* wp = window.data.data() + size_t(wy) * window.stride + size_t(wx) * 4;
      const uint32_t diff = uint32_t(std::abs(int(gp[0]) - int(wp[0])) +
                                     std::abs(int(gp[1]) - int(wp[1])) +
                                     std::abs(int(gp[2]) - int(wp[2]))) / 3;
      diff_sum += diff;
      if (diff <= 56) ++similar;
      ++samples;
    }
  }
  result.mean_abs_diff = samples ? double(diff_sum) / samples : 255.0;
  result.similar_samples = samples ? double(similar) / samples : 0.0;
  result.content_match = result.mean_abs_diff <= 72.0 && result.similar_samples >= 0.20;
  return result;
}

// Compara duas capturas do mesmo tamanho: quantos pixels mudaram (além de um limiar),
// por quadrante, e a diferença média RGB (para validar os perfis de distância de desenho).
struct DiffStats {
  uint64_t samples = 0, differing = 0;
  uint64_t quad_samples[4] = {0, 0, 0, 0}, quad_differing[4] = {0, 0, 0, 0};
  double mean_abs_diff = 0;
};

DiffStats CompareImages(const rex::ui::RawImage& a, const rex::ui::RawImage& b) {
  DiffStats s;
  if (a.width != b.width || a.height != b.height || !a.width || !a.data.size()) return s;
  double sum = 0;
  for (uint32_t y = 0; y < a.height; y += 4) {
    const uint8_t* ra = a.data.data() + size_t(y) * a.stride;
    const uint8_t* rb = b.data.data() + size_t(y) * b.stride;
    for (uint32_t x = 0; x < a.width; x += 4) {
      const uint8_t* pa = ra + size_t(x) * 4;
      const uint8_t* pb = rb + size_t(x) * 4;
      const int d = std::abs(pa[0] - pb[0]) + std::abs(pa[1] - pb[1]) + std::abs(pa[2] - pb[2]);
      sum += d / 3.0;
      const int q = (y >= a.height / 2 ? 2 : 0) + (x >= a.width / 2 ? 1 : 0);
      ++s.samples;
      ++s.quad_samples[q];
      if (d > 24) {
        ++s.differing;
        ++s.quad_differing[q];
      }
    }
  }
  s.mean_abs_diff = s.samples ? sum / s.samples : 0;
  return s;
}

[[noreturn]] void Finish(int code) {
  // Espera (até 2 s) as gravações de opções ainda na fila da interface (pc_menu PostUi).
  for (int i = 0; i < 200 && pc::menu::PendingUiWork(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  // Registro de sessão (M04, C-10): último bloco de desempenho e o motivo, antes do fim abrupto.
  pc::log::Stop("fim da sonda/auto-teste (código " + std::to_string(code) + ")");
  fflush(nullptr);
  TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
  for (;;) {}
}

// ---------------------------------------------------------------- esperas e entrada
void WaitFrames(uint64_t frames, uint64_t limit_frames = 4000) {
  const uint64_t target = state::guest_swaps.load() + frames;
  const uint64_t limit = state::guest_swaps.load() + limit_frames;
  while (state::guest_swaps.load() < target && state::guest_swaps.load() < limit)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void Tap(uint16_t button, int hold = 5) {
  input::SetButton(button, true);
  WaitFrames(hold, 30);
  input::SetButton(button, false);
  WaitFrames(2, 10);
}

struct NavResult {
  bool reached = false;
  std::string trace;
};
NavResult NavigateToGameplayByState(double max_secs);
void SkipScenesToFreePlay(NavResult& r, double max_secs);

// Leva do título até o jogo livre, como um jogador faria: Start, JOGAR, JOGO 1 e pulando as
// cenas com B. Guiada pelo estado do jogo (mapa e cena), serve tanto a um jogo novo quanto a
// um save carregado; as sondas antigas usavam esperas fixas (NavigateToGameplayFixed).
void NavigateToGameplay(bool /*fast*/ = false) {
  NavigateToGameplayByState(90.0);
  for (int i = 0; i < 2; ++i) {
    Tap(input::kB);
    WaitFrames(35);
  }
  WaitFrames(30);
}

[[maybe_unused]] void NavigateToGameplayFixed(bool fast = false) {
  // tela de título: ~1500 quadros no ritmo normal; com o início rápido do auto-teste,
  // os logos passam em passo 4 e a tela chega por volta do quadro 900.
  const uint64_t title_frame = fast ? 950 : 1500;
  while (state::guest_swaps.load() < title_frame) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  Tap(input::kStart);
  WaitFrames(fast ? 100 : 150);
  // menu principal: JOGAR
  Tap(input::kA);
  WaitFrames(fast ? 100 : 150);
  // seleção de jogo: JOGO 1
  Tap(input::kA);
  WaitFrames(fast ? 160 : 240);
  // introdução: o botão B pula as cenas já mostradas
  const int skips = fast ? 3 : 4;
  for (int i = 0; i < skips; ++i) {
    Tap(input::kB);
    WaitFrames(fast ? 180 : 220);
  }
  WaitFrames(fast ? 180 : 300);
}

// ---------------------------------------------------------------- leitura da memória do jogo
struct MemBlock {
  uint32_t addr = 0;
  std::vector<uint8_t> data;
};

std::vector<MemBlock> SnapshotMemory(uint32_t kStart = 0x80000000, uint32_t kEnd = 0xC0000000) {
  auto* mem = rex::system::kernel_state()->memory();
  const uint8_t* base = mem->virtual_membase();
  std::vector<MemBlock> out;
  // O jogo guarda os objetos também na memória física (0xA0000000..0xBFFFFFFF); o heap
  // virtual do jogo fica em 0x40000000..0x7FFFFFFF (M04: a câmera não estava acima de 0x8...).
  const uint32_t kChunk = 0x10000;
  for (uint32_t a = kStart; a < kEnd; a += kChunk) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(base + a, &mbi, sizeof(mbi))) continue;
    if (mbi.State != MEM_COMMIT) continue;
    const DWORD p = mbi.Protect & 0xFF;
    if (p == PAGE_NOACCESS || p == PAGE_GUARD) continue;
    MemBlock b;
    b.addr = a;
    b.data.assign(base + a, base + a + kChunk);
    out.push_back(std::move(b));
  }
  return out;
}

struct Candidate {
  uint32_t addr;
  float a[3], b[3], c[3];
  float d1, d2;
};

// Grava um trecho da memória do jogo (endereços de memória convidada).
void DumpGuestRange(const std::filesystem::path& file, uint32_t addr, size_t len) {
  auto* mem = rex::system::kernel_state()->memory();
  std::error_code ec;
  if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
  std::ofstream(file, std::ios::binary | std::ios::trunc)
      .write(reinterpret_cast<const char*>(mem->virtual_membase() + addr),
             static_cast<std::streamsize>(len));
}

bool Finite(float v) { return std::isfinite(v) && std::fabs(v) < 1.0e6f; }

float Dist(const float* p, const float* q) {
  const float dx = q[0] - p[0], dy = q[1] - p[1], dz = q[2] - p[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Procura um vetor de 3 floats (posição) que muda de forma coerente com o movimento.
std::vector<Candidate> FindMovingTriples(const std::vector<MemBlock>& s0,
                                         const std::vector<MemBlock>& s1,
                                         const std::vector<MemBlock>& s2) {
  std::vector<Candidate> found;
  for (size_t bi = 0; bi < s0.size() && bi < s1.size() && bi < s2.size(); ++bi) {
    const auto& b0 = s0[bi];
    const auto& b1 = s1[bi];
    const auto& b2 = s2[bi];
    if (b1.addr != b0.addr || b2.addr != b0.addr) continue;
    for (size_t off = 0; off + 12 <= b0.data.size(); off += 4) {
      const float* a = reinterpret_cast<const float*>(b0.data.data() + off);
      const float* b = reinterpret_cast<const float*>(b1.data.data() + off);
      const float* c = reinterpret_cast<const float*>(b2.data.data() + off);
      if (!Finite(a[0]) || !Finite(a[1]) || !Finite(a[2]) || !Finite(b[0]) ||
          !Finite(b[1]) || !Finite(b[2]) || !Finite(c[0]) || !Finite(c[1]) || !Finite(c[2]))
        continue;
      const float d1 = Dist(a, b), d2 = Dist(b, c);
      if (d1 < 0.5f || d1 > 5000.0f || d2 < 0.2f || d2 > 5000.0f) continue;
      // o segundo trecho deve continuar na mesma direção (andar para a frente)
      float dot = 0;
      for (int k = 0; k < 3; ++k) dot += (b[k] - a[k]) * (c[k] - b[k]);
      if (dot < 0.5f * d1 * d2) continue;
      // pelo menos duas coordenadas mudaram de forma significativa nos dois trechos
      int moved = 0;
      for (int k = 0; k < 3; ++k) {
        if (std::fabs(b[k] - a[k]) > 0.05f && std::fabs(c[k] - b[k]) > 0.05f) ++moved;
      }
      if (moved < 2) continue;
      Candidate cand;
      cand.addr = b0.addr + uint32_t(off);
      for (int k = 0; k < 3; ++k) { cand.a[k] = a[k]; cand.b[k] = b[k]; cand.c[k] = c[k]; }
      cand.d1 = d1;
      cand.d2 = d2;
      found.push_back(cand);
    }
  }
  std::sort(found.begin(), found.end(), [](const Candidate& x, const Candidate& y) {
    return x.d1 + x.d2 > y.d1 + y.d2;
  });
  if (found.size() > 60) found.resize(60);
  return found;
}

// Grava só as páginas que mudaram entre as três fotos (para análise fora do jogo).
void DumpChangedPages(const std::filesystem::path& file, const std::vector<MemBlock>& s0,
                      const std::vector<MemBlock>& s1, const std::vector<MemBlock>& s2) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  const size_t kPage = 4096;
  for (size_t bi = 0; bi < s0.size() && bi < s1.size() && bi < s2.size(); ++bi) {
    if (s0[bi].addr != s1[bi].addr || s1[bi].addr != s2[bi].addr) continue;
    const auto& d0 = s0[bi].data;
    for (size_t off = 0; off + kPage <= d0.size(); off += kPage) {
      if (std::memcmp(d0.data() + off, s1[bi].data.data() + off, kPage) == 0 &&
          std::memcmp(d0.data() + off, s2[bi].data.data() + off, kPage) == 0)
        continue;
      const uint32_t addr = s0[bi].addr + uint32_t(off), size = uint32_t(kPage);
      out.write(reinterpret_cast<const char*>(&addr), 4);
      out.write(reinterpret_cast<const char*>(&size), 4);
      out.write(reinterpret_cast<const char*>(d0.data() + off), kPage);
      out.write(reinterpret_cast<const char*>(s1[bi].data.data() + off), kPage);
      out.write(reinterpret_cast<const char*>(s2[bi].data.data() + off), kPage);
    }
  }
}

// ---------------------------------------------------------------- estado do jogador
// Transformação do modelo do jogador (Banjo): três floats big-endian x,y,z.
// Encontrada por comparação de memória durante o movimento (ver docs/STATUS.md).
constexpr uint32_t kPlayerPosAddr = 0x8266BB74;
constexpr uint32_t kMapIdAddr = 0x82673720;

uint8_t ReadU8(uint32_t addr) {
  auto* mem = rex::system::kernel_state()->memory();
  return mem->virtual_membase()[addr];
}

uint16_t ReadU16(uint32_t addr) {
  auto* mem = rex::system::kernel_state()->memory();
  const uint8_t* p = mem->virtual_membase() + addr;
  return uint16_t((uint16_t(p[0]) << 8) | p[1]);
}

uint32_t ReadU32(uint32_t addr) {
  auto* mem = rex::system::kernel_state()->memory();
  const uint8_t* p = mem->virtual_membase() + addr;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Objeto da tela atual dos menus (endereço fixo; o cursor fica em tela+7, as páginas em
// tela+0x154 e a página atual em tela+0x2C — mesma geometria lida na sonda --probe-menucursor
// e pelo gancho da entrada de PC em pc_hooks.cpp).
constexpr uint32_t kScreenObjAddr = 0x82648110;

// Navegação guiada pelo estado do próprio jogo (em vez de contar quadros): espera a tela de
// título (mapa 0x14F), entra em JOGAR -> JOGO 1 e pula as cenas com B até o jogo livre em
// (qualquer mapa fora do título com o estado de cena 0x8266EF30 = 0 por 1,5 s seguido;\n// no jogo novo é o mapa 0x142, depois da introdução no 0x158).
// Devolve um resumo do caminho (mapas e tempos) para o relatório.
NavResult NavigateToGameplayByState(double max_secs) {
  using namespace std::chrono;
  constexpr uint16_t kTitleMap = 0x14F, kIntroMap = 0x158;
  constexpr uint32_t kSceneAddr = 0x8266EF30;
  const auto t0 = steady_clock::now();
  auto secs = [&]() { return duration<double>(steady_clock::now() - t0).count(); };
  NavResult r;
  char buf[96];
  auto mark = [&](const char* what) {
    std::snprintf(buf, sizeof buf, "%s%s@%.1fs(mapa 0x%X)", r.trace.empty() ? "" : " ", what, secs(),
                  ReadU16(kMapIdAddr));
    r.trace += buf;
  };
  // 1) tela de título
  while (ReadU16(kMapIdAddr) != kTitleMap && secs() < max_secs)
    std::this_thread::sleep_for(milliseconds(20));
  mark("título");
  WaitFrames(90);
  Tap(input::kStart);
  WaitFrames(100);
  Tap(input::kA);  // JOGAR
  WaitFrames(100);
  Tap(input::kA);  // JOGO 1
  // 2) saiu do título
  while (ReadU16(kMapIdAddr) == kTitleMap && secs() < max_secs) {
    std::this_thread::sleep_for(milliseconds(20));
  }
  mark("jogo");
  // 3) pula as cenas até o jogo livre
  SkipScenesToFreePlay(r, max_secs - secs());
  return r;
}

// Passo 3 de NavigateToGameplayByState, também usado a partir do menu principal já aberto
// (sonda --probe-conquistas): B pula as cenas (mapas 0x14F/0x158 ou byte de cena) até o jogo
// ficar livre por 90 quadros.
void SkipScenesToFreePlay(NavResult& r, double max_secs) {
  using namespace std::chrono;
  constexpr uint16_t kTitleMap = 0x14F, kIntroMap = 0x158;
  constexpr uint32_t kSceneAddr = 0x8266EF30;
  const auto t0 = steady_clock::now();
  auto secs = [&]() { return duration<double>(steady_clock::now() - t0).count(); };
  char buf[96];
  auto mark = [&](const char* what) {
    std::snprintf(buf, sizeof buf, "%s%s@%.1fs(mapa 0x%X)", r.trace.empty() ? "" : " ", what, secs(),
                  ReadU16(kMapIdAddr));
    r.trace += buf;
  };
  uint64_t free_since = 0;
  int skips = 0;
  uint16_t last_map = ReadU16(kMapIdAddr);
  while (secs() < max_secs) {
    const bool scene = ReadU8(kSceneAddr) != 0;  // o mesmo byte que o gancho de 60 FPS lê
    const uint16_t map = ReadU16(kMapIdAddr);
    if (map != last_map) {
      mark("mapa");
      last_map = map;
    }
    if (map != kTitleMap && map != kIntroMap && map != 0 && !scene) {
      if (!free_since) free_since = state::guest_swaps.load();
      if (state::guest_swaps.load() - free_since >= 90) {
        r.reached = true;
        break;
      }
      std::this_thread::sleep_for(milliseconds(10));
      continue;
    }
    free_since = 0;
    Tap(input::kB);
    ++skips;
    WaitFrames(50);
  }
  std::snprintf(buf, sizeof buf, " %d toques em B;", skips);
  r.trace += buf;
  mark(r.reached ? "jogo livre" : "sem jogo livre");
}

float ReadBeF32(uint32_t addr) {
  auto* mem = rex::system::kernel_state()->memory();
  const uint8_t* p = mem->virtual_membase() + addr;
  uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  float f;
  std::memcpy(&f, &v, sizeof(f));
  return f;
}

struct Vec3 {
  float x, y, z;
};

Vec3 ReadPlayerPos() {
  return {ReadBeF32(kPlayerPosAddr), ReadBeF32(kPlayerPosAddr + 4),
          ReadBeF32(kPlayerPosAddr + 8)};
}

// Jogo livre: sem cena (0x8266EF30 = 0) e com o jogador numa posição plausível. Mesma regra da
// sonda --probe-dd-worlds.
bool InGameplay() {
  const Vec3 p = ReadPlayerPos();
  const bool finite_pos = std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
  const bool nonzero = std::fabs(p.x) > 0.5f || std::fabs(p.y) > 1.0f || std::fabs(p.z) > 0.5f;
  const bool sane =
      std::fabs(p.x) < 1.0e5f && std::fabs(p.y) < 1.0e5f && std::fabs(p.z) < 1.0e5f;
  return state::cutscene_state.load() == 0 && finite_pos && nonzero && sane;
}

// Espera o mapa ficar o mesmo e o jogo livre por 'frames' quadros seguidos.
bool WaitStableGameplay(int frames, int limit = 2400) {
  uint16_t last_map = ReadU16(kMapIdAddr);
  int stable = 0;
  for (int w = 0; w < limit; ++w) {
    const uint16_t m = ReadU16(kMapIdAddr);
    if (m == last_map && InGameplay()) {
      if (++stable >= frames) return true;
    } else {
      stable = 0;
    }
    last_map = m;
    WaitFrames(1, 1500);
  }
  return false;
}

// Viagem pela rotina do próprio jogo (0x82248810, chamada na thread do jogo pelo gancho de
// início de quadro). Espera a guarda de viagem (0x826CC2B8) ficar livre ~1 s; até 4 tentativas,
// fechando diálogos com B como um jogador faria. Mesma lógica da sonda --probe-dd-worlds.
bool RequestWarp(uint16_t map, uint8_t entry) {
  int clear = 0;
  for (int w = 0; w < 3600; ++w) {
    if (ReadU8(0x826CC2B8) == 0) {
      if (++clear >= 60) break;
    } else {
      clear = 0;
    }
    WaitFrames(1, 3600);
  }
  for (int attempt = 0; attempt < 4; ++attempt) {
    state::warp_invoked.store(0);
    state::warp_result.store(0);
    state::warp_was_blocked.store(0);
    state::warp_requested.store((uint32_t(map) << 8) | entry, std::memory_order_relaxed);
    for (int w = 0; w < 240 && !state::warp_invoked.load(); ++w) WaitFrames(1, 300);
    if (state::warp_invoked.load() != 0) break;
    state::warp_requested.store(0, std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i) {
      Tap(input::kB, 8);
      WaitFrames(40);
    }
  }
  return state::warp_invoked.load() != 0 && state::warp_result.load() != 0;
}

// Depois de RequestWarp: espera o mapa pedido e o jogo livre (pulando cenas com B).
bool WaitWarpArrival(uint16_t map) {
  bool changed = false;
  const uint64_t start = state::guest_swaps.load();
  while (state::guest_swaps.load() - start < 2000) {
    if (ReadU16(kMapIdAddr) == map) {
      changed = true;
      break;
    }
    WaitFrames(5, 90);
  }
  if (!changed) return false;
  const uint64_t s2 = state::guest_swaps.load();
  while (state::guest_swaps.load() - s2 < 2200) {
    if (InGameplay()) return true;
    if (state::cutscene_state.load() != 0) {
      Tap(input::kB, 8);
      WaitFrames(60);
    } else {
      WaitFrames(20);
    }
  }
  return false;
}

// Contador de itens (ovos/penas) do jogo: u16 em 0x82673668 + id*2, XOR com a
// palavra em 0x82249598 + id*8 (a própria rotina do jogo usa os bytes como chave).
uint16_t ReadItemCount(int id) {
  auto* mem = rex::system::kernel_state()->memory();
  const uint8_t* p = mem->virtual_membase();
  // ponteiro para a tabela de contadores atuais (0x82673668), em big-endian
  const uint32_t ptr_addr = 0x82673668;
  const uint32_t table = (uint32_t(p[ptr_addr]) << 24) | (uint32_t(p[ptr_addr + 1]) << 16) |
                         (uint32_t(p[ptr_addr + 2]) << 8) | p[ptr_addr + 3];
  const uint32_t cur_addr = table + uint32_t(id) * 2;
  const uint32_t key_addr = 0x82249598 + uint32_t(id) * 8;
  const uint16_t cur = uint16_t((p[cur_addr] << 8) | p[cur_addr + 1]);
  const uint16_t key = uint16_t((p[key_addr] << 8) | p[key_addr + 1]);
  return uint16_t(cur ^ key);
}

uint16_t ReadItemCap(int id) {
  auto* mem = rex::system::kernel_state()->memory();
  const uint8_t* p = mem->virtual_membase();
  const uint32_t a = 0x8262E772 + uint32_t(id) * 4;
  return uint16_t((p[a] << 8) | p[a + 1]);
}

void WriteCandidates(const std::filesystem::path& file, const std::vector<Candidate>& c,
                     size_t committed_bytes) {
  std::ostringstream o;
  o << "{\"version\":1,\"committed_bytes\":" << committed_bytes << ",\"candidates\":[";
  for (size_t i = 0; i < c.size(); ++i) {
    o << (i ? "," : "") << "{\"addr\":\"0x" << std::hex << c[i].addr << std::dec << "\",\"a\":["
      << c[i].a[0] << "," << c[i].a[1] << "," << c[i].a[2] << "],\"b\":[" << c[i].b[0] << ","
      << c[i].b[1] << "," << c[i].b[2] << "],\"c\":[" << c[i].c[0] << "," << c[i].c[1] << ","
      << c[i].c[2] << "],\"d1\":" << c[i].d1 << ",\"d2\":" << c[i].d2 << "}";
  }
  o << "]}\n";
  std::ofstream(file, std::ios::binary | std::ios::trunc) << o.str();
}

// ---------------------------------------------------------------- save avançado de verdade
// Arquivo do save do jogo dentro da pasta de dados (perfil fixo do runtime).
std::filesystem::path SaveFilePath(const std::filesystem::path& root) {
  return root / "B13EBABEBABEBABE" / "58410955" / "00000001" / "BanjoSave.bin" / "savegame.dat";
}

struct FileFingerprint {
  bool exists = false;
  uint64_t size = 0;
  uint64_t fnv = 0;  // FNV-1a 64 do conteúdo
  std::vector<uint8_t> bytes;
};

FileFingerprint Fingerprint(const std::filesystem::path& file) {
  FileFingerprint f;
  std::ifstream in(file, std::ios::binary);
  if (!in) return f;
  f.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  f.exists = true;
  f.size = f.bytes.size();
  uint64_t h = 1469598103934665603ull;
  for (uint8_t b : f.bytes) {
    h ^= b;
    h *= 1099511628211ull;
  }
  f.fnv = h;
  return f;
}

std::string Hex64(uint64_t v) {
  char b[20];
  std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
  return b;
}

// A pergunta ao próprio jogo: 0x8223E300 (a função da tela "Totais") conta, na thread do jogo
// (gancho PcHook_FrameStart), quantos itens de cada tipo há em cada mundo.
struct GameCountRow {
  int w, k, game, tracker;
};

std::vector<GameCountRow> GameCountMatrix(const pc::progress::Snapshot& snap) {
  static constexpr int kGameKinds[6] = {0, 1, 2, 3, 4, 6};   // tipos do jogo
  static constexpr int kTrackKinds[6] = {0, 1, 2, 3, 4, 5};  // mesmos tipos no rastreador
  static constexpr int kWorldIds[pc::progress::kWorldCount] = {16, 15, 18, 20, 21,
                                                               22, 23, 24, 26};
  std::vector<GameCountRow> rows;
  for (int w = 0; w < pc::progress::kWorldCount; ++w) {
    for (int ki = 0; ki < 6; ++ki) {
      int game = 0;
      const int ids[2] = {kWorldIds[w], (w == 8 ? 19 : 0)};  // Ilha O' Hags soma o id 19
      for (int id : ids) {
        if (!id) continue;
        const uint32_t req = (uint32_t(kGameKinds[ki]) << 16) | uint32_t(id);
        state::count_invoked.store(0, std::memory_order_relaxed);
        state::count_requested.store(req, std::memory_order_relaxed);
        for (int i = 0; i < 120 && state::count_invoked.load() != req; ++i) WaitFrames(1, 40);
        const uint64_t v = state::count_invoked.load() == req ? state::count_result.load() : ~0ull;
        if (v < 10000) {
          game += int(v);
        } else {
          game = -1;
          break;
        }
      }
      rows.push_back({w, kTrackKinds[ki], game, int(snap.worlds[w].got[kTrackKinds[ki]])});
    }
  }
  return rows;
}

bool OpenPauseMenu() {
  for (int tries = 0; tries < 5 && ReadU8(0x8266EF55) != 1; ++tries) {
    Tap(input::kStart, 12);
    WaitFrames(90);
  }
  return ReadU8(0x8266EF55) == 1;
}

// Põe o cursor da página atual do menu do jogo na entrada com este rótulo (Latin-1, lido em
// entrada+0x3B), conferindo o cursor real (tela+7). Antes as sondas contavam toques fixos e
// pararam de funcionar quando a pausa perdeu CLASSIFICAÇÕES (M04: --make-advanced-save
// confirmava CONTINUAR JOGO em vez de SAIR DO JOGO).
bool StepMenuCursorToLabel(const std::string& label) {
  namespace mm = pc::mechanics;
  auto target = [&]() -> int {
    const uint32_t pages = ReadU32(kScreenObjAddr + 0x154);
    const uint32_t page = ReadU32(kScreenObjAddr + 0x2C);
    if (pages < 0x40000000u || pages >= 0xA0000000u || page >= 16) return -1;
    const uint32_t entries = ReadU32(pages + page * 0x2C);
    const uint32_t count = ReadU32(pages + page * 0x2C + 4);
    if (entries < 0x40000000u || entries >= 0xA0000000u || count > 64) return -1;
    for (uint32_t i = 0; i < count; ++i) {
      std::string t;
      for (uint32_t k = 0; k < mm::kEntryLabelMax; ++k) {
        const uint8_t c = ReadU8(entries + i * mm::kEntrySize + mm::kEntryLabelOff + k);
        if (!c) break;
        t += char(c);
      }
      if (t == label) return int(i);
    }
    return -1;
  };
  const int want = target();
  if (want < 0) return false;
  for (int i = 0; i < 16; ++i) {
    const int cur = ReadU8(kScreenObjAddr + mm::kScreenCursorOff);
    if (cur == want) return true;
    const auto button = cur < want ? input::kDpadDown : input::kDpadUp;
    input::SetButton(button, true);
    WaitFrames(30);
    input::SetButton(button, false);
    WaitFrames(25);
  }
  return ReadU8(kScreenObjAddr + mm::kScreenCursorOff) == want;
}

// ---------------------------------------------------------------- --probe-saves (M04, etapa B)
std::string PathUtf8(const std::filesystem::path& p) {
  const std::u8string s = p.u8string();
  return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string Fnv64(const std::vector<uint8_t>& d) {
  uint64_t h = 1469598103934665603ull;
  for (uint8_t b : d) {
    h ^= b;
    h *= 1099511628211ull;
  }
  char buf[32];
  std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
  return buf;
}

std::string InspectJson(const std::vector<uint8_t>& d) {
  const pc::savefmt::Report r = pc::savefmt::Inspect(d);
  std::ostringstream o;
  o << "{\"size\":" << d.size() << ",\"usable\":" << (r.usable ? "true" : "false")
    << ",\"blank\":" << (r.blank ? "true" : "false") << ",\"reason\":\"" << JsonEscape(r.reason)
    << "\",\"globals\":[\"" << pc::savefmt::BlockName(r.global[0]) << "\",\""
    << pc::savefmt::BlockName(r.global[1]) << "\"],\"slots\":[";
  for (int i = 0; i < pc::savefmt::kSlots; ++i)
    o << (i ? "," : "") << "\"" << pc::savefmt::BlockName(r.slot[i]) << "\"";
  o << "],\"games\":" << r.ValidSlotCount() << ",\"fnv64\":\"" << Fnv64(d) << "\"}";
  return o.str();
}

// Roda as partes pedidas e grava o relatório. Não espera o jogo: a conferência da abertura já
// aconteceu antes de o jogo começar (pc::saves::Initialize em OnPreLaunchModule).
int RunSavesProbe(const Options& opts) {
  namespace fs = std::filesystem;
  std::ostringstream o;
  const pc::saves::StartupResult st = pc::saves::LastStartup();
  o << "{\"version\":1,\"startup\":{\"action\":\"" << st.action << "\",\"reason\":\""
    << JsonEscape(st.reason) << "\",\"interrupted\":" << (st.interrupted ? "true" : "false")
    << ",\"save_file\":\"" << JsonEscape(PathUtf8(st.save_file))
    << "\",\"corrupt_copy\":\"" << JsonEscape(PathUtf8(st.corrupt_copy.filename()))
    << "\",\"restored_from\":\"" << JsonEscape(PathUtf8(st.restored_from.filename())) << "\"}";
  // Conferência de um conjunto de saves (sem mexer neles).
  if (!opts.probe_saves_inspect.empty()) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(opts.probe_saves_inspect, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
      if (it->is_regular_file(ec) && it->path().extension() == L".dat") files.push_back(it->path());
    std::sort(files.begin(), files.end());
    o << ",\"inspect\":[";
    for (size_t i = 0; i < files.size(); ++i)
      o << (i ? "," : "") << "{\"file\":\""
        << JsonEscape(PathUtf8(fs::relative(files[i], opts.probe_saves_inspect, ec))) << "\",\"result\":"
        << InspectJson(ReadBytes(files[i])) << "}";
    o << "]";
  }
  // Gravações simuladas: cada arquivo da pasta vira o save, como uma gravação do jogo (no próprio
  // arquivo), com tempo para a vigia de backups ver cada uma.
  if (!opts.probe_saves_feed.empty()) {
    std::vector<fs::path> feed;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(opts.probe_saves_feed, ec))
      if (e.is_regular_file(ec) && e.path().extension() == L".dat") feed.push_back(e.path());
    std::sort(feed.begin(), feed.end());
    fs::path save = pc::saves::SaveFile();
    if (save.empty())
      save = opts.user_data_root / L"B13EBABEBABEBABE" / L"58410955" / L"00000001" /
             L"BanjoSave.bin" / L"savegame.dat";
    fs::create_directories(save.parent_path(), ec);
    o << ",\"feed\":[";
    for (size_t i = 0; i < feed.size(); ++i) {
      const auto data = ReadBytes(feed[i]);
      {
        std::ofstream out(save, std::ios::binary | std::ios::in | std::ios::out);
        if (!out) out.open(save, std::ios::binary | std::ios::out | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
      }
      std::error_code ec2;
      fs::resize_file(save, data.size(), ec2);
      std::this_thread::sleep_for(std::chrono::milliseconds(5500));
      o << (i ? "," : "") << "{\"file\":\"" << JsonEscape(PathUtf8(feed[i].filename()))
        << "\",\"status\":\"" << JsonEscape(pc::saves::Status()) << "\"}";
    }
    o << "]";
  }
  // Linha RESTAURAR do menu do jogo (mesmo código que o controle aciona).
  if (opts.probe_saves_choose >= 0) {
    namespace opt = pc::menu::options;
    int section = -1;
    for (int s = 0; s < opt::SectionCount(); ++s)
      if (opt::SectionTitle(s).find("SAVES") != std::string::npos) section = s;
    // A linha pelo texto (RESTAURAR / NENHUM BACKUP / RESTORE / NO VALID): a M04 pôs as linhas
    // do registro de sessão depois dela, e a posição "última linha" passou a apontar outra.
    int row = -1;
    for (int r = 0; section >= 0 && r < opt::RowCount(section); ++r) {
      const std::string t = opt::RowText(section, r);
      if (t.rfind("RESTAURAR", 0) == 0 || t.rfind("NENHUM BACKUP", 0) == 0 ||
          t.rfind("RESTORE", 0) == 0 || t.rfind("NO VALID", 0) == 0)
        row = r;
    }
    std::string before, after_moves, after_confirm, after;
    if (row >= 0) {
      before = opt::RowText(section, row);
      for (int k = 0; k < opts.probe_saves_choose; ++k) opt::RowInput(section, row, +1);
      after_moves = opt::RowText(section, row);
      opt::RowInput(section, row, 0);
      after_confirm = opt::RowText(section, row);
      opt::RowInput(section, row, 0);
      for (int k = 0; k < 100 && pc::menu::PendingUiWork(); ++k)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      after = opt::RowText(section, row);
    }
    o << ",\"choose\":{\"section\":" << section << ",\"row\":" << row << ",\"before\":\""
      << JsonEscape(before) << "\",\"after_moves\":\"" << JsonEscape(after_moves)
      << "\",\"after_confirm\":\"" << JsonEscape(after_confirm) << "\",\"after\":\""
      << JsonEscape(after) << "\"}";
  }
  if (opts.probe_saves_wait > 0)
    std::this_thread::sleep_for(std::chrono::seconds(std::min(opts.probe_saves_wait, 60)));
  // Estado final.
  const fs::path save = pc::saves::SaveFile();
  std::error_code ec;
  o << ",\"status\":\"" << JsonEscape(pc::saves::Status()) << "\",\"save\":";
  if (!save.empty() && fs::exists(save, ec))
    o << InspectJson(ReadBytes(save));
  else
    o << "null";
  o << ",\"restorable\":[";
  const auto list = pc::saves::ListBackups();
  for (size_t i = 0; i < list.size(); ++i)
    o << (i ? "," : "") << "{\"name\":\"" << JsonEscape(list[i].name) << "\",\"kind\":\""
      << list[i].kind << "\",\"stamp\":\"" << list[i].stamp << "\",\"games\":" << list[i].games
      << "}";
  o << "],\"backup_files\":[";
  {
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(opts.user_data_root / L"backups", ec))
      names.push_back(PathUtf8(e.path().filename()));
    std::sort(names.begin(), names.end());
    for (size_t i = 0; i < names.size(); ++i)
      o << (i ? "," : "") << "\"" << JsonEscape(names[i]) << "\"";
  }
  o << "]}\n";
  std::ofstream(opts.probe_saves, std::ios::binary | std::ios::trunc) << o.str();
  return 0;
}

}  // namespace

Options ParseCommandLine() {
  Options o;
  // Opção sem valor: o laço abaixo só olha pares (opção, valor) e pularia a última palavra.
  o.self_test_f1 = wcsstr(GetCommandLineW(), L"--self-test-f1") != nullptr;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i + 1 < argc; ++i) {
    std::wstring a = argv[i];
    // Banjo 3: --screenshot fica com a implementacao da Missao 01 (banjotooie_app.cpp: relatorio
    // de boot, --screenshot_seconds, capturas de teste). Aqui so consome o argumento.
    if (a == L"--screenshot") ++i;
    else if (a == L"--self-test") o.self_test = argv[++i];
    else if (a == L"--self-test-f1") o.self_test_f1 = true;
    else if (a == L"--frames") o.frames = _wtoi(argv[++i]);
    else if (a == L"--measure-secs") o.measure_secs = _wtoi(argv[++i]);
    else if (a == L"--probe-position") o.probe_position = argv[++i];
    else if (a == L"--probe-game") o.probe_game = argv[++i];
    else if (a == L"--probe-pause") o.probe_pause = argv[++i];
    else if (a == L"--probe-focus") o.probe_focus = argv[++i];
    else if (a == L"--probe-save") o.probe_save = argv[++i];
    else if (a == L"--probe-totals") o.probe_totals = argv[++i];
    else if (a == L"--probe-quit") o.probe_quit = argv[++i];
    else if (a == L"--probe-items") o.probe_items = argv[++i];
    else if (a == L"--probe-scan") o.probe_scan = argv[++i];
    else if (a == L"--probe-dialog") o.probe_dialog = argv[++i];
    else if (a == L"--probe-scenes") o.probe_scenes = argv[++i];
    else if (a == L"--probe-drawdistance") o.probe_drawdistance = argv[++i];
    else if (a == L"--probe-menu") o.probe_menu = argv[++i];
    else if (a == L"--probe-titlepc") o.probe_titlepc = argv[++i];
    else if (a == L"--probe-titlemenu") o.probe_titlemenu = argv[++i];
    else if (a == L"--probe-pcpages") o.probe_pcpages = argv[++i];
    else if (a == L"--probe-fullscreen") o.probe_fullscreen = argv[++i];
    else if (a == L"--probe-menucursor") o.probe_menucursor = argv[++i];
    else if (a == L"--probe-gamepad") o.probe_gamepad = argv[++i];
    else if (a == L"--probe-dd-maps") o.probe_dd_maps = argv[++i];
    else if (a == L"--probe-dd-worlds") o.probe_dd_worlds = argv[++i];
    else if (a == L"--probe-testpad") o.probe_testpad = argv[++i];
    else if (a == L"--probe-display") o.probe_display = argv[++i];
    else if (a == L"--probe-display-set-resolution") {
      const std::wstring value = argv[++i];
      o.probe_display_set_resolution = value == L"1080p" ? "1080p"
                                       : value == L"1440p" ? "1440p"
                                       : value == L"4k" ? "4k" : "invalid";
    }
    else if (a == L"--probe-display-transitions") o.probe_display_transitions = true;
    else if (a == L"--probe-display-transition-input") o.probe_display_transition_input = true;
    else if (a == L"--probe-display-no-capture") o.probe_display_no_capture = true;
    else if (a == L"--probe-save-sim") o.probe_save_sim = argv[++i];
    else if (a == L"--make-advanced-save") o.make_advanced_save = argv[++i];
    else if (a == L"--probe-advanced-save") o.probe_advanced_save = argv[++i];
    else if (a == L"--probe-saves") o.probe_saves = argv[++i];
    else if (a == L"--probe-saves-feed") o.probe_saves_feed = argv[++i];
    else if (a == L"--probe-saves-inspect") o.probe_saves_inspect = argv[++i];
    else if (a == L"--probe-saves-choose") o.probe_saves_choose = _wtoi(argv[++i]);
    else if (a == L"--probe-saves-wait") o.probe_saves_wait = _wtoi(argv[++i]);
    else if (a == L"--dialog-speed") o.dialog_speed = _wtoi(argv[++i]);
    else if (a == L"--probe-fisica") o.probe_fisica = argv[++i];
    else if (a == L"--probe-conquistas") o.probe_conquistas = argv[++i];
    else if (a == L"--probe-longa") o.probe_longa = argv[++i];
    else if (a == L"--fisica-mapas") {
      const std::wstring w = argv[++i];
      o.fisica_mapas.assign(w.begin(), w.end());  // só dígitos hex, ':' e ','
    }
  }
  if (argv) LocalFree(argv);
  if (o.frames < 1) o.frames = 120;
  if (o.measure_secs < 1) o.measure_secs = 5;
  return o;
}

bool IsRunning() { return g_automation_running.load(); }

void Start(const Options& opts, rex::ui::WindowedAppContext* ui,
           std::function<rex::ui::Presenter*()> presenter,
           rex::ui::Window* window,
           rex::system::IInputSystem* input_system) {
  g_automation_running.store(true);
  // a sonda de cenas compara o ritmo original: sem o "início rápido" dos logos
  if (!opts.probe_scenes.empty()) pc::Get().skip_intro_logos = false;
  // As sondas de save avançado só rodam em pasta de teste (--user_data_root): logos acelerados
  // como no auto-teste (antes levavam ~50 s até o título).
  if (!opts.make_advanced_save.empty() || !opts.probe_advanced_save.empty())
    pc::Get().skip_intro_logos = true;
  if (!opts.self_test.empty()) {
    // Início rápido só durante o teste (os logos passam em passo 4); a escolha do jogador
    // volta logo que o teste chega ao jogo livre, antes de qualquer gravação das opções.
    g_saved_skip_intro_logos = pc::Get().skip_intro_logos;
    pc::Get().skip_intro_logos = true;
    // Cão de guarda: grava o que já foi medido e encerra antes do limite da verificação.
    // Com --self-test-f1 (só verificação manual, o Craft não passa) o prazo é 120 s: a prova do
    // F1 no jogo livre não cabe nos 80 s da verificação do Craft.
    const double budget = opts.self_test_f1 ? 120.0 : kSelfTestBudgetSecs;
    std::thread([file = opts.self_test, budget]() {
      while (SecondsSinceProcessStart() < budget)
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
      std::vector<Step> steps;
      {
        std::lock_guard<std::mutex> lk(g_selftest_mu);
        steps = g_selftest_steps;
      }
      char buf[160];
      std::snprintf(buf, sizeof buf,
                    "o auto-teste passou de %.0f s e foi encerrado pelo cão de guarda depois de "
                    "%zu passos",
                    budget, steps.size());
      steps.push_back({"time_budget", false, buf});
      WriteSelfTestOnce(file, steps);
      Finish(4);
    }).detach();
  }
  std::thread([opts, ui, presenter, window, input_system]() {
    using namespace std::chrono;
    // Segurança dos saves: não precisa do jogo rodando (a conferência foi antes de ele começar).
    if (!opts.probe_saves.empty()) Finish(RunSavesProbe(opts));
    const auto t0 = steady_clock::now();
    const auto deadline = t0 + seconds(240);
    // 1) esperar o jogo apresentar ~N quadros
    while (state::guest_swaps.load() < uint64_t(opts.frames) && steady_clock::now() < deadline)
      std::this_thread::sleep_for(milliseconds(50));
    const uint64_t swaps_at_n = state::guest_swaps.load();
    const double secs_to_n = duration<double>(steady_clock::now() - t0).count();

    // 2) capturar a imagem do jogo na thread da interface
    auto capture = [&]() {
      rex::ui::RawImage img;
      bool ok = false;
      for (int attempt = 0; attempt < 20 && !ok; ++attempt) {
        ui->CallInUIThreadSynchronous([&]() {
          if (auto* p = presenter()) ok = p->CaptureGuestOutput(img);
        });
        if (!ok) std::this_thread::sleep_for(milliseconds(250));
      }
      return img;
    };
    rex::ui::RawImage img = capture();
    const bool blank = LooksBlank(img);

    if (!opts.probe_display.empty()) {
      const DisplayMode requested_mode = pc::Get().display_mode;
      const Resolution loaded_resolution = pc::Get().resolution;
      const int effective_scale_x =
          std::atoi(rex::cvar::GetFlagByName("draw_resolution_scale_x").c_str());
      const int effective_scale_y =
          std::atoi(rex::cvar::GetFlagByName("draw_resolution_scale_y").c_str());
      bool resolution_saved = false;
      Resolution selected_resolution = loaded_resolution;
      if (!opts.probe_display_set_resolution.empty()) {
        if (opts.probe_display_set_resolution == "1080p") selected_resolution = Resolution::R1080p;
        else if (opts.probe_display_set_resolution == "1440p") selected_resolution = Resolution::R1440p;
        else if (opts.probe_display_set_resolution == "4k") selected_resolution = Resolution::R2160p;
        else selected_resolution = loaded_resolution;
        ui->CallInUIThreadSynchronous([&]() {
          resolution_saved = pc::menu::SetResolutionForAutomation(selected_resolution);
        });
      }
      const Resolution requested_resolution = pc::Get().resolution;
      HWND hwnd = nullptr;
      bool sdk_fullscreen = false;
      ui->CallInUIThreadSynchronous([&]() {
        if (window) {
          hwnd = reinterpret_cast<HWND>(window->GetNativeWindowHandle());
          sdk_fullscreen = window->IsFullscreen();
        }
      });
      WindowMetrics initial_window = InspectWindow(hwnd, sdk_fullscreen);
      rex::ui::RawImage window_img = CaptureWindowOnScreen(true);
      ImageCorrelation correlation = CompareGuestAndWindow(img, window_img);
      // M04: a imagem do jogo e a da janela saem em instantes diferentes; durante o esmaecer
      // dos logos elas não batem (matriz de 2026-11-14: 82 de diferença média só por isso).
      // Se não bater, tira o par de novo, uma logo depois da outra (até 4 vezes).
      int capture_retries = 0;
      // (também quando a foto caiu num quadro preto entre os logos)
      for (; capture_retries < 6 && (!correlation.content_match || LooksBlank(img));
           ++capture_retries) {
        std::this_thread::sleep_for(milliseconds(400));
        rex::ui::RawImage g2 = capture();
        rex::ui::RawImage w2 = CaptureWindowOnScreen(true);
        if (!g2.width || !w2.width) continue;
        correlation = CompareGuestAndWindow(g2, w2);
        img = std::move(g2);
        window_img = std::move(w2);
      }
      const bool guest_content = img.width && img.height && !LooksBlank(img) &&
                                 uint64_t(img.width) * 9 == uint64_t(img.height) * 16;
      const bool window_nonblank = window_img.width && window_img.height && !LooksBlank(window_img);
      const bool initial_mode_match = ModeMatches(requested_mode, initial_window);
      std::filesystem::path guest_png = opts.probe_display;
      guest_png.replace_extension(".guest.png");
      std::filesystem::path window_png = opts.probe_display;
      window_png.replace_extension(".window.png");
      bool guest_png_written = false, window_png_written = false;
      if (!opts.probe_display_no_capture && guest_content && window_nonblank) {
        guest_png_written = pc::WritePng(guest_png, img.width, img.height, img.data.data(), img.stride);
        window_png_written = pc::WritePng(window_png, window_img.width, window_img.height,
                                          window_img.data.data(), window_img.stride);
      }

      std::ostringstream transitions_json;
      bool transitions_ok = true;
      bool restored = true;
      bool input_ok = !opts.probe_display_transition_input;
      WindowMetrics final_window = initial_window;
      bool settings_saved = resolution_saved;
      std::string input_json = "null";
      if (opts.probe_display_transitions) {
        std::array<DisplayMode, 8> sequence{};
        if (requested_mode == DisplayMode::Janela) {
          sequence = {DisplayMode::SemBordas, DisplayMode::TelaCheia, DisplayMode::Janela,
                      DisplayMode::SemBordas, DisplayMode::TelaCheia, DisplayMode::SemBordas,
                      DisplayMode::Janela, DisplayMode::Janela};
        } else if (requested_mode == DisplayMode::SemBordas) {
          sequence = {DisplayMode::TelaCheia, DisplayMode::Janela, DisplayMode::SemBordas,
                      DisplayMode::TelaCheia, DisplayMode::Janela, DisplayMode::SemBordas,
                      DisplayMode::TelaCheia, DisplayMode::SemBordas};
        } else {
          sequence = {DisplayMode::Janela, DisplayMode::SemBordas, DisplayMode::TelaCheia,
                      DisplayMode::Janela, DisplayMode::SemBordas, DisplayMode::TelaCheia,
                      DisplayMode::Janela, DisplayMode::TelaCheia};
        }

        bool navigation_reached = false;
        struct MoveCheck { bool moved = false; double distance = 0; };
        MoveCheck input_before, input_after;
        auto move_player = [&]() {
          MoveCheck result;
          const Vec3 before = ReadPlayerPos();
          input::Get().thumb_ly = 32767;
          WaitFrames(90, 1200);
          input::Get().thumb_ly = 0;
          WaitFrames(10, 240);
          const Vec3 after = ReadPlayerPos();
          result.distance = Dist(&before.x, &after.x);
          result.moved = result.distance > 20.0;
          return result;
        };
        if (opts.probe_display_transition_input) {
          input::SetActive(true);
          const bool old_skip = pc::Get().skip_intro_logos;
          pc::Get().skip_intro_logos = true;
          const NavResult nav = NavigateToGameplayByState(50.0);
          navigation_reached = nav.reached;
          pc::Get().skip_intro_logos = old_skip;
          // Como NavigateToGameplay (auto-teste): 2 toques em B fecham o diálogo de abertura;
          // sem eles o controle ainda não move o Banjo (M03: "movimento medido foi zero").
          if (navigation_reached) {
            for (int b = 0; b < 2; ++b) {
              Tap(input::kB);
              WaitFrames(35);
            }
            WaitStableGameplay(60, 900);
            input_before = move_player();
          }
          std::ostringstream i;
          i << "{\"navigation_reached\":" << (navigation_reached ? "true" : "false")
            << ",\"navigation_trace\":\"" << JsonEscape(nav.trace) << "\",\"before\":{\"moved\":"
            << (input_before.moved ? "true" : "false") << ",\"distance\":" << input_before.distance
            << "}}";
          input_json = i.str();
        }

        transitions_json << "[";
        for (size_t index = 0; index < sequence.size(); ++index) {
          const uint64_t frames_before = state::guest_swaps.load();
          bool applied = false;
          ui->CallInUIThreadSynchronous([&]() {
            applied = pc::menu::ApplyDisplayModeForAutomation(window, sequence[index], false);
          });
          WaitFrames(24, 1200);
          bool transition_fullscreen = false;
          ui->CallInUIThreadSynchronous([&]() {
            transition_fullscreen = window && window->IsFullscreen();
          });
          const WindowMetrics metrics = InspectWindow(hwnd, transition_fullscreen);
          const uint64_t frames_after = state::guest_swaps.load();
          const bool match = applied && ModeMatches(sequence[index], metrics);
          transitions_ok = transitions_ok && match && frames_after > frames_before;
          if (index) transitions_json << ",";
          transitions_json << "{\"requested_mode\":\"" << pc::DisplayModeName(sequence[index])
                           << "\",\"mode_match\":" << (match ? "true" : "false")
                           << ",\"frames_advanced\":" << (frames_after - frames_before)
                           << ",\"physical_coordinates\":"
                           << (metrics.physical_coordinates ? "true" : "false")
                           << ",\"fullscreen\":" << (metrics.fullscreen ? "true" : "false")
                           << ",\"style\":\"0x" << std::hex << uint64_t(metrics.style) << std::dec
                           << "\",\"outer_rect\":" << RectJson(metrics.outer)
                           << ",\"client_rect\":" << RectJson(metrics.client)
                           << ",\"monitor\":\"" << JsonEscape(metrics.monitor_device) << "\"}";
        }
        transitions_json << "]";

        ui->CallInUIThreadSynchronous([&]() {
          settings_saved = pc::menu::ApplyDisplayModeForAutomation(window, requested_mode, true);
        });
        WaitFrames(30, 1200);
        bool final_fullscreen = false;
        ui->CallInUIThreadSynchronous([&]() {
          final_fullscreen = window && window->IsFullscreen();
        });
        final_window = InspectWindow(hwnd, final_fullscreen);
        restored = ModeMatches(requested_mode, final_window) &&
                   RectNear(initial_window.outer, final_window.outer) &&
                   initial_window.style == final_window.style &&
                   initial_window.ex_style == final_window.ex_style &&
                   initial_window.monitor_device == final_window.monitor_device &&
                   initial_window.dpi == final_window.dpi;
        if (opts.probe_display_transition_input && navigation_reached) {
          input_after = move_player();
          input_ok = navigation_reached && input_before.moved && input_after.moved;
          const size_t close = input_json.rfind('}');
          std::ostringstream i;
          i << input_json.substr(0, close)
            << ",\"after\":{\"moved\":" << (input_after.moved ? "true" : "false")
            << ",\"distance\":" << input_after.distance << "}}";
          input_json = i.str();
        }
      } else {
        transitions_json << "[]";
        if (opts.probe_display_set_resolution.empty()) {
          ui->CallInUIThreadSynchronous([&]() { settings_saved = pc::Save(); });
        }
      }

      const int requested_scale_x =
          std::atoi(rex::cvar::GetFlagByName("draw_resolution_scale_x").c_str());
      const int requested_scale_y =
          std::atoi(rex::cvar::GetFlagByName("draw_resolution_scale_y").c_str());
      const int requested_output_width =
          requested_resolution == Resolution::R1080p ? 1920
          : requested_resolution == Resolution::R1440p ? 2560
                                                        : 3840;
      const int requested_output_height =
          requested_resolution == Resolution::R1080p ? 1080
          : requested_resolution == Resolution::R1440p ? 1440
                                                        : 2160;
      const int physical_client_width = initial_window.client.right - initial_window.client.left;
      const int physical_client_height = initial_window.client.bottom - initial_window.client.top;
      const int work_width = initial_window.work.right - initial_window.work.left;
      const int work_height = initial_window.work.bottom - initial_window.work.top;
      const int frame_width = std::max(0, int(initial_window.outer.right -
                                            initial_window.outer.left) - physical_client_width);
      const int frame_height = std::max(0, int(initial_window.outer.bottom -
                                             initial_window.outer.top) - physical_client_height);
      const int available_client_width = std::max(0, work_width - frame_width);
      const int available_client_height = std::max(0, work_height - frame_height);
      const bool work_area_fits_requested = available_client_width >= requested_output_width &&
                                            available_client_height >= requested_output_height;
      const bool client_matches_requested = physical_client_width == requested_output_width &&
                                            physical_client_height == requested_output_height;
      const int applied_output_width =
          loaded_resolution == Resolution::R1080p ? 1920
          : loaded_resolution == Resolution::R1440p ? 2560
                                                    : 3840;
      const int applied_output_height =
          loaded_resolution == Resolution::R1080p ? 1080
          : loaded_resolution == Resolution::R1440p ? 1440
                                                    : 2160;
      const bool work_area_fits_applied = available_client_width >= applied_output_width &&
                                         available_client_height >= applied_output_height;
      const bool client_matches_applied = physical_client_width == applied_output_width &&
                                          physical_client_height == applied_output_height;
      const bool surface_not_mismatched = requested_mode != DisplayMode::Janela ||
                                          client_matches_applied || !work_area_fits_applied;
      const char* surface_status = requested_mode == DisplayMode::Janela
                                       ? client_matches_applied ? "MATCH"
                                         : work_area_fits_applied ? "MISMATCH"
                                                                  : "BLOCKED_WORK_AREA"
                                   : requested_mode == DisplayMode::SemBordas ? "MONITOR_WORK_AREA"
                                                                              : "MONITOR_NATIVE";
      const uint64_t frames_after = state::guest_swaps.load();
      const bool image_files_ok = opts.probe_display_no_capture ||
                                  (guest_png_written && window_png_written);
      const bool ok = initial_mode_match && guest_content && window_nonblank &&
                      correlation.content_match && image_files_ok && settings_saved &&
                      frames_after > swaps_at_n && transitions_ok && restored && input_ok &&
                      surface_not_mismatched &&
                      (opts.probe_display_set_resolution.empty() || resolution_saved);
      std::ostringstream o;
      o << std::fixed << std::setprecision(3)
        << "{\"version\":1,\"ok\":" << (ok ? "true" : "false")
        << ",\"requested\":{\"resolution\":\"" << pc::ResolutionName(requested_resolution)
        << "\",\"display_mode\":\"" << pc::DisplayModeName(requested_mode) << "\"}"
        << ",\"applied\":{\"resolution\":\"" << pc::ResolutionName(loaded_resolution)
        << "\",\"display_mode\":\"" << pc::DisplayModeName(requested_mode) << "\"}"
        << ",\"effective\":{\"scale_x\":" << effective_scale_x << ",\"scale_y\":"
        << effective_scale_y << "}"
        << ",\"resolution_change\":";
      if (opts.probe_display_set_resolution.empty()) {
        o << "null";
      } else {
        o << "{\"from\":\"" << pc::ResolutionName(loaded_resolution) << "\",\"to\":\""
          << pc::ResolutionName(requested_resolution) << "\",\"restart_required\":"
          << (loaded_resolution == requested_resolution ? "false" : "true")
          << ",\"saved\":" << (resolution_saved ? "true" : "false")
          << ",\"requested_scale_x\":" << requested_scale_x
          << ",\"requested_scale_y\":" << requested_scale_y << "}";
      }
      o
        << ",\"resolution_surface\":{\"status\":\"" << surface_status
        << "\",\"comparison\":\"applied_resolution\",\"applied_width\":" << applied_output_width
        << ",\"applied_height\":" << applied_output_height
        << ",\"client_matches_applied\":" << (client_matches_applied ? "true" : "false")
        << ",\"work_area_fits_applied\":" << (work_area_fits_applied ? "true" : "false")
        << ",\"requested_width\":" << requested_output_width
        << ",\"requested_height\":" << requested_output_height
        << ",\"client_matches_requested\":" << (client_matches_requested ? "true" : "false")
        << ",\"work_area_fits_requested\":" << (work_area_fits_requested ? "true" : "false")
        << ",\"physical_client_width\":" << physical_client_width
        << ",\"physical_client_height\":" << physical_client_height
        << ",\"work_width\":" << work_width << ",\"work_height\":" << work_height
        << ",\"frame_width\":" << frame_width << ",\"frame_height\":" << frame_height
        << ",\"available_client_width\":" << available_client_width
        << ",\"available_client_height\":" << available_client_height << "}"
        << ",\"guest_framebuffer\":{\"source\":\"Presenter::CaptureGuestOutput\",\"captured\":"
        << (img.width && img.height ? "true" : "false") << ",\"non_blank\":"
        << (guest_content ? "true" : "false") << ",\"width\":" << img.width
        << ",\"height\":" << img.height << ",\"aspect\":"
        << (img.height ? double(img.width) / img.height : 0.0) << ",\"path\":\""
        << (guest_png_written ? JsonEscape(guest_png.string()) : "") << "\"}"
        << ",\"window\":{\"visible\":" << (initial_window.visible ? "true" : "false")
        << ",\"minimized\":" << (initial_window.minimized ? "true" : "false")
        << ",\"fullscreen\":" << (initial_window.fullscreen ? "true" : "false")
        << ",\"mode_match\":" << (initial_mode_match ? "true" : "false")
        << ",\"physical_coordinates\":"
        << (initial_window.physical_coordinates ? "true" : "false") << ",\"dpi\":"
        << initial_window.dpi << ",\"style\":\"0x" << std::hex
        << uint64_t(initial_window.style) << "\",\"ex_style\":\"0x"
        << uint64_t(initial_window.ex_style) << std::dec << "\",\"outer_rect\":"
        << RectJson(initial_window.outer) << ",\"client_rect\":" << RectJson(initial_window.client)
        << ",\"monitor\":{\"device\":\"" << JsonEscape(initial_window.monitor_device)
        << "\",\"rect\":" << RectJson(initial_window.monitor)
        << ",\"work_area\":" << RectJson(initial_window.work) << "}"
        << ",\"capture\":{\"source\":\"Win32 PrintWindow client surface\",\"captured\":"
        << (window_img.width && window_img.height ? "true" : "false") << ",\"non_blank\":"
        << (window_nonblank ? "true" : "false") << ",\"content_match\":"
        << (correlation.content_match ? "true" : "false") << ",\"mean_abs_diff\":"
        << correlation.mean_abs_diff << ",\"similar_sample_ratio\":" << correlation.similar_samples
        << ",\"width\":" << window_img.width << ",\"height\":" << window_img.height
        << ",\"path\":\"" << (window_png_written ? JsonEscape(window_png.string()) : "") << "\"}}"
        << ",\"chosen_monitor\":{\"device\":\"" << JsonEscape(pc::display::Chosen().device)
        << "\",\"width\":" << pc::display::Chosen().width << ",\"height\":"
        << pc::display::Chosen().height << ",\"setting\":\"" << JsonEscape(pc::Get().monitor)
        << "\",\"resolution_auto\":" << (pc::Get().resolution_auto ? "true" : "false") << "}"
        << ",\"output_backbuffer\":{\"available\":false,\"width\":null,\"height\":null}"
        << ",\"frames\":{\"before\":" << swaps_at_n << ",\"after\":" << frames_after
        << ",\"advanced\":" << (frames_after - swaps_at_n) << "}"
        << ",\"settings_file\":\"" << JsonEscape(pc::Path().string())
        << "\",\"settings_saved\":" << (settings_saved ? "true" : "false")
        << ",\"restoration_ok\":" << (restored ? "true" : "false")
        << ",\"restoration\":{\"initial_outer_rect\":" << RectJson(initial_window.outer)
        << ",\"final_outer_rect\":" << RectJson(final_window.outer)
        << ",\"initial_monitor\":\"" << JsonEscape(initial_window.monitor_device)
        << "\",\"final_monitor\":\"" << JsonEscape(final_window.monitor_device)
        << "\",\"initial_dpi\":" << initial_window.dpi << ",\"final_dpi\":"
        << final_window.dpi << ",\"initial_style\":\"0x" << std::hex
        << uint64_t(initial_window.style) << "\",\"final_style\":\"0x"
        << uint64_t(final_window.style) << std::dec << "\",\"ok\":"
        << (restored ? "true" : "false") << "}"
        << ",\"transitions\":" << transitions_json.str() << ",\"input\":" << input_json << "}\n";
      std::error_code report_ec;
      if (opts.probe_display.has_parent_path())
        std::filesystem::create_directories(opts.probe_display.parent_path(), report_ec);
      std::ofstream report(opts.probe_display, std::ios::binary | std::ios::trunc);
      report << o.str();
      report.close();
      Finish(!report ? 2 : ok ? 0 : 1);
    }

    // 3) taxa de quadros medida no jogo livre, depois de entrar no jogo (ver auto-teste)
    double guest_fps = 0, logic_fps = 0;
    rex::ui::HostPresentStats host;

    if (opts.dialog_speed >= 0) pc::Get().dialog_speed = std::clamp(opts.dialog_speed, 0, 3);

    // Sonda das cenas: sem nenhuma entrada, registra por --measure-secs segundos (desde o
    // início do processo de automação) cada mudança do estado de cena 0x8266EF30, com o
    // tempo real e os quadros apresentados. Rodando com e sem --scenes30 dá para ver se as
    // cenas de abertura/título duram o mesmo tempo real a 30 e a 60 FPS.
    if (!opts.probe_scenes.empty()) {
      std::ostringstream o;
      o << "{\"version\":2,\"lock60\":" << (pc::Get().lock60 ? "true" : "false")
        << ",\"scenes60\":" << (pc::Get().scenes60 ? "true" : "false")
        << ",\"scenes30\":" << (wcsstr(GetCommandLineW(), L"--scenes30") ? "true" : "false")
        << ",\"events\":[";
      uint32_t last = 0xFFFFFFFFu;
      bool first = true;
      uint64_t swaps_prev = state::guest_swaps.load();
      auto t_prev = steady_clock::now();
      double max_fps = 0;
      int samples = 0;
      double fps_sum = 0;
      bool host_reset = false;
      while (steady_clock::now() - t0 < seconds(opts.measure_secs)) {
        std::this_thread::sleep_for(milliseconds(50));
        if (!host_reset && steady_clock::now() - t0 >= seconds(opts.measure_secs - 20)) {
          rex::ui::GetHostPresentStats(true);
          host_reset = true;
        }
        const uint32_t sc = state::cutscene_state.load();
        const double t = duration<double>(steady_clock::now() - t0).count();
        if (sc != last) {
          o << (first ? "" : ",") << "{\"t\":" << t << ",\"swaps\":" << state::guest_swaps.load()
            << ",\"scene\":" << sc << ",\"step\":" << state::frame_step.load() << "}";
          first = false;
          last = sc;
        }
        const auto now = steady_clock::now();
        const double dt = duration<double>(now - t_prev).count();
        if (dt >= 1.0) {
          const uint64_t s = state::guest_swaps.load();
          const double fps = double(s - swaps_prev) / dt;
          if (fps > max_fps) max_fps = fps;
          fps_sum += fps;
          ++samples;
          swaps_prev = s;
          t_prev = now;
        }
      }
      {
        const rex::ui::HostPresentStats h = rex::ui::GetHostPresentStats(true);
        o << "],\"host_last20s\":{\"presents_per_s\":" << (h.mean_ms > 0 ? 1000.0 / h.mean_ms : 0)
          << ",\"mean_ms\":" << h.mean_ms << ",\"stddev_ms\":" << h.stddev_ms
          << ",\"max_ms\":" << h.max_ms << ",\"repeated_frames\":" << h.repeated_frames << "}";
      }
      o << ",\"avg_fps\":" << (samples ? fps_sum / samples : 0) << ",\"max_fps\":" << max_fps
        << ",\"frames\":" << state::guest_swaps.load() << "}\n";
      std::ofstream(opts.probe_scenes) << o.str();
      {
        rex::ui::RawImage im = capture();
        if (im.width) {
          std::filesystem::path png = opts.probe_scenes;
          png.replace_extension(".png");
          WritePng(png, im.width, im.height, im.data.data(), im.stride);
        }
      }
      Finish(0);
    }

    // Sonda dos diálogos: mede quantas letras por segundo a caixa de texto do jogo revela
    // (caixa em r31 de 0x8225D580: +0x1C9 estado, +0x1F4 letras da página, +0x1F6 letras
    // já mostradas). Mede até 3 páginas, avançando com A como um jogador.
    if (!opts.probe_dialog.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      std::ostringstream o;
      o << "{\"version\":1,\"dialog_speed\":" << pc::Get().dialog_speed << ",\"factor\":"
        << pc::DialogSpeedFactor(pc::Get().dialog_speed) << ",\"pages\":[";
      int pages = 0;
      const auto t_end = steady_clock::now() + seconds(40);
      rex::ui::RawImage shot;
      while (pages < 3 && steady_clock::now() < t_end) {
        // espera uma caixa começar a digitar
        const uint64_t c0 = state::dialog_type_calls.load();
        WaitFrames(2, 10);
        if (state::dialog_type_calls.load() == c0) {
          std::this_thread::sleep_for(milliseconds(20));
          continue;
        }
        const uint32_t box = state::dialog_box.load();
        const uint8_t total = ReadU8(box + 0x1F4);
        const uint8_t shown0 = ReadU8(box + 0x1F6);
        const auto ts = steady_clock::now();
        const uint64_t fs = state::guest_swaps.load();
        uint8_t shown = shown0;
        while (ReadU8(box + 0x1C9) == 2 && steady_clock::now() < ts + seconds(20)) {
          shown = ReadU8(box + 0x1F6);
          if (!shot.width && shown > total / 2) shot = capture();
          std::this_thread::sleep_for(milliseconds(2));
        }
        const double secs = duration<double>(steady_clock::now() - ts).count();
        const uint64_t frames = state::guest_swaps.load() - fs;
        const int typed = int(total) - int(shown0);
        // Legenda com tempo (estado 3): a caixa espera caixa+0x224 quadros/60 s e segue sozinha.
        // Mede quanto tempo ela fica (sem apertar nada); deve ser igual ao original.
        const uint8_t after = ReadU8(box + 0x1C9);
        const uint8_t hold_frames = ReadU8(box + 0x224);
        double hold_secs = -1;
        if (after == 3) {
          const auto th = steady_clock::now();
          while (ReadU8(box + 0x1C9) == 3 && steady_clock::now() < th + seconds(30))
            std::this_thread::sleep_for(milliseconds(2));
          hold_secs = duration<double>(steady_clock::now() - th).count();
        }
        if (pages) o << ",";
        o << "{\"chars\":" << int(total) << ",\"shown_at_start\":" << int(shown0)
          << ",\"seconds\":" << secs << ",\"frames\":" << frames
          << ",\"chars_per_second\":" << (secs > 0 ? typed / secs : 0)
          << ",\"state_after_typing\":" << int(after) << ",\"hold_frames_at_60\":" << int(hold_frames)
          << ",\"hold_seconds_original\":" << (after == 3 ? hold_frames / 60.0 : 0)
          << ",\"hold_seconds_measured\":" << hold_secs << "}";
        ++pages;
        WaitFrames(30);
        Tap(input::kA);  // próxima página
        WaitFrames(20);
      }
      o << "],\"type_calls\":" << state::dialog_type_calls.load() << "}\n";
      std::ofstream(opts.probe_dialog) << o.str();
      if (shot.width) {
        ReduceTo1920(shot);
        std::filesystem::path png = opts.probe_dialog;
        png.replace_extension(".png");
        WritePng(png, shot.width, shot.height, shot.data.data(), shot.stride);
      }
      Finish(0);
    }

    if (!opts.probe_focus.empty()) {
      // M04 (C-91): só a janela DO JOGO é minimizada e restaurada, sem ativar nada (a versão
      // antiga minimizava a janela em primeiro plano, que podia ser de outro programa). O vigia
      // de foco fica ligado nesta sonda (banjotooie_app.cpp).
      input::SetActive(true);
      NavigateToGameplay();
      WaitStableGameplay(60, 900);
      const uint8_t before = ReadU8(0x8266EF55);
      HWND hwnd = nullptr;
      ui->CallInUIThreadSynchronous([&]() {
        if (window) hwnd = reinterpret_cast<HWND>(window->GetNativeWindowHandle());
      });
      std::ostringstream o;
      o << "{\"version\":1,\"paused_before\":" << int(before);
      uint8_t while_min = 0, after_restore = 0, after_resume = 0;
      double moved = 0;
      if (hwnd) {
        ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
        for (int i = 0; i < 40 && ReadU8(0x8266EF55) != 1; ++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        while_min = ReadU8(0x8266EF55);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        after_restore = ReadU8(0x8266EF55);  // continua pausado até o jogador voltar
        Tap(input::kStart, 12);               // retomada pelo jogador
        WaitFrames(90);
        after_resume = ReadU8(0x8266EF55);
        const Vec3 p0 = ReadPlayerPos();
        input::Get().thumb_ly = 32767;
        WaitFrames(60);
        input::Get().thumb_ly = 0;
        WaitFrames(10);
        const Vec3 p1 = ReadPlayerPos();
        moved = std::hypot(double(p1.x - p0.x), double(p1.z - p0.z));
      }
      const bool ok = hwnd && before == 0 && while_min == 1 && after_restore == 1 &&
                      after_resume == 0 && moved > 20;
      o << ",\"paused_while_minimized\":" << int(while_min)
        << ",\"paused_after_restore\":" << int(after_restore)
        << ",\"paused_after_start\":" << int(after_resume) << ",\"moved_after_resume\":" << moved
        << ",\"hwnd\":" << (hwnd ? 1 : 0) << ",\"ok\":" << (ok ? "true" : "false") << "}\n";
      std::ofstream(opts.probe_focus) << o.str();
      rex::ui::RawImage im = capture();
      if (im.width) {
        ReduceTo1920(im);
        std::filesystem::path png = opts.probe_focus;
        png.replace_extension(".png");
        WritePng(png, im.width, im.height, im.data.data(), im.stride);
      }
      Finish(0);
    }

    if (!opts.probe_save.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      WaitFrames(120);
      // Buffer que o jogo entrega à gravação (0x8227E640 -> 0x822E8D28): é o save vivo.
      DumpGuestRange(opts.probe_save, 0x826CB620, 0xA11);
      std::ostringstream o;
      o << "{\"version\":1,\"addr\":\"826CB620\",\"size\":2577,\"map\":"
        << ((ReadU8(0x82673720) << 8) | ReadU8(0x82673721)) << "}\n";
      std::filesystem::path meta = opts.probe_save;
      meta.replace_extension(".json");
      std::ofstream(meta) << o.str();
      Finish(0);
    }

    if (!opts.probe_totals.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      Tap(input::kStart);
      WaitFrames(160);
      // "VER TOTAIS" é o segundo item do menu de pausa.
      Tap(input::kDpadDown);
      WaitFrames(40);
      Tap(input::kA);
      WaitFrames(200);
      // a tela de totais deve estar aberta; guarda a imagem e o save vivo
      rex::ui::RawImage im = capture();
      if (im.width) {
        ReduceTo1920(im);
        std::filesystem::path png = opts.probe_totals;
        png.replace_extension(".png");
        WritePng(png, im.width, im.height, im.data.data(), im.stride);
      }
      DumpGuestRange(opts.probe_totals, 0x826CB620, 0xA11);
      Finish(0);
    }

    // Sonda do "save avançado" simulado (--probe-save-sim): sem um save de verdade com
    // progresso, liga as bandeiras de todos os itens dos 3 primeiros mundos (16 Mayahem
    // Temple, 15 Glitter Gulch Mine, 18 Witchyworld) como um save avançado teria, confere o
    // rastreador item por item, captura a tela "Totais" do PRÓPRIO jogo e o painel do
    // rastreador, e desfaz tudo conferindo a restauração byte a byte.
    if (!opts.probe_save_sim.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      WaitFrames(60);
      std::filesystem::path base = opts.probe_save_sim;
      base.replace_extension();

      const pc::progress::Snapshot before = pc::progress::Read();
      uint32_t block_addr = 0;
      size_t block_len = 0;
      const bool block_ok = pc::progress::FlagBlockRange(&block_addr, &block_len);
      std::vector<uint8_t> block_before;
      if (block_ok) {
        block_before.resize(block_len);
        std::memcpy(block_before.data(),
                    rex::system::kernel_state()->memory()->virtual_membase() + block_addr,
                    block_len);
      }

      // Liga as bandeiras dos 3 primeiros mundos (como um save avançado teria).
      const std::vector<uint8_t> kWorlds = {16, 15, 18};
      const auto items = pc::progress::ItemsInWorlds(kWorlds);
      std::vector<std::pair<int, int>> changed;
      changed.reserve(items.size());
      int changed_kind[pc::progress::kKindCount] = {};
      for (const auto& it : items) {
        if (pc::progress::SetItemFlag(pc::progress::Kind(it.first), it.second, true) == 0) {
          changed.emplace_back(it.first, it.second);
          ++changed_kind[it.first];
        }
      }
      WaitFrames(10);
      const pc::progress::Snapshot during = pc::progress::Read();

      // Confere item por item: a diferença em cada mundo/tipo é exatamente o que foi ligado
      // (cada item vale os pontos do rastreador: ninho 5, clave 20, demais 1).
      struct Cell {
        int w, k;
        int delta, expected;
      };
      std::vector<Cell> mismatches;
      int per_world_delta[pc::progress::kWorldCount][pc::progress::kKindCount] = {};
      int per_world_expected[pc::progress::kWorldCount][pc::progress::kKindCount] = {};
      for (int w = 0; w < pc::progress::kWorldCount; ++w)
        for (int k = 0; k < pc::progress::kKindCount; ++k)
          per_world_delta[w][k] = int(during.worlds[w].got[k]) - int(before.worlds[w].got[k]);
      for (const auto& it : changed) {
        const int world = pc::progress::ItemWorld(pc::progress::Kind(it.first), it.second);
        if (world >= 0 && world < pc::progress::kWorldCount)
          per_world_expected[world][it.first] +=
              pc::progress::ItemValue(pc::progress::Kind(it.first), it.second);
      }
      for (int w = 0; w < pc::progress::kWorldCount; ++w)
        for (int k = 0; k < pc::progress::kKindCount; ++k)
          if (per_world_delta[w][k] != per_world_expected[w][k])
            mismatches.push_back({w, k, per_world_delta[w][k], per_world_expected[w][k]});

      // A pergunta definitiva ao jogo: 0x8223E300 (a MESMA função da tela "Totais") conta,
      // na thread do jogo, quantos itens do tipo em cada mundo. O rastreador precisa bater
      // com esses números para todos os tipos e mundos.
      struct GameCount {
        int w, k, game, tracker;
      };
      std::vector<GameCount> game_rows;
      std::vector<GameCount> game_mismatch;
      bool game_match = true;
      {
        static constexpr int kGameKinds[6] = {0, 1, 2, 3, 4, 6};  // Jinjo, Jiggy, favo, glowbo,
                                                                  // página Cheato, notas (jogo)
        static constexpr int kTrackKinds[6] = {0, 1, 2, 3, 4, 5};  // mesmos tipos no rastreador
        static constexpr int kWorldIds[pc::progress::kWorldCount] = {16, 15, 18, 20, 21,
                                                                     22, 23, 24, 26};
        for (int w = 0; w < pc::progress::kWorldCount; ++w) {
          for (int ki = 0; ki < 6; ++ki) {
            const int k = kGameKinds[ki];
            int game = 0;
            const int ids[2] = {kWorldIds[w], (w == 8 ? 19 : 0)};  // Ilha O' Hags soma o id 19
            for (int id : ids) {
              if (!id) continue;
              const uint32_t req = (uint32_t(k) << 16) | uint32_t(id);
              state::count_invoked.store(0, std::memory_order_relaxed);
              state::count_requested.store(req, std::memory_order_relaxed);
              for (int i = 0; i < 120 && state::count_invoked.load() != req; ++i)
                WaitFrames(1, 40);
              const uint64_t v =
                  state::count_invoked.load() == req ? state::count_result.load() : ~0ull;
              if (v < 10000) {
                game += int(v);
              } else {
                game = -1;
                break;
              }
            }
            const int tracker = int(during.worlds[w].got[kTrackKinds[ki]]);
            game_rows.push_back({w, kTrackKinds[ki], game, tracker});
            if (game != tracker) {
              game_match = false;
              game_mismatch.push_back({w, kTrackKinds[ki], game, tracker});
            }
          }
        }
      }

      // Tela "Totais" do próprio jogo (pausa -> VER TOTAIS), com as bandeiras ligadas.
      bool totals_ok = false;
      {
        Tap(input::kStart);
        WaitFrames(160);
        Tap(input::kDpadDown);
        WaitFrames(40);
        Tap(input::kA);
        WaitFrames(200);
        rex::ui::RawImage im = capture();
        if (im.width) {
          ReduceTo1920(im);
          std::filesystem::path png = base;
          png += "_totals.png";
          WritePng(png, im.width, im.height, im.data.data(), im.stride);
          totals_ok = !LooksBlank(im);
        }
        Tap(input::kB);
        WaitFrames(80);
        Tap(input::kB);
        WaitFrames(80);
      }

      // Painel "PROGRESSO POR MUNDO" do menu de PC, com as mesmas bandeiras ligadas.
      // A sobreposição só existe na janela: captura da JANELA inteira (render + overlay).
      bool tracker_ok = false;
      {
        pc::Get().show_collectibles = true;
        WaitFrames(40);
        rex::ui::RawImage win = CaptureWindowOnScreen();
        if (win.width && !LooksBlank(win)) {
          ReduceTo1920(win);
          std::filesystem::path png = base;
          png += "_tracker.png";
          WritePng(png, win.width, win.height, win.data.data(), win.stride);
          tracker_ok = true;
        }
        pc::Get().show_collectibles = false;
        WaitFrames(10);
      }

      // Desfaz tudo e confere: rastreador igual ao início e bloco de bandeiras idêntico.
      for (const auto& it : changed)
        pc::progress::SetItemFlag(pc::progress::Kind(it.first), it.second, false);
      WaitFrames(10);
      const pc::progress::Snapshot after = pc::progress::Read();
      bool restored = after.valid && before.valid;
      for (int w = 0; restored && w < pc::progress::kWorldCount; ++w)
        for (int k = 0; k < pc::progress::kKindCount; ++k)
          restored = restored && after.worlds[w].got[k] == before.worlds[w].got[k];
      bool block_identical = block_ok;
      if (block_ok) {
        for (size_t i = 0; i < block_len; ++i) {
          if (rex::system::kernel_state()->memory()->virtual_membase()[block_addr + i] !=
              block_before[i]) {
            block_identical = false;
            break;
          }
        }
      }

      std::ostringstream o;
      o << "{\"version\":2,\"worlds_sim\":[16,15,18],\"items_set\":" << changed.size()
        << ",\"by_kind\":[";
      for (int k = 0; k < pc::progress::kKindCount; ++k)
        o << (k ? "," : "") << changed_kind[k];
      o << "],\"mismatches\":" << mismatches.size() << ",\"mismatch_rows\":[";
      for (size_t i = 0; i < mismatches.size(); ++i)
        o << (i ? "," : "") << "{\"world\":\"" << during.worlds[mismatches[i].w].name
          << "\",\"kind\":\"" << pc::progress::KindName(pc::progress::Kind(mismatches[i].k))
          << "\",\"delta\":" << mismatches[i].delta << ",\"expected\":" << mismatches[i].expected
          << "}";
      o << "],\"totals_screen\":" << (totals_ok ? 1 : 0)
        << ",\"tracker_panel\":" << (tracker_ok ? 1 : 0) << ",\"restored\":" << (restored ? 1 : 0)
        << ",\"block_identical\":" << (block_identical ? 1 : 0)
        << ",\"game_count_match\":" << (game_match ? 1 : 0) << ",\"game_count_rows\":[";
      for (size_t i = 0; i < game_rows.size(); ++i)
        o << (i ? "," : "") << "{\"world\":\"" << during.worlds[game_rows[i].w].name
          << "\",\"kind\":\"" << pc::progress::KindName(pc::progress::Kind(game_rows[i].k))
          << "\",\"game\":" << game_rows[i].game << ",\"tracker\":" << game_rows[i].tracker << "}";
      o << "],\"game_count_mismatch\":[";
      for (size_t i = 0; i < game_mismatch.size(); ++i)
        o << (i ? "," : "") << "{\"world\":\"" << during.worlds[game_mismatch[i].w].name
          << "\",\"kind\":\"" << pc::progress::KindName(pc::progress::Kind(game_mismatch[i].k))
          << "\",\"game\":" << game_mismatch[i].game
          << ",\"tracker\":" << game_mismatch[i].tracker << "}";
      o << "],\"rows\":[";
      bool first = true;
      for (int w = 0; w < pc::progress::kWorldCount; ++w) {
        o << (first ? "" : ",") << "{\"world\":\"" << during.worlds[w].name << "\",\"got\":[";
        for (int k = 0; k < pc::progress::kKindCount; ++k)
          o << (k ? "," : "") << during.worlds[w].got[k];
        o << "],\"total\":[";
        for (int k = 0; k < pc::progress::kKindCount; ++k)
          o << (k ? "," : "") << during.worlds[w].total[k];
        o << "]}";
        first = false;
      }
      o << "],\"before_got\":[";
      for (int k = 0; k < pc::progress::kKindCount; ++k) o << (k ? "," : "") << before.got[k];
      o << "],\"after_got\":[";
      for (int k = 0; k < pc::progress::kKindCount; ++k) o << (k ? "," : "") << after.got[k];
      o << "]}\n";
      std::ofstream(opts.probe_save_sim) << o.str();
      Finish(restored && block_identical && mismatches.empty() && game_match ? 0 : 2);
    }

    // ------------------------------------------------ save avançado DE VERDADE
    // Só numa pasta de dados de teste (--user_data_root=...): nunca toca no save do jogador.
    const bool isolated_user_data = wcsstr(GetCommandLineW(), L"--user_data_root") != nullptr;
    auto refuse_shared = [&](const std::filesystem::path& out) {
      std::ofstream(out, std::ios::binary | std::ios::trunc)
          << "{\"version\":1,\"ok\":false,\"error\":\"use --user_data_root=<pasta de teste>: "
             "esta sonda nunca grava na pasta de saves do jogador\"}\n";
      Finish(2);
    };
    auto write_rows = [](std::ostringstream& o, const pc::progress::Snapshot& s) {
      o << "[";
      for (int w = 0; w < pc::progress::kWorldCount; ++w) {
        o << (w ? "," : "") << "{\"world\":\"" << JsonEscape(s.worlds[w].name) << "\",\"got\":[";
        for (int k = 0; k < pc::progress::kKindCount; ++k) o << (k ? "," : "") << s.worlds[w].got[k];
        o << "],\"total\":[";
        for (int k = 0; k < pc::progress::kKindCount; ++k)
          o << (k ? "," : "") << s.worlds[w].total[k];
        o << "]}";
      }
      o << "]";
    };
    auto complete = [](const pc::progress::Snapshot& s) {
      if (!s.valid) return false;
      for (int k = 0; k < pc::progress::kKindCount; ++k)
        if (s.got[k] != s.total[k]) return false;
      return true;
    };

    // 1) Cria o save: jogo novo, liga TODOS os colecionáveis dos 9 mundos (Jinjos, Jiggies,
    //    favos, glowbos, páginas Cheato, ninhos de notas) e grava pelo caminho do jogador:
    //    pausa -> SALVAR E SAIR -> SIM. O jogo serializa e calcula as somas de verificação.
    if (!opts.make_advanced_save.empty()) {
      if (!isolated_user_data) refuse_shared(opts.make_advanced_save);
      std::filesystem::path base = opts.make_advanced_save;
      base.replace_extension();
      const std::filesystem::path save_file = SaveFilePath(opts.user_data_root);
      const FileFingerprint f0 = Fingerprint(save_file);
      input::SetActive(true);
      const NavResult nav = NavigateToGameplayByState(70.0);
      for (int i = 0; i < 2; ++i) {
        Tap(input::kB);
        WaitFrames(35);
      }
      WaitFrames(30);
      const pc::progress::Snapshot before = pc::progress::Read();
      const auto items = pc::progress::ItemsInWorlds({16, 15, 18, 20, 21, 22, 23, 24, 26, 19});
      int by_kind[pc::progress::kKindCount] = {};
      int set_now = 0;
      for (const auto& it : items) {
        if (pc::progress::SetItemFlag(pc::progress::Kind(it.first), it.second, true) == 0) {
          ++set_now;
          ++by_kind[it.first];
        }
      }
      WaitFrames(10);
      const pc::progress::Snapshot during = pc::progress::Read();
      auto* membase = rex::system::kernel_state()->memory()->virtual_membase();
      auto eeprom_hash = [&]() {
        uint64_t h = 1469598103934665603ull;
        for (uint32_t i = 0; i < 0xA11; ++i) {
          h ^= membase[0x826CB620 + i];
          h *= 1099511628211ull;
        }
        return h;
      };
      const uint64_t eeprom0 = eeprom_hash();
      const uint16_t map_play = ReadU16(kMapIdAddr);
      const FileFingerprint f_pre = Fingerprint(save_file);  // o jogo novo já criou o arquivo
      // pausa -> SAIR DO JOGO (grava; a última entrada desde 2026-11-12) -> confirma SIM
      const bool paused = OpenPauseMenu();
      WaitFrames(40);
      const bool on_quit = StepMenuCursorToLabel(GameText(61));
      REXLOG_INFO("make-advanced-save: cursor em SAIR DO JOGO = {}", on_quit);
      if (rex::ui::RawImage im = capture(); im.width) {
        ReduceTo1920(im);
        WritePng(std::filesystem::path(base) += "_menu.png", im.width, im.height, im.data.data(),
                 im.stride);
      }
      Tap(input::kA);
      WaitFrames(200);
      if (rex::ui::RawImage im = capture(); im.width) {
        ReduceTo1920(im);
        WritePng(std::filesystem::path(base) += "_confirm.png", im.width, im.height,
                 im.data.data(), im.stride);
      }
      Tap(input::kA);
      // espera sair do mapa de jogo e o arquivo do save mudar (até 30 s)
      const auto ts = steady_clock::now();
      FileFingerprint f1;
      bool at_title = false, file_changed = false;
      std::string quit_trace;
      uint16_t last_map = map_play;
      while (steady_clock::now() - ts < seconds(30)) {
        std::this_thread::sleep_for(milliseconds(250));
        const uint16_t m = ReadU16(kMapIdAddr);
        if (m != last_map) {
          char b[48];
          std::snprintf(b, sizeof b, "%smapa 0x%X@%.1fs", quit_trace.empty() ? "" : " ", m,
                        duration<double>(steady_clock::now() - ts).count());
          quit_trace += b;
          last_map = m;
        }
        at_title = m != map_play;
        f1 = Fingerprint(save_file);
        file_changed = f1.exists && f_pre.exists && f1.fnv != f_pre.fnv;
        if (at_title && file_changed) break;
      }
      std::this_thread::sleep_for(milliseconds(1500));  // gravação assíncrona do runtime
      f1 = Fingerprint(save_file);
      const uint64_t eeprom1 = eeprom_hash();
      if (f1.exists) {
        std::ofstream(std::filesystem::path(base) += "_savegame.dat", std::ios::binary)
            .write(reinterpret_cast<const char*>(f1.bytes.data()), std::streamsize(f1.size));
      }
      const bool ok = nav.reached && complete(during) && paused && at_title && f1.exists &&
                      f_pre.exists && f1.fnv != f_pre.fnv;
      std::ostringstream o;
      o << "{\"version\":1,\"ok\":" << (ok ? "true" : "false") << ",\"navigate\":\""
        << JsonEscape(nav.trace) << "\",\"items_set\":" << set_now << ",\"by_kind\":[";
      for (int k = 0; k < pc::progress::kKindCount; ++k) o << (k ? "," : "") << by_kind[k];
      o << "],\"complete_in_memory\":" << (complete(during) ? 1 : 0) << ",\"before_got\":[";
      for (int k = 0; k < pc::progress::kKindCount; ++k) o << (k ? "," : "") << before.got[k];
      o << "],\"paused\":" << (paused ? 1 : 0) << ",\"left_play_map\":" << (at_title ? 1 : 0)
        << ",\"map_play\":\"0x" << std::hex << map_play << std::dec << "\",\"quit_trace\":\""
        << quit_trace << "\",\"eeprom_changed\":" << (eeprom1 != eeprom0 ? 1 : 0)
        << ",\"file_before_save\":{\"exists\":" << (f_pre.exists ? 1 : 0)
        << ",\"fnv64\":\"" << Hex64(f_pre.fnv) << "\"}"
        << ",\"file_before\":{\"exists\":" << (f0.exists ? 1 : 0) << ",\"size\":" << f0.size
        << ",\"fnv64\":\"" << Hex64(f0.fnv) << "\"},\"file_after\":{\"exists\":"
        << (f1.exists ? 1 : 0) << ",\"size\":" << f1.size << ",\"fnv64\":\"" << Hex64(f1.fnv)
        << "\"},\"save_file\":\""
        << JsonEscape(std::string(reinterpret_cast<const char*>(save_file.u8string().c_str())))
        << "\",\"rows\":";
      write_rows(o, during);
      o << "}\n";
      std::ofstream(opts.make_advanced_save, std::ios::binary | std::ios::trunc) << o.str();
      Finish(ok ? 0 : 2);
    }

    // 2) Usa o save: carrega JOGO 1 do disco pelo menu, como um jogador, e confere o rastreador
    //    (100% em todos os mundos), a contagem do PRÓPRIO jogo (54 células), a tela "Totais",
    //    o painel do rastreador e 10 s de jogo estável a 60 FPS com o save carregado.
    if (!opts.probe_advanced_save.empty()) {
      if (!isolated_user_data) refuse_shared(opts.probe_advanced_save);
      std::filesystem::path base = opts.probe_advanced_save;
      base.replace_extension();
      const std::filesystem::path save_file = SaveFilePath(opts.user_data_root);
      const FileFingerprint f0 = Fingerprint(save_file);
      input::SetActive(true);
      const NavResult nav = NavigateToGameplayByState(70.0);
      const uint16_t map_loaded = ReadU16(kMapIdAddr);
      for (int i = 0; i < 2; ++i) {
        Tap(input::kB);
        WaitFrames(35);
      }
      WaitFrames(30);
      const pc::progress::Snapshot snap = pc::progress::Read();
      const auto rows = GameCountMatrix(snap);
      int mismatches = 0;
      for (const auto& r : rows)
        if (r.game != r.tracker) ++mismatches;
      // estabilidade: anda 3 s e mede 5 s de quadros com o save carregado
      const Vec3 p0 = ReadPlayerPos();
      input::Get().thumb_ly = 32767;
      WaitFrames(180);
      input::Get().thumb_ly = 0;
      const Vec3 p1 = ReadPlayerPos();
      const double moved = std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.z - p0.z) * (p1.z - p0.z));
      // tela "Totais" do jogo: pausa -> VER TOTAIS (como o auto-teste: B fecha diálogos, Start)
      bool totals_ok = false;
      std::string menu_trace;
      auto note_state = [&](const char* what) {
        char b[64];
        std::snprintf(b, sizeof b, "%s%s:menu=%u,cena=%u", menu_trace.empty() ? "" : " ", what,
                      ReadU8(0x8266EF55), ReadU8(0x8266EF30));
        menu_trace += b;
      };
      note_state("antes");
      bool paused = false;
      // diálogos com tarja (ex.: "VAMOS LÁ PROCURAR PELO TAL JIMIUÍGU") deixam a cena em 0 e
      // ignoram o Start: B avança o diálogo, como um jogador faria
      for (int attempt = 0; attempt < 12 && !paused; ++attempt) {
        for (int i = 0; i < (attempt ? 3 : 1); ++i) {
          Tap(input::kB);
          WaitFrames(45);
        }
        Tap(input::kStart);
        WaitFrames(110);
        paused = ReadU8(0x8266EF55) == 1;
        note_state("start");
        if (attempt == 0) {
          if (rex::ui::RawImage im = capture(); im.width) {
            ReduceTo1920(im);
            WritePng(std::filesystem::path(base) += "_start.png", im.width, im.height,
                     im.data.data(), im.stride);
          }
        }
      }
      if (paused) {
        WaitFrames(40);
        Tap(input::kDpadDown);
        WaitFrames(40);
        Tap(input::kA);
        WaitFrames(200);
        if (rex::ui::RawImage im = capture(); im.width) {
          totals_ok = !LooksBlank(im);
          ReduceTo1920(im);
          WritePng(std::filesystem::path(base) += "_totals.png", im.width, im.height,
                   im.data.data(), im.stride);
        }
        note_state("totais");
        for (int i = 0; i < 4 && ReadU8(0x8266EF55) != 0; ++i) {
          Tap(input::kB);
          WaitFrames(80);
        }
        note_state("fechado");
      }
      // painel "PROGRESSO POR MUNDO" (janela inteira: jogo + sobreposição)
      bool panel_ok = false;
      pc::Get().show_collectibles = true;
      WaitFrames(40);
      if (rex::ui::RawImage win = CaptureWindowOnScreen(); win.width && !LooksBlank(win)) {
        ReduceTo1920(win);
        WritePng(std::filesystem::path(base) += "_tracker.png", win.width, win.height,
                 win.data.data(), win.stride);
        panel_ok = true;
      }
      pc::Get().show_collectibles = false;
      note_state("andou");
      const uint64_t s0 = state::guest_swaps.load();
      std::this_thread::sleep_for(seconds(5));
      const double fps = double(state::guest_swaps.load() - s0) / 5.0;
      if (rex::ui::RawImage im = capture(); im.width) {
        ReduceTo1920(im);
        WritePng(std::filesystem::path(base) += "_end.png", im.width, im.height, im.data.data(),
                 im.stride);
      }
      const bool ok = f0.exists && nav.reached && complete(snap) && mismatches == 0 &&
                      totals_ok && panel_ok && moved > 20.0 && fps >= 55.0;
      std::ostringstream o;
      o << "{\"version\":1,\"ok\":" << (ok ? "true" : "false") << ",\"save_file\":{\"exists\":"
        << (f0.exists ? 1 : 0) << ",\"size\":" << f0.size << ",\"fnv64\":\"" << Hex64(f0.fnv)
        << "\"},\"navigate\":\"" << JsonEscape(nav.trace) << "\",\"map_loaded\":\"0x" << std::hex
        << map_loaded << std::dec << "\",\"complete\":" << (complete(snap) ? 1 : 0)
        << ",\"got\":[";
      for (int k = 0; k < pc::progress::kKindCount; ++k) o << (k ? "," : "") << snap.got[k];
      o << "],\"total\":[";
      for (int k = 0; k < pc::progress::kKindCount; ++k) o << (k ? "," : "") << snap.total[k];
      o << "],\"game_count_cells\":" << rows.size() << ",\"game_count_mismatches\":" << mismatches
        << ",\"totals_screen\":" << (totals_ok ? 1 : 0) << ",\"tracker_panel\":"
        << (panel_ok ? 1 : 0) << ",\"menu_trace\":\"" << menu_trace << "\",\"moved\":" << moved
        << ",\"fps\":" << fps
        << ",\"game_count_rows\":[";
      for (size_t i = 0; i < rows.size(); ++i)
        o << (i ? "," : "") << "{\"world\":\"" << JsonEscape(snap.worlds[rows[i].w].name)
          << "\",\"kind\":\"" << pc::progress::KindName(pc::progress::Kind(rows[i].k))
          << "\",\"game\":" << rows[i].game << ",\"tracker\":" << rows[i].tracker << "}";
      o << "],\"rows\":";
      write_rows(o, snap);
      o << "}\n";
      std::ofstream(opts.probe_advanced_save, std::ios::binary | std::ios::trunc) << o.str();
      Finish(ok ? 0 : 2);
    }

    if (!opts.probe_quit.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      Tap(input::kStart);
      WaitFrames(160);
      for (int i = 0; i < 5; ++i) {
        Tap(input::kDpadDown);
        WaitFrames(30);
      }
      rex::ui::RawImage before = capture();
      if (before.width) {
        std::filesystem::path png = opts.probe_quit;
        png.replace_extension();
        png += "_menu.png";
        WritePng(png, before.width, before.height, before.data.data(), before.stride);
      }
      Tap(input::kA);
      WaitFrames(300);
      rex::ui::RawImage after = capture();
      if (after.width) {
        std::filesystem::path png = opts.probe_quit;
        png.replace_extension();
        png += "_after.png";
        ReduceTo1920(after);
        WritePng(png, after.width, after.height, after.data.data(), after.stride);
      }
      // confirma "SIM" (grava o jogo e volta ao título)
      Tap(input::kA);
      WaitFrames(600);
      DumpGuestRange(opts.probe_quit, 0x826CB620, 0xA11);
      Finish(0);
    }

    if (!opts.probe_items.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      WaitFrames(80);
      const char* names[9] = {"azul", "fogo", "gelo", "granada", "relogio",
                              "?",    "vermelha", "dourada", "glowbo"};
      auto dump = [&](const char* suffix) {
        std::filesystem::path bin = opts.probe_items;
        bin.replace_extension();
        bin += suffix;
        bin += ".bin";
        DumpGuestRange(bin, 0x826CB620, 0xA11);
      };
      auto counts = [&](std::ostringstream& o) {
        o << "[";
        for (int i = 0; i < 9; ++i) o << (i ? "," : "") << ReadItemCount(i);
        o << "]";
      };
      dump("_before");
      std::ostringstream before;
      counts(before);
      Tap(input::kB);
      WaitFrames(90);
      dump("_after_fire");
      std::ostringstream after;
      counts(after);
      input::Get().thumb_ly = 32767;
      WaitFrames(240);
      input::Get().thumb_ly = 0;
      WaitFrames(30);
      dump("_after_walk");
      std::ostringstream walk;
      counts(walk);
      std::ostringstream o;
      o << "{\"version\":1,\"before\":" << before.str() << ",\"after_fire\":" << after.str()
        << ",\"after_walk\":" << walk.str() << "}\n";
      std::filesystem::path meta = opts.probe_items;
      meta.replace_extension(".json");
      std::ofstream(meta) << o.str();
      Finish(0);
    }

    if (!opts.probe_scan.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      WaitFrames(120);
      auto* mem = rex::system::kernel_state()->memory();
      const uint8_t* p = mem->virtual_membase();
      std::ostringstream o;
      o << "{\"version\":1,\"magic\":\"KHJC\",\"hits\":[";
      bool first = true;
      int n = 0;
      for (uint32_t a = 0x80000000; a < 0xC0000000 && n < 40; a += 4) {
        if (p[a] == 0x4B && p[a + 1] == 0x48 && p[a + 2] == 0x4A && p[a + 3] == 0x43) {
          o << (first ? "" : ",") << "{\"addr\":\"0x" << std::hex << a << std::dec << "\"}";
          first = false;
          ++n;
        }
      }
      o << "]}\n";
      std::ofstream(opts.probe_scan) << o.str();
      Finish(0);
    }

    if (!opts.probe_pause.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      std::vector<MemBlock> s0 = SnapshotMemory();
      Tap(input::kStart);
      WaitFrames(140);
      std::vector<MemBlock> s1 = SnapshotMemory();
      Tap(input::kStart);
      WaitFrames(140);
      std::vector<MemBlock> s2 = SnapshotMemory();
      DumpChangedPages(opts.probe_pause, s0, s1, s2);
      rex::ui::RawImage im = capture();
      if (im.width) {
        std::filesystem::path png = opts.probe_pause;
        png.replace_extension(".png");
        WritePng(png, im.width, im.height, im.data.data(), im.stride);
      }
      Finish(0);
    }

    if (!opts.probe_menu.empty()) {
      // Sonda da entrada "CONFIGURAÇÕES DE PC": registra os textos que cada menu pede
      // (id, chamador), abre o menu de pausa, desce até a 7ª entrada (a acrescentada) e confirma.
      pc::state::string_log_enabled.store(true);
      input::SetActive(true);
      const uint8_t* p = rex::system::kernel_state()->memory()->virtual_membase();
      auto be32 = [&](uint32_t a) {
        return (uint32_t(p[a]) << 24) | (uint32_t(p[a + 1]) << 16) | (uint32_t(p[a + 2]) << 8) |
               p[a + 3];
      };
      std::ostringstream o;
      o << "{\"version\":3,\"phases\":[";
      bool first_phase = true;
      auto shot = [&](const char* suffix) {
        rex::ui::RawImage im = capture();
        if (!im.width) return;
        std::filesystem::path png = opts.probe_menu;
        png.replace_extension();
        png += suffix;
        WritePng(png, im.width, im.height, im.data.data(), im.stride);
      };
      auto dump_phase = [&](const char* name) {
        o << (first_phase ? "" : ",") << "{\"name\":\"" << name
          << "\",\"pause\":" << int(ReadU8(0x8266EF55))
          << ",\"label_requests\":" << pc::state::pc_entry_label_requests.load()
          << ",\"activations\":" << pc::state::pc_entry_activations.load()
          << ",\"menu_open\":" << (pc::menu::IsOpen() ? 1 : 0) << ",\"strings\":[";
        first_phase = false;
        bool first = true;
        const uint32_t table = be32(0x826A60E8);
        for (int i = 0; i < pc::state::kStringLogSize; ++i) {
          const uint64_t key = pc::state::string_log_key[i].load();
          if (!key) continue;
          const uint32_t id = uint32_t(key), lr = uint32_t(key >> 32);
          std::string text;
          if (table >= 0x40000000u && id < 20000) {
            const uint32_t sp = be32(table + id * 4);
            for (uint32_t k = 0; sp && k < 40 && p[sp + k]; ++k) {
              const char c = char(p[sp + k]);
              text += (c >= 32 && c < 127 && c != '"' && c != '\\') ? c : '?';
            }
          }
          o << (first ? "" : ",") << "{\"id\":" << id << ",\"lr\":\"0x" << std::hex << lr
            << std::dec << "\",\"n\":" << pc::state::string_log_count[i].load()
            << ",\"text\":\"" << text << "\"}";
          first = false;
          pc::state::string_log_key[i].store(0);
          pc::state::string_log_count[i].store(0);
        }
        o << "]}";
      };
      NavigateToGameplay();
      WaitFrames(80);
      dump_phase("title_and_gameplay");
      for (int tries = 0; tries < 6 && ReadU8(0x8266EF55) != 1; ++tries) {
        Tap(input::kStart, 12);
        WaitFrames(80);
      }
      WaitFrames(60);
      dump_phase("pause");
      shot("_pause.png");
      auto nav_down = [&](int hold_frames) {
        input::Get().thumb_ly = 32767;
        input::SetButton(input::kDpadDown, true);
        WaitFrames(hold_frames);
        input::SetButton(input::kDpadDown, false);
        input::Get().thumb_ly = 0;
        WaitFrames(25);
      };
      nav_down(45);
      nav_down(45);
      nav_down(45);
      nav_down(45);
      nav_down(45);
      nav_down(45);
      WaitFrames(20);
      dump_phase("cursor_on_entry");
      shot("_entry.png");
      Tap(input::kA, 12);
      WaitFrames(90);
      dump_phase("after_confirm");
      shot("_confirm.png");
      {
        rex::ui::RawImage im = CaptureWindowOnScreen();
        if (im.width) {
          std::filesystem::path png = opts.probe_menu;
          png.replace_extension();
          png += "_screen.png";
          WritePng(png, im.width, im.height, im.data.data(), im.stride);
        }
      }
      o << "]}\n";
      std::filesystem::path meta = opts.probe_menu;
      meta.replace_extension(".json");
      std::ofstream(meta) << o.str();
      Finish(0);
    }

    if (!opts.probe_game.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      std::ostringstream o;
      o << "{\"version\":1,\"frames\":" << state::guest_swaps.load()
        << ",\"scene\":" << state::cutscene_state.load();
      const Vec3 p0 = ReadPlayerPos();
      o << ",\"pos0\":[" << p0.x << "," << p0.y << "," << p0.z << "]";
      // tela de pausa (mostra vida e contadores) antes de sair andando
      Tap(input::kStart);
      WaitFrames(110);
      rex::ui::RawImage pause_img = capture();
      if (pause_img.width) {
        std::filesystem::path png = opts.probe_game;
        png.replace_extension();
        png += "_pause.png";
        WritePng(png, pause_img.width, pause_img.height, pause_img.data.data(),
                 pause_img.stride);
      }
      Tap(input::kStart);
      WaitFrames(20);
      // caminhada para a frente
      input::Get().thumb_ly = 32767;
      WaitFrames(120);
      input::Get().thumb_ly = 0;
      WaitFrames(10);
      const Vec3 p1 = ReadPlayerPos();
      o << ",\"pos1\":[" << p1.x << "," << p1.y << "," << p1.z << "]";
      o << ",\"move_delta\":" << std::sqrt((p1.x - p0.x) * (p1.x - p0.x) +
                                           (p1.z - p0.z) * (p1.z - p0.z));
      // pulo
      const Vec3 j0 = ReadPlayerPos();
      float max_y = j0.y;
      input::SetButton(input::kA, true);
      WaitFrames(5);
      input::SetButton(input::kA, false);
      for (int i = 0; i < 40; ++i) {
        WaitFrames(2);
        const Vec3 p = ReadPlayerPos();
        if (p.y > max_y) max_y = p.y;
      }
      o << ",\"jump_base\":" << j0.y << ",\"jump_max\":" << max_y;
      // contadores de itens (0 = ovos azuis, 6 = penas vermelhas, 7 = douradas)
      o << ",\"eggs\":[" << ReadItemCount(0) << "," << ReadItemCap(0) << "]";
      o << ",\"red_feathers\":[" << ReadItemCount(6) << "," << ReadItemCap(6) << "]";
      o << ",\"gold_feathers\":[" << ReadItemCount(7) << "," << ReadItemCap(7) << "]";
      o << "}\n";
      std::ofstream(opts.probe_game) << o.str();
      auto save = [&](const char* suffix) {
        rex::ui::RawImage im = capture();
        if (!im.width) return;
        std::filesystem::path png = opts.probe_game;
        png.replace_extension();
        png += suffix;
        png += ".png";
        WritePng(png, im.width, im.height, im.data.data(), im.stride);
      };
      // dispara um ovo: a barra de informações (HUD) aparece por alguns instantes
      Tap(input::kB);
      WaitFrames(4);
      save("_fire");
      // andar mais um pouco para sair da área do tutorial
      input::Get().thumb_ly = 32767;
      WaitFrames(240);
      input::Get().thumb_ly = 0;
      WaitFrames(20);
      save("_walk");
      Finish(0);
    }

    if (!opts.probe_position.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      // calibração: duas caminhadas curtas para a frente e três fotos da memória
      std::vector<MemBlock> s0 = SnapshotMemory();
      input::Get().thumb_ly = 32767;
      WaitFrames(80);
      input::Get().thumb_ly = 0;
      WaitFrames(12);
      std::vector<MemBlock> s1 = SnapshotMemory();
      input::Get().thumb_ly = 32767;
      WaitFrames(80);
      input::Get().thumb_ly = 0;
      WaitFrames(12);
      std::vector<MemBlock> s2 = SnapshotMemory();
      size_t bytes = 0;
      for (const auto& b : s0) bytes += b.data.size();
      // guarda as três fotos para análise fora do jogo (não faz parte do produto)
      std::filesystem::path base = opts.probe_position;
      base.replace_extension();
      DumpChangedPages(base.string() + "_diff.bin", s0, s1, s2);
      std::vector<Candidate> cand = FindMovingTriples(s0, s1, s2);
      WriteCandidates(opts.probe_position, cand, bytes);
      rex::ui::RawImage final_img = capture();
      if (final_img.width) {
        // grava um PNG auxiliar ao lado do JSON para conferir a fase alcançada
        std::filesystem::path png = opts.probe_position;
        png.replace_extension(".png");
        WritePng(png, final_img.width, final_img.height, final_img.data.data(), final_img.stride);
      }
      Finish(0);
    }

    // Sonda da distância de desenho: no jogo livre (mesmo caminho do auto-teste), sem mover
    // o personagem, captura a mesma cena nos três perfis e compara por quadrantes, para
    // validar que Longe/Total só acrescentam desenho novo (e que o Total cobre o alcance
    // prometido). Grava também o alcance original máximo pedido pelo jogo no mapa.
    if (!opts.probe_drawdistance.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      for (int i = 0; i < 3; ++i) {
        Tap(input::kB);
        WaitFrames(30);
      }
      WaitFrames(150);
      const uint16_t map = ReadU16(kMapIdAddr);
      static constexpr struct {
        const char* name;
        pc::DrawDistance dd;
      } kProfiles[] = {
          {"Original", pc::DrawDistance::Original},
          {"Longe", pc::DrawDistance::Longe},
          {"Total", pc::DrawDistance::Total},
      };
      rex::ui::RawImage imgs[3];
      uint64_t calls[3] = {0, 0, 0};
      uint32_t max_orig[3] = {0, 0, 0};
      std::filesystem::path base = opts.probe_drawdistance;
      base.replace_extension();
      for (int p = 0; p < 3; ++p) {
        pc::Get().draw_distance = kProfiles[p].dd;
        state::draw_range_calls.store(0);
        state::max_draw_range_original.store(0);
        WaitFrames(120);
        calls[p] = state::draw_range_calls.load();
        max_orig[p] = state::max_draw_range_original.load();
        imgs[p] = capture();
        if (imgs[p].width) {
          ReduceTo1920(imgs[p]);
          std::filesystem::path png = base;
          png += "_" + std::string(kProfiles[p].name) + ".png";
          WritePng(png, imgs[p].width, imgs[p].height, imgs[p].data.data(), imgs[p].stride);
        }
      }
      std::ostringstream o;
      o << "{\"version\":1,\"map\":\"0x" << std::hex << map << std::dec << "\",\"profiles\":[";
      for (int p = 0; p < 3; ++p) {
        o << (p ? "," : "") << "{\"name\":\"" << kProfiles[p].name << "\",\"range_calls\":"
          << calls[p] << ",\"max_original_range\":0x" << std::hex << max_orig[p] << std::dec
          << "}";
      }
      static constexpr int kPairs[3][2] = {{1, 0}, {2, 0}, {2, 1}};
      o << "],\"diffs\":[";
      for (int i = 0; i < 3; ++i) {
        const DiffStats s = CompareImages(imgs[kPairs[i][0]], imgs[kPairs[i][1]]);
        o << (i ? "," : "") << "{\"a\":\"" << kProfiles[kPairs[i][0]].name << "\",\"b\":\""
          << kProfiles[kPairs[i][1]].name << "\",\"samples\":" << s.samples << ",\"differing\":"
          << s.differing << ",\"differing_pct\":"
          << (s.samples ? 100.0 * s.differing / s.samples : 0) << ",\"mean_abs_diff\":"
          << s.mean_abs_diff << ",\"quadrants\":[" << s.quad_differing[0] << ","
          << s.quad_differing[1] << "," << s.quad_differing[2] << "," << s.quad_differing[3]
          << "]}";
      }
      o << "]}\n";
      std::ofstream(opts.probe_drawdistance) << o.str();
      Finish(0);
    }

    // Entrada "CONFIGURAÇÕES DE PC" no menu de TÍTULO: mesma ação 0x822DEC40 do menu de
    // pausa, agora executada de verdade. Para não confirmar a entrada errada, a sonda usa o
    // registro de pedidos de texto do jogo (pc::state::string_log): o menu pede o rótulo da
    // entrada em que o cursor está; só se o id 1 (CONFIGURAÇÕES DE PC) for pedido durante a
    // navegação a sonda confirma com A e mede a ativação e a abertura do menu de PC.
    // Sonda do cursor dos menus do jogo: no menu de título, fotografa o objeto da tela
    // (0x82648110), a tabela de páginas (tela+0x154, 0x2C bytes cada; tela+0x2C é a PÁGINA
    // atual, não o cursor) e as entradas da página atual (0x88 bytes cada) antes e depois de
    // cada toque para baixo/cima, com uma foto da tela por passo. A análise fora do jogo
    // procura o campo que acompanha os toques.
    if (!opts.probe_menucursor.empty()) {
      input::SetActive(true);
      std::filesystem::path base = opts.probe_menucursor;
      base.replace_extension();
      auto* mem = rex::system::kernel_state()->memory();
      const uint8_t* mb = mem->virtual_membase();
      auto rd32 = [&](uint32_t a) {
        const uint8_t* p = mb + a;
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
      };
      auto hex = [&](uint32_t a, uint32_t len) {
        static const char* d = "0123456789abcdef";
        std::string s;
        if (a < 0x80000000u || len > 0x4000) return s;
        s.reserve(len * 2);
        for (uint32_t i = 0; i < len; ++i) {
          s += d[mb[a + i] >> 4];
          s += d[mb[a + i] & 15];
        }
        return s;
      };
      constexpr uint32_t kScreen = 0x82648110;
      std::ostringstream o;
      o << "{\"version\":1,\"screen\":" << kScreen << ",\"steps\":[";
      int n = 0;
      auto snap = [&](const char* label) {
        const uint32_t page = rd32(kScreen + 0x2C);
        const uint32_t pages = rd32(kScreen + 0x154);
        uint32_t entries = 0, count = 0;
        if (pages >= 0x80000000u && page < 32) {
          entries = rd32(pages + page * 0x2C);
          count = rd32(pages + page * 0x2C + 4);
        }
        if (count > 32) count = 0;
        o << (n++ ? "," : "") << "\n{\"label\":\"" << label << "\",\"frame\":"
          << state::guest_swaps.load() << ",\"page\":" << page << ",\"pages\":" << pages
          << ",\"entries\":" << entries << ",\"count\":" << count << ",\"screen_hex\":\""
          << hex(kScreen, 0x400) << "\",\"pages_hex\":\"" << hex(pages, 12 * 0x2C)
          << "\",\"entries_hex\":\"" << hex(entries, count * 0x88) << "\"}";
        rex::ui::RawImage im = capture();
        if (im.width) {
          std::filesystem::path png = base;
          png += "_";
          png += label;
          png += ".png";
          WritePng(png, im.width, im.height, im.data.data(), im.stride);
        }
      };
      while (state::guest_swaps.load() < 1500 && steady_clock::now() - t0 < seconds(200))
        std::this_thread::sleep_for(milliseconds(10));
      WaitFrames(60);
      Tap(input::kStart, 12);
      WaitFrames(180);
      snap("s0_open");
      Tap(input::kDpadDown, 8);
      WaitFrames(40);
      snap("s1_dpad_down");
      Tap(input::kDpadDown, 8);
      WaitFrames(40);
      snap("s2_dpad_down");
      Tap(input::kDpadUp, 8);
      WaitFrames(40);
      snap("s3_dpad_up");
      input::Get().thumb_ly = -32767;  // analógico para baixo
      WaitFrames(8);
      input::Get().thumb_ly = 0;
      WaitFrames(40);
      snap("s4_stick_down");
      input::Get().thumb_ly = 32767;  // analógico para cima
      WaitFrames(8);
      input::Get().thumb_ly = 0;
      WaitFrames(40);
      snap("s5_stick_up");
      Tap(input::kDpadUp, 8);
      WaitFrames(40);
      snap("s6_dpad_up");
      o << "]}\n";
      std::ofstream(opts.probe_menucursor) << o.str();
      Finish(0);
    }

    if (!opts.probe_titlepc.empty()) {
      pc::state::string_log_enabled.store(true);
      input::SetActive(true);
      std::filesystem::path base = opts.probe_titlepc;
      base.replace_extension();
      auto shot = [&](const char* suffix) {
        rex::ui::RawImage im = capture();
        if (!im.width) return;
        std::filesystem::path png = base;
        png += suffix;
        png += ".png";
        WritePng(png, im.width, im.height, im.data.data(), im.stride);
      };
      // espera a tela de título (com "Início rápido" desligado ela chega ~1500-2000 quadros)
      while (state::guest_swaps.load() < 1500 && steady_clock::now() - t0 < seconds(200))
        std::this_thread::sleep_for(milliseconds(10));
      WaitFrames(60);
      Tap(input::kStart, 12);
      WaitFrames(180);  // o menu de título é montado aqui
      auto nav = [&](uint16_t button, int16_t ly, int hold_frames) {
        input::Get().thumb_ly = ly;
        input::SetButton(button, true);
        WaitFrames(hold_frames);
        input::SetButton(button, false);
        input::Get().thumb_ly = 0;
        WaitFrames(25);
      };
      shot("_titlemenu");
      // Receita determinística que deixa o cursor sobre a entrada: 8x cima leva o cursor
      // (tela+7, ver --probe-menucursor) até JOGAR e 1x baixo até CONFIGURAÇÕES DE PC. Como o
      // menu de título não repede o rótulo ao mover o cursor (diferente do menu de pausa),
      // a sonda confirma com A nessa posição e confere a ativação (0x822DEC40) e o menu.
      auto settle_top = [&]() {
        for (int i = 0; i < 8; ++i) nav(input::kDpadUp, -32767, 25);
      };
      int found_pos = -1;
      bool activated = false;
      bool opened = false;
      int attempts = 0;
      settle_top();  // linha de base, igual à corrida de calibração
      for (int attempt = 0; attempt < 3 && !opened; ++attempt) {
        if (attempt > 0) {
          // recuperação: tenta reaproximar do estado base depois do B
          settle_top();
          settle_top();
          settle_top();
        }
        settle_top();                       // varredura k=1 (0 para baixo)
        settle_top();                       // varredura k=2, primeira parte
        nav(input::kDpadDown, 32767, 45);   // k=2: 1 para baixo -> entrada de PC
        WaitFrames(15);
        {
          char buf[32];
          std::snprintf(buf, sizeof buf, "_try%d", attempt + 1);
          shot(buf);
        }
        const uint32_t a0 = pc::state::pc_entry_activations.load();
        Tap(input::kA, 12);
        for (int w = 0; w < 60 && !opened; ++w) {
          WaitFrames(2);
          opened = pc::menu::IsOpen();
        }
        activated = pc::state::pc_entry_activations.load() > a0;
        ++attempts;
        if (opened) {
          found_pos = 2;
          break;
        }
        // o A atingiu outra entrada (submenu): volta com B e tenta de novo
        Tap(input::kB, 12);
        WaitFrames(90);
        Tap(input::kB, 12);
        WaitFrames(90);
      }
      std::ostringstream o;
      o << "{\"version\":1,\"position_found\":" << found_pos
        << ",\"activated\":" << (activated ? 1 : 0) << ",\"menu_opened\":" << (opened ? 1 : 0)
        << ",\"attempts\":" << attempts << "}\n";
      std::ofstream(opts.probe_titlepc) << o.str();
      if (opened) {
        WaitFrames(20);  // alguns quadros da interface com o menu já desenhado
        rex::ui::RawImage im = CaptureWindowOnScreen();
        if (im.width) {
          ReduceTo1920(im);
          std::filesystem::path png = base;
          png += "_pcmenu.png";
          WritePng(png, im.width, im.height, im.data.data(), im.stride);
        }
        pc::state::menu_close_request.store(true);
        WaitFrames(10);
      }
      Finish(0);
    }

    // Tela cheia pelo menu DO JOGO durante o jogo (relato de 2026-11-12: "durante a gameplay ao
    // configurar tela cheia o jogo crasha"; relatório crash-20261112-220036: 0xC0000005 no driver
    // de vídeo dentro de D3D12Presenter::PaintAndPresentImpl). Do jogo livre: F1 (página de PC),
    // GRÁFICOS, MODO e direita três vezes (SEM BORDAS, TELA CHEIA, JANELA), medindo depois de
    // cada troca se o jogo e a tela continuam apresentando quadros e o estado real da janela.
    // O JSON é regravado a cada passo: se o processo cair, o último passo gravado mostra onde.
    if (!opts.probe_fullscreen.empty()) {
      input::SetActive(true);
      using clk = std::chrono::steady_clock;
      std::ostringstream o;
      o << "{\"version\":1";
      auto flush = [&](const std::string& tail) {
        std::ofstream(opts.probe_fullscreen, std::ios::binary | std::ios::trunc) << o.str() << tail;
      };
      auto pg_state = [&](uint32_t* page, uint32_t* count, uint32_t* cursor) {
        *page = ReadU32(kScreenObjAddr + 0x2C);
        *cursor = ReadU8(kScreenObjAddr + 7);
        *count = 0;
        const uint32_t pages = ReadU32(kScreenObjAddr + 0x154);
        if (pages >= 0x40000000u && pages < 0xA0000000u && *page < 12)
          *count = ReadU32(pages + *page * 0x2C + 4);
        return ReadU8(kScreenObjAddr + 4);
      };
      auto wait_until = [&](auto pred, double secs) {
        const auto end = clk::now() + std::chrono::milliseconds(int(secs * 1000));
        while (clk::now() < end) {
          if (pred()) return true;
          std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        return pred();
      };
      const NavResult nav = NavigateToGameplayByState(70.0);
      for (int i = 0; i < 2; ++i) {
        Tap(input::kB);
        WaitFrames(35);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      o << ",\"jogo_livre\":" << (nav.reached ? 1 : 0);
      flush("}\n");
      const uint32_t kPc = pc::ingame::PageIndex();
      uint32_t page = 0, count = 0, cursor = 0;
      pc::ingame::RequestToggle();
      const bool at_root = wait_until([&] {
        return pg_state(&page, &count, &cursor) == 6 && page == kPc && count == 5;
      }, 10.0);
      std::this_thread::sleep_for(std::chrono::milliseconds(800));
      bool at_graphics = false, at_mode = false;
      if (at_root) {
        Tap(input::kA, 12);
        at_graphics = wait_until([&] {
          return pg_state(&page, &count, &cursor) == 6 && page == kPc &&
                 int(count) == pc::menu::options::RowCount(0) && pc::ingame::GetStatus().view == 0;
        }, 6.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        if (at_graphics) {
          Tap(input::kDpadDown, 10);
          std::this_thread::sleep_for(std::chrono::milliseconds(400));
          pg_state(&page, &count, &cursor);
          at_mode = cursor == 1;
        }
      }
      o << ",\"pagina_pc\":" << (at_root ? 1 : 0) << ",\"graficos\":" << (at_graphics ? 1 : 0)
        << ",\"linha_modo\":" << (at_mode ? 1 : 0) << ",\"passos\":[";
      flush("]}\n");
      bool all_alive = at_mode;
      auto measure = [&](const char* name, int index) {
        rex::ui::GetHostPresentStats(true);
        const uint64_t g0 = state::guest_swaps.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        const uint64_t guest = state::guest_swaps.load() - g0;
        const rex::ui::HostPresentStats h = rex::ui::GetHostPresentStats(true);
        bool fs = false;
        int cw = 0, ch = 0;
        LONG_PTR style = 0;
        ui->CallInUIThreadSynchronous([&]() {
          if (!window) return;
          fs = window->IsFullscreen();
          HWND hwnd = reinterpret_cast<HWND>(window->GetNativeWindowHandle());
          RECT rc{};
          if (hwnd && GetClientRect(hwnd, &rc)) {
            cw = rc.right - rc.left;
            ch = rc.bottom - rc.top;
          }
          style = hwnd ? GetWindowLongPtrW(hwnd, GWL_STYLE) : 0;
        });
        const auto mode = pc::Get().display_mode;
        const bool alive = guest > 60 && h.presents > 60;
        all_alive = all_alive && alive;
        char b[512];
        std::snprintf(b, sizeof b,
                      "%s{\"passo\":\"%s\",\"modo_salvo\":%d,\"tela_cheia\":%d,\"cliente\":\"%dx%d\","
                      "\"estilo\":\"0x%08llX\",\"quadros_jogo_2_5s\":%llu,\"apresentacoes_2_5s\":%llu,"
                      "\"frametime_ms\":%.2f,\"desvio_ms\":%.2f,\"vivo\":%d}",
                      index ? "," : "", name, int(mode), fs ? 1 : 0, cw, ch,
                      (unsigned long long)(uint64_t(style) & 0xFFFFFFFFull), (unsigned long long)guest,
                      (unsigned long long)h.presents, h.mean_ms, h.stddev_ms, alive ? 1 : 0);
        o << b;
        flush("]}\n");
      };
      if (at_mode) {
        measure("janela_inicial", 0);
        const char* names[] = {"sem_bordas", "tela_cheia", "janela_de_novo"};
        for (int i = 0; i < 3; ++i) {
          Tap(input::kDpadRight, 10);
          std::this_thread::sleep_for(std::chrono::milliseconds(1500));
          measure(names[i], i + 1);
        }
      }
      o << "],\"ok\":" << (all_alive ? 1 : 0) << "}\n";
      flush("");
      Finish(all_alive ? 0 : 7);
    }

    // Configurações de PC DENTRO do menu do jogo (pedido de 2026-11-12, MECHANICS.md §18), no
    // menu principal e pelo controle virtual: entra pela CONFIGURAÇÃO DA RECOMPILAÇÃO, visita as
    // 5 seções e rola cada uma até o fim (uma captura a cada 5 linhas), troca e desfaz um valor
    // de cvar (ANISOTROPIA, aplicado na thread da interface), volta com B e abre/fecha pelo F1.
    // Lê tudo da memória do jogo (página, cursor, rótulos em entrada+0x3B).
    if (!opts.probe_pcpages.empty()) {
      input::SetActive(true);
      std::filesystem::path base = opts.probe_pcpages;
      base.replace_extension();
      auto shot = [&](const std::string& suffix) {
        rex::ui::RawImage im = capture();
        if (!im.width) return;
        ReduceTo1920(im);
        std::filesystem::path png = base;
        png += suffix;
        WritePng(png, im.width, im.height, im.data.data(), im.stride);
      };
      auto utf8 = [](const std::string& latin1) {
        std::string o;
        for (unsigned char c : latin1) {
          if (c == '"' || c == '\\') {
            o += '\\';
            o += char(c);
          } else if (c < 0x80) {
            o += char(c);
          } else {
            o += char(0xC0 | (c >> 6));
            o += char(0x80 | (c & 0x3F));
          }
        }
        return o;
      };
      struct PView {
        uint32_t state = 0, page = 99, cursor = 0, count = 0;
        bool title = false;
        std::vector<std::string> labels;
      };
      auto pview = [&]() {
        PView v;
        v.state = ReadU8(kScreenObjAddr + 4);
        v.title = ReadU8(kScreenObjAddr + 0x198) == 0;
        v.page = ReadU32(kScreenObjAddr + 0x2C);
        v.cursor = ReadU8(kScreenObjAddr + 7);
        const uint32_t pages = ReadU32(kScreenObjAddr + 0x154);
        if (pages < 0x40000000u || pages >= 0xA0000000u || v.page >= 12) return v;
        const uint32_t entries = ReadU32(pages + v.page * 0x2C);
        const uint32_t count = ReadU32(pages + v.page * 0x2C + 4);
        if (entries < 0x40000000u || entries >= 0xA0000000u || count > 64) return v;
        v.count = count;
        for (uint32_t i = 0; i < count; ++i) {
          std::string s;
          for (uint32_t k = 0; k < 0x40; ++k) {
            const uint8_t c = ReadU8(entries + i * 0x88 + 0x3B + k);
            if (!c) break;
            s += char(c);
          }
          v.labels.push_back(s);
        }
        return v;
      };
      auto wait_for = [&](auto pred, int frames) {
        for (int w = 0; w < frames / 2; ++w) {
          if (pred(pview())) return true;
          WaitFrames(2);
        }
        return pred(pview());
      };
      auto press = [&](uint16_t b) {
        input::SetButton(b, true);
        WaitFrames(10);
        input::SetButton(b, false);
        WaitFrames(16);
      };
      auto move_to = [&](uint32_t target) {
        for (int i = 0; i < 48; ++i) {
          const PView v = pview();
          if (v.cursor == target) return true;
          press(v.cursor < target ? input::kDpadDown : input::kDpadUp);
        }
        return pview().cursor == target;
      };
      const uint32_t kPc = pc::ingame::PageIndex();
      std::ostringstream o;
      o << "{\"version\":1";
      while (ReadU16(kMapIdAddr) != 0x14F && steady_clock::now() - t0 < seconds(200))
        std::this_thread::sleep_for(milliseconds(20));
      WaitFrames(90);
      PView v0;
      for (int tries = 0; tries < 6; ++tries) {
        Tap(input::kStart, 12);
        WaitFrames(150);
        v0 = pview();
        if (v0.title && v0.page == 0 && v0.count >= 4) break;
      }
      const std::string kRecomp =
          PortLabel("CONFIGURA\xC7\xC3O DA RECOMPILA\xC7\xC3O", "RECOMPILATION SETTINGS");
      int recomp = -1;
      for (size_t i = 0; i < v0.labels.size(); ++i)
        if (v0.labels[i] == kRecomp) recomp = int(i);
      bool root_ok = false;
      if (recomp >= 0 && move_to(uint32_t(recomp))) {
        press(input::kA);
        root_ok = wait_for([&](const PView& v) { return v.page == kPc && v.count == 5 && v.state == 6; }, 180);
        WaitFrames(120);
        shot("_raiz.png");
      }
      o << ",\"titulo\":" << (v0.title ? 1 : 0) << ",\"raiz\":" << (root_ok ? 1 : 0)
        << ",\"secoes\":[";
      bool all_ok = root_ok;
      int max_len = 0;
      std::string max_label;
      bool cvar_changed = false, cvar_restored = false;
      std::string cvar_before, cvar_mid, cvar_after, aniso_label;
      const int sections = pc::menu::options::SectionCount();
      for (int s = 0; root_ok && s < sections; ++s) {
        bool sec_ok = move_to(uint32_t(s));
        const int rows = pc::menu::options::RowCount(s);
        if (sec_ok) {
          press(input::kA);
          sec_ok = wait_for([&](const PView& v) {
            return v.page == kPc && int(v.count) == rows && v.state == 6 &&
                   pc::ingame::GetStatus().view == s;
          }, 180);
          WaitFrames(120);
        }
        const PView sv = pview();
        int shots = 0;
        if (sec_ok) {
          shot("_s" + std::to_string(s) + "_0.png");
          ++shots;
          for (int r = 1; r < rows; ++r) {
            press(input::kDpadDown);
            if (r % 5 == 4 || r == rows - 1) {
              WaitFrames(20);
              shot("_s" + std::to_string(s) + "_" + std::to_string(r) + ".png");
              ++shots;
            }
          }
          sec_ok = pview().cursor == uint32_t(rows - 1);
          if (s == 0) {  // GRÁFICOS: ANISOTROPIA (cvar anisotropic_override) troca e desfaz
            int an = -1;
            for (int i = 0; i < rows; ++i)
              if (pc::menu::options::RowText(0, i).rfind(PortLabel("ANISOTROPIA", "ANISOTROPY"), 0) == 0)
                an = i;
            if (an >= 0 && move_to(uint32_t(an))) {
              cvar_before = rex::cvar::GetFlagByName("anisotropic_override");
              press(input::kDpadRight);
              WaitFrames(20);
              cvar_mid = rex::cvar::GetFlagByName("anisotropic_override");
              aniso_label = utf8(pview().labels.size() > size_t(an) ? pview().labels[size_t(an)] : "");
              shot("_s0_anisotropia.png");
              press(input::kDpadLeft);
              WaitFrames(20);
              cvar_after = rex::cvar::GetFlagByName("anisotropic_override");
              cvar_changed = cvar_mid != cvar_before;
              cvar_restored = cvar_after == cvar_before;
            }
          }
        }
        press(input::kB);
        const bool back = wait_for([&](const PView& v) {
          return v.page == kPc && v.count == 5 && v.state == 6 && v.cursor == uint32_t(s);
        }, 180);
        WaitFrames(30);
        o << (s ? "," : "") << "{\"secao\":\"" << pc::menu::options::SectionTitle(s)  // UTF-8
          << "\",\"linhas\":" << rows << ",\"abriu\":" << (sec_ok ? 1 : 0)
          << ",\"capturas\":" << shots << ",\"volta_no_cursor\":" << (back ? 1 : 0) << ",\"rotulos\":[";
        for (size_t i = 0; i < sv.labels.size(); ++i) {
          o << (i ? "," : "") << "\"" << utf8(sv.labels[i]) << "\"";
          if (int(sv.labels[i].size()) > max_len) {
            max_len = int(sv.labels[i].size());
            max_label = sv.labels[i];
          }
        }
        o << "]}";
        all_ok = all_ok && sec_ok && back;
      }
      o << "],\"anisotropia\":{\"antes\":\"" << cvar_before << "\",\"depois_direita\":\""
        << cvar_mid << "\",\"rotulo\":\"" << aniso_label << "\",\"depois_esquerda\":\""
        << cvar_after << "\",\"trocou\":" << (cvar_changed ? 1 : 0)
        << ",\"desfez\":" << (cvar_restored ? 1 : 0) << "}";
      o << ",\"maior_rotulo\":\"" << utf8(max_label) << "\",\"maior_rotulo_bytes\":" << max_len;
      // B na raiz: volta ao menu principal com o cursor na entrada da recompilação
      bool back_title = false;
      if (root_ok) {
        press(input::kB);
        back_title = wait_for([&](const PView& v) {
          return v.title && v.page == 0 && v.state == 6 && v.cursor == uint32_t(recomp);
        }, 240);
        WaitFrames(30);
      }
      // F1 no menu principal: vai à página 7; F1 de novo: volta (no título, um B)
      bool f1_open = false, f1_close = false;
      if (back_title) {
        pc::ingame::RequestToggle();
        f1_open = wait_for([&](const PView& v) { return v.page == kPc && v.count == 5 && v.state == 6; }, 240);
        WaitFrames(120);
        shot("_f1.png");
        pc::ingame::RequestToggle();
        f1_close = wait_for([&](const PView& v) { return v.page == 0 && v.state == 6; }, 240);
      }
      const auto st = pc::ingame::GetStatus();
      all_ok = all_ok && back_title && f1_open && f1_close && cvar_changed && cvar_restored &&
               !pc::menu::IsOpen();
      o << ",\"volta_ao_titulo_na_entrada\":" << (back_title ? 1 : 0)
        << ",\"f1_abre\":" << (f1_open ? 1 : 0) << ",\"f1_fecha\":" << (f1_close ? 1 : 0)
        << ",\"janela_sobreposta\":" << (pc::menu::IsOpen() ? 1 : 0)
        << ",\"trocas_de_pagina\":" << st.switches << ",\"f1_saltos\":" << st.f1_jumps
        << ",\"linhas_reescritas\":" << st.refreshes << ",\"ok\":" << (all_ok ? 1 : 0) << "}\n";
      std::ofstream(opts.probe_pcpages) << o.str();
      Finish(all_ok ? 0 : 7);
    }

    // Menu principal (pedido de 2026-11-11), no jogo real e pelo controle virtual:
    //  1) lê os rótulos da página 0 do menu de título (entrada+0x3B, Latin-1);
    //     e confere a ordem pedida (JOGAR, CONQUISTAS, AJUDA E OPÇÕES, CONFIGURAÇÃO DA
    //     RECOMPILAÇÃO, SAIR PARA O DESKTOP) e que CLASSIFICAÇÕES saiu;
    //  2) entra em AJUDA E OPÇÕES e volta com B (o cursor deve voltar à mesma entrada);
    //  3) leva o cursor do jogo (tela+7) à penúltima entrada, confere "CONFIGURAÇÃO DA
    //     RECOMPILAÇÃO", aperta A e confere que o menu de PC (o mesmo do F1) abriu e que o menu
    //     do jogo ficou onde estava;
    //  4) leva o cursor à última entrada, confere "SAIR PARA O DESKTOP", grava o JSON e aperta A.
    //     O programa deve fechar sozinho pelo caminho normal (código 0, motivo no relatório de
    //     boot); se ainda estiver aberto 20 s depois, o JSON diz isso e a sonda sai com 9.
    if (!opts.probe_titlemenu.empty()) {
      input::SetActive(true);
      std::filesystem::path base = opts.probe_titlemenu;
      base.replace_extension();
      auto shot_guest = [&](const char* suffix) {
        rex::ui::RawImage im = capture();
        if (!im.width) return;
        ReduceTo1920(im);
        std::filesystem::path png = base;
        png += suffix;
        WritePng(png, im.width, im.height, im.data.data(), im.stride);
      };
      auto json_text = [](const std::string& latin1) {
        std::string out;
        for (unsigned char c : latin1) {
          if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
          } else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            out += b;
          } else if (c < 0x80) {
            out += char(c);
          } else {  // Latin-1 -> UTF-8
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
          }
        }
        return out;
      };
      struct MenuView {
        bool title = false;
        uint32_t page = 0, cursor = 0, count = 0;
        std::vector<std::string> labels;
      };
      auto view = [&]() {
        MenuView v;
        v.title = ReadU8(kScreenObjAddr + 0x198) == 0;
        v.page = ReadU32(kScreenObjAddr + 0x2C);
        v.cursor = ReadU8(kScreenObjAddr + 7);
        const uint32_t pages = ReadU32(kScreenObjAddr + 0x154);
        if (pages < 0x40000000u || pages >= 0xA0000000u || v.page >= 16) return v;
        const uint32_t entries = ReadU32(pages + v.page * 0x2C);
        const uint32_t count = ReadU32(pages + v.page * 0x2C + 4);
        if (entries < 0x40000000u || entries >= 0xA0000000u || count > 12) return v;
        v.count = count;
        for (uint32_t i = 0; i < count; ++i) {
          std::string s;
          for (uint32_t k = 0; k < 0x40; ++k) {
            const uint8_t c = ReadU8(entries + i * 0x88 + 0x3B + k);
            if (!c) break;
            s += char(c);
          }
          v.labels.push_back(s);
        }
        return v;
      };
      auto nav = [&](uint16_t button, int16_t ly) {
        input::Get().thumb_ly = ly;
        input::SetButton(button, true);
        WaitFrames(30);
        input::SetButton(button, false);
        input::Get().thumb_ly = 0;
        WaitFrames(25);
      };
      // Um passo de cada vez, conferido no cursor do jogo (tela+7): primeiro só o direcional;
      // se o cursor não andar, só o analógico (para baixo = -32767, como na sonda
      // --probe-menucursor). Direcional e analógico juntos em sentidos opostos não andam no
      // menu de título (1ª execução desta sonda: cursor parado em JOGAR).
      std::string trace;  // passos: d/u = direcional, D/U = analógico, com o cursor depois
      auto step = [&](bool down) {
        const uint32_t before = view().cursor;
        nav(down ? input::kDpadDown : input::kDpadUp, 0);
        uint32_t now = view().cursor;
        trace += (down ? "d" : "u") + std::to_string(before) + ">" + std::to_string(now) + " ";
        if (now != before) return;
        input::Get().thumb_ly = down ? -32767 : 32767;
        WaitFrames(12);
        input::Get().thumb_ly = 0;
        WaitFrames(25);
        now = view().cursor;
        trace += (down ? "D" : "U") + std::to_string(before) + ">" + std::to_string(now) + " ";
      };
      auto move_to = [&](uint32_t target) {
        for (int i = 0; i < 12; ++i) {
          const MenuView v = view();
          if (v.cursor == target) return true;
          step(v.cursor < target);
        }
        return view().cursor == target;
      };
      // Rótulos do port no idioma do jogo: decididos depois que o jogo carregou os textos.
      std::string kRecomp, kExit;
      std::ostringstream o;
      o << "{\"version\":1";
      // tela de título: espera o mapa do título e abre o menu com Start
      while (ReadU16(kMapIdAddr) != 0x14F && steady_clock::now() - t0 < seconds(200))
        std::this_thread::sleep_for(milliseconds(20));
      WaitFrames(90);
      MenuView v0;
      for (int tries = 0; tries < 6; ++tries) {
        Tap(input::kStart, 12);
        WaitFrames(150);
        v0 = view();
        if (v0.title && v0.page == 0 && v0.count >= 4) break;
      }
      shot_guest("_1_menu.png");
      kRecomp = PortLabel("CONFIGURA\xC7\xC3O DA RECOMPILA\xC7\xC3O", "RECOMPILATION SETTINGS");
      kExit = PortLabel("SAIR PARA O DESKTOP", "EXIT TO DESKTOP");
      o << ",\"title_menu\":" << (v0.title && v0.page == 0 && v0.count >= 4 ? 1 : 0)
        << ",\"count\":" << v0.count << ",\"labels\":[";
      for (size_t i = 0; i < v0.labels.size(); ++i)
        o << (i ? "," : "") << "\"" << json_text(v0.labels[i]) << "\"";
      o << "],\"recomp_entry_built\":" << pc::state::title_recomp_entry_built.load()
        << ",\"exit_entry_built\":" << pc::state::title_exit_entry_built.load();
      const bool has_leaderboards =
          std::find(v0.labels.begin(), v0.labels.end(), GameText(1)) !=
          v0.labels.end();
      // ordem pedida: a entrada da recompilação logo acima de SAIR PARA O DESKTOP
      const std::string kHelp = GameText(3);  // Latin-1, como na entrada
      const std::vector<std::string> kExpected = {GameText(0), GameText(2), kHelp, kRecomp, kExit};
      const bool order_ok = v0.labels == kExpected;
      const uint32_t recomp_idx = v0.count >= 2 ? v0.count - 2 : 0;
      const bool recomp_above_exit = v0.labels.size() >= 2 && v0.labels[recomp_idx] == kRecomp;
      const bool last_is_exit = !v0.labels.empty() && v0.labels.back() == kExit;
      o << ",\"classificacoes_ausente\":" << (has_leaderboards ? 0 : 1)
        << ",\"ordem_pedida\":" << (order_ok ? 1 : 0)
        << ",\"recompilacao_acima_de_sair\":" << (recomp_above_exit ? 1 : 0)
        << ",\"ultima_e_sair\":" << (last_is_exit ? 1 : 0);
      // 2) AJUDA E OPÇÕES entra e volta (com uma entrada a menos, o cursor volta ao lugar?)
      bool help_ok = false;
      {
        const auto it = std::find(v0.labels.begin(), v0.labels.end(), kHelp);
        if (it != v0.labels.end()) {
          const uint32_t help_idx = uint32_t(it - v0.labels.begin());
          if (move_to(0) && move_to(help_idx)) {
            Tap(input::kA, 12);
            WaitFrames(90);
            const uint32_t page_in = view().page;
            Tap(input::kB, 12);
            WaitFrames(90);
            const MenuView vb = view();
            help_ok = page_in != 0 && vb.title && vb.page == 0 && vb.cursor == help_idx;
            o << ",\"ajuda_pagina\":" << page_in << ",\"ajuda_volta_cursor\":" << vb.cursor;
          }
        }
        o << ",\"ajuda_entra_e_volta\":" << (help_ok ? 1 : 0);
      }
      // 3) CONFIGURAÇÃO DA RECOMPILAÇÃO -> Configurações de PC DENTRO do menu do jogo
      //    (pedido de 2026-11-12: página 7 do próprio menu, MECHANICS.md §18), e B volta.
      const bool at_recomp = recomp_above_exit && move_to(recomp_idx) &&
                             view().labels.size() > recomp_idx &&
                             view().labels[view().cursor] == kRecomp;
      const uint32_t switches0 = pc::ingame::GetStatus().switches;
      bool opened = false;
      if (at_recomp) {
        WaitFrames(10);
        Tap(input::kA, 12);
        for (int w = 0; w < 90 && !opened; ++w) {
          WaitFrames(2);
          const MenuView vp = view();
          opened = vp.title && vp.page == pc::ingame::PageIndex() && vp.count == 5;
        }
      }
      const uint32_t activated = pc::ingame::GetStatus().switches - switches0;
      if (opened) {
        WaitFrames(150);  // as linhas aparecem letra a letra
        shot_guest("_2_configuracao.png");
        Tap(input::kB, 12);
        WaitFrames(90);
      }
      const MenuView v1 = view();
      const bool closed = !pc::menu::IsOpen() && v1.page == 0;
      const bool stayed = v1.title && v1.page == 0 && v1.cursor == recomp_idx;
      o << ",\"cursor_na_recompilacao\":" << (at_recomp ? 1 : 0) << ",\"ativada\":" << activated
        << ",\"menu_pc_aberto\":" << (opened ? 1 : 0) << ",\"menu_pc_fechado\":" << (closed ? 1 : 0)
        << ",\"menu_do_jogo_no_lugar\":" << (stayed ? 1 : 0);
      // 4) SAIR PARA O DESKTOP
      const uint32_t last = v1.count ? v1.count - 1 : 0;
      const bool at_exit = v1.count >= 4 && move_to(last) && view().labels.size() == v1.count &&
                           view().labels[view().cursor] == kExit;
      WaitFrames(10);
      shot_guest("_3_sair.png");
      o << ",\"cursor_na_saida\":" << (at_exit ? 1 : 0) << ",\"navegacao\":\"" << trace << "\"";
      const bool ready = order_ok && recomp_above_exit && last_is_exit && !has_leaderboards &&
                         help_ok && opened && activated > 0 && closed && stayed && at_exit;
      o << ",\"pronto_para_sair\":" << (ready ? 1 : 0);
      auto flush = [&](const std::string& tail) {
        std::ofstream(opts.probe_titlemenu) << o.str() << tail << "}\n";
      };
      flush("");
      if (!at_exit) Finish(8);
      // 5) Regra do original (MECHANICS.md §15): a escolha abre a confirmação do jogo
      //    (0x822DE260) com título = texto 5 (aqui SAIR PARA O DESKTOP) e pergunta = texto 53
      //    "VOCÊ TEM CERTEZA?". Caixa em *(tela+0x38): título em +0x24, pergunta em +0x28.
      constexpr uint32_t kDialogOff = mechanics::kScreenDialogOff;
      auto read_cstr = [&](uint32_t a) {
        std::string t;
        if (a < 0x10000u || a >= 0xFFFF0000u) return t;  // heap do jogo ou do sistema
        for (uint32_t k = 0; k < 160; ++k) {
          const uint8_t c = ReadU8(a + k);
          if (!c) break;
          t += char(c);
        }
        return t;
      };
      auto dialog_ptr = [&]() {
        const uint32_t dlg = ReadU32(kScreenObjAddr + kDialogOff);
        return (dlg >= 0x40000000u && dlg < 0xA0000000u) ? dlg : 0u;
      };
      auto open_dialog = [&]() {
        const uint32_t sel0 = pc::state::title_exit_activations.load();
        Tap(input::kA, 12);
        for (int w = 0; w < 90; ++w) {
          WaitFrames(2);
          if (pc::state::title_exit_activations.load() > sel0 && dialog_ptr()) break;
        }
        WaitFrames(45);  // a caixa anima ao abrir
        return dialog_ptr();
      };
      const uint32_t dlg = open_dialog();
      const uint32_t dlg_title_ptr = dlg ? ReadU32(dlg + 0x24) : 0;
      const std::string dlg_title = read_cstr(dlg_title_ptr);
      const std::string dlg_question = dlg ? read_cstr(ReadU32(dlg + 0x28)) : "";
      shot_guest("_4_confirmacao.png");
      const bool dlg_ok = dlg && dlg_title == kExit && dlg_question == GameText(53);
      o << ",\"confirmacao_aberta\":" << (dlg ? 1 : 0) << ",\"confirmacao_titulo\":\""
        << json_text(dlg_title) << "\",\"confirmacao_pergunta\":\"" << json_text(dlg_question)
        << "\",\"confirmacao_ok\":" << (dlg_ok ? 1 : 0)
        << ",\"titulo_trocado\":" << pc::state::title_exit_dialog_labels.load()
        << ",\"confirmacao_titulo_ptr\":" << dlg_title_ptr;
      flush("");
      if (!dlg) Finish(10);
      // 6) Botões da caixa (captura da rodada de 2026-11-12: "(A) SIM" e "(B) NÃO", como no
      //    original). Primeiro B = NÃO: tem de chamar 0x822DDA70 (cancelado++), fechar a caixa,
      //    deixar o menu principal como estava e o programa aberto. Depois reabre e A = SIM:
      //    tem de chamar 0x822DD720 (confirmado++) e o app fecha (conferido no relatório de boot).
      bool no_ok = false;
      {
        const uint32_t c0 = pc::state::title_exit_cancelled.load();
        Tap(input::kB, 12);
        for (int w = 0; w < 120 && pc::state::title_exit_cancelled.load() == c0; ++w)
          WaitFrames(2);
        WaitFrames(60);
        const MenuView vn = view();
        // (o ponteiro da caixa em tela+0x38 não é zerado ao fechar: 0x822E2538 só libera)
        no_ok = pc::state::title_exit_cancelled.load() > c0 && vn.title && vn.page == 0 &&
                vn.count == v1.count && vn.cursor == last &&
                !pc::state::title_exit_confirmed.load() &&
                !pc::state::exit_to_desktop_request.load();
        shot_guest("_5_nao.png");
        o << ",\"nao_volta_ao_menu\":" << (no_ok ? 1 : 0)
          << ",\"nao_cancelado\":" << (pc::state::title_exit_cancelled.load() - c0)
          << ",\"menu_apos_nao\":" << vn.count;
      }
      const bool reopened = move_to(last) && open_dialog() != 0;
      o << ",\"reaberta\":" << (reopened ? 1 : 0);
      flush(",\"sim_apertado\":1");
      if (!reopened) Finish(11);
      // a caixa reaberta depois do NÃO leva mais tempo para aceitar o A (volta do estado 4):
      // espera e repete o A até 3 vezes, registrando quantos foram precisos
      int sim_presses = 0;
      WaitFrames(60);
      for (int k = 0; k < 3 && !pc::state::title_exit_confirmed.load(); ++k) {
        ++sim_presses;
        flush(",\"sim_apertado\":" + std::to_string(sim_presses));
        Tap(input::kA, 12);
        for (int w = 0; w < 90 && !pc::state::title_exit_confirmed.load(); ++w) WaitFrames(2);
      }
      // o app fecha pela thread da interface; se ainda estiver aberto em 20 s, registra
      std::this_thread::sleep_for(seconds(20));
      flush(",\"sim_apertado\":" + std::to_string(sim_presses) + ",\"sim_confirmado\":" +
            std::to_string(pc::state::title_exit_confirmed.load()) +
            ",\"programa_ainda_aberto_20s\":1");
      Finish(9);
    }

    // Gamepad: estado do XInput real nos 4 slots + teste sintético que atravessa o MESMO
    // caminho de navegação do menu de PC (FeedGamepad -> ImGui), sem precisar de controle
    // físico: LB/RB trocam de aba, o direcional move o foco e B/Start fecham o menu.
    if (!opts.probe_gamepad.empty()) {
      input::SetActive(true);
      std::filesystem::path base = opts.probe_gamepad;
      base.replace_extension();
      std::ostringstream o;
      o << "{\"version\":1,\"xinput\":[";
      const auto slots = pc::menu::QueryXInput();
      for (int i = 0; i < 4; ++i) {
        o << (i ? "," : "") << "{\"slot\":" << i << ",\"loaded\":" << (slots[i].loaded ? 1 : 0)
          << ",\"connected\":" << slots[i].connected << ",\"error\":\"0x" << std::hex
          << slots[i].error << std::dec << "\",\"buttons\":\"0x" << std::hex << slots[i].buttons
          << std::dec << "\"}";
      }
      o << "],\"synthetic\":{";
      auto feed = [&](uint16_t buttons, int hold) {
        pc::menu::GamepadState s{buttons, 0, 0, 0, 0, 0, 0};
        pc::menu::SetSyntheticGamepad(&s);
        WaitFrames(hold);
        pc::menu::SetSyntheticGamepad(nullptr);
        WaitFrames(4);
      };
      // abre o menu de PC como a entrada do jogo faz (menu_open_request)
      pc::state::menu_open_request.store(true);
      bool opened = false;
      for (int w = 0; w < 60 && !opened; ++w) {
        WaitFrames(2);
        opened = pc::menu::IsOpen();
      }
      o << "\"opened\":" << (opened ? 1 : 0);
      o << ",\"tab_start\":" << pc::menu::CurrentTab();
      feed(0x0200, 8);  // RB
      o << ",\"tab_after_rb\":" << pc::menu::CurrentTab();
      feed(0x0200, 8);
      o << ",\"tab_after_rb2\":" << pc::menu::CurrentTab();
      feed(0x0200, 8);
      o << ",\"tab_after_rb3\":" << pc::menu::CurrentTab();
      feed(0x0100, 8);  // LB
      o << ",\"tab_after_lb\":" << pc::menu::CurrentTab();
      // navegação de verdade: vai para a aba Jogo (3), move o foco com o direcional até o
      // primeiro controle ("60 FPS") e ativa com A, verificando se a caixa alterna — isso
      // prova direcional + A de ponta a ponta (os valores são restaurados logo em seguida;
      // os três são protegidos porque o foco pode cair na caixa ao lado).
      feed(0x0200, 8);  // RB -> aba Jogo (3)
      o << ",\"tab_before_nav\":" << pc::menu::CurrentTab();
      // fotografia completa das configurações (restaurada no fim, antes de salvar)
      const pc::Settings s_before = pc::Get();
      // foco de navegação preso à caixa "60 FPS" por alguns quadros (ponto de partida
      // conhecido), depois liberado: o direcional move o foco para o próximo controle e A
      // ativa. O campo que mudar diz se o foco andou (lock60) ou não (fps60).
      pc::menu::SetGamepadTestFocus(true);
      WaitFrames(4);
      pc::menu::SetGamepadTestFocus(false);
      std::string nav_log;
      auto log_trace = [&](const char* tag) {
        const auto t = pc::menu::TakeGamepadTrace();
        bool na = false, aa = false;
        int tab = -1;
        uint32_t nav_id = 0, active_id = 0, pin_id = 0;
        int down_seen = 0, move_seen = 0;
        for (const auto& e : t) {
          if (e.nav_active) na = true;
          if (e.any_active) aa = true;
          tab = e.tab;
          if (e.nav_id) nav_id = e.nav_id;
          if (e.active_id) active_id = e.active_id;
          pin_id = e.pin_id;
          down_seen = std::max(down_seen, e.dpad_down);
          move_seen = std::max(move_seen, e.move_dir);
        }
        char buf[224];
        std::snprintf(buf, sizeof buf,
                      "%s{frames=%zu,nav=%d,active=%d,tab=%d,nav_id=%u,active_id=%u,pin=%u,"
                      "dpad_down=%d,move_dir=%d};",
                      tag, t.size(), na ? 1 : 0, aa ? 1 : 0, tab, nav_id, active_id, pin_id,
                      down_seen, move_seen);
        nav_log += buf;
      };
      // observa onde o foco ficou depois do alfinete, sem apertar nada (estado vazio)
      {
        pc::menu::GamepadState z{0, 0, 0, 0, 0, 0, 0};
        pc::menu::SetSyntheticGamepad(&z);
        WaitFrames(4);
        pc::menu::SetSyntheticGamepad(nullptr);
        WaitFrames(3);
      }
      log_trace("focus0");
      feed(0x0002, 12);  // direcional para BAIXO (XInput: 0x0002): foco no próximo controle
      log_trace("down1");
      feed(0x1000, 12);  // A: ativa o controle em foco (caixa "Ritmo travado em 60")
      log_trace("a");
      WaitFrames(6);
      auto changed_fields = [&]() {
        std::string c;
        const pc::Settings s_now = pc::Get();
        auto chk = [&](const char* name, auto a, auto b) {
          if (a != b) {
            if (!c.empty()) c += ",";
            c += name;
          }
        };
        chk("fps60", s_now.fps60, s_before.fps60);
        chk("lock60", s_now.lock60, s_before.lock60);
        chk("scenes60", s_now.scenes60, s_before.scenes60);
        chk("draw_distance", int(s_now.draw_distance), int(s_before.draw_distance));
        chk("dialog_speed", s_now.dialog_speed, s_before.dialog_speed);
        chk("show_fps", s_now.show_fps, s_before.show_fps);
        chk("show_perf_stats", s_now.show_perf_stats, s_before.show_perf_stats);
        chk("show_collectibles", s_now.show_collectibles, s_before.show_collectibles);
        chk("pause_on_focus_loss", s_now.pause_on_focus_loss, s_before.pause_on_focus_loss);
        chk("skip_intro_logos", s_now.skip_intro_logos, s_before.skip_intro_logos);
        chk("save_backup", s_now.save_backup, s_before.save_backup);
        chk("backup_count", s_now.backup_count, s_before.backup_count);
        return c;
      };
      const std::string changed = changed_fields();
      pc::Get() = s_before;  // restaura tudo antes de salvar (CloseMenu grava as opções)
      o << ",\"nav_activated_field\":\"" << changed << "\",\"nav_log\":\"" << nav_log << "\"";
      // B fecha o menu e o jogo fica sem entrada por um instante (bloqueio de 300 ms)
      feed(0x2000, 6);
      o << ",\"closed_by_b\":" << (pc::menu::IsOpen() ? 0 : 1)
        << ",\"input_blocked\":" << (pc::menu::BlocksGameInput() ? 1 : 0);
      WaitFrames(30);  // deixa o bloqueio de 300 ms expirar antes de reabrir
      // reabre e fecha com Start
      pc::state::menu_open_request.store(true);
      bool reopened = false;
      for (int w = 0; w < 60 && !reopened; ++w) {
        WaitFrames(2);
        reopened = pc::menu::IsOpen();
      }
      feed(0x0010, 10);
      WaitFrames(8);
      o << ",\"reopened\":" << (reopened ? 1 : 0)
        << ",\"closed_by_start\":" << (pc::menu::IsOpen() ? 0 : 1);
      // evidência visual: o menu aberto por cima do jogo
      pc::state::menu_open_request.store(true);
      for (int w = 0; w < 60 && !pc::menu::IsOpen(); ++w) WaitFrames(2);
      WaitFrames(20);
      {
        rex::ui::RawImage im = CaptureWindowOnScreen();
        if (im.width) {
          ReduceTo1920(im);
          std::filesystem::path png = base;
          png += "_menu.png";
          WritePng(png, im.width, im.height, im.data.data(), im.stride);
        }
      }
      pc::state::menu_close_request.store(true);
      o << "}}\n";
      std::ofstream(opts.probe_gamepad) << o.str();
      Finish(0);
    }

    // Sonda dos mapas visitados no boot: quais mapas o jogo carrega (logos, tela de título,
    // cenas de introdução e jogo livre) e o maior alcance original pedido em cada um — a
    // distância de desenho por perfil foi validada por imagem em Spiral Mountain; esta sonda
    // mostra a cobertura nos demais mapas que a inicialização visita.
    if (!opts.probe_dd_maps.empty()) {
      input::SetActive(true);
      std::filesystem::path base = opts.probe_dd_maps;
      base.replace_extension();
      struct Seen {
        uint16_t map;
        uint64_t first_swap, last_swap;
        uint32_t max_range;
        std::string phase;
        bool shot;
      };
      std::vector<Seen> seen;
      auto capture_new = [&](const char* phase) {
        const uint16_t map = ReadU16(kMapIdAddr);
        const uint32_t mr = pc::state::max_draw_range_original.load();
        for (auto& s : seen) {
          if (s.map == map && s.phase == phase) {
            s.last_swap = state::guest_swaps.load();
            s.max_range = std::max(s.max_range, mr);
            return;
          }
        }
        seen.push_back({map, state::guest_swaps.load(), state::guest_swaps.load(), mr, phase,
                        false});
        rex::ui::RawImage im = capture();
        if (!im.width) return;
        ReduceTo1920(im);
        std::filesystem::path png = base;
        char name[80];
        std::snprintf(name, sizeof name, "_%s_map%X.png", phase, unsigned(map));
        png += name;
        seen.back().shot = WritePng(png, im.width, im.height, im.data.data(), im.stride);
      };
      // fase 1: logos + tela de título (sem entrada)
      while (state::guest_swaps.load() < 1600 && steady_clock::now() - t0 < seconds(200)) {
        capture_new("titulo");
        WaitFrames(30, 4000);
      }
      // fase 2: menu de título -> JOGAR -> JOGO 1 -> cenas de introdução SEM pular
      Tap(input::kStart, 12);
      WaitFrames(180);
      Tap(input::kA, 12);
      WaitFrames(180);
      Tap(input::kA, 12);
      WaitFrames(240);
      const auto intro_end = steady_clock::now() + seconds(150);
      while (steady_clock::now() < intro_end) {
        capture_new("intro");
        WaitFrames(30, 4000);
      }
      // fase 3: pular as cenas restantes e chegar ao jogo livre (Spiral Mountain)
      for (int i = 0; i < 5; ++i) {
        Tap(input::kB, 12);
        WaitFrames(200);
      }
      WaitFrames(120);
      capture_new("jogo");
      WaitFrames(60);
      capture_new("jogo");
      std::ostringstream o;
      o << "{\"version\":1,\"maps\":[";
      bool first = true;
      for (const auto& s : seen) {
        o << (first ? "" : ",") << "{\"map\":\"0x" << std::hex << s.map << std::dec
          << "\",\"phase\":\"" << s.phase << "\",\"first_swap\":" << s.first_swap
          << ",\"last_swap\":" << s.last_swap << ",\"max_original_range\":\"0x" << std::hex
          << s.max_range << std::dec << "\",\"shot\":" << (s.shot ? 1 : 0) << "}";
        first = false;
      }
      o << "]}\n";
      std::ofstream(opts.probe_dd_maps) << o.str();
      Finish(0);
    }

    // Sessão longa (--probe-longa <json> [--longa-minutos N], M04 C-7): joga por N minutos
    // (padrão 60) viajando pelos mundos com a rotina de viagem do jogo e, em cada um, corre,
    // pula, gira a câmera e abre/fecha a pausa. A cada minuto grava FPS, atualizações da
    // lógica, memória, handles e mapa; um travamento (sem quadro novo por 15 s) encerra e
    // fica registrado. O registro de sessão (logs\sessao-*.txt) traz os blocos de desempenho.
    if (!opts.probe_longa.empty()) {
      input::SetActive(true);
      int minutes = 60;
      {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i + 1 < argc; ++i)
          if (std::wstring(argv[i]) == L"--longa-minutos") minutes = std::max(1, _wtoi(argv[i + 1]));
        if (argv) LocalFree(argv);
      }
      NavigateToGameplay();
      WaitStableGameplay(120);
      static constexpr struct {
        uint16_t map;
        uint8_t entry;
      } kMaps[] = {{0x112, 5}, {0x128, 8}, {0x127, 1}, {0x136, 0x1F}, {0x142, 1},
                   {0x15D, 5}, {0x12D, 5}, {0x124, 8}, {0xD6, 8},    {0x163, 3},
                   {0xBB, 4},  {0x18A, 1}, {0x1A7, 1}, {0x1A8, 1},  {0x1A9, 1},
                   {0x100, 1}, {0xB8, 1},  {0xC7, 1}};
      using clk = std::chrono::steady_clock;
      const auto start = clk::now();
      const auto end = start + std::chrono::minutes(minutes);
      std::ostringstream samples, visits;
      int nsamples = 0, nvisits = 0, warps_ok = 0, warps_fail = 0, pauses_ok = 0;
      bool hang = false;
      std::string hang_detail;
      auto last_sample = clk::now();
      uint64_t s_swaps = state::guest_swaps.load(), s_logic = state::logic_updates.load();
      auto mem = []() {
        PROCESS_MEMORY_COUNTERS_EX pm{};
        pm.cb = sizeof(pm);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm),
                             sizeof(pm));
        return pm;
      };
      auto write = [&](bool finished) {
        std::ostringstream o;
        const double secs = std::chrono::duration<double>(clk::now() - start).count();
        o << "{\"version\":1,\"minutes_requested\":" << minutes << ",\"seconds\":" << secs
          << ",\"finished\":" << (finished ? 1 : 0) << ",\"hang\":" << (hang ? 1 : 0)
          << ",\"hang_detail\":\"" << JsonEscape(hang_detail) << "\",\"warps_ok\":" << warps_ok
          << ",\"warps_fail\":" << warps_fail << ",\"pauses_ok\":" << pauses_ok
          << ",\"visits\":" << nvisits << ",\"samples\":[" << samples.str()
          << "],\"visit_log\":[" << visits.str() << "]}\n";
        std::ofstream(opts.probe_longa, std::ios::binary | std::ios::trunc) << o.str();
      };
      // vigia de travamento: quadros parados por 15 s
      auto alive = [&]() {
        const uint64_t a = state::guest_swaps.load();
        for (int i = 0; i < 150; ++i) {
          if (state::guest_swaps.load() != a) return true;
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
      };
      auto sample = [&]() {
        const auto now = clk::now();
        const double dt = std::chrono::duration<double>(now - last_sample).count();
        if (dt < 60.0) return;
        const uint64_t sw = state::guest_swaps.load(), lg = state::logic_updates.load();
        const auto pm = mem();
        DWORD handles = 0;
        GetProcessHandleCount(GetCurrentProcess(), &handles);
        samples << (nsamples++ ? "," : "") << "{\"t\":"
                << std::chrono::duration<double>(now - start).count()
                << ",\"fps\":" << double(sw - s_swaps) / dt
                << ",\"logic\":" << double(lg - s_logic) / dt
                << ",\"private_mb\":" << double(pm.PrivateUsage) / 1048576.0
                << ",\"working_set_mb\":" << double(pm.WorkingSetSize) / 1048576.0
                << ",\"handles\":" << handles << ",\"map\":\"0x" << std::hex
                << ReadU16(kMapIdAddr) << std::dec << "\"}";
        last_sample = now;
        s_swaps = sw;
        s_logic = lg;
        write(false);
      };
      auto& pad = input::Get();
      size_t k = 0;
      while (clk::now() < end && !hang) {
        const auto& m = kMaps[k++ % (sizeof(kMaps) / sizeof(kMaps[0]))];
        const bool inv = RequestWarp(m.map, m.entry);
        const bool arrived = inv && WaitWarpArrival(m.map);
        if (arrived) ++warps_ok;
        else ++warps_fail;
        if (!alive()) {
          hang = true;
          hang_detail = "sem quadro novo por 15 s depois da viagem ao mapa 0x" +
                        [&] { char b[8]; std::snprintf(b, 8, "%X", m.map); return std::string(b); }();
          break;
        }
        WaitStableGameplay(60, 600);
        const uint64_t v0 = state::guest_swaps.load();
        const auto tv = clk::now();
        // ~15 s de jogo: corre, pula, câmera, volta, pausa e despausa
        pad = input::PadState{};
        pad.thumb_ly = 32767;
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        pad.thumb_ly = 0;
        Tap(input::kA, 12);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        pad.thumb_rx = 32767;
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        pad.thumb_rx = 0;
        pad.thumb_ly = -32767;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        pad.thumb_ly = 0;
        Tap(input::kB, 8);  // golpe / fecha diálogo
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        if (state::cutscene_state.load() == 0) {
          const bool p = OpenPauseMenu();
          if (p) {
            ++pauses_ok;
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            for (int t = 0; t < 4 && ReadU8(0x8266EF55) == 1; ++t) {
              Tap(input::kStart, 12);
              WaitFrames(60);
            }
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        const double vs = std::chrono::duration<double>(clk::now() - tv).count();
        visits << (nvisits++ ? "," : "") << "{\"map\":\"0x" << std::hex << m.map << std::dec
               << "\",\"arrived\":" << (arrived ? 1 : 0) << ",\"fps\":"
               << double(state::guest_swaps.load() - v0) / vs << "}";
        sample();
        if (!alive()) {
          hang = true;
          hang_detail = "sem quadro novo por 15 s durante o jogo";
        }
      }
      write(true);
      Finish(hang ? 3 : 0);
    }

    // Sonda das CONQUISTAS do PC (--probe-conquistas, M04 C-6): no menu principal e na pausa,
    // escolhe CONQUISTAS, confere a lista (página 7, vista de conquistas), a descrição com A,
    // uma conquista registrada como se o jogo a informasse, e que B volta ao menu de origem com
    // as mesmas entradas.
    if (!opts.probe_conquistas.empty()) {
      input::SetActive(true);
      std::filesystem::path base = opts.probe_conquistas;
      base.replace_extension();
      auto shot = [&](const char* suffix) {
        rex::ui::RawImage im = capture();
        if (!im.width) return;
        ReduceTo1920(im);
        std::filesystem::path png = base;
        png += suffix;
        WritePng(png, im.width, im.height, im.data.data(), im.stride);
      };
      auto labels_now = [&](uint32_t* page_out) {
        std::vector<std::string> out;
        const uint32_t pages = ReadU32(kScreenObjAddr + 0x154);
        const uint32_t page = ReadU32(kScreenObjAddr + 0x2C);
        if (page_out) *page_out = page;
        if (pages < 0x40000000u || pages >= 0xA0000000u || page >= 16) return out;
        const uint32_t entries = ReadU32(pages + page * 0x2C);
        const uint32_t count = ReadU32(pages + page * 0x2C + 4);
        if (entries < 0x40000000u || entries >= 0xA0000000u || count > 64) return out;
        for (uint32_t i = 0; i < count; ++i) {
          std::string s;
          for (uint32_t k = 0; k < 0x40; ++k) {
            const uint8_t c = ReadU8(entries + i * 0x88 + 0x3B + k);
            if (!c) break;
            s += char(c);
          }
          out.push_back(s);
        }
        return out;
      };
      auto json_list = [&](const std::vector<std::string>& v) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i) {
          s += i ? ",\"" : "\"";
          for (unsigned char c : v[i]) {  // Latin-1 -> UTF-8
            if (c == '"' || c == '\\') {
              s += '\\';
              s += char(c);
            } else if (c < 0x80) {
              s += char(c);
            } else {
              s += char(0xC0 | (c >> 6));
              s += char(0x80 | (c & 0x3F));
            }
          }
          s += "\"";
        }
        return s + "]";
      };
      std::ostringstream o;
      o << "{\"version\":1";
      bool all_ok = true;
      auto phase = [&](const char* name) {
        uint32_t page0 = 0;
        const auto before = labels_now(&page0);
        std::string label;
        for (const auto& l : before)
          if (l == "CONQUISTAS" || l == "ACHIEVEMENTS") label = l;
        const bool found = !label.empty() && StepMenuCursorToLabel(label);
        const uint32_t opened0 = state::achievements_opened.load();
        if (found) Tap(input::kA, 10);
        WaitFrames(150);
        uint32_t page1 = 0;
        const auto list = labels_now(&page1);
        const auto st = pc::ingame::GetStatus();
        const bool on_list = page1 == pc::ingame::PageIndex() &&
                             st.view == pc::menu::options::kAchievementsView;
        std::string p = std::string("_") + name;
        shot((p + "_1_lista.png").c_str());
        // A na 1ª conquista: descrição no lugar do nome
        std::string row1_before = list.size() > 1 ? list[1] : "", row1_after;
        if (on_list && list.size() > 1) {
          Tap(input::kDpadDown, 10);
          WaitFrames(40);
          Tap(input::kA, 10);
          WaitFrames(40);
          const auto l2 = labels_now(nullptr);
          row1_after = l2.size() > 1 ? l2[1] : "";
          shot((p + "_2_descricao.png").c_str());
        }
        // conquista registrada como se o jogo a informasse (o jogo não dá conquista no início)
        const auto items = pc::achievements::List();
        bool recorded = false;
        std::string row1_unlocked;
        if (on_list && !items.empty()) {
          pc::achievements::SimulateUnlock(items[0].id);
          Tap(input::kDpadDown, 10);  // mover o cursor redesenha as linhas
          WaitFrames(30);
          Tap(input::kDpadUp, 10);
          WaitFrames(40);
          const auto l3 = labels_now(nullptr);
          row1_unlocked = l3.size() > 1 ? l3[1] : "";
          for (const auto& it : pc::achievements::List())
            if (it.id == items[0].id) recorded = it.unlocked;
          shot((p + "_3_obtida.png").c_str());
        }
        Tap(input::kB, 10);
        WaitFrames(150);
        uint32_t page2 = 0;
        const auto after = labels_now(&page2);
        const bool back_ok = page2 == page0 && after == before;
        const bool ok = found && on_list && list.size() == items.size() + 1 &&
                        row1_after != row1_before && recorded &&
                        row1_unlocked.rfind("* ", 0) == 0 &&
                        back_ok && state::achievements_opened.load() == opened0 + 1;
        all_ok &= ok;
        o << ",\"" << name << "\":{\"ok\":" << (ok ? "true" : "false")
          << ",\"entry_found\":" << (found ? 1 : 0) << ",\"menu_before\":" << json_list(before)
          << ",\"page_before\":" << page0 << ",\"page_list\":" << page1
          << ",\"view\":" << st.view << ",\"rows\":" << json_list(list)
          << ",\"achievements\":" << items.size() << ",\"row1_before\":"
          << json_list({row1_before}) << ",\"row1_after_A\":" << json_list({row1_after})
          << ",\"row1_unlocked\":" << json_list({row1_unlocked})
          << ",\"recorded\":" << (recorded ? 1 : 0) << ",\"page_after_B\":" << page2
          << ",\"menu_after\":" << json_list(after) << ",\"back_ok\":" << (back_ok ? 1 : 0)
          << "}";
      };
      // menu principal
      while (ReadU16(kMapIdAddr) != 0x14F && steady_clock::now() - t0 < seconds(200))
        std::this_thread::sleep_for(milliseconds(20));
      WaitFrames(90);
      for (int tries = 0; tries < 6; ++tries) {
        Tap(input::kStart, 12);
        WaitFrames(150);
        uint32_t pg = 99;
        if (labels_now(&pg).size() >= 4 && pg == 0) break;
      }
      phase("titulo");
      // pausa: o jogo já está no menu principal (mapa 0x158), então daqui: JOGAR, JOGO 1 e as
      // cenas puladas com B até o jogo livre; depois START e CONQUISTAS
      StepMenuCursorToLabel(pc::lang::English() ? "PLAY GAME" : "JOGAR");
      Tap(input::kA, 12);
      WaitFrames(150);
      Tap(input::kA, 12);
      WaitFrames(240);
      {
        NavResult nr;
        SkipScenesToFreePlay(nr, 90.0);
        for (int i = 0; i < 2; ++i) {  // como NavigateToGameplay
          Tap(input::kB);
          WaitFrames(35);
        }
      }
      // O jogo novo ainda passa de 0xA1 para 0xAF logo depois (sessão de 2026-11-14): a pausa
      // só abre com o mapa parado. Tenta por até ~40 s, como o jogador faria.
      bool paused = false;
      for (int i = 0; i < 8 && !paused; ++i) {
        WaitStableGameplay(120, 900);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        paused = OpenPauseMenu();
        if (!paused && state::cutscene_state.load() != 0) Tap(input::kB, 10);
      }
      WaitFrames(60);
      if (paused) phase("pausa");
      else all_ok = false;
      const std::filesystem::path file = pc::achievements::File();
      std::error_code ec;
      o << ",\"lista\":[";
      {
        const auto all = pc::achievements::List();
        for (size_t i = 0; i < all.size(); ++i)
          o << (i ? "," : "") << "{\"id\":" << all[i].id << ",\"nome\":\""
            << JsonEscape(all[i].name) << "\",\"descricao\":\"" << JsonEscape(all[i].description)
            << "\",\"pontos\":" << all[i].points << ",\"obtida\":" << (all[i].unlocked ? 1 : 0)
            << "}";
      }
      o << "]";
      o << ",\"pause_opened\":" << (paused ? 1 : 0) << ",\"file\":\""
        << JsonEscape(PathUtf8(file)) << "\",\"file_exists\":"
        << (std::filesystem::exists(file, ec) ? 1 : 0)
        << ",\"ok\":" << (all_ok ? "true" : "false") << "}\n";
      std::ofstream(opts.probe_conquistas, std::ios::binary | std::ios::trunc) << o.str();
      Finish(all_ok ? 0 : 1);
    }

    // Sonda de física 30 x 60 FPS (--probe-fisica, M04 etapa D, C-32/C-36): a MESMA entrada
    // roteirizada, com tempos de relógio, em cada mapa pedido. O gancho de início de quadro
    // grava a posição do jogador em cada atualização da lógica (pc_state.h). O jogo roda
    // com o fps60 do pc_settings.toml da pasta de teste; tools/m04/fisica_30x60.py roda as
    // duas configurações e compara velocidade, distância, pulo e a imagem depois do giro
    // da câmera.
    if (!opts.probe_fisica.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      const bool start_ok = WaitStableGameplay(90);
      std::filesystem::path base = opts.probe_fisica;
      base.replace_extension();
      using clk = std::chrono::steady_clock;
      auto now_ns = []() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   clk::now().time_since_epoch())
            .count();
      };
      struct Target {
        uint16_t map;
        uint8_t entry;
        bool warp;
      };
      std::vector<Target> targets;
      targets.push_back({ReadU16(kMapIdAddr), 0, false});
      {
        std::string s = opts.fisica_mapas;
        size_t pos = 0;
        while (pos < s.size()) {
          size_t comma = s.find(',', pos);
          if (comma == std::string::npos) comma = s.size();
          const std::string item = s.substr(pos, comma - pos);
          const size_t colon = item.find(':');
          if (colon != std::string::npos) {
            const unsigned long m = std::strtoul(item.substr(0, colon).c_str(), nullptr, 16);
            const unsigned long e = std::strtoul(item.substr(colon + 1).c_str(), nullptr, 16);
            if (m && m < 0x10000 && e < 0x100) targets.push_back({uint16_t(m), uint8_t(e), true});
          }
          pos = comma + 1;
        }
      }
      const bool camera_dump = wcsstr(GetCommandLineW(), L"--fisica-camera-dump") != nullptr;
      const bool camera_scan = wcsstr(GetCommandLineW(), L"--fisica-camera-scan") != nullptr;
      const bool discovery = wcsstr(GetCommandLineW(), L"--fisica-descoberta") != nullptr;
      std::ostringstream cam_json;
      const int64_t t0_ns = now_ns();
      state::phys_trace_count.store(0);
      state::phys_trace_enabled.store(true);
      std::ostringstream mj;
      bool firstm = true;
      auto& pad = input::Get();
      auto sleep_s = [](double s) {
        std::this_thread::sleep_for(std::chrono::microseconds(int64_t(s * 1e6)));
      };
      for (const auto& t : targets) {
        bool reached = true, gameplay = start_ok, invoked = true;
        if (t.warp) {
          invoked = RequestWarp(t.map, t.entry);
          reached = invoked && WaitWarpArrival(t.map);
          gameplay = reached && WaitStableGameplay(90);
        }
        std::ostringstream marks;
        bool firstk = true;
        auto mark = [&](const char* seg, const char* ev) {
          marks << (firstk ? "" : ",") << "{\"seg\":\"" << seg << "\",\"ev\":\"" << ev
                << "\",\"t\":" << double(now_ns() - t0_ns) / 1e9 << "}";
          firstk = false;
        };
        auto shot = [&](const char* tag) {
          rex::ui::RawImage im = capture();
          if (!im.width) return false;
          ReduceTo1920(im);
          char name[64];
          std::snprintf(name, sizeof name, "_map%X_%s.png", unsigned(t.map), tag);
          std::filesystem::path png = base;
          png += name;
          return WritePng(png, im.width, im.height, im.data.data(), im.stride);
        };
        if (gameplay) {
          pad = input::PadState{};
          // Assentar por TEMPO de relógio (não por quadros: a 30 FPS 90 quadros seriam 3 s e a
          // 60 FPS 1,5 s, e o jogador chegaria ao "parado" em momentos diferentes).
          sleep_s(3.0);
          if (camera_scan && t.map == targets.front().map) {
            // Varredura da câmera no heap (0x80000000..0xC0000000): 5 fotos (parado, 2 giros
            // para a direita, 1 para a esquerda, andando) e procura de trios x,y,z que orbitam
            // o jogador (distância horizontal estável), giram com o analógico direito e o
            // acompanham quando ele anda.
            std::vector<std::vector<MemBlock>> snaps;
            std::vector<Vec3> pls;
            auto snap = [&]() {
              snaps.push_back(SnapshotMemory(0x40000000u, 0xC0000000u));
              pls.push_back(ReadPlayerPos());
            };
            snap();
            pad.thumb_rx = 32767;
            sleep_s(0.4);
            pad.thumb_rx = 0;
            sleep_s(0.6);
            snap();
            pad.thumb_rx = 32767;
            sleep_s(0.4);
            pad.thumb_rx = 0;
            sleep_s(0.6);
            snap();
            pad.thumb_rx = -32767;
            sleep_s(0.8);
            pad.thumb_rx = 0;
            sleep_s(0.6);
            snap();
            pad.thumb_ly = 32767;
            sleep_s(0.8);
            pad.thumb_ly = 0;
            sleep_s(0.8);
            snap();
            auto find_block = [](const std::vector<MemBlock>& v, uint32_t addr) -> const MemBlock* {
              auto it = std::lower_bound(v.begin(), v.end(), addr,
                                         [](const MemBlock& b, uint32_t a) { return b.addr < a; });
              return it != v.end() && it->addr == addr ? &*it : nullptr;
            };
            auto f32 = [](const MemBlock& b, size_t off) {
              const uint8_t* p = b.data.data() + off;
              const uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                                 (uint32_t(p[2]) << 8) | p[3];
              float f;
              std::memcpy(&f, &v, 4);
              return f;
            };
            auto angd = [](double a, double b) {
              double x = b - a;
              while (x > 180) x -= 360;
              while (x < -180) x += 360;
              return x;
            };
            std::ostringstream cs;
            int found = 0;
            for (const MemBlock& b0 : snaps[0]) {
              const MemBlock* bs[5] = {&b0, find_block(snaps[1], b0.addr),
                                       find_block(snaps[2], b0.addr), find_block(snaps[3], b0.addr),
                                       find_block(snaps[4], b0.addr)};
              if (!bs[1] || !bs[2] || !bs[3] || !bs[4]) continue;
              for (size_t off = 0; off + 12 <= b0.data.size() && found < 200; off += 4) {
                double dist[5], ang[5], py[5];
                bool ok = true;
                float cx[5], cz[5];
                for (int k = 0; k < 5 && ok; ++k) {
                  const float x = f32(*bs[k], off), y = f32(*bs[k], off + 4),
                              z = f32(*bs[k], off + 8);
                  if (!Finite(x) || !Finite(y) || !Finite(z)) {
                    ok = false;
                    break;
                  }
                  cx[k] = x;
                  cz[k] = z;
                  dist[k] = std::hypot(double(x) - pls[k].x, double(z) - pls[k].z);
                  ang[k] = std::atan2(double(z) - pls[k].z, double(x) - pls[k].x) * 57.29578;
                  py[k] = double(y) - pls[k].y;
                  if (dist[k] < 50 || dist[k] > 3000) ok = false;
                }
                if (!ok) continue;
                double dmin = dist[0], dmax = dist[0];
                for (double d : dist) dmin = std::min(dmin, d), dmax = std::max(dmax, d);
                if (dmax > 1.6 * dmin) continue;
                const double moved = std::hypot(double(cx[4] - cx[3]), double(cz[4] - cz[3]));
                if (moved < 100) continue;
                const double a1 = angd(ang[0], ang[1]), a2 = angd(ang[1], ang[2]),
                             a3 = angd(ang[2], ang[3]);
                if (std::fabs(a1) < 5 && std::fabs(a2) < 5) continue;
                cs << (found ? "," : "") << "{\"addr\":\"0x" << std::hex << (b0.addr + off)
                   << std::dec << "\",\"dist\":[" << dist[0] << "," << dist[1] << "," << dist[2]
                   << "," << dist[3] << "," << dist[4] << "],\"giros\":[" << a1 << "," << a2
                   << "," << a3 << "],\"andou\":" << moved << ",\"altura\":" << py[0] << "}";
                ++found;
              }
            }
            std::filesystem::path f = base;
            f += "_camscan.json";
            std::ofstream(f) << "{\"candidates\":[" << cs.str() << "],\"count\":" << found
                             << "}\n";
            snaps.clear();
            sleep_s(1.0);
          }
          if (discovery && t.map == targets.front().map) {
            // Descoberta (M04): fotos da memória de dados do jogo (0x82600000..0x82700000) em
            // momentos conhecidos, para achar o estado do jogador (parado/andando/no ar/nadando)
            // e contadores de tempo (tools/m04/descoberta.py).
            int k = 0;
            auto dump = [&](const char* when) {
              char name[48];
              std::snprintf(name, sizeof name, "_desc%d.bin", k);
              std::filesystem::path f = base;
              f += name;
              DumpGuestRange(f, 0x82600000u, 0x100000u);
              const Vec3 pp = ReadPlayerPos();
              cam_json << (k ? "," : "") << "{\"i\":" << k << ",\"when\":\"" << when
                       << "\",\"t\":" << double(now_ns() - t0_ns) / 1e9
                       << ",\"logic\":" << state::logic_updates.load()
                       << ",\"swaps\":" << state::guest_swaps.load() << ",\"player\":["
                       << pp.x << "," << pp.y << "," << pp.z << "]}";
              ++k;
            };
            dump("parado");
            sleep_s(2.0);
            dump("parado");
            pad.thumb_ly = 32767;
            sleep_s(0.8);
            dump("andando");
            pad.thumb_ly = 0;
            sleep_s(1.5);
            dump("parado");
            input::SetButton(input::kA, true);
            sleep_s(0.3);
            dump("no_ar");
            input::SetButton(input::kA, false);
            sleep_s(2.0);
            dump("parado");
            sleep_s(2.0);
            dump("parado");
          }
          if (camera_dump && t.map == targets.front().map) {
            // Fotos da memória de dados do jogo (0x82600000..0x82700000) com a câmera girando,
            // para achar a câmera (tools/m04/achar_camera.py).
            auto dump = [&](int i) {
              char name[48];
              std::snprintf(name, sizeof name, "_cam%d.bin", i);
              std::filesystem::path f = base;
              f += name;
              DumpGuestRange(f, 0x82600000u, 0x100000u);
              const Vec3 pp = ReadPlayerPos();
              cam_json << (i ? "," : "") << "{\"i\":" << i << ",\"player\":[" << pp.x << ","
                       << pp.y << "," << pp.z << "]}";
            };
            dump(0);
            pad.thumb_rx = 32767;
            sleep_s(0.4);
            pad.thumb_rx = 0;
            sleep_s(0.6);
            dump(1);
            pad.thumb_rx = 32767;
            sleep_s(0.4);
            pad.thumb_rx = 0;
            sleep_s(0.6);
            dump(2);
            pad.thumb_rx = -32767;
            sleep_s(0.8);
            pad.thumb_rx = 0;
            sleep_s(0.6);
            dump(3);
            pad.thumb_ly = 32767;  // anda: a câmera acompanha
            sleep_s(0.8);
            pad.thumb_ly = 0;
            sleep_s(0.8);
            dump(4);
            sleep_s(1.0);
          }
          // A ordem e os tempos são fixos: as duas execuções recebem a mesma entrada.
          mark("parado", "inicio");
          sleep_s(1.0);
          mark("parado", "fim");
          shot("inicio");
          mark("andar", "inicio");  // meia inclinação do analógico: andar
          pad.thumb_ly = 12000;
          sleep_s(1.5);
          pad.thumb_ly = 0;
          mark("andar", "solta");
          sleep_s(1.2);
          mark("andar", "fim");
          mark("correr", "inicio");  // analógico todo para frente: correr
          pad.thumb_ly = 32767;
          sleep_s(2.0);
          pad.thumb_ly = 0;
          mark("correr", "solta");
          sleep_s(1.5);
          mark("correr", "fim");
          mark("pulo", "inicio");  // A segurado 0,4 s: pulo alto
          input::SetButton(input::kA, true);
          sleep_s(0.4);
          input::SetButton(input::kA, false);
          mark("pulo", "solta");
          sleep_s(2.0);
          mark("pulo", "fim");
          mark("camera", "inicio");  // analógico direito todo para a direita por 1 s
          pad.thumb_rx = 32767;
          sleep_s(1.0);
          pad.thumb_rx = 0;
          mark("camera", "solta");
          sleep_s(1.0);
          mark("camera", "fim");
          shot("camera");
          mark("re", "inicio");  // analógico todo para trás 1,2 s (vira e corre)
          pad.thumb_ly = -32767;
          sleep_s(1.2);
          pad.thumb_ly = 0;
          mark("re", "solta");
          sleep_s(1.5);
          mark("re", "fim");
          shot("fim");
        }
        mj << (firstm ? "" : ",") << "{\"map\":\"0x" << std::hex << t.map << std::dec
           << "\",\"entry\":" << unsigned(t.entry) << ",\"warp\":" << (t.warp ? 1 : 0)
           << ",\"invoked\":" << (invoked ? 1 : 0) << ",\"reached\":" << (reached ? 1 : 0)
           << ",\"gameplay\":" << (gameplay ? 1 : 0) << ",\"map_after\":\"0x" << std::hex
           << ReadU16(kMapIdAddr) << std::dec << "\",\"marks\":[" << marks.str() << "]}";
        firstm = false;
      }
      state::phys_trace_enabled.store(false);
      const uint32_t n = std::min(state::phys_trace_count.load(std::memory_order_acquire),
                                  state::kPhysTraceSize);
      std::ostringstream o;
      o << std::setprecision(9);
      o << "{\"version\":1,\"fps60\":" << (pc::Get().fps60 ? 1 : 0)
        << ",\"scenes60\":" << (pc::Get().scenes60 ? 1 : 0) << ",\"camera_dumps\":["
        << cam_json.str() << "],\"maps\":[" << mj.str()
        << "],\"samples_fields\":[\"t\",\"swaps\",\"step\",\"scene\",\"x\",\"y\",\"z\","
        << "\"cx\",\"cy\",\"cz\",\"estado\",\"relogio_s\",\"quadros\"]"
        << ",\"samples\":[";
      for (uint32_t i = 0; i < n; ++i) {
        const auto& s = state::phys_trace[i];
        o << (i ? "," : "") << "[" << double(s.t_ns - t0_ns) / 1e9 << "," << s.swaps << ","
          << s.step << "," << s.scene << "," << s.x << "," << s.y << "," << s.z << "," << s.cx
          << "," << s.cy << "," << s.cz << "," << s.pstate << "," << s.clock_s << "," << s.frames
          << "]";
      }
      o << "]}\n";
      std::ofstream(opts.probe_fisica, std::ios::binary | std::ios::trunc) << o.str();
      Finish(0);
    }

    // Sonda dos mundos (--probe-dd-worlds): viaja por todos os mundos do jogo usando a
    // PRÓPRIA rotina de viagem (0x82248810, chamada na thread do jogo pelo gancho de
    // quadro), espera o mapa carregar e o jogo ficar livre, e mede os 3 perfis de
    // distância de desenho + estabilidade + pop-in em cada um. Sem save avançado, sem
    // teleporte externo, sem interferência humana.
    if (!opts.probe_dd_worlds.empty()) {
      input::SetActive(true);
      NavigateToGameplay();
      // --all-flags: liga TODAS as bandeiras de progresso do jogo (eventos, cenas já
      // vistas, mundos abertos, golpes), não só os colecionáveis — com o save avançado,
      // o mapa inicial (0x18C) dispara um evento que deixa a guarda de viagem
      // (0x826CC2B8) ocupada e a primeira viagem (0x112) fica bloqueada; com as bandeiras
      // ligadas o evento não roda e as 18 viagens passam (shots/ddworlds_advall.json).
      if (wcsstr(GetCommandLineW(), L"--all-flags") != nullptr) {
        uint32_t block = 0;
        size_t len = 0;
        if (pc::progress::FlagBlockRange(&block, &len)) {
          auto* mb = rex::system::kernel_state()->memory()->virtual_membase();
          for (size_t i = 0; i < len; ++i) mb[block + i] = 0xFF;
        }
        WaitFrames(10);
      }
      std::filesystem::path base = opts.probe_dd_worlds;
      base.replace_extension();

      // Pares (mapa, entrada) retirados do próprio código do jogo (chamadas a 0x82248810,
      // Ghidra, out/re/dec_warps*.txt) e da tabela de mundos em 0x82601E2C. Cobrem as
      // quatro classes de alcance do perfil Total: 0x112/0x128 (10400), 0x127 (12800),
      // 0x136 (21504) e o padrão (25600).
      static constexpr struct {
        uint16_t map;
        uint8_t entry;
        const char* note;
      } kTargets[] = {
          {0x112, 5, "alcance 10400"},   {0x128, 8, "alcance 10400"},
          {0x127, 1, "alcance 12800"},   {0x136, 0x1F, "alcance 21504"},
          {0x142, 1, "alcance 25600"},   {0x15D, 5, "alcance 25600"},
          {0x12D, 5, "alcance 25600"},   {0x124, 8, "alcance 25600"},
          {0xD6, 8, "alcance 25600"},    {0x163, 3, "alcance 25600"},
          {0xBB, 4, "alcance 25600"},    {0x18A, 1, "alcance 25600"},
          {0x1A7, 1, "lista de mundos"}, {0x1A8, 1, "lista de mundos"},
          {0x1A9, 1, "lista de mundos"}, {0x100, 1, "lista de mundos"},
          {0xB8, 1, "lista de mundos"},  {0xC7, 1, "lista de mundos"},
      };
      static constexpr struct {
        const char* name;
        pc::DrawDistance dd;
      } kProfiles[] = {
          {"Original", pc::DrawDistance::Original},
          {"Longe", pc::DrawDistance::Longe},
          {"Total", pc::DrawDistance::Total},
      };

      auto write_u8 = [&](uint32_t addr, uint8_t v) {
        rex::system::kernel_state()->memory()->virtual_membase()[addr] = v;
      };
      auto current_map_alt = [&]() { return ReadU16(0x8269E526); };  // FUN_8227ba48
      auto in_gameplay = [&]() {
        const Vec3 p = ReadPlayerPos();
        const bool finite_pos = std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        const bool nonzero =
            std::fabs(p.x) > 0.5f || std::fabs(p.y) > 1.0f || std::fabs(p.z) > 0.5f;
        const bool sane = std::fabs(p.x) < 1.0e5f && std::fabs(p.y) < 1.0e5f &&
                          std::fabs(p.z) < 1.0e5f;
        return state::cutscene_state.load() == 0 && finite_pos && nonzero && sane;
      };
      auto request_warp = [&](uint16_t map, uint8_t entry) {
        // A guarda de viagem (0x826CC2B8) fica ocupada durante transições do próprio jogo
        // (ex.: 0x142 -> 0x18C ao carregar o save avançado). Espera ela liberar e ficar
        // liberada por ~1 s antes de pedir; depois, até 4 tentativas com B fechando
        // diálogos, como um jogador faria.
        {
          int clear = 0;
          for (int w = 0; w < 3600; ++w) {
            if (ReadU8(0x826CC2B8) == 0) {
              if (++clear >= 60) break;
            } else {
              clear = 0;
            }
            WaitFrames(1, 3600);
          }
        }
        for (int attempt = 0; attempt < 4; ++attempt) {
          state::warp_invoked.store(0);
          state::warp_result.store(0);
          state::warp_was_blocked.store(0);
          state::warp_requested.store((uint32_t(map) << 8) | entry,
                                      std::memory_order_relaxed);
          for (int w = 0; w < 240 && !state::warp_invoked.load(); ++w) WaitFrames(1, 300);
          if (state::warp_invoked.load() != 0) break;
          state::warp_requested.store(0, std::memory_order_relaxed);
          for (int i = 0; i < 3; ++i) {
            Tap(input::kB, 8);
            WaitFrames(40);
          }
        }
        return state::warp_invoked.load() != 0 && state::warp_result.load() != 0;
      };

      // O save avançado carrega no mapa da introdução (0x142) e o jogo faz a transição
      // sozinho (0x142 -> 0x18C): a guarda de viagem fica ocupada durante a transição e a
      // primeira viagem (0x112) seria bloqueada ("gancho de viagem não executou"). Espera o
      // mapa estabilizar E o jogo ficar livre por ~2 s antes de começar as viagens.
      {
        uint16_t last_map = ReadU16(kMapIdAddr);
        int stable = 0;
        for (int w = 0; w < 2400; ++w) {
          const uint16_t m = ReadU16(kMapIdAddr);
          if (m == last_map && in_gameplay()) {
            if (++stable >= 120) break;
          } else {
            stable = 0;
          }
          last_map = m;
          WaitFrames(1, 1500);
        }
        WaitFrames(30);
      }

      std::ostringstream o;
      o << "{\"version\":2,\"start_map\":\"0x" << std::hex << ReadU16(kMapIdAddr) << std::dec
        << "\",\"worlds\":[";
      bool first = true;
      const auto t_start = std::chrono::steady_clock::now();
      // grava o JSON a cada mundo: se o jogo travar no meio, a evidência dos mundos
      // anteriores não se perde
      auto flush_partial = [&](bool finished) {
        // 'o' guarda só a lista: o fechamento vai numa cópia, para o JSON final ser válido
        std::ofstream(opts.probe_dd_worlds)
            << o.str() << "],\"finished\":" << (finished ? 1 : 0) << "}\n";
      };
      for (const auto& t : kTargets) {
        auto t_world = std::chrono::steady_clock::now();
        // --- viagem
        bool invoked = request_warp(t.map, t.entry);
        const bool was_blocked = state::warp_was_blocked.load() != 0;
        // se a guarda do jogo ficou presa da viagem anterior, solta (só a sonda faz isso)
        if (was_blocked) write_u8(0x826CC2B8, 0);
        const bool map_changed = [&]() {
          const uint64_t start = state::guest_swaps.load();
          while (state::guest_swaps.load() - start < 2000) {
            if (ReadU16(kMapIdAddr) == t.map) return true;
            WaitFrames(5, 90);
          }
          return false;
        }();
        // --- espera o jogo livre (pulando cenas de introdução com B, como o jogo mostra)
        bool gameplay = false;
        {
          const uint64_t start = state::guest_swaps.load();
          while (state::guest_swaps.load() - start < 2200) {
            if (in_gameplay()) {
              gameplay = true;
              break;
            }
            if (state::cutscene_state.load() != 0) {
              Tap(input::kB, 8);
              WaitFrames(60);
            } else {
              WaitFrames(20);
            }
          }
        }
        // deixa o mundo assentar (streaming de geometria/atores depois do carregamento)
        WaitFrames(120);

        char name[64];
        std::snprintf(name, sizeof name, "_map%X", unsigned(t.map));
        std::filesystem::path pbase = base;
        pbase += name;

        rex::ui::RawImage imgs[3];
        uint64_t calls[3] = {0, 0, 0};
        uint32_t max_orig[3] = {0, 0, 0};
        bool captured[3] = {false, false, false};
        bool cutscene_seen = false;  // cena ativa em algum ponto da medição
        auto note_cutscene = [&]() {
          if (state::cutscene_state.load() != 0) cutscene_seen = true;
        };
        if (gameplay) {
          for (int p = 0; p < 3; ++p) {
            pc::Get().draw_distance = kProfiles[p].dd;
            state::draw_range_calls.store(0);
            state::max_draw_range_original.store(0);
            WaitFrames(60);
            calls[p] = state::draw_range_calls.load();
            max_orig[p] = state::max_draw_range_original.load();
            note_cutscene();
            imgs[p] = capture();
            if (imgs[p].width) {
              ReduceTo1920(imgs[p]);
              std::filesystem::path png = pbase;
              png += "_" + std::string(kProfiles[p].name) + ".png";
              captured[p] = WritePng(png, imgs[p].width, imgs[p].height, imgs[p].data.data(),
                                     imgs[p].stride);
            }
          }
          pc::Get().draw_distance = pc::DrawDistance::Total;
        }

        // --- estabilidade: mesma cena, perfil Total, 15 quadros de diferença
        double stability_pct = -1;
        if (captured[2]) {
          WaitFrames(15);
          rex::ui::RawImage again = capture();
          if (again.width) {
            ReduceTo1920(again);
            const DiffStats ds = CompareImages(imgs[2], again);
            stability_pct = ds.samples ? 100.0 * ds.differing / ds.samples : -1;
          }
        }

        // --- pop-in: câmera girando devagar, 8 fotos, diferenças entre quadros
        std::vector<double> pop_diffs;
        if (captured[2]) {
          input::Get().thumb_rx = 8192;  // gira a câmera devagar (como o analógico direito)
          WaitFrames(30);
          rex::ui::RawImage prev = capture();
          if (prev.width) ReduceTo1920(prev);
          for (int i = 0; i < 8; ++i) {
            WaitFrames(12);
            note_cutscene();
            rex::ui::RawImage now = capture();
            if (!now.width) continue;
            ReduceTo1920(now);
            if (prev.width) {
              const DiffStats ds = CompareImages(prev, now);
              pop_diffs.push_back(ds.samples ? 100.0 * ds.differing / ds.samples : -1);
            }
            prev = std::move(now);
          }
          input::Get().thumb_rx = 0;
          WaitFrames(20);
        }

        const double secs = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - t_world)
                                .count();
        // veredito automático
        std::string verdict = "FAIL";
        std::string visual = "-";
        std::string verdict_detail;
        if (!invoked) {
          verdict = "FAIL";
          verdict_detail = "gancho de viagem não executou";
        } else if (!map_changed) {
          verdict = "FAIL";
          verdict_detail = "mapa não mudou";
        } else if (!gameplay) {
          verdict = "CUTSCENE";
          verdict_detail = "mapa sem jogo livre (cena ou sem jogador)";
        } else {
          const DiffStats total_vs_orig = CompareImages(imgs[2], imgs[0]);
          const double diff_pct =
              total_vs_orig.samples ? 100.0 * total_vs_orig.differing / total_vs_orig.samples
                                    : -1;
          double max_pop = -1;
          for (double d : pop_diffs) max_pop = std::max(max_pop, d);
          // Visual: a cena ficou parada o bastante para comparar os perfis? Títulos de fase,
          // cenas que começam sozinhas e animações deixam "REVISAR" (não é defeito por si só).
          if (captured[0] && captured[2] && diff_pct > 0.5 && stability_pct >= 0 &&
              stability_pct < 3.0 && max_pop <= 12.0) {
            visual = "ESTAVEL";
          } else if (captured[0] && captured[2]) {
            visual = "REVISAR";
            verdict_detail = cutscene_seen ? "cena/título durante a medição"
                                           : "cena em movimento durante a medição";
          }
          // Funcional: o mundo carregou, o jogo ficou livre, os 3 perfis foram aplicados e
          // capturados sem falha, e o perfil Total consultou o alcance pelo menos tanto quanto
          // o Original (o gancho de alcance está ativo nesse mundo).
          if (captured[0] && captured[1] && captured[2] && calls[2] >= calls[0]) verdict = "PASS";
          else verdict_detail = "perfil sem captura ou alcance não consultado";
        }

        o << (first ? "" : ",") << "{\"map\":\"0x" << std::hex << t.map << std::dec
          << "\",\"entry\":" << unsigned(t.entry) << ",\"note\":\"" << t.note
          << "\",\"invoked\":" << (invoked ? 1 : 0) << ",\"blocked\":" << (was_blocked ? 1 : 0)
          << ",\"map_changed\":" << (map_changed ? 1 : 0)
          << ",\"map_after\":\"0x" << std::hex << ReadU16(kMapIdAddr) << std::dec
          << "\",\"map_alt\":\"0x" << std::hex << current_map_alt() << std::dec
          << "\",\"gameplay\":" << (gameplay ? 1 : 0) << ",\"secs\":" << secs
          << ",\"profiles\":[";
        for (int p = 0; p < 3; ++p) {
          o << (p ? "," : "") << "{\"name\":\"" << kProfiles[p].name
            << "\",\"range_calls\":" << calls[p] << ",\"max_original_range\":\"0x" << std::hex
            << max_orig[p] << std::dec << "\",\"png\":" << (captured[p] ? 1 : 0) << "}";
        }
        o << "],\"total_vs_original_pct\":";
        {
          const DiffStats d = CompareImages(imgs[2], imgs[0]);
          o << (d.samples ? 100.0 * d.differing / d.samples : -1);
        }
        o << ",\"stability_pct\":" << stability_pct << ",\"pop_diffs_pct\":[";
        for (size_t i = 0; i < pop_diffs.size(); ++i)
          o << (i ? "," : "") << pop_diffs[i];
        o << "],\"cutscene_seen\":" << (cutscene_seen ? 1 : 0) << ",\"visual\":\"" << visual
          << "\",\"verdict\":\"" << verdict << "\",\"verdict_detail\":\""
          << JsonEscape(verdict_detail) << "\"}";
        first = false;
        (void)t_start;
        flush_partial(false);
      }
      flush_partial(true);
      Finish(0);
    }

    // Sonda do controle físico (--probe-testpad): um gamepad VIRTUAL do SDL atravessa o
    // caminho completo de um controle real — SDL (eventos de dispositivo/botão/analógico)
    // -> SDLInputDriver -> estado XInput -> menu de PC (navegação) e -> vibração
    // (SDL_RumbleGamepad -> Rumble do dispositivo). Prova navegação, zona morta e
    // vibração sem hardware conectado.
    if (!opts.probe_testpad.empty()) {
      input::SetActive(true);
      auto* in = static_cast<rex::input::InputSystem*>(input_system);
      std::filesystem::path base = opts.probe_testpad;
      base.replace_extension();
      std::ostringstream o;
      o << "{\"version\":1,\"xinput\":[";
      const auto slots = pc::menu::QueryXInput();
      for (int i = 0; i < 4; ++i) {
        o << (i ? "," : "") << "{\"slot\":" << i << ",\"connected\":" << slots[i].connected
          << "}";
      }
      o << "],\"before_attach\":\"0x" << std::hex;
      if (in) {
        rex::input::X_INPUT_STATE st{};
        o << in->GetGamepadState(0, &st);
      } else {
        o << 0;
      }
      o << std::dec << "\"";
      const bool attached = rex::input::AttachTestGamepad();
      o << ",\"attached\":" << (attached ? 1 : 0);
      // espera o driver abrir o gamepad virtual e passar a devolver estado
      bool seen = false;
      {
        const auto t0 = std::chrono::steady_clock::now();
        while (in && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(12)) {
          rex::input::X_INPUT_STATE st{};
          if (in->GetGamepadState(0, &st) == 0) {  // X_ERROR_SUCCESS
            seen = true;
            break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      }
      o << ",\"driver_sees_pad\":" << (seen ? 1 : 0);
      {
        rex::input::X_INPUT_CAPABILITIES caps{};
        const auto r = in ? in->GetGamepadCapabilities(0, 0, &caps) : 0;
        o << ",\"caps_result\":\"0x" << std::hex << r << std::dec << "\",\"caps_type\":"
          << unsigned(caps.type) << ",\"caps_buttons\":\"0x" << std::hex << caps.gamepad.buttons
          << std::dec << "\",\"caps_vibration\":[" << caps.vibration.left_motor_speed << ","
          << caps.vibration.right_motor_speed << "]";
      }
      auto pad = [](uint16_t buttons, int hold) {
        rex::input::SetTestGamepadState(buttons, 0, 0, 0, 0, 0, 0);
        WaitFrames(hold);
        rex::input::SetTestGamepadState(0, 0, 0, 0, 0, 0, 0);
        WaitFrames(4);
      };
      // navegação de ponta a ponta pelo caminho real (SDL -> driver -> menu)
      pc::state::menu_open_request.store(true);
      bool opened = false;
      for (int w = 0; w < 60 && !opened; ++w) {
        WaitFrames(2);
        opened = pc::menu::IsOpen();
      }
      o << ",\"menu_opened\":" << (opened ? 1 : 0)
        << ",\"tab_start\":" << pc::menu::CurrentTab();
      pad(0x0200, 8);  // RB
      o << ",\"tab_after_rb\":" << pc::menu::CurrentTab();
      pad(0x0200, 8);  // RB
      o << ",\"tab_after_rb2\":" << pc::menu::CurrentTab();
      pad(0x0200, 8);  // RB -> aba Jogo (3), onde ficam as caixas medidas
      o << ",\"tab_after_rb3\":" << pc::menu::CurrentTab();
      const pc::Settings s_before = pc::Get();
      pc::menu::SetGamepadTestFocus(true);
      WaitFrames(4);
      pc::menu::SetGamepadTestFocus(false);
      pad(0x0002, 12);  // direcional para baixo: foco no próximo controle
      pad(0x1000, 12);  // A: ativa o controle em foco
      WaitFrames(6);
      std::string changed;
      {
        const pc::Settings s_now = pc::Get();
        auto chk = [&](const char* n, auto a, auto b) {
          if (a != b) {
            if (!changed.empty()) changed += ",";
            changed += n;
          }
        };
        chk("fps60", s_now.fps60, s_before.fps60);
        chk("lock60", s_now.lock60, s_before.lock60);
        chk("scenes60", s_now.scenes60, s_before.scenes60);
        chk("draw_distance", int(s_now.draw_distance), int(s_before.draw_distance));
        chk("dialog_speed", s_now.dialog_speed, s_before.dialog_speed);
        chk("show_fps", s_now.show_fps, s_before.show_fps);
      }
      pc::Get() = s_before;
      o << ",\"nav_activated_field\":\"" << changed << "\"";
      pad(0x2000, 6);  // B fecha
      o << ",\"closed_by_b\":" << (pc::menu::IsOpen() ? 0 : 1)
        << ",\"input_blocked\":" << (pc::menu::BlocksGameInput() ? 1 : 0);
      WaitFrames(30);
      // zona morta: valor pequeno fica em 0, valor grande passa (padrão 0,12)
      const float saved_dz = pc::Get().pad_deadzone;
      pc::Get().pad_deadzone = 0.5f;
      pc::ApplyRuntimeOptions();
      {
        rex::input::SetTestGamepadState(0, int16_t(0.3f * 32767), 0, 0, 0, 0, 0);
        WaitFrames(8);
        rex::input::X_INPUT_STATE st{};
        const auto r = in ? in->GetGamepadState(0, &st) : 0;
        o << ",\"deadzone_inside_ok\":" << (r == 0 && st.gamepad.thumb_lx == 0 ? 1 : 0)
          << ",\"deadzone_inside_lx\":" << st.gamepad.thumb_lx;
        rex::input::SetTestGamepadState(0, int16_t(0.9f * 32767), 0, 0, 0, 0, 0);
        WaitFrames(8);
        const auto r2 = in ? in->GetGamepadState(0, &st) : 0;
        o << ",\"deadzone_outside_ok\":" << (r2 == 0 && st.gamepad.thumb_lx != 0 ? 1 : 0)
          << ",\"deadzone_outside_lx\":" << st.gamepad.thumb_lx;
        rex::input::SetTestGamepadState(0, 0, 0, 0, 0, 0, 0);
      }
      pc::Get().pad_deadzone = saved_dz;
      pc::ApplyRuntimeOptions();
      // vibração: SetState passa pelos drivers; o gamepad virtual recebe pelo SDL
      {
        rex::input::X_INPUT_VIBRATION vib{};
        vib.left_motor_speed = 0x8000;
        vib.right_motor_speed = 0x4000;
        const auto r = in ? in->SetState(0, &vib) : 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        uint16_t l = 0, h = 0;
        const bool got = rex::input::TakeTestGamepadRumble(&l, &h);
        o << ",\"rumble_result\":\"0x" << std::hex << r << std::dec
          << "\",\"rumble_received\":" << (got ? 1 : 0) << ",\"rumble_l\":\"0x" << std::hex << l
          << "\",\"rumble_r\":\"0x" << h << std::dec << "\"";
        // com a vibração desligada nas opções, o driver zera os motores
        const bool saved_vib = pc::Get().pad_vibration;
        pc::Get().pad_vibration = false;
        pc::ApplyRuntimeOptions();
        const auto r2 = in ? in->SetState(0, &vib) : 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        uint16_t l2 = 0, h2 = 0;
        const bool got2 = rex::input::TakeTestGamepadRumble(&l2, &h2);
        // com a vibração desligada o driver não envia rumble (retorno cedo): a ausência
        // de um novo evento de vibração no controle virtual é o comportamento esperado
        const bool suppressed = !got2 || (l2 == 0 && h2 == 0);
        o << ",\"rumble_off_result\":\"0x" << std::hex << r2 << "\",\"rumble_off_received\":"
          << (got2 ? 1 : 0) << std::dec << ",\"rumble_off_l\":\"" << l2
          << "\",\"rumble_off_suppressed\":" << (suppressed ? 1 : 0);
        pc::Get().pad_vibration = saved_vib;
        pc::ApplyRuntimeOptions();
      }
      // evidência visual: menu de PC aberto com o gamepad virtual conectado
      pc::state::menu_open_request.store(true);
      for (int w = 0; w < 60 && !pc::menu::IsOpen(); ++w) WaitFrames(2);
      WaitFrames(20);
      {
        rex::ui::RawImage im = CaptureWindowOnScreen();
        if (im.width) {
          ReduceTo1920(im);
          std::filesystem::path png = base;
          png += "_menu.png";
          WritePng(png, im.width, im.height, im.data.data(), im.stride);
        }
      }
      pc::state::menu_close_request.store(true);
      rex::input::DetachTestGamepad();
      {
        rex::input::X_INPUT_STATE st{};
        const auto r = in ? in->GetGamepadState(0, &st) : 0;
        o << ",\"after_detach\":\"0x" << std::hex << r << std::dec << "\"";
      }
      o << "}\n";
      std::ofstream(opts.probe_testpad) << o.str();
      Finish(0);
    }

    int code = 0;
    if (!opts.screenshot.empty()) {
      // Imagens 4K são reduzidas por um fator inteiro (média de blocos) até no máx. 1920 px de
      // largura; o PNG é gravado sem compressão e ficaria com ~33 MB.
      if (img.width > 1920) {
        const uint32_t f = (img.width + 1919) / 1920;
        const uint32_t w = img.width / f, h = img.height / f;
        std::vector<uint8_t> reduced(size_t(w) * h * 4);
        for (uint32_t y = 0; y < h; ++y)
          for (uint32_t x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c) {
              uint32_t sum = 0;
              for (uint32_t dy = 0; dy < f; ++dy)
                for (uint32_t dx = 0; dx < f; ++dx)
                  sum += img.data[size_t(y * f + dy) * img.stride + size_t(x * f + dx) * 4 + c];
              reduced[(size_t(y) * w + x) * 4 + c] = uint8_t(sum / (f * f));
            }
        img.width = w;
        img.height = h;
        img.stride = size_t(w) * 4;
        img.data = std::move(reduced);
      }
      if (WritePng(opts.screenshot, img.width, img.height, img.data.data(), img.stride)) {
        std::printf("screenshot: %ux%u -> %ls\n", img.width, img.height, opts.screenshot.c_str());
      } else {
        code = 2;
      }
    }
    if (!opts.self_test.empty()) {
      std::vector<Step> steps;
      // cada passo medido fica visível para o cão de guarda do orçamento de tempo
      auto push = [&steps](Step s) {
        steps.push_back(std::move(s));
        PublishSteps(steps);
      };
      char buf[256];
      std::snprintf(buf, sizeof buf, "%llu quadros do jogo em %.1f s; passo de quadros=%u",
                    (unsigned long long)swaps_at_n, secs_to_n, state::frame_step.load());
      push({"boot", swaps_at_n >= uint64_t(opts.frames), buf});
      std::snprintf(buf, sizeof buf, "imagem %ux%u %s", img.width, img.height,
                    blank ? "vazia ou não capturada" : "com conteúdo");
      push({"render", img.width != 0 && !blank, buf});

      // -------- jogo de verdade: título -> JOGAR -> JOGO 1 -> pular cenas
      input::SetActive(true);
      const NavResult nav = NavigateToGameplayByState(50.0);
      pc::Get().skip_intro_logos = g_saved_skip_intro_logos;  // a escolha do jogador volta
      push({"navigate", nav.reached, "título -> JOGAR -> JOGO 1 -> cenas puladas: " + nav.trace});
      // fecha diálogos/cenas opcionais que ainda estejam abertos
      for (int i = 0; i < 3; ++i) {
        Tap(input::kB);
        WaitFrames(35);
      }
      WaitFrames(60);
      const Vec3 p0 = ReadPlayerPos();
      // mover: segura o analógico para cima como um jogador
      input::Get().thumb_ly = 32767;
      WaitFrames(90);
      input::Get().thumb_ly = 0;
      WaitFrames(10);
      const Vec3 p1 = ReadPlayerPos();
      const double moved = std::sqrt((p1.x - p0.x) * (p1.x - p0.x) +
                                     (p1.z - p0.z) * (p1.z - p0.z));
      std::snprintf(buf, sizeof buf,
                    "andou %.1f unidades (x %.0f->%.0f, z %.0f->%.0f) a partir do início do jogo",
                    moved, p0.x, p1.x, p0.z, p1.z);
      push({"move", moved > 20.0, buf});

      // agir: pulo com A
      const Vec3 j0 = ReadPlayerPos();
      float max_y = j0.y;
      input::SetButton(input::kA, true);
      WaitFrames(5);
      input::SetButton(input::kA, false);
      for (int i = 0; i < 45; ++i) {
        WaitFrames(2);
        const Vec3 p = ReadPlayerPos();
        if (p.y > max_y) max_y = p.y;
      }
      std::snprintf(buf, sizeof buf, "pulo com A: altura %.1f -> %.1f (%.1f unidades acima)",
                    j0.y, max_y, max_y - j0.y);
      push({"act", max_y - j0.y > 10.0f, buf});

      // 60 FPS medidos em jogo livre, depois do aquecimento (fora de cenas/transições)
      WaitFrames(180);
      {
        const uint64_t s0 = state::guest_swaps.load();
        const uint64_t l0 = state::frame_step_calls.load();
        rex::ui::GetHostPresentStats(true);
        std::this_thread::sleep_for(seconds(opts.measure_secs));
        host = rex::ui::GetHostPresentStats(true);
        guest_fps = double(state::guest_swaps.load() - s0) / opts.measure_secs;
        logic_fps = double(state::frame_step_calls.load() - l0) / opts.measure_secs;
      }
      const uint32_t scene = state::cutscene_state.load();
      const int target = (pc::Get().fps60 && (pc::Get().scenes60 || scene == 0)) ? 60 : 30;
      std::snprintf(buf, sizeof buf,
                    "%.1f quadros/s apresentados, %.1f atualizações/s da lógica em %d s; alvo %d (%s, estado de cena=%u)",
                    guest_fps, logic_fps, opts.measure_secs, target,
                    scene ? "cena/transição" : "jogo livre", scene);
      push({"fps", guest_fps >= target * 0.92, buf});
      // ritmo na tela: o que um contador externo (MSI Afterburner/RTSS) mede no swap chain.
      // Um pico único (antivírus, primeira passada de shaders) estraga o desvio da primeira
      // janela; quando isso acontece, descarta e mede mais uma vez, valendo a mais estável.
      {
        if (host.stddev_ms >= 2.5) {
          rex::ui::GetHostPresentStats(true);
          std::this_thread::sleep_for(std::chrono::seconds(2));
          rex::ui::GetHostPresentStats(true);
          std::this_thread::sleep_for(std::chrono::seconds(opts.measure_secs));
          const rex::ui::HostPresentStats h2 = rex::ui::GetHostPresentStats(true);
          if (h2.stddev_ms < host.stddev_ms) host = h2;
        }
        const double host_fps = host.mean_ms > 0 ? 1000.0 / host.mean_ms : 0;
        std::snprintf(buf, sizeof buf,
                      "%.1f apresentações/s na tela (alvo %d), frametime %.2f ms ± %.2f ms, "
                      "máximo %.1f ms, %llu redesenhos só da interface descartados, %llu quadros repetidos",
                      host_fps, target, host.mean_ms, host.stddev_ms, host.max_ms,
                      (unsigned long long)host.dropped_ui_repaints,
                      (unsigned long long)host.repeated_frames);
        const bool ok = host_fps >= target * 0.95 && host_fps <= target * 1.03 &&
                        host.stddev_ms < 2.5;
        push({"frame_pacing", ok, buf});
      }

      // hud: a tela de status do jogo (Start) desenha os favos e os contadores.
      // Se houver diálogo/cena aberto, B/A primeiro (como um jogador faria); depois Start.
      const uint8_t menu_before = ReadU8(0x8266EF55);
      uint8_t menu_open = menu_before;
      for (int attempt = 0; attempt < 4 && menu_open != 1; ++attempt) {
        Tap(input::kB);
        WaitFrames(45);
        Tap(input::kStart);
        WaitFrames(110);
        menu_open = ReadU8(0x8266EF55);
      }
      rex::ui::RawImage hud_img = capture();
      const bool hud_drawn = menu_open == 1 && LooksLikeHud(hud_img);
      const int eggs = ReadItemCount(0), eggs_cap = ReadItemCap(0);
      const int red = ReadItemCount(6), red_cap = ReadItemCap(6);
      const int goldf = ReadItemCount(7), gold_cap = ReadItemCap(7);
      std::snprintf(buf, sizeof buf,
                    "tela de status desenhada: barra de favos %s (menu %u->%u); ovos %d/%d, penas "
                    "vermelhas %d/%d, penas douradas %d/%d",
                    hud_drawn ? "visível" : "não encontrada", menu_before, menu_open, eggs,
                    eggs_cap, red, red_cap, goldf, gold_cap);
      push({"hud", hud_drawn && hud_img.width != 0 &&
                                eggs >= 0 && eggs <= eggs_cap && red >= 0 && red <= red_cap &&
                                goldf >= 0 && goldf <= gold_cap,
                       buf});
      // rastreador por mundo: totais do jogo e leitura de uma bandeira de cada tipo
      {
        const pc::progress::Snapshot snap = pc::progress::Read();
        bool ok = snap.valid;
        std::string detail = snap.valid ? "" : "bloco de progresso não carregado; ";
        static constexpr int kExpect[pc::progress::kKindCount] = {45, 90, 25, 17, 25, 900};
        for (int k = 0; k < pc::progress::kKindCount; ++k) {
          int sum = 0;
          for (const auto& w : snap.worlds) sum += w.total[k];
          ok = ok && snap.total[k] == kExpect[k] && sum == kExpect[k];
          std::snprintf(buf, sizeof buf, "%s %u/%u; ",
                        pc::progress::KindName(pc::progress::Kind(k)), snap.got[k], snap.total[k]);
          detail += buf;
        }
        int world = -1;
        const int dj = pc::progress::ProbeItemFlag(pc::progress::kJiggy, 1, &world);
        const int dc = pc::progress::ProbeItemFlag(pc::progress::kCheato, 25, nullptr);
        ok = ok && (dj == 1 || dj == -1) && (dc == 1 || dc == -1) && world == 0;
        std::snprintf(buf, sizeof buf,
                      "bandeira do Jiggy 1 muda %+d em %s; da página 25 muda %+d (desfeito)", dj,
                      world >= 0 ? snap.worlds[world].name : "?", dc);
        detail += buf;
        push({"tracker", ok, detail});
      }
      // Menu de pausa (pedido de 2026-11-12, MECHANICS.md §17): sem CLASSIFICAÇÕES, SAIR DO
      // JOGO por último e "CONFIGURAÇÕES DE PC" (id 0x0F00, ícone do Kazooie = 13) no fim de
      // AJUDA E OPÇÕES. O passo lê as entradas do próprio jogo (rótulo em entrada+0x3B, ícone em
      // entrada+8), entra em AJUDA E OPÇÕES, desce até a última entrada conferindo o cursor real
      // (tela+7) e confirma com A: a entrada tem de abrir o menu de PC.
      {
        namespace mm = pc::mechanics;
        if (ReadU8(0x8266EF55) != 1) {
          for (int tries = 0; tries < 4 && ReadU8(0x8266EF55) != 1; ++tries) {
            Tap(input::kStart, 12);
            WaitFrames(80);
          }
        }
        WaitFrames(60);
        struct Entry {
          std::string label;
          uint32_t icon;
        };
        auto page_entries = [&](uint32_t* page_out, uint32_t* cursor_out) {
          std::vector<Entry> list;
          const uint32_t pages = ReadU32(kScreenObjAddr + 0x154);
          const uint32_t page = ReadU32(kScreenObjAddr + 0x2C);
          if (page_out) *page_out = page;
          if (cursor_out) *cursor_out = ReadU8(kScreenObjAddr + mm::kScreenCursorOff);
          if (pages < 0x40000000u || pages >= 0xA0000000u || page >= 16) return list;
          const uint32_t entries = ReadU32(pages + page * 0x2C);
          const uint32_t count = ReadU32(pages + page * 0x2C + 4);
          if (entries < 0x40000000u || entries >= 0xA0000000u || count > 64) return list;
          for (uint32_t i = 0; i < count; ++i) {
            const uint32_t e = entries + i * mm::kEntrySize;
            std::string t;
            for (uint32_t k = 0; k < mm::kEntryLabelMax; ++k) {
              const uint8_t c = ReadU8(e + mm::kEntryLabelOff + k);
              if (!c) break;
              t += char(c);
            }
            list.push_back({t, ReadU8(e + mm::kEntryIconOff)});
          }
          return list;
        };
        auto index_of = [](const std::vector<Entry>& l, const std::string& t) {
          for (size_t i = 0; i < l.size(); ++i)
            if (l[i].label == t) return int(i);
          return -1;
        };
        auto step_to = [&](uint32_t target) {
          for (int i = 0; i < 12; ++i) {
            uint32_t cur = 0;
            page_entries(nullptr, &cur);
            if (cur == target) return true;
            const bool down = cur < target;
            input::SetButton(down ? input::kDpadDown : input::kDpadUp, true);
            WaitFrames(30);
            input::SetButton(down ? input::kDpadDown : input::kDpadUp, false);
            WaitFrames(25);
          }
          uint32_t cur = 0;
          page_entries(nullptr, &cur);
          return cur == target;
        };
        const std::string kLeader = GameText(1);  // CLASSIFICAÇÕES / LEADERBOARDS
        const std::string kQuit = GameText(61);   // SAIR DO JOGO / EXIT GAME
        const std::string kHelp = GameText(3);    // AJUDA E OPÇÕES / HELP & OPTIONS
        const std::string kPcEntry = PortLabel("CONFIGURA\xC7\xD5" "ES DE PC", "PC SETTINGS");
        uint32_t page0 = 99;
        const std::vector<Entry> p0 = page_entries(&page0, nullptr);
        const bool no_leader = index_of(p0, kLeader) < 0;
        const bool quit_last = !p0.empty() && p0.back().label == kQuit;
        const bool pc_not_on_p0 = index_of(p0, kPcEntry) < 0;
        const int help_idx = index_of(p0, kHelp);
        bool in_help = false;
        uint32_t page1 = 99;
        std::vector<Entry> p1;
        if (page0 == 0 && help_idx >= 0 && step_to(uint32_t(help_idx))) {
          Tap(input::kA, 12);
          for (int w = 0; w < 60 && !in_help; ++w) {
            WaitFrames(2);
            p1 = page_entries(&page1, nullptr);
            in_help = page1 == mm::kHelpPageIndex && !p1.empty();
          }
          WaitFrames(70);  // os rótulos da página aparecem letra a letra
          p1 = page_entries(&page1, nullptr);
        }
        const bool pc_last = !p1.empty() && p1.back().label == kPcEntry;
        const bool kazooie = pc_last && p1.back().icon == mm::kKazooieIcon;
        rex::ui::RawImage help_img = capture();
        if (help_img.width) {
          ReduceTo1920(help_img);
          std::filesystem::path png = opts.self_test;
          png.replace_extension();
          png += "_ajuda.png";
          WritePng(png, help_img.width, help_img.height, help_img.data.data(), help_img.stride);
        }
        const uint32_t labels0 = pc::state::pc_entry_label_requests.load();
        const bool at_pc = pc_last && step_to(uint32_t(p1.size() - 1));
        WaitFrames(20);
        const uint32_t labels = pc::state::pc_entry_label_requests.load();
        // Pedido de 2026-11-12: a entrada abre as Configurações de PC DENTRO do menu do jogo
        // (página 7, MECHANICS.md §18), não a janela sobreposta. Confere pela memória do jogo:
        // a página atual, as entradas que o jogo desenha (rótulos em entrada+0x3B), o cursor e
        // o valor da opção; e que nenhuma janela sobreposta abriu.
        const uint32_t kPcPage = pc::ingame::PageIndex();
        auto wait_page = [&](uint32_t want, size_t min_entries, std::vector<Entry>* out) {
          uint32_t pg = 99;
          std::vector<Entry> l;
          for (int w = 0; w < 90; ++w) {
            WaitFrames(2);
            l = page_entries(&pg, nullptr);
            if (pg == want && l.size() >= min_entries && ReadU8(kScreenObjAddr + 4) == 6) break;
          }
          if (out) *out = l;
          return pg == want && l.size() >= min_entries;
        };
        auto starts = [](const std::string& s, const std::string& p) {
          return s.compare(0, p.size(), p) == 0;
        };
        // Passo curto na página 7 (o direcional age na borda; 8 + 12 quadros bastam, como na
        // sonda --probe-pcpages): o auto-teste inteiro tem de caber em 80 s.
        auto fast_to = [&](uint32_t target) {
          for (int i = 0; i < 30; ++i) {
            uint32_t cur = 99;
            page_entries(nullptr, &cur);
            if (cur == target) return true;
            const uint16_t b = cur < target ? input::kDpadDown : input::kDpadUp;
            input::SetButton(b, true);
            WaitFrames(8);
            input::SetButton(b, false);
            WaitFrames(12);
          }
          uint32_t cur = 99;
          page_entries(nullptr, &cur);
          return cur == target;
        };
        const std::string kGraphics = Utf8ToLatin1(pc::menu::options::SectionTitle(0));
        const std::string kGameSec = Utf8ToLatin1(pc::menu::options::SectionTitle(1));
        const std::string kFpsRow = PortLabel("CONTADOR DE FPS : ", "FPS COUNTER : ");
        const std::string kYesL = PortLabel("SIM", "YES");
        const std::string kNoL = PortLabel("N\xC3O", "NO");
        std::vector<Entry> root, game;
        bool in_root = false, root_ok = false, in_game = false, toggled = false,
             restored = false, back_root = false, back_help = false;
        int fps_row = -1;
        std::string label_on, label_off;
        const bool fps_before = pc::Get().show_fps;
        if (at_pc) {
          Tap(input::kA, 12);
          in_root = wait_page(kPcPage, 5, &root);
          WaitFrames(70);  // as linhas aparecem letra a letra, como nas páginas do jogo
          root = page_entries(nullptr, nullptr);
          root_ok = root.size() == 5 && root[0].label == kGraphics && root[1].label == kGameSec;
        }
        rex::ui::RawImage menu_img = capture();  // quadro do PRÓPRIO jogo (sem sobreposição)
        if (menu_img.width) {
          ReduceTo1920(menu_img);
          std::filesystem::path png = opts.self_test;
          png.replace_extension();
          png += "_pcmenu.png";
          WritePng(png, menu_img.width, menu_img.height, menu_img.data.data(), menu_img.stride);
        }
        if (root_ok && fast_to(1)) {
          Tap(input::kA, 12);
          in_game = wait_page(kPcPage, 8, &game) && !game.empty() && starts(game[0].label, "60 FPS");
          WaitFrames(20);
          game = page_entries(nullptr, nullptr);
          for (size_t i = 0; i < game.size(); ++i)
            if (starts(game[i].label, kFpsRow)) fps_row = int(i);
        }
        if (in_game && fps_row >= 0 && fast_to(uint32_t(fps_row))) {
          WaitFrames(10);
          Tap(input::kDpadRight, 12);
          WaitFrames(24);
          std::vector<Entry> now = page_entries(nullptr, nullptr);
          label_on = size_t(fps_row) < now.size() ? now[size_t(fps_row)].label : "";
          toggled = pc::Get().show_fps != fps_before &&
                    label_on == kFpsRow + (pc::Get().show_fps ? kYesL : kNoL);
          rex::ui::RawImage jogo_img = capture();
          if (jogo_img.width) {
            ReduceTo1920(jogo_img);
            std::filesystem::path png = opts.self_test;
            png.replace_extension();
            png += "_pcmenu_jogo.png";
            WritePng(png, jogo_img.width, jogo_img.height, jogo_img.data.data(), jogo_img.stride);
          }
          Tap(input::kDpadLeft, 12);
          WaitFrames(24);
          now = page_entries(nullptr, nullptr);
          label_off = size_t(fps_row) < now.size() ? now[size_t(fps_row)].label : "";
          restored = pc::Get().show_fps == fps_before &&
                     label_off == kFpsRow + (fps_before ? kYesL : kNoL);
        }
        if (in_game) {
          Tap(input::kB, 12);
          std::vector<Entry> r2;
          back_root = wait_page(kPcPage, 5, &r2) && r2.size() == 5 &&
                      ReadU8(kScreenObjAddr + mm::kScreenCursorOff) == 1;
          WaitFrames(15);
        }
        if (in_root) {
          Tap(input::kB, 12);
          uint32_t cur = 99;
          back_help = wait_page(mm::kHelpPageIndex, 1, nullptr);
          WaitFrames(10);
          page_entries(nullptr, &cur);
          back_help = back_help && p1.size() > 0 && cur == uint32_t(p1.size() - 1);
        }
        // F1 durante o jogo livre: fecha a pausa (START), F1 abre a pausa já nas Configurações
        // de PC (toque de START injetado + troca de página pelo gancho) e F1 de novo volta ao
        // jogo. Pausa aberta = byte 0x8266EF55 == 1 (o mesmo que este teste usa acima).
        bool f1_open = false, f1_close = false;
        int f1_ms = -1;
        const bool f1_tested = opts.self_test_f1;
        if (f1_tested && back_help) {
          Tap(input::kStart, 12);
          // Espera por RELÓGIO o jogo voltar de fato (pausa fechada e sem cena): o F1 só injeta
          // START no jogo livre. Na pausa o contador de quadros apresentados anda mais rápido.
          {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            while (std::chrono::steady_clock::now() < until &&
                   (ReadU8(0x8266EF55) != 0 || ReadU8(mechanics::kCutsceneStateAddr) != 0))
              std::this_thread::sleep_for(std::chrono::milliseconds(30));
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
          }
          if (ReadU8(0x8266EF55) == 0) {
            const auto f1_t0 = std::chrono::steady_clock::now();
            pc::ingame::RequestToggle();
            // Espera por RELÓGIO: na pausa o contador de quadros apresentados anda mais rápido que
            // 60/s (1ª medida: a espera de 360 quadros acabou antes de a página aparecer).
            const auto f1_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!f1_open && std::chrono::steady_clock::now() < f1_deadline) {
              std::this_thread::sleep_for(std::chrono::milliseconds(30));
              uint32_t pg = 99;
              const std::vector<Entry> l = page_entries(&pg, nullptr);
              f1_open = pg == kPcPage && l.size() == 5 && ReadU8(kScreenObjAddr + 4) == 6 &&
                        ReadU8(0x8266EF55) == 1;
            }
            f1_ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - f1_t0).count());
            std::this_thread::sleep_for(std::chrono::milliseconds(900));  // linhas letra a letra
            rex::ui::RawImage f1_img = capture();
            if (f1_img.width) {
              ReduceTo1920(f1_img);
              std::filesystem::path png = opts.self_test;
              png.replace_extension();
              png += "_f1.png";
              WritePng(png, f1_img.width, f1_img.height, f1_img.data.data(), f1_img.stride);
            }
            pc::ingame::RequestToggle();
            const auto close_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            while (!f1_close && std::chrono::steady_clock::now() < close_deadline) {
              std::this_thread::sleep_for(std::chrono::milliseconds(30));
              f1_close = ReadU8(0x8266EF55) == 0;
            }
            WaitFrames(10);
          }
        }
        auto utf8 = [](const std::string& latin1) {  // rótulos do jogo são Latin-1
          std::string o;
          for (unsigned char c : latin1) {
            if (c < 0x80) {
              o += char(c);
            } else {
              o += char(0xC0 | (c >> 6));
              o += char(0x80 | (c & 0x3F));
            }
          }
          return o;
        };
        label_on = utf8(label_on);
        label_off = utf8(label_off);
        const auto st = pc::ingame::GetStatus();
        const bool overlay = pc::menu::IsOpen();
        const bool ok = no_leader && quit_last && pc_not_on_p0 && in_help && pc_last && kazooie &&
                        in_root && root_ok && in_game && toggled && restored && back_root &&
                        back_help && (!f1_tested || (f1_open && f1_close)) && !overlay;
        char pbuf[1000];
        std::snprintf(pbuf, sizeof pbuf,
                      "pausa com %zu entradas (CLASSIFICAÇÕES ausente=%d, SAIR DO JOGO por "
                      "último=%d); AJUDA E OPÇÕES com %zu entradas, CONFIGURAÇÕES DE PC por "
                      "último=%d com ícone %u (Kazooie=%u); rótulo pedido %u vez(es); A abriu a "
                      "página %u DO JOGO=%d com %zu seções (GRÁFICOS/JOGO=%d); seção JOGO=%d "
                      "(%zu linhas); direita: '%s' (valor trocado=%d), esquerda: '%s' "
                      "(desfeito=%d); B voltou à raiz no cursor 1=%d e a AJUDA E OPÇÕES na "
                      "entrada de PC=%d; F1 no jogo (só com --self-test-f1: %s) abriu a pausa "
                      "já na página de PC=%d (%d ms) e F1 de novo voltou ao jogo=%d; janela sobreposta "
                      "aberta=%d; trocas de página=%u",
                      p0.size(), no_leader ? 1 : 0, quit_last ? 1 : 0, p1.size(), pc_last ? 1 : 0,
                      p1.empty() ? 0u : p1.back().icon, mm::kKazooieIcon, labels - labels0, kPcPage,
                      in_root ? 1 : 0, root.size(), root_ok ? 1 : 0, in_game ? 1 : 0, game.size(),
                      label_on.c_str(), toggled ? 1 : 0, label_off.c_str(), restored ? 1 : 0,
                      back_root ? 1 : 0, back_help ? 1 : 0, f1_tested ? "testado" : "não testado",
                      f1_open ? 1 : 0, f1_ms, f1_close ? 1 : 0,
                      overlay ? 1 : 0, st.switches);
        push({"pcmenu", ok, pbuf});
        if (overlay) pc::state::menu_close_request.store(true);
        WaitFrames(10);
      }
      if (hud_img.width) {
        ReduceTo1920(hud_img);
        std::filesystem::path png = opts.self_test;
        png.replace_extension(".png");
        WritePng(png, hud_img.width, hud_img.height, hud_img.data.data(), hud_img.stride);
      }
      WriteSelfTestOnce(opts.self_test, steps);
    }
    Finish(code);
  }).detach();
}

}  // namespace pc::automation
