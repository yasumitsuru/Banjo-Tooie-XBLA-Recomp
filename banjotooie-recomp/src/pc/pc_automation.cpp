#include "pc_automation.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <rex/input/input_system.h>
#include <rex/input/test_gamepad.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/presenter.h>
#include <rex/ui/windowed_app_context.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "pc_menu.h"
#include "pc_progress.h"
#include "pc_settings.h"
#include "pc_state.h"
#include "png_write.h"
#include "scripted_input.h"

namespace pc::automation {
namespace {

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

rex::ui::RawImage CaptureWindowOnScreen() {
  rex::ui::RawImage img;
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

std::vector<MemBlock> SnapshotMemory() {
  auto* mem = rex::system::kernel_state()->memory();
  const uint8_t* base = mem->virtual_membase();
  std::vector<MemBlock> out;
  // O jogo guarda os objetos também na memória física (0xA0000000..0xBFFFFFFF).
  const uint32_t kStart = 0x80000000, kEnd = 0xC0000000, kChunk = 0x10000;
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
  return r;
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

}  // namespace

Options ParseCommandLine() {
  Options o;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i + 1 < argc; ++i) {
    std::wstring a = argv[i];
    // Banjo 3: --screenshot fica com a implementacao da Missao 01 (banjotooie_app.cpp: relatorio
    // de boot, --screenshot_seconds, capturas de teste). Aqui so consome o argumento.
    if (a == L"--screenshot") ++i;
    else if (a == L"--self-test") o.self_test = argv[++i];
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
    else if (a == L"--probe-menucursor") o.probe_menucursor = argv[++i];
    else if (a == L"--probe-gamepad") o.probe_gamepad = argv[++i];
    else if (a == L"--probe-dd-maps") o.probe_dd_maps = argv[++i];
    else if (a == L"--probe-dd-worlds") o.probe_dd_worlds = argv[++i];
    else if (a == L"--probe-testpad") o.probe_testpad = argv[++i];
    else if (a == L"--probe-save-sim") o.probe_save_sim = argv[++i];
    else if (a == L"--make-advanced-save") o.make_advanced_save = argv[++i];
    else if (a == L"--probe-advanced-save") o.probe_advanced_save = argv[++i];
    else if (a == L"--dialog-speed") o.dialog_speed = _wtoi(argv[++i]);
  }
  if (argv) LocalFree(argv);
  if (o.frames < 1) o.frames = 120;
  if (o.measure_secs < 1) o.measure_secs = 5;
  return o;
}

bool IsRunning() { return g_automation_running.load(); }

void Start(const Options& opts, rex::ui::WindowedAppContext* ui,
           std::function<rex::ui::Presenter*()> presenter,
           rex::system::IInputSystem* input_system) {
  g_automation_running.store(true);
  // a sonda de cenas compara o ritmo original: sem o "início rápido" dos logos
  if (!opts.probe_scenes.empty()) pc::Get().skip_intro_logos = false;
  if (!opts.self_test.empty()) {
    // Início rápido só durante o teste (os logos passam em passo 4); a escolha do jogador
    // volta logo que o teste chega ao jogo livre, antes de qualquer gravação das opções.
    g_saved_skip_intro_logos = pc::Get().skip_intro_logos;
    pc::Get().skip_intro_logos = true;
    // Cão de guarda: grava o que já foi medido e encerra antes do limite da verificação.
    std::thread([file = opts.self_test]() {
      while (SecondsSinceProcessStart() < kSelfTestBudgetSecs)
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
                    kSelfTestBudgetSecs, steps.size());
      steps.push_back({"time_budget", false, buf});
      WriteSelfTestOnce(file, steps);
      Finish(4);
    }).detach();
  }
  std::thread([opts, ui, presenter, input_system]() {
    using namespace std::chrono;
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
        if (pages) o << ",";
        o << "{\"chars\":" << int(total) << ",\"shown_at_start\":" << int(shown0)
          << ",\"seconds\":" << secs << ",\"frames\":" << frames
          << ",\"chars_per_second\":" << (secs > 0 ? typed / secs : 0) << "}";
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
      input::SetActive(true);
      NavigateToGameplay();
      const uint8_t before = ReadU8(0x8266EF55);
      HWND hwnd = GetForegroundWindow();
      if (!hwnd) hwnd = FindWindowW(L"RexWindowClass", nullptr);
      std::ostringstream o;
      o << "{\"version\":1,\"paused_before\":" << int(before);
      if (hwnd) {
        ShowWindow(hwnd, SW_MINIMIZE);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
      const uint8_t after = ReadU8(0x8266EF55);
      o << ",\"paused_after\":" << int(after) << ",\"hwnd\":" << (hwnd ? 1 : 0) << "}\n";
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
      // pausa -> 6ª entrada (SAIR DO JOGO, que grava) -> confirma SIM
      const bool paused = OpenPauseMenu();
      WaitFrames(40);
      for (int i = 0; i < 5; ++i) {
        Tap(input::kDpadDown);
        WaitFrames(30);
      }
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
      // (id, chamador), abre o menu de pausa, desce até a 4ª entrada e confirma.
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
      // Entrada "CONFIGURAÇÕES DE PC" no menu de pausa do jogo (4ª entrada, no lugar de
      // "CLASSIFICAÇÕES"): o jogo pede o rótulo novo e, confirmada, a entrada abre o menu de PC.
      {
        if (ReadU8(0x8266EF55) != 1) {
          for (int tries = 0; tries < 4 && ReadU8(0x8266EF55) != 1; ++tries) {
            Tap(input::kStart, 12);
            WaitFrames(80);
          }
        }
        WaitFrames(60);
        auto nav = [&](uint16_t button, int16_t ly, int hold_frames) {
          input::Get().thumb_ly = ly;
          input::SetButton(button, true);
          WaitFrames(hold_frames);
          input::SetButton(button, false);
          input::Get().thumb_ly = 0;
          WaitFrames(25);
        };
        // volta ao topo (CONTINUAR JOGO) e desce três entradas
        for (int i = 0; i < 6; ++i) nav(input::kDpadUp, -32767, 30);
        const uint32_t labels0 = pc::state::pc_entry_label_requests.load();
        for (int i = 0; i < 3; ++i) nav(input::kDpadDown, 32767, 45);
        WaitFrames(20);
        const uint32_t labels = pc::state::pc_entry_label_requests.load();
        const uint32_t act0 = pc::state::pc_entry_activations.load();
        Tap(input::kA, 12);
        bool opened = false;
        for (int w = 0; w < 60 && !opened; ++w) {
          WaitFrames(2);
          opened = pc::menu::IsOpen();
        }
        const uint32_t activated = pc::state::pc_entry_activations.load() - act0;
        WaitFrames(20);  // alguns quadros da interface com o menu já desenhado
        rex::ui::RawImage menu_img = CaptureWindowOnScreen();
        if (menu_img.width) {
          ReduceTo1920(menu_img);
          std::filesystem::path png = opts.self_test;
          png.replace_extension();
          png += "_pcmenu.png";
          WritePng(png, menu_img.width, menu_img.height, menu_img.data.data(), menu_img.stride);
        }
        const bool ok = labels > 0 && activated > 0 && opened;
        std::snprintf(buf, sizeof buf,
                      "rótulo pedido pelo jogo %u vezes (%u na navegação); entrada confirmada %u "
                      "vez(es); menu de PC aberto=%d",
                      labels, labels - labels0, activated, opened ? 1 : 0);
        push({"pcmenu", ok, buf});
        if (opened) pc::state::menu_close_request.store(true);
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
