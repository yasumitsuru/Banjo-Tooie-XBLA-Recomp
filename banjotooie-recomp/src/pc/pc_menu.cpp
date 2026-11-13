#include "pc_menu.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "pc_automation.h"
#include "pc_ingame_menu.h"
#include "pc_lang.h"
#include "pc_progress.h"
#include "pc_saves.h"
#include "pc_settings.h"
#include "pc_state.h"
#include "scripted_input.h"

#include <rex/ui/virtual_key.h>

namespace pc::menu {
namespace {

std::filesystem::path g_runtime_config;

// Opções passadas na LINHA DE COMANDO valem só para aquela execução (pastas e log de um teste,
// --audio_mute do auto-teste, o --cache_path que o lançador do Craft acrescenta...). Guardadas
// na partida: nome -> valor naquele momento; e as linhas que o arquivo tinha para elas.
std::map<std::string, std::string> g_cmdline_cvars;
std::map<std::string, std::string> g_file_lines_at_start;

void CaptureSessionCvars() {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; ++i) {
    const std::wstring a = argv[i];
    if (a.size() < 3 || a[0] != L'-' || a[1] != L'-') continue;
    std::string name;
    for (size_t k = 2; k < a.size() && a[k] != L'='; ++k)
      name += a[k] < 0x80 ? char(a[k]) : '?';
    const std::string value = rex::cvar::GetFlagByName(name);
    if (!value.empty()) g_cmdline_cvars[name] = value;
  }
  if (argv) LocalFree(argv);
  FILE* f = nullptr;
  if (g_runtime_config.empty() || _wfopen_s(&f, g_runtime_config.c_str(), L"rb") != 0 || !f) return;
  std::string text;
  char buf[4096];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
  std::fclose(f);
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    g_file_lines_at_start[line.substr(0, line.find(' '))] = line;
  }
}

// Grava banjotooie.toml só com as opções que o jogador escolhe. rex::cvar::SaveConfig gravaria
// todo valor diferente do padrão, inclusive os da sessão: pastas passadas na linha de comando
// (--user_data_root de um teste) e o tamanho de janela calculado a cada início — e o próximo
// início leria a pasta de teste e uma janela velha. Gravação atômica (temporário + troca).
// Opção vinda da linha de comando e não mudada depois: fica a linha que o arquivo já tinha (ou
// nenhuma). Um teste com --audio_mute não deixa o jogo mudo para sempre (achado em 2026-11-12).
void SaveRuntimeConfig() {
  if (g_runtime_config.empty()) return;
  static const char* const kSessionOnly[] = {"user_data_root", "game_data_root", "window_width",
                                             "window_height", "config_path"};
  std::string out = "# Auto-generated cvar configuration\n";
  const std::string all = rex::cvar::SerializeToTOML();
  size_t pos = 0;
  while (pos < all.size()) {
    size_t end = all.find('\n', pos);
    if (end == std::string::npos) end = all.size();
    const std::string line = all.substr(pos, end - pos);
    pos = end + 1;
    if (line.empty()) continue;
    const std::string key = line.substr(0, line.find(' '));
    bool skip = false;
    for (const char* s : kSessionOnly) skip = skip || key == s;
    if (skip) continue;
    const auto cmd = g_cmdline_cvars.find(key);
    if (cmd != g_cmdline_cvars.end() && rex::cvar::GetFlagByName(key) == cmd->second) {
      const auto old = g_file_lines_at_start.find(key);
      if (old != g_file_lines_at_start.end()) out += old->second + "\n";
      continue;
    }
    out += line + "\n";
  }
  std::error_code ec;
  const auto tmp = std::filesystem::path(g_runtime_config).concat(L".tmp");
  {
    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f) return;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    if (!ok) return;
  }
  if (!MoveFileExW(tmp.c_str(), g_runtime_config.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(tmp, ec);
  }
}

rex::ui::Window* g_window = nullptr;
bool g_menu_open = false;
bool g_restart_needed = false;
std::string g_status;
std::mutex g_trace_mu;                       // protege o controle sintético e o rastro
std::optional<GamepadState> g_synthetic_pad; // definido = teste sintético no lugar do XInput
std::vector<GamepadTraceEntry> g_trace;      // quadros do menu durante o teste sintético
bool g_synthetic_was_set = false;            // o quadro anterior usou o controle sintético
rex::input::InputSystem* g_input_system = nullptr;  // estado fundido dos gamepads reais (SDL/XInput)
bool g_focus_test = false;                   // foco de navegação preso à caixa "60 FPS" (sonda)
uint32_t g_pin_id = 0;                       // id da caixa "60 FPS" (para comparar o foco)

// ---------------------------------------------------------------- controle no menu de PC
// O menu é aberto pelo controle (entrada do menu de pausa), então ele também precisa ser
// usado pelo controle: lê o XInput direto (o jogo fica bloqueado enquanto o menu está aberto)
// e entrega à navegação do ImGui. LB/RB trocam de aba; B (sem lista aberta) ou Start fecha.
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, void*);
struct XPad {
  DWORD packet;
  WORD buttons;
  BYTE lt, rt;
  SHORT lx, ly, rx, ry;
};
XInputGetStateFn LoadXInput() {
  static XInputGetStateFn fn = []() -> XInputGetStateFn {
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
      if (HMODULE m = LoadLibraryW(dll))
        if (auto f = GetProcAddress(m, "XInputGetState")) return reinterpret_cast<XInputGetStateFn>(f);
    }
    return nullptr;
  }();
  return fn;
}
WORD g_pad_prev = 0;
std::chrono::steady_clock::time_point g_unblock_at{};
int g_tab_request = -1;  // aba a selecionar no próximo quadro (LB/RB)
int g_tab_current = 0;

// Mapa de botões do controle para teclas do ImGui (usado para pressionar e SOLTAR).
constexpr struct {
  WORD mask;
  ImGuiKey key;
} kPadKeyMap[] = {{0x0001, ImGuiKey_GamepadDpadUp},    {0x0002, ImGuiKey_GamepadDpadDown},
                  {0x0004, ImGuiKey_GamepadDpadLeft},  {0x0008, ImGuiKey_GamepadDpadRight},
                  {0x1000, ImGuiKey_GamepadFaceDown},  {0x2000, ImGuiKey_GamepadFaceRight},
                  {0x4000, ImGuiKey_GamepadFaceLeft},  {0x8000, ImGuiKey_GamepadFaceUp}};

// Lê o controle 0 e alimenta o ImGui. Devolve os botões que acabaram de ser apertados.
// Ordem de fontes: (1) controle sintético definido (SetSyntheticGamepad) — MESMO caminho
// de navegação para o auto-teste; (2) estado fundido dos gamepads reais do runtime
// (SDL/XInput, GetGamepadState — qualquer controle que o SDL reconheça, sem o bloqueio do
// host); (3) XInput direto como reserva quando o runtime não está disponível.
WORD FeedGamepad(ImGuiIO& io) {
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
  static uint16_t last_synthetic = 0;
  GamepadState pad{};
  bool synthetic = false;
  bool deadzone_applied = false;  // fonte já aplicou a zona morta do usuário
  {
    std::lock_guard<std::mutex> lk(g_trace_mu);
    if (g_synthetic_pad) {
      pad = *g_synthetic_pad;
      g_synthetic_was_set = true;
      synthetic = true;
    } else {
      g_synthetic_was_set = false;
      rex::input::X_INPUT_STATE st{};
      // X_ERROR_SUCCESS = 0 (macros X_ERROR_* expandem para rex::X_RESULT, que não
      // fica visível daqui sem using; o código numérico é o mesmo).
      if (g_input_system && g_input_system->GetGamepadState(0, &st) == 0) {
        pad.buttons = st.gamepad.buttons;
        pad.left_trigger = st.gamepad.left_trigger;
        pad.right_trigger = st.gamepad.right_trigger;
        pad.lx = st.gamepad.thumb_lx;
        pad.ly = st.gamepad.thumb_ly;
        pad.rx = st.gamepad.thumb_rx;
        pad.ry = st.gamepad.thumb_ry;
        deadzone_applied = true;  // os drivers SDL/XInput já aplicam pad_deadzone
      } else {
        XPad xp{};
        auto get = LoadXInput();
        if (!get || get(0, &xp) != 0) {
          g_pad_prev = 0;
          return 0;
        }
        pad.buttons = xp.buttons;
        pad.left_trigger = xp.lt;
        pad.right_trigger = xp.rt;
        pad.lx = xp.lx;
        pad.ly = xp.ly;
        pad.rx = xp.rx;
        pad.ry = xp.ry;
      }
    }
  }
  // Ao sair do modo sintético, solta as teclas que ficaram abaixadas (senão o ImGui as
  // manteria pressionadas para sempre e só o primeiro aperto registraria).
  if (!synthetic && last_synthetic) {
    for (const auto& m : kPadKeyMap)
      if (last_synthetic & m.mask) io.AddKeyEvent(m.key, false);
    last_synthetic = 0;
  } else if (synthetic) {
    last_synthetic = pad.buttons;
  }
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  const WORD b = pad.buttons;
  for (const auto& m : kPadKeyMap) io.AddKeyEvent(m.key, (b & m.mask) != 0);
  // Limiar do analógico: a zona morta configurada (Controles → Zona morta) quando a fonte
  // é bruta; os drivers já a aplicaram e rescataram o eixo quando deadzone_applied.
  const float dz =
      deadzone_applied ? 0.05f : std::clamp(pc::Get().pad_deadzone, 0.0f, 0.9f);
  auto axis = [&](ImGuiKey neg, ImGuiKey pos, SHORT v) {
    const float f = std::clamp(float(v) / 32767.0f, -1.0f, 1.0f);
    io.AddKeyAnalogEvent(neg, f < -dz, f < -dz ? -f : 0.0f);
    io.AddKeyAnalogEvent(pos, f > dz, f > dz ? f : 0.0f);
  };
  axis(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, pad.lx);
  axis(ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp, pad.ly);
  const WORD pressed = static_cast<WORD>(b & ~g_pad_prev);
  g_pad_prev = b;
  return pressed;
}

void CloseMenu();

// Captura de tecla para remapeamento. -1 = nenhuma captura em andamento.
int g_capture_action = -1;
int g_capture_delay = 0;
std::string g_capture_label;

struct KeyAction {
  const char* label;
  const char* cvar;   // nullptr = tecla da câmera (pc_settings)
  int camera_slot;    // 0=cima 1=baixo 2=esquerda 3=direita
};

const KeyAction kKeyActions[] = {
    {"Iniciar / pausa", "keybind_start", -1},
    {"Botão A (pular)", "keybind_a", -1},
    {"Botão B (voltar/ovo)", "keybind_b", -1},
    {"Botão X", "keybind_x", -1},
    {"Botão Y", "keybind_y", -1},
    {"Direcional cima", "keybind_dpad_up", -1},
    {"Direcional baixo", "keybind_dpad_down", -1},
    {"Direcional esquerda", "keybind_dpad_left", -1},
    {"Direcional direita", "keybind_dpad_right", -1},
    {"Mover cima", "keybind_lstick_up", -1},
    {"Mover baixo", "keybind_lstick_down", -1},
    {"Mover esquerda", "keybind_lstick_left", -1},
    {"Mover direita", "keybind_lstick_right", -1},
    {"Câmera cima", nullptr, 0},
    {"Câmera baixo", nullptr, 1},
    {"Câmera esquerda", nullptr, 2},
    {"Câmera direita", nullptr, 3},
};

std::string CurrentKeyName(const KeyAction& a) {
  if (a.cvar) return rex::cvar::GetFlagByName(a.cvar);
  const auto& s = pc::Get();
  switch (a.camera_slot) {
    case 0: return s.cam_key_up;
    case 1: return s.cam_key_down;
    case 2: return s.cam_key_left;
    default: return s.cam_key_right;
  }
}

void AssignKey(const KeyAction& a, const std::string& name) {
  if (a.cvar) {
    rex::cvar::SetFlagByName(a.cvar, name);
    SaveRuntimeConfig();
    return;
  }
  auto& s = pc::Get();
  switch (a.camera_slot) {
    case 0: s.cam_key_up = name; break;
    case 1: s.cam_key_down = name; break;
    case 2: s.cam_key_left = name; break;
    default: s.cam_key_right = name; break;
  }
  pc::Save();
}

// Lê a próxima tecla pressionada (fora do ImGui) para remapear.
void PollKeyCapture() {
  if (g_capture_action < 0) return;
  if (g_capture_delay > 0) {
    --g_capture_delay;
    return;
  }
  for (int vk = 1; vk < 256; ++vk) {
    if (!(GetAsyncKeyState(vk) & 0x8000)) continue;
    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
    const std::string name =
        rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk));
    if (name.empty()) continue;
    AssignKey(kKeyActions[g_capture_action], name);
    g_capture_action = -1;
    g_capture_label.clear();
    g_status = "Tecla alterada para " + name + ".";
    return;
  }
}

// ----------------------------------------------------------------- contadores do jogo
uint16_t ReadItemCount(int id) {
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return 0;
  const uint8_t* p = ks->memory()->virtual_membase();
  if (!p) return 0;
  const uint32_t pa = 0x82673668;
  const uint32_t table = (uint32_t(p[pa]) << 24) | (uint32_t(p[pa + 1]) << 16) |
                         (uint32_t(p[pa + 2]) << 8) | p[pa + 3];
  if (table < 0x80000000u || table >= 0xC0000000u) return 0;
  const uint32_t cur_addr = table + uint32_t(id) * 2;
  const uint32_t key_addr = 0x82249598 + uint32_t(id) * 8;
  const uint16_t cur = uint16_t((p[cur_addr] << 8) | p[cur_addr + 1]);
  const uint16_t key = uint16_t((p[key_addr] << 8) | p[key_addr + 1]);
  return uint16_t(cur ^ key);
}

uint16_t ReadItemCap(int id) {
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return 0;
  const uint8_t* p = ks->memory()->virtual_membase();
  if (!p) return 0;
  const uint32_t a = 0x8262E772 + uint32_t(id) * 4;
  return uint16_t((p[a] << 8) | p[a + 1]);
}

// ----------------------------------------------------------------- cvars do runtime
std::string CvarStr(const char* name) { return rex::cvar::GetFlagByName(name); }
bool CvarBool(const char* name) { return CvarStr(name) == "true"; }
int CvarInt(const char* name) { return std::atoi(CvarStr(name).c_str()); }
double CvarDouble(const char* name) { return std::atof(CvarStr(name).c_str()); }
void SetCvar(const char* name, const std::string& v) { rex::cvar::SetFlagByName(name, v); }

// Resolução de saída + escala interna de renderização (o jogo desenha em 1280x720;
// a escala inteira 2x/3x renderiza em 2560x1440 / 3840x2160).
void ApplyResolutionCvars(Resolution r) {
  const char* preset = r == Resolution::R1080p ? "1080p" : r == Resolution::R1440p ? "1440p" : "4k";
  const int scale = r == Resolution::R2160p ? 3 : r == Resolution::R1440p ? 2 : 1;
  SetCvar("resolution", preset);
  SetCvar("draw_resolution_scale_x", std::to_string(scale));
  SetCvar("draw_resolution_scale_y", std::to_string(scale));
}

void SaveAll() {
  bool ok = pc::Save();
  // Testes automáticos (--self-test, sondas) não tocam o arquivo do runtime ao lado do jogo; as
  // opções de PC deles vão para a pasta de dados do teste (pc_settings.toml).
  if (!pc::automation::IsRunning()) SaveRuntimeConfig();
  g_status = ok ? "Configurações salvas." : "Falha ao salvar as configurações.";
}

// ----------------------------------------------------------------- tarefas da thread da interface
// As páginas de PC dentro do jogo rodam na thread do jogo; janela, cursor, cvars do runtime e
// arquivos são aplicados na thread da interface, pelo LAÇO DE MENSAGENS da janela
// (CallInUIThreadDeferred) — nunca dentro do desenho do quadro. Correção de 2026-11-12: antes a
// fila era esvaziada em PcSettingsDialog::OnDraw, que roda dentro de
// D3D12Presenter::PaintAndPresentImpl; trocar para sem bordas/tela cheia ali redimensionava a
// janela no meio da pintura e o driver de vídeo caía (0xC0000005 em nvwgf2umx.dll, reproduzido
// pela sonda --probe-fullscreen na primeira troca).
std::mutex g_ui_mu;
std::vector<std::function<void()>> g_ui_tasks;
std::map<std::string, std::string> g_cvar_pending;  // valor pedido e ainda não aplicado
std::atomic<bool> g_ui_drain_scheduled{false};

void DrainUi() {
  g_ui_drain_scheduled.store(false);
  std::vector<std::function<void()>> tasks;
  {
    std::lock_guard<std::mutex> lk(g_ui_mu);
    tasks.swap(g_ui_tasks);
  }
  for (auto& t : tasks) t();
}

void PostUi(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lk(g_ui_mu);
    g_ui_tasks.push_back(std::move(task));
  }
  if (!g_window || g_ui_drain_scheduled.exchange(true)) return;
  if (!g_window->app_context().CallInUIThreadDeferred([] { DrainUi(); }))
    g_ui_drain_scheduled.store(false);  // laço já encerrado: nada mais a aplicar
}

// Valor de um cvar contando o pedido ainda na fila (o texto da página mostra o valor novo já).
std::string CvarNow(const char* name) {
  {
    std::lock_guard<std::mutex> lk(g_ui_mu);
    auto it = g_cvar_pending.find(name);
    if (it != g_cvar_pending.end()) return it->second;
  }
  return CvarStr(name);
}

void SetCvarAsync(const char* name, const std::string& value) {
  const std::string key = name;
  {
    std::lock_guard<std::mutex> lk(g_ui_mu);
    g_cvar_pending[key] = value;
  }
  PostUi([key, value] {
    SetCvar(key.c_str(), value);
    std::lock_guard<std::mutex> lk(g_ui_mu);
    auto it = g_cvar_pending.find(key);
    if (it != g_cvar_pending.end() && it->second == value) g_cvar_pending.erase(it);
  });
}

std::atomic<bool> g_save_queued{false};
void RequestSave() {
  if (g_save_queued.exchange(true)) return;
  PostUi([] {
    g_save_queued.store(false);
    SaveAll();
  });
}

// Valores com que o programa começou (resolução, MSAA, FXAA e escala só mudam ao reiniciar).
Resolution g_startup_resolution = Resolution::R1440p;
bool g_startup_msaa = false;
std::map<std::string, std::string> g_startup_cvars;

// Algo terminou fora da thread do jogo e as linhas da página precisam ser reescritas.
std::atomic<uint32_t> g_options_generation{1};

// ----------------------------------------------------------------- atalho novo pela página do jogo
// A na linha de um atalho começa a captura: espera soltar tudo (o próprio A/Enter que pediu
// a captura não vale), aceita a próxima tecla com a janela do jogo em primeiro plano e desiste
// em 10 s. O jogo não recebe entrada durante a captura nem 300 ms depois.
std::atomic<int> g_ig_capture{-1};  // índice em kKeyActions, -1 = nenhuma
int g_ig_phase = 0;                 // 0 = esperando soltar, 1 = esperando a tecla
std::chrono::steady_clock::time_point g_ig_deadline{};

bool IsMouseVk(int vk) {
  return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 ||
         vk == VK_XBUTTON2;
}

void PollIngameCapture() {
  const int action = g_ig_capture.load();
  if (action < 0) return;
  const auto now = std::chrono::steady_clock::now();
  if (now > g_ig_deadline) {
    g_ig_capture.store(-1);
    g_unblock_at = now + std::chrono::milliseconds(300);
    g_options_generation.fetch_add(1);
    return;
  }
  HWND hwnd = g_window ? reinterpret_cast<HWND>(g_window->GetNativeWindowHandle()) : nullptr;
  if (!hwnd || GetForegroundWindow() != hwnd) return;  // teclas de outros programas não valem
  int down = 0;
  for (int vk = 1; vk < 256 && !down; ++vk)
    if (!IsMouseVk(vk) && (GetAsyncKeyState(vk) & 0x8000)) down = vk;
  if (g_ig_phase == 0) {
    if (!down) g_ig_phase = 1;
    return;
  }
  if (!down) return;
  const std::string name = rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(down));
  if (name.empty()) return;
  AssignKey(kKeyActions[action], name);
  g_ig_capture.store(-1);
  g_unblock_at = now + std::chrono::milliseconds(300);
  g_options_generation.fetch_add(1);
}

void StartIngameCapture(int action) {
  g_ig_phase = 0;
  g_ig_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  g_ig_capture.store(action);
}

// Restaurar backup pela página do jogo: 0 = parado, 1 = esperando confirmar, 2/3 = resultado.
std::atomic<int> g_restore_state{0};

// ----------------------------------------------------------------- modo de exibição
LONG_PTR g_saved_style = 0;
RECT g_saved_rect{};

// Depois de criar a janela, usa seu monitor real e uma única unidade (pixel físico).
// O clamp anterior à criação só é uma estimativa, pois SPI_GETWORKAREA/DPI globais
// podem ser virtualizados pelo Windows ou pertencer a outro monitor.
bool FitInitialWindowToResolution(rex::ui::Window* window, Resolution resolution) {
  if (!window || window->IsFullscreen()) return false;
  HWND hwnd = reinterpret_cast<HWND>(window->GetNativeWindowHandle());
  if (!hwnd || IsIconic(hwnd) || IsZoomed(hwnd)) return false;
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  using SetContextFn = HANDLE(WINAPI*)(HANDLE);
  auto set_context = user32 ? reinterpret_cast<SetContextFn>(
                                  GetProcAddress(user32, "SetThreadDpiAwarenessContext"))
                            : nullptr;
  if (!set_context) return false;
  HANDLE previous = set_context(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
  if (!previous) return false;
  const bool fitted = [&]() {
    MONITORINFO monitor{sizeof(monitor)};
    RECT outer{}, client{};
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) ||
        !GetWindowRect(hwnd, &outer) || !GetClientRect(hwnd, &client))
      return false;
    const int frame_width = std::max(0L, (outer.right - outer.left) -
                                           (client.right - client.left));
    const int frame_height = std::max(0L, (outer.bottom - outer.top) -
                                            (client.bottom - client.top));
    const int max_width = (monitor.rcWork.right - monitor.rcWork.left) - frame_width;
    const int max_height = (monitor.rcWork.bottom - monitor.rcWork.top) - frame_height;
    if (max_width <= 0 || max_height <= 0) return false;
    int width = resolution == Resolution::R1080p ? 1920
                : resolution == Resolution::R1440p ? 2560 : 3840;
    int height = resolution == Resolution::R1080p ? 1080
                 : resolution == Resolution::R1440p ? 1440 : 2160;
    if (width > max_width) {
      width = max_width;
      height = width * 9 / 16;
    }
    if (height > max_height) {
      height = max_height;
      width = height * 16 / 9;
    }
    if (width <= 0 || height <= 0) return false;
    const int outer_width = width + frame_width, outer_height = height + frame_height;
    const int left = std::clamp(int(outer.left), int(monitor.rcWork.left),
                                int(monitor.rcWork.right) - outer_width);
    const int top = std::clamp(int(outer.top), int(monitor.rcWork.top),
                               int(monitor.rcWork.bottom) - outer_height);
    return SetWindowPos(hwnd, nullptr, left, top, outer_width, outer_height,
                        SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE) != FALSE;
  }();
  set_context(previous);
  return fitted;
}

void SetBorderless(HWND hwnd, bool borderless) {
  if (!hwnd) return;
  LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  if (borderless) {
    if (!g_saved_style) {
      g_saved_style = style;
      GetWindowRect(hwnd, &g_saved_rect);
    }
    SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU |
                                                 WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
    // ocupa a área de trabalho do monitor atual (sem cobrir a barra de tarefas)
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& r = mi.rcWork;
    SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
  } else if (g_saved_style) {
    SetWindowLongPtrW(hwnd, GWL_STYLE, g_saved_style);
    SetWindowPos(hwnd, nullptr, g_saved_rect.left, g_saved_rect.top,
                 g_saved_rect.right - g_saved_rect.left, g_saved_rect.bottom - g_saved_rect.top,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
    g_saved_style = 0;
  }
}

void ApplyDisplayModeNow(rex::ui::Window* window, DisplayMode mode) {
  if (!window) return;
  HWND hwnd = reinterpret_cast<HWND>(window->GetNativeWindowHandle());
  switch (mode) {
    case DisplayMode::Janela:
      if (window->IsFullscreen()) window->SetFullscreen(false);
      SetBorderless(hwnd, false);
      break;
    case DisplayMode::SemBordas:
      if (window->IsFullscreen()) window->SetFullscreen(false);
      SetBorderless(hwnd, true);
      break;
    case DisplayMode::TelaCheia:
      SetBorderless(hwnd, false);
      if (!window->IsFullscreen()) window->SetFullscreen(true);
      break;
  }
  SetCvar("fullscreen", mode == DisplayMode::TelaCheia ? "true" : "false");
}

// ----------------------------------------------------------------- contador de FPS
class FpsOverlay : public rex::ui::ImGuiDialog {
 public:
  explicit FpsOverlay(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    using clock = std::chrono::steady_clock;
    const auto now = clock::now();
    const uint64_t swaps = pc::state::guest_swaps.load();
    const uint64_t logic = pc::state::frame_step_calls.load();
    const double dt = std::chrono::duration<double>(now - last_).count();
    if (dt >= 0.5) {
      fps_ = double(swaps - last_swaps_) / dt;
      ups_ = double(logic - last_logic_) / dt;
      last_ = now;
      last_swaps_ = swaps;
      last_logic_ = logic;
    }
    const auto& s = pc::Get();
    if (!s.show_fps && !s.show_perf_stats) return;
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##pc_fps", nullptr, flags)) {
      ImGui::Text("%.0f FPS", fps_);
      if (s.show_perf_stats) {
        ImGui::Text("Lógica: %.0f/s  passo: %u", ups_, pc::state::frame_step.load());
        ImGui::Text("Cena: %u  alcance orig.: %u", pc::state::cutscene_state.load(),
                    pc::state::last_draw_range_original.load());
        ImGui::Text("Distância: %s", pc::DrawDistanceName(s.draw_distance));
      }
    }
    ImGui::End();
  }

 private:
  std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
  uint64_t last_swaps_ = 0, last_logic_ = 0;
  double fps_ = 0, ups_ = 0;
};

// ----------------------------------------------------------------- rastreador de itens
class CollectibleOverlay : public rex::ui::ImGuiDialog {
 public:
  explicit CollectibleOverlay(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;
    if (!pc::Get().show_collectibles) return;
    // a memória do jogo só está pronta depois dos primeiros quadros
    if (pc::state::guest_swaps.load() < 60) return;
    const float scale = std::max(1.0f, ImGui::GetIO().DisplaySize.y / 1080.0f);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 18 * scale, 18 * scale),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.6f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##pc_collectibles", nullptr, flags)) {
      ImGui::SetWindowFontScale(scale * 1.05f);
      const pc::progress::Snapshot snap = pc::progress::Read();
      ImGui::TextUnformatted("PROGRESSO POR MUNDO");
      using pc::progress::Kind;
      static constexpr Kind kCols[] = {pc::progress::kJiggy, pc::progress::kJinjo,
                                       pc::progress::kHoneycomb, pc::progress::kCheato,
                                       pc::progress::kGlowbo, pc::progress::kNotes};
      static constexpr const char* kHeads[] = {"Jiggies", "Jinjos", "Favos", "Páginas",
                                               "Glowbos", "Notas"};
      if (snap.valid &&
          ImGui::BeginTable("##pc_progress", 7,
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Mundo");
        for (const char* h : kHeads) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        auto cell = [](uint16_t got, uint16_t total) {
          ImGui::TableNextColumn();
          if (total == 0) {
            ImGui::TextDisabled("-");
          } else if (got >= total) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "%u/%u", got, total);
          } else {
            ImGui::Text("%u/%u", got, total);
          }
        };
        for (const auto& w : snap.worlds) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(w.name);
          for (Kind k : kCols) cell(w.got[k], w.total[k]);
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("TOTAL");
        for (Kind k : kCols) cell(snap.got[k], snap.total[k]);
        ImGui::EndTable();
      } else if (!snap.valid) {
        ImGui::TextDisabled("aguardando o jogo carregar o progresso");
      }
      ImGui::Separator();
      ImGui::TextUnformatted("ITENS");
      auto row = [](const char* name, int id) {
        ImGui::Text("%s: %u / %u", name, unsigned(ReadItemCount(id)),
                    unsigned(ReadItemCap(id)));
      };
      row("Ovos azuis", 0);
      row("Ovos de fogo", 1);
      row("Ovos de gelo", 2);
      row("Ovos granada", 3);
      row("Ovos relógio", 4);
      row("Penas vermelhas", 6);
      row("Penas douradas", 7);
    }
    ImGui::End();
  }
};

// Fecha o menu, grava as opções e mantém o jogo sem entrada por mais 300 ms, para o B/Start
// que fechou o menu não chegar ao menu de pausa logo atrás.
void CloseMenu() {
  if (!g_menu_open) return;
  g_menu_open = false;
  g_unblock_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
  SaveAll();
  if (pc::Get().mouse_look) pc::input::SetCaptureEnabled(true);
}

// ---------------------------------------------------------------- menu
// Grava um quadro do teste sintético no rastro (chamado na thread da interface).
void TraceRecord(ImGuiIO& io) {
  if (!g_synthetic_pad.has_value() && !g_synthetic_was_set) return;
  ImGuiContext& g = *ImGui::GetCurrentContext();
  const int dpad_down = ImGui::IsKeyDown(ImGuiKey_GamepadDpadDown) ? 1 : 0;
  const int move_dir = g.NavMoveDir;
  std::lock_guard<std::mutex> lk(g_trace_mu);
  g_trace.push_back({pc::state::guest_swaps.load(), g_tab_current, io.NavActive ? 1 : 0,
                     ImGui::IsAnyItemActive() ? 1 : 0, g.NavId, g.ActiveId, g_pin_id,
                     dpad_down, move_dir, g_menu_open ? 1 : 0});
}

class PcSettingsDialog : public rex::ui::ImGuiDialog {
 public:
  explicit PcSettingsDialog(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    // Os pedidos das páginas de PC rodam no laço de mensagens (PostUi), não aqui: este método é
    // chamado DENTRO do desenho do quadro. A captura de tecla só lê o teclado.
    PollIngameCapture();
    // Menu antigo (sobreposto): só como reserva (Shift+F1, --open-pc-menu e sondas antigas).
    if (pc::state::menu_open_request.exchange(false) && !g_menu_open) {
      g_menu_open = true;
      pc::input::SetCaptureEnabled(false);
    }
    if (pc::state::menu_close_request.exchange(false)) g_menu_open = false;
    if (!g_menu_open) {
      g_pad_prev = 0;
      return;
    }
    const WORD pressed = FeedGamepad(io);
    if (pressed & 0x0100) g_tab_request = std::max(0, g_tab_current - 1);  // LB
    if (pressed & 0x0200) g_tab_request = std::min(3, g_tab_current + 1);  // RB
    const bool popup_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if (((pressed & 0x2000) && !popup_open && g_capture_action < 0) || (pressed & 0x0010)) {
      CloseMenu();  // B sem lista aberta, ou Start
      TraceRecord(io);
      return;
    }
    // legibilidade: escala proporcional à altura da tela (1080p = 1.2x, 4K = 2.4x)
    const float scale = std::max(1.0f, io.DisplaySize.y / 1080.0f * 1.2f);
    ImGui::SetNextWindowSize(ImVec2(620 * scale, 520 * scale), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    PollKeyCapture();
    if (!ImGui::Begin("Configurações de PC", &g_menu_open, ImGuiWindowFlags_NoCollapse)) {
      ImGui::End();
      return;
    }
    ImGui::SetWindowFontScale(scale);
    auto& s = pc::Get();
    if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
    if (ImGui::BeginTabBar("##pc_tabs")) {
      if (ImGui::BeginTabItem("Gráficos", nullptr, TabFlags(0))) {
        g_tab_current = 0;
        DrawGraphics(s);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Controles", nullptr, TabFlags(1))) {
        g_tab_current = 1;
        DrawControls();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Áudio", nullptr, TabFlags(2))) {
        g_tab_current = 2;
        DrawAudio();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Jogo", nullptr, TabFlags(3))) {
        g_tab_current = 3;
        DrawGame(s);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    g_tab_request = -1;
    ImGui::Separator();
    ImGui::TextDisabled("Controle: direcional/analógico navega, A escolhe, LB/RB trocam de aba, B fecha.");
    if (ImGui::Button("Salvar")) SaveAll();
    ImGui::SameLine();
    if (ImGui::Button("Fechar")) {
      CloseMenu();
      ImGui::End();
      return;
    }
    if (g_restart_needed) {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Algumas opções valem ao reiniciar o jogo.");
    }
    if (!g_status.empty()) ImGui::TextUnformatted(g_status.c_str());
    ImGui::End();
    TraceRecord(io);
    if (!g_menu_open) {
      // fechado pelo X da janela
      g_menu_open = true;
      CloseMenu();
    }
  }

  static ImGuiTabItemFlags TabFlags(int index) {
    return g_tab_request == index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
  }

 private:
  void DrawGraphics(Settings& s) {
    ImGui::TextUnformatted("Resolução (reinicie para aplicar)");
    int res = int(s.resolution);
    const char* res_names[] = {"1920 x 1080 (1080p)", "2560 x 1440 (1440p)", "3840 x 2160 (4K)"};
    if (ImGui::Combo("##res", &res, res_names, 3)) {
      s.resolution = Resolution(res);
      s.resolution_auto = false;
      ApplyResolutionCvars(s.resolution);
      g_restart_needed = true;
    }
    ImGui::TextUnformatted("Modo de exibição");
    int mode = int(s.display_mode);
    bool changed = ImGui::RadioButton("Janela", &mode, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Janela sem bordas", &mode, 1);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Tela cheia", &mode, 2);
    if (changed) {
      s.display_mode = DisplayMode(mode);
      const DisplayMode m = s.display_mode;
      PostUi([m] { ApplyDisplayModeNow(g_window, m); });  // fora do desenho do quadro
    }
    bool vsync = CvarNow("host_vsync") != "false";
    if (ImGui::Checkbox("VSync (imagem sincronizada com o monitor, sem tearing)", &vsync))
      SetCvarAsync("host_vsync", vsync ? "true" : "false");
    ImGui::TextUnformatted("Anti-serrilhado");
    std::string pe = CvarStr("swap_post_effect");
    int aa = pe == "fxaa" ? 1 : pe == "fxaa_extreme" ? 2 : 0;
    const char* aa_names[] = {"Nenhum", "FXAA", "FXAA (extremo)"};
    if (ImGui::Combo("##aa", &aa, aa_names, 3))
      SetCvar("swap_post_effect", aa == 1 ? "fxaa" : aa == 2 ? "fxaa_extreme" : "none");
    bool msaa = CvarBool("native_2x_msaa");
    if (ImGui::Checkbox("MSAA 2x nativo (reinicie)", &msaa)) {
      SetCvar("native_2x_msaa", msaa ? "true" : "false");
      g_restart_needed = true;
    }
    ImGui::TextUnformatted("Filtragem anisotrópica");
    // runtime: -1 = o jogo decide, 0 = desligada, 2 = 2x, 3 = 4x, 4 = 8x, 5 = 16x
    static const int kAnisoValues[] = {-1, 0, 2, 3, 4, 5};
    const char* an_names[] = {"Como no jogo", "Desligada", "2x", "4x", "8x", "16x"};
    const int an = CvarInt("anisotropic_override");
    int an_idx = 0;
    for (int i = 0; i < 6; ++i)
      if (kAnisoValues[i] == an) an_idx = i;
    if (ImGui::Combo("##aniso", &an_idx, an_names, 6))
      SetCvar("anisotropic_override", std::to_string(kAnisoValues[an_idx]));
    ImGui::TextUnformatted("Escalonamento da imagem");
    std::string pres = CvarStr("present_effect");
    int pidx = pres == "cas" ? 1 : pres == "fsr" ? 2 : 0;
    const char* pnames[] = {"Bilinear", "AMD CAS (nitidez)", "AMD FSR"};
    if (ImGui::Combo("##present", &pidx, pnames, 3))
      SetCvar("present_effect", pidx == 1 ? "cas" : pidx == 2 ? "fsr" : "bilinear");
  }

  void DrawControls() {
    auto& s = pc::Get();
    const float scale = std::max(1.0f, ImGui::GetIO().DisplaySize.y / 1080.0f * 1.2f);
    if (ImGui::BeginChild("##controls_scroll",
                          ImVec2(0, ImGui::GetContentRegionAvail().y - 20 * scale), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
      ImGui::TextUnformatted("Câmera");
      bool mouse_look = s.mouse_look;
      if (ImGui::Checkbox("Câmera pelo mouse (captura o cursor)", &mouse_look)) {
        s.mouse_look = mouse_look;
        pc::input::SetMouseLook(mouse_look);
        pc::input::SetCaptureEnabled(mouse_look && !g_menu_open);
        pc::Save();
      }
      if (s.mouse_look)
        ImGui::TextWrapped(
            "O cursor fica preso na janela. Abra este menu (F1) para liberar o mouse.");
      float sx = s.cam_sens_x, sy = s.cam_sens_y;
      if (ImGui::SliderFloat("Sensibilidade horizontal", &sx, 0.1f, 3.0f, "%.2f")) {
        s.cam_sens_x = sx;
        pc::Save();
      }
      if (ImGui::SliderFloat("Sensibilidade vertical", &sy, 0.1f, 3.0f, "%.2f")) {
        s.cam_sens_y = sy;
        pc::Save();
      }
      bool ix = s.cam_invert_x, iy = s.cam_invert_y;
      if (ImGui::Checkbox("Inverter eixo horizontal", &ix)) {
        s.cam_invert_x = ix;
        pc::Save();
      }
      ImGui::SameLine();
      if (ImGui::Checkbox("Inverter eixo vertical", &iy)) {
        s.cam_invert_y = iy;
        pc::Save();
      }
      float smooth = s.cam_smoothing;
      if (ImGui::SliderFloat("Suavização da câmera", &smooth, 0.0f, 0.9f, "%.2f")) {
        s.cam_smoothing = smooth;
        pc::Save();
      }

      ImGui::Separator();
      ImGui::TextUnformatted("Controle (gamepad)");
      float dz = s.pad_deadzone;
      if (ImGui::SliderFloat("Zona morta dos analógicos", &dz, 0.0f, 0.5f, "%.2f")) {
        s.pad_deadzone = dz;
        pc::ApplyRuntimeOptions();
        pc::Save();
      }
      float psx = s.pad_sens_x, psy = s.pad_sens_y;
      if (ImGui::SliderFloat("Sensibilidade do analógico direito X", &psx, 0.1f, 3.0f, "%.2f")) {
        s.pad_sens_x = psx;
        pc::ApplyRuntimeOptions();
        pc::Save();
      }
      if (ImGui::SliderFloat("Sensibilidade do analógico direito Y", &psy, 0.1f, 3.0f, "%.2f")) {
        s.pad_sens_y = psy;
        pc::ApplyRuntimeOptions();
        pc::Save();
      }
      bool pix = s.pad_invert_x, piy = s.pad_invert_y;
      if (ImGui::Checkbox("Inverter analógico X", &pix)) {
        s.pad_invert_x = pix;
        pc::ApplyRuntimeOptions();
        pc::Save();
      }
      ImGui::SameLine();
      if (ImGui::Checkbox("Inverter analógico Y", &piy)) {
        s.pad_invert_y = piy;
        pc::ApplyRuntimeOptions();
        pc::Save();
      }
      bool vib = s.pad_vibration;
      if (ImGui::Checkbox("Vibração", &vib)) {
        s.pad_vibration = vib;
        pc::ApplyRuntimeOptions();
        pc::Save();
      }

      ImGui::Separator();
      ImGui::TextUnformatted("Teclas");
      ImGui::TextWrapped("Clique em Alterar e pressione a nova tecla.");
      const int count = int(sizeof(kKeyActions) / sizeof(kKeyActions[0]));
      for (int i = 0; i < count; ++i) {
        const auto& a = kKeyActions[i];
        ImGui::PushID(i);
        ImGui::TextUnformatted(a.label);
        ImGui::SameLine(410 * scale);
        if (g_capture_action == i) {
          ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "Pressione...");
        } else {
          ImGui::TextDisabled("%s", CurrentKeyName(a).c_str());
        }
        ImGui::SameLine(560 * scale);
        if (ImGui::SmallButton("Alterar")) {
          g_capture_action = i;
          g_capture_delay = 10;
          g_capture_label = a.label;
        }
        ImGui::PopID();
      }
      if (g_capture_action >= 0) {
        ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "Aguardando tecla para: %s",
                           kKeyActions[g_capture_action].label);
      }
      ImGui::EndChild();
    }
  }

  void DrawAudio() {
    auto& s = pc::Get();
    int vol = s.volume_master;
    if (ImGui::SliderInt("Volume geral", &vol, 0, 100, "%d%%")) {
      s.volume_master = vol;
      pc::ApplyRuntimeOptions();
      pc::Save();
    }
    bool mute = CvarBool("audio_mute");
    if (ImGui::Checkbox("Silenciar todo o áudio", &mute)) SetCvar("audio_mute", mute ? "true" : "false");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "A música e os efeitos sonoros têm volumes separados no menu do próprio jogo "
        "(Ajuda e Opções > Volume da música / Volume dos efeitos sonoros). O volume geral "
        "acima vale para tudo e é independente dos saves.");
  }

  void DrawGame(Settings& s) {
    if (ImGui::BeginChild("##game_scroll",
                          ImVec2(0, ImGui::GetContentRegionAvail().y - 16 * ImGui::GetIO().DisplaySize.y / 1080.0f * 1.2f),
                          false, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
      if (g_focus_test) ImGui::SetKeyboardFocusHere();
      ImGui::Checkbox("60 FPS", &s.fps60);
      if (g_focus_test) g_pin_id = ImGui::GetItemID();
      if (s.fps60) {
        ImGui::SameLine();
        ImGui::Checkbox("Ritmo travado em 60 na tela", &s.lock60);
        ImGui::Checkbox("Cenas também a 60 (experimental: cenas ficam mais rápidas)", &s.scenes60);
      }
      ImGui::TextUnformatted("Distância de desenho");
      int dd = int(s.draw_distance);
      ImGui::RadioButton("Original", &dd, 0);
      ImGui::SameLine();
      ImGui::RadioButton("Longe", &dd, 1);
      ImGui::SameLine();
      ImGui::RadioButton("Total", &dd, 2);
      s.draw_distance = DrawDistance(dd);
      ImGui::TextUnformatted("Velocidade dos diálogos");
      int ds = s.dialog_speed;
      for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        ImGui::RadioButton(pc::DialogSpeedName(i), &ds, i);
      }
      if (ds != s.dialog_speed) {
        s.dialog_speed = ds;
        pc::Save();
      }
      ImGui::Checkbox("Mostrar contador de FPS", &s.show_fps);
      ImGui::Checkbox("Mostrar estatísticas de desempenho", &s.show_perf_stats);
      ImGui::Checkbox("Mostrar rastreador de itens", &s.show_collectibles);
      ImGui::Checkbox("Pausar ao minimizar ou perder o foco", &s.pause_on_focus_loss);
      if (ImGui::Checkbox("Pular introduções opcionais (experimental)", &s.skip_intro_logos))
        pc::Save();

      ImGui::Separator();
      ImGui::TextUnformatted("Saves");
      if (ImGui::Checkbox("Backups automáticos do save", &s.save_backup)) pc::Save();
      int keep = s.backup_count;
      if (ImGui::SliderInt("Backups mantidos", &keep, 1, 30)) {
        s.backup_count = keep;
        pc::Save();
      }
      if (ImGui::Button("Restaurar backup mais recente")) {
        g_status = pc::saves::RestoreLatest() ? "Backup restaurado. Reinicie o jogo para usar."
                                              : "Nenhum backup válido para restaurar.";
      }
      ImGui::TextWrapped("%s", pc::saves::Status().c_str());
      ImGui::TextWrapped(
          "Os backups ficam em Documentos\\banjotooie\\backups, separados dos saves e das "
          "configurações. O save atual é copiado antes de qualquer restauração.");
      ImGui::EndChild();
    }
  }
};

std::unique_ptr<PcSettingsDialog> g_dialog;
std::unique_ptr<FpsOverlay> g_fps;
std::unique_ptr<CollectibleOverlay> g_collectibles;

}  // namespace

void Create(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
            const std::filesystem::path& runtime_config) {
  g_runtime_config = runtime_config;
  CaptureSessionCvars();
  g_window = window;
  g_startup_resolution = pc::Get().resolution;
  g_startup_msaa = CvarBool("native_2x_msaa");
  for (const char* c : {"swap_post_effect", "native_2x_msaa", "present_effect",
                        "anisotropic_override", "host_vsync", "audio_mute"})
    g_startup_cvars[c] = CvarStr(c);
  // 'vsync' do runtime = ritmo de 60 Hz do console; desligado, o "retraço" vira 1 ms e o jogo
  // perde o ritmo. O menu antigo o expunha como VSync; uma escolha antiga é desfeita aqui.
  if (!CvarBool("vsync")) {
    SetCvar("vsync", "true");
    REXLOG_WARN("PC: 'vsync' (ritmo de 60 Hz do console) estava desligado; religado. O VSync do "
                "menu agora é 'host_vsync' (imagem no retraço do monitor).");
  }
  g_fps = std::make_unique<FpsOverlay>(drawer);
  g_collectibles = std::make_unique<CollectibleOverlay>(drawer);
  g_dialog = std::make_unique<PcSettingsDialog>(drawer);
  // F1 abre as páginas "CONFIGURAÇÕES DE PC" dentro do menu do próprio jogo (pedido de
  // 2026-11-12). O menu sobreposto antigo ficou só como reserva, em Shift+F1.
  rex::ui::RegisterBind("bind_pc_settings", "F1", "Configurações de PC", [] {
    if (g_menu_open) {
      CloseMenu();
    } else if (GetKeyState(VK_SHIFT) & 0x8000) {
      g_menu_open = true;
      pc::input::SetCaptureEnabled(false);
    } else {
      pc::ingame::RequestToggle();
    }
  });
}

void Destroy() {
  rex::ui::UnregisterBind("bind_pc_settings");
  g_dialog.reset();
  g_fps.reset();
  g_collectibles.reset();
}

void Open() { g_menu_open = true; }

void SetInputSystem(rex::input::InputSystem* system) { g_input_system = system; }

bool BlocksGameInput() {
  return g_menu_open || g_ig_capture.load() >= 0 ||
         std::chrono::steady_clock::now() < g_unblock_at;
}
bool IsOpen() { return g_menu_open; }

// ---------------------------------------------------------------- diagnóstico de gamepad
std::array<XInputDiag, 4> QueryXInput() {
  std::array<XInputDiag, 4> out{};
  auto get = LoadXInput();
  for (int i = 0; i < 4; ++i) {
    if (!get) {
      out[i].loaded = false;
      out[i].connected = -1;
      continue;
    }
    out[i].loaded = true;
    XPad xp{};
    const DWORD e = get(i, &xp);
    out[i].error = e;
    out[i].connected = (e == ERROR_SUCCESS) ? 1 : 0;
    out[i].buttons = xp.buttons;
  }
  return out;
}

void SetSyntheticGamepad(const GamepadState* state) {
  std::lock_guard<std::mutex> lk(g_trace_mu);
  if (state) {
    g_synthetic_pad = *state;
    g_trace.clear();  // limpa só ao INICIAR um novo trecho (não ao soltar)
  } else {
    g_synthetic_pad.reset();
  }
}

std::vector<GamepadTraceEntry> TakeGamepadTrace() {
  std::lock_guard<std::mutex> lk(g_trace_mu);
  std::vector<GamepadTraceEntry> t = g_trace;
  g_trace.clear();
  return t;
}

int CurrentTab() { return g_tab_current; }

void SetGamepadTestFocus(bool set) { g_focus_test = set; }

void ApplyDisplayMode(rex::ui::Window* window) {
  FitInitialWindowToResolution(window, pc::Get().resolution);
  ApplyDisplayModeNow(window, pc::Get().display_mode);
}

bool ApplyDisplayModeForAutomation(rex::ui::Window* window, DisplayMode mode,
                                   bool persist_settings) {
  if (!window) return false;
  pc::Get().display_mode = mode;
  ApplyDisplayModeNow(window, mode);
  return !persist_settings || pc::Save();
}

bool SetResolutionForAutomation(Resolution resolution) {
  pc::Get().resolution = resolution;
  pc::Get().resolution_auto = false;
  ApplyResolutionCvars(resolution);
  const bool saved = pc::Save();
  SaveRuntimeConfig();
  return saved;
}

// ---------------------------------------------------------------- opções dentro do jogo
namespace options {
namespace {

// Rótulo nos dois idiomas; o texto mostrado segue o idioma do jogo (pc_lang.h): a versão global
// é em inglês (pedido de 2026-11-13).
struct L {
  const char* pt;
  const char* en;
};
std::string Tx(const L& l) { return pc::lang::T(l.pt, l.en); }
bool En() { return pc::lang::English(); }

std::string YesNo(bool b) { return b ? Tx({"SIM", "YES"}) : Tx({"NÃO", "NO"}); }

// Número com 2 casas: vírgula em PT-BR, ponto em inglês.
std::string Num2(float v) {
  char b[24];
  std::snprintf(b, sizeof b, "%.2f", double(v));
  if (!En())
    for (char* p = b; *p; ++p)
      if (*p == '.') *p = ',';
  return b;
}

std::string Upper(std::string s) {
  for (char& c : s)
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
  return s;
}

int Wrap(int v, int n, int dir) { return ((v + (dir < 0 ? -1 : 1)) % n + n) % n; }

// Passo fixo com limites; arredonda ao passo para não acumular erro de ponto flutuante.
float StepF(float v, float lo, float hi, float step, int dir) {
  const float n = std::round(v / step) + (dir < 0 ? -1.0f : 1.0f);
  return std::clamp(n * step, lo, hi);
}

std::string RestartHint() { return Tx({" (REINICIE)", " (RESTART)"}); }

// Linha da página: texto atual e o que esquerda/direita (dir ±1) ou A (dir 0) fazem.
struct Row {
  std::function<std::string()> text;
  std::function<bool(int)> input;  // devolve true se mudou
  bool value = true;
};

Row Bool(L label, bool Settings::*field, std::function<void()> after = {}) {
  return {[label, field] { return Tx(label) + " : " + YesNo(pc::Get().*field); },
          [field, after](int) {
            pc::Get().*field = !(pc::Get().*field);
            if (after) after();
            RequestSave();
            return true;
          }};
}

Row Float(L label, float Settings::*field, float lo, float hi, float step,
          std::function<void()> after = {}) {
  return {[label, field] { return Tx(label) + " : " + Num2(pc::Get().*field); },
          [field, lo, hi, step, after](int dir) {
            const float old = pc::Get().*field;
            const float v = StepF(old, lo, hi, step, dir == 0 ? 1 : dir);
            if (v == old) return false;
            pc::Get().*field = v;
            if (after) after();
            RequestSave();
            return true;
          }};
}

// Cvar com uma lista fixa de valores (o texto mostrado em names[i]). Só entram os valores que
// o runtime aceita (lista .allowed() do cvar): este build não tem o AMD FidelityFX, então
// present_effect só aceita "bilinear" e os pedidos "cas"/"fsr" eram recusados ("not in allowed
// values", 45 avisos nos logs de 2026-11-12) sem mudar nada. "(REINICIE)" aparece quando o
// runtime só aplica o cvar ao reiniciar (lifecycle) e o valor difere do da partida.
Row CvarList(L label, const char* cvar, std::vector<std::string> values, std::vector<L> names,
             bool restart_hint = false) {
  if (const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(cvar)) {
    if (info->constraints.HasAllowedValues()) {
      std::vector<std::string> v2;
      std::vector<L> n2;
      for (size_t i = 0; i < values.size(); ++i) {
        const auto& ok = info->constraints.allowed_values;
        if (std::find(ok.begin(), ok.end(), values[i]) != ok.end()) {
          v2.push_back(values[i]);
          n2.push_back(names[i]);
        }
      }
      if (!v2.empty()) {
        values = std::move(v2);
        names = std::move(n2);
      }
    }
    restart_hint = restart_hint || info->lifecycle == rex::cvar::Lifecycle::kRequiresRestart;
  }
  auto index = [cvar, values] {
    const std::string cur = CvarNow(cvar);
    for (size_t i = 0; i < values.size(); ++i)
      if (values[i] == cur) return int(i);
    return 0;
  };
  Row row{[label, names, index, restart_hint, cvar] {
            std::string t = Tx(label) + " : " + Tx(names[size_t(index())]);
            const auto start = g_startup_cvars.find(cvar);
            if (restart_hint && start != g_startup_cvars.end() && CvarNow(cvar) != start->second)
              t += RestartHint();
            return t;
          },
          [cvar, values, index](int dir) {
            if (values.size() < 2) return false;
            const int i = Wrap(index(), int(values.size()), dir);
            SetCvarAsync(cvar, values[size_t(i)]);
            RequestSave();
            return true;
          }};
  row.value = values.size() > 1;
  return row;
}

void ApplyRuntimeLater() { PostUi([] { pc::ApplyRuntimeOptions(); }); }

const L kYes{"SIM", "YES"};
const L kNo{"NÃO", "NO"};

std::vector<Row> BuildGraphics() {
  std::vector<Row> r;
  r.push_back({[] {
                 const auto res = pc::Get().resolution;
                 const std::string v = res == Resolution::R1080p   ? "1080P"
                                       : res == Resolution::R1440p ? "1440P"
                                                                   : "4K";
                 // Pendente até reiniciar: rótulo curto para caber numa linha no título
                 // (texto maior; "INVERTER ANALÓGICO X : NÃO", 26 letras largas, já quebra).
                 if (res != g_startup_resolution)
                   return Tx({"RESOL.", "RES."}) + " : " + v + RestartHint();
                 return Tx({"RESOLUÇÃO", "RESOLUTION"}) + " : " + v;
               },
               [](int dir) {
                 auto& s = pc::Get();
                 s.resolution = Resolution(Wrap(int(s.resolution), 3, dir));
                 s.resolution_auto = false;
                 const Resolution res = s.resolution;
                 PostUi([res] { ApplyResolutionCvars(res); });
                 g_restart_needed = true;
                 RequestSave();
                 return true;
               }});
  r.push_back({[] {
                 const auto m = pc::Get().display_mode;
                 return Tx({"MODO", "MODE"}) + " : " +
                        (m == DisplayMode::Janela      ? Tx({"JANELA", "WINDOWED"})
                         : m == DisplayMode::SemBordas ? Tx({"SEM BORDAS", "BORDERLESS"})
                                                       : Tx({"TELA CHEIA", "FULLSCREEN"}));
               },
               [](int dir) {
                 auto& s = pc::Get();
                 s.display_mode = DisplayMode(Wrap(int(s.display_mode), 3, dir));
                 const DisplayMode m = s.display_mode;
                 PostUi([m] { ApplyDisplayModeNow(g_window, m); });
                 RequestSave();
                 return true;
               }});
  // VSYNC = imagem entregue no retraço do monitor (host_vsync, sem tearing). O cvar 'vsync' do
  // runtime é o ritmo de 60 Hz do console: desligado, o jogo receberia um "retraço" a cada 1 ms
  // (graphics_system.cpp) — fica sempre ligado (pc_menu::Create).
  r.push_back(CvarList({"VSYNC", "VSYNC"}, "host_vsync", {"true", "false"}, {kYes, kNo}));
  r.push_back(CvarList({"FXAA", "FXAA"}, "swap_post_effect", {"none", "fxaa", "fxaa_extreme"},
                       {kNo, kYes, {"FORTE", "STRONG"}}));
  r.push_back(CvarList({"MSAA 2X", "MSAA 2X"}, "native_2x_msaa", {"false", "true"}, {kNo, kYes},
                       true));
  r.push_back(CvarList({"ANISOTROPIA", "ANISOTROPY"}, "anisotropic_override",
                       {"-1", "0", "2", "3", "4", "5"},
                       {{"DO JOGO", "GAME"}, {"DESLIGADA", "OFF"}, {"2X", "2X"}, {"4X", "4X"},
                        {"8X", "8X"}, {"16X", "16X"}}));
  r.push_back(CvarList({"ESCALA", "SCALING"}, "present_effect", {"bilinear", "cas", "fsr"},
                       {{"BILINEAR", "BILINEAR"}, {"AMD CAS", "AMD CAS"}, {"AMD FSR", "AMD FSR"}}));
  return r;
}

std::vector<Row> BuildGame() {
  std::vector<Row> r;
  r.push_back(Bool({"60 FPS", "60 FPS"}, &Settings::fps60));
  r.push_back(Bool({"RITMO FIXO EM 60", "LOCKED 60 PACING"}, &Settings::lock60));
  r.push_back(Bool({"CENAS A 60 FPS", "CUTSCENES AT 60 FPS"}, &Settings::scenes60));
  r.push_back({[] {
                 const auto d = pc::Get().draw_distance;
                 return Tx({"DISTÂNCIA", "DRAW DISTANCE"}) + " : " +
                        (d == DrawDistance::Longe   ? Tx({"LONGE", "FAR"})
                         : d == DrawDistance::Total ? Tx({"TOTAL", "FULL"})
                                                    : Tx({"ORIGINAL", "ORIGINAL"}));
               },
               [](int dir) {
                 auto& s = pc::Get();
                 s.draw_distance = DrawDistance(Wrap(int(s.draw_distance), 3, dir));
                 RequestSave();
                 return true;
               }});
  r.push_back({[] {
                 static const L kNames[] = {{"ORIGINAL", "ORIGINAL"},
                                            {"RÁPIDOS", "FAST"},
                                            {"MUITO RÁPIDOS", "VERY FAST"},
                                            {"INSTANTÂNEOS", "INSTANT"}};
                 return Tx({"DIÁLOGOS", "DIALOGUE"}) + " : " +
                        Tx(kNames[std::clamp(pc::Get().dialog_speed, 0, 3)]);
               },
               [](int dir) {
                 auto& s = pc::Get();
                 s.dialog_speed = Wrap(std::clamp(s.dialog_speed, 0, 3), 4, dir);
                 RequestSave();
                 return true;
               }});
  r.push_back(Bool({"CONTADOR DE FPS", "FPS COUNTER"}, &Settings::show_fps));
  r.push_back(Bool({"DESEMPENHO NA TELA", "PERFORMANCE STATS"}, &Settings::show_perf_stats));
  r.push_back(Bool({"RASTREADOR DE ITENS", "ITEM TRACKER"}, &Settings::show_collectibles));
  r.push_back(Bool({"PAUSAR SEM FOCO", "PAUSE ON FOCUS LOSS"}, &Settings::pause_on_focus_loss));
  r.push_back(Bool({"INÍCIO RÁPIDO", "QUICK START"}, &Settings::skip_intro_logos));
  return r;
}

std::vector<Row> BuildController() {
  std::vector<Row> r;
  r.push_back(Bool({"VIBRAÇÃO", "VIBRATION"}, &Settings::pad_vibration, ApplyRuntimeLater));
  r.push_back(Float({"ZONA MORTA", "DEAD ZONE"}, &Settings::pad_deadzone, 0.0f, 0.5f, 0.01f,
                    ApplyRuntimeLater));
  r.push_back(Float({"ANALÓGICO DIREITO X", "RIGHT STICK X"}, &Settings::pad_sens_x, 0.1f, 3.0f,
                    0.05f, ApplyRuntimeLater));
  r.push_back(Float({"ANALÓGICO DIREITO Y", "RIGHT STICK Y"}, &Settings::pad_sens_y, 0.1f, 3.0f,
                    0.05f, ApplyRuntimeLater));
  r.push_back(Bool({"INVERTER ANALÓG. X", "INVERT STICK X"}, &Settings::pad_invert_x,
                   ApplyRuntimeLater));
  r.push_back(Bool({"INVERTER ANALÓG. Y", "INVERT STICK Y"}, &Settings::pad_invert_y,
                   ApplyRuntimeLater));
  return r;
}

// Rótulos curtos dos 17 atalhos (mesma ordem de kKeyActions).
const L kKeyLabels[] = {
    {"START / PAUSA", "START / PAUSE"}, {"BOTÃO A", "A BUTTON"},
    {"BOTÃO B", "B BUTTON"},            {"BOTÃO X", "X BUTTON"},
    {"BOTÃO Y", "Y BUTTON"},            {"DIRECIONAL CIMA", "D-PAD UP"},
    {"DIRECIONAL BAIXO", "D-PAD DOWN"}, {"DIRECIONAL ESQ.", "D-PAD LEFT"},
    {"DIRECIONAL DIR.", "D-PAD RIGHT"}, {"MOVER CIMA", "MOVE UP"},
    {"MOVER BAIXO", "MOVE DOWN"},       {"MOVER ESQ.", "MOVE LEFT"},
    {"MOVER DIR.", "MOVE RIGHT"},       {"CÂMERA CIMA", "CAMERA UP"},
    {"CÂMERA BAIXO", "CAMERA DOWN"},    {"CÂMERA ESQ.", "CAMERA LEFT"},
    {"CÂMERA DIR.", "CAMERA RIGHT"}};

// Nome de tecla para a tela (maiúsculas; os mais comuns traduzidos em PT-BR).
std::string KeyDisplayName(const std::string& raw) {
  const std::string up = Upper(raw);
  if (up == "RETURN" || up == "ENTER") return "ENTER";
  if (up == "SPACE") return Tx({"ESPAÇO", "SPACE"});
  if (up == "ESCAPE") return "ESC";
  if (up.empty()) return Tx({"NENHUMA", "NONE"});
  return up;
}
static_assert(sizeof(kKeyLabels) / sizeof(kKeyLabels[0]) ==
              sizeof(kKeyActions) / sizeof(kKeyActions[0]));

std::vector<Row> BuildKeyboardMouse() {
  std::vector<Row> r;
  r.push_back(Bool({"CÂMERA PELO MOUSE", "MOUSE CAMERA"}, &Settings::mouse_look, [] {
    const bool on = pc::Get().mouse_look;
    PostUi([on] {
      pc::input::SetMouseLook(on);
      pc::input::SetCaptureEnabled(on && !g_menu_open);
    });
  }));
  r.push_back(Float({"MOUSE HORIZONTAL", "MOUSE HORIZONTAL"}, &Settings::cam_sens_x, 0.1f, 3.0f,
                    0.05f));
  r.push_back(Float({"MOUSE VERTICAL", "MOUSE VERTICAL"}, &Settings::cam_sens_y, 0.1f, 3.0f,
                    0.05f));
  r.push_back(Bool({"INVERTER MOUSE X", "INVERT MOUSE X"}, &Settings::cam_invert_x));
  r.push_back(Bool({"INVERTER MOUSE Y", "INVERT MOUSE Y"}, &Settings::cam_invert_y));
  r.push_back(Float({"SUAVIZAÇÃO", "SMOOTHING"}, &Settings::cam_smoothing, 0.0f, 0.9f, 0.05f));
  const int keys = int(sizeof(kKeyActions) / sizeof(kKeyActions[0]));
  for (int i = 0; i < keys; ++i) {
    Row k;
    k.value = false;
    k.text = [i] {
      if (g_ig_capture.load() == i)
        return Tx(kKeyLabels[i]) + " : " + Tx({"APERTE A TECLA", "PRESS A KEY"});
      return Tx(kKeyLabels[i]) + " : " + KeyDisplayName(CurrentKeyName(kKeyActions[i]));
    };
    k.input = [i](int dir) {
      if (dir != 0 || g_ig_capture.load() >= 0) return false;
      StartIngameCapture(i);
      return true;
    };
    r.push_back(std::move(k));
  }
  return r;
}

std::vector<Row> BuildAudioSaves() {
  std::vector<Row> r;
  r.push_back({[] {
                 return Tx({"VOLUME GERAL", "MASTER VOLUME"}) + " : " +
                        std::to_string(pc::Get().volume_master);
               },
               [](int dir) {
                 auto& s = pc::Get();
                 const int v = std::clamp(s.volume_master + (dir < 0 ? -5 : 5), 0, 100);
                 if (v == s.volume_master) return false;
                 s.volume_master = v;
                 ApplyRuntimeLater();
                 RequestSave();
                 return true;
               }});
  r.push_back(CvarList({"SILENCIAR TUDO", "MUTE ALL"}, "audio_mute", {"false", "true"},
                       {kNo, kYes}));
  r.push_back(Bool({"BACKUPS DO SAVE", "SAVE BACKUPS"}, &Settings::save_backup));
  r.push_back({[] {
                 return Tx({"BACKUPS MANTIDOS", "BACKUPS KEPT"}) + " : " +
                        std::to_string(pc::Get().backup_count);
               },
               [](int dir) {
                 auto& s = pc::Get();
                 const int v = std::clamp(s.backup_count + (dir < 0 ? -1 : 1), 1, 30);
                 if (v == s.backup_count) return false;
                 s.backup_count = v;
                 RequestSave();
                 return true;
               }});
  Row restore;
  restore.value = false;
  restore.text = [] {
    switch (g_restore_state.load()) {
      case 1: return Tx({"CONFIRMA? APERTE A", "SURE? PRESS A AGAIN"});
      case 2: return Tx({"RESTAURADO - REINICIE", "RESTORED - RESTART"});
      case 3: return Tx({"NENHUM BACKUP VÁLIDO", "NO VALID BACKUP"});
      default: return Tx({"RESTAURAR ÚLTIMO BACKUP", "RESTORE LATEST BACKUP"});
    }
  };
  restore.input = [](int dir) {
    if (dir != 0) return false;
    if (g_restore_state.load() != 1) {
      g_restore_state.store(1);
      return true;
    }
    PostUi([] {
      g_restore_state.store(pc::saves::RestoreLatest() ? 2 : 3);
      g_options_generation.fetch_add(1);
    });
    return false;
  };
  r.push_back(std::move(restore));
  return r;
}

struct Section {
  L title;
  std::vector<Row> rows;
};

std::vector<Section>& Sections() {
  static std::vector<Section> s = {{{"GRÁFICOS", "GRAPHICS"}, BuildGraphics()},
                                   {{"JOGO", "GAME"}, BuildGame()},
                                   {{"CONTROLE", "CONTROLLER"}, BuildController()},
                                   {{"TECLADO E MOUSE", "KEYBOARD & MOUSE"}, BuildKeyboardMouse()},
                                   {{"ÁUDIO E SAVES", "AUDIO & SAVES"}, BuildAudioSaves()}};
  return s;
}

const Row* RowAt(int section, int row) {
  auto& s = Sections();
  if (section < 0 || section >= int(s.size())) return nullptr;
  if (row < 0 || row >= int(s[size_t(section)].rows.size())) return nullptr;
  return &s[size_t(section)].rows[size_t(row)];
}

}  // namespace

int SectionCount() { return int(Sections().size()); }

std::string SectionTitle(int section) {
  auto& s = Sections();
  return section >= 0 && section < int(s.size()) ? Tx(s[size_t(section)].title) : "";
}

int RowCount(int section) {
  auto& s = Sections();
  return section >= 0 && section < int(s.size()) ? int(s[size_t(section)].rows.size()) : 0;
}

std::string RowText(int section, int row) {
  const Row* r = RowAt(section, row);
  return r ? r->text() : std::string();
}

bool RowIsValue(int section, int row) {
  const Row* r = RowAt(section, row);
  return r && r->value;
}

bool RowInput(int section, int row, int dir) {
  const Row* r = RowAt(section, row);
  return r && r->input(dir);
}

void RowLeft() { g_restore_state.store(0); }

uint32_t Generation() { return g_options_generation.load(); }

bool CaptureActive() { return g_ig_capture.load() >= 0; }

}  // namespace options

}  // namespace pc::menu
