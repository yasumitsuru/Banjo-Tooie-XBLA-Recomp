// Banjo-Tooie XBLA PT-BR Recompiled - ganchos do host (MISSAO 01).
//
// So diagnostico e testes automatizados; o jogo recompilado roda sem alteracoes.
//
// Opcoes de linha de comando (alem das do runtime, como --game_data_root):
//   --preflight_only            confere a pasta do jogo, grava o relatorio e sai (0 ou 2/3)
//   --boot_report <arq.json>    onde gravar o relatorio (padrao: <exe>\logs\banjotooie_boot.json)
//   --screenshot <arq.png>      captura a imagem do jogo e sai (0, ou 5 se falhar)
//   --screenshot_frames <n>     quadros do jogo antes da captura (padrao 120)
//   --screenshot_seconds <s>    segundos minimos desde o inicio do jogo antes da captura
//   --smoke_seconds <s>         roda s segundos e sai (0 se houve quadros, 4 se nao)
//   --smoke_timeout_seconds <s> limite para os modos automaticos (padrao 240)
//   --background_window         abre a janela atras das outras e devolve o foco
//   --port_silent               sem caixas de mensagem (testes)
//   --port_test_crash           SO TESTE: forca uma falha para provar o minidump/relatorio
//
// MISSAO 02 (integridade da traducao PT-BR) - so testes, todas desligadas por padrao:
//   --port_test_input <roteiro> controle virtual que aperta botoes em tempos fixos (ver
//                               port_test_input.h), para os testes chegarem aos menus sozinhos
//   --port_test_shots <lista>   capturas extras sem sair: "segundos=arquivo.png;..."
//   --port_dump_db <arq>        na captura final, grava o db360 como o jogo o tem na memoria
//   --port_dump_ranges <lista>  na captura final, grava faixas da memoria do jogo:
//                               "0xENDERECO:0xTAMANHO=arquivo;..." (so leitura)

#include "generated/default/banjotooie_init.h"

#include "banjotooie_app.h"
#include "port_diagnostics.h"
#include "port_test_input.h"

// Camada de PC (ver banjotooie_app.h)
#include "pc/pc_achievements.h"
#include "pc/pc_display.h"
#include "pc/pc_log.h"
#include "pc/pc_automation.h"
#include "pc/pc_crash.h"
#include "pc/pc_menu.h"
#include "pc/pc_saves.h"
#include "pc/pc_settings.h"
#include "pc/pc_shaders.h"
#include "pc/pc_state.h"
#include "pc/pc_userdir.h"
#include "pc/scripted_input.h"
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/graphics/graphics_system.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window_win.h>
#include <rex/version.h>

#include <fmt/format.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>

REXCVAR_DEFINE_BOOL(preflight_only, false, "Port",
                    "Confere a pasta do jogo, grava o relatorio de boot e sai");
REXCVAR_DEFINE_STRING(boot_report, "", "Port",
                      "Arquivo JSON do relatorio de boot (padrao: logs\\banjotooie_boot.json)");
REXCVAR_DEFINE_STRING(screenshot, "", "Port", "Captura a imagem do jogo neste PNG e sai");
REXCVAR_DEFINE_INT32(screenshot_frames, 120, "Port", "Quadros do jogo antes da captura");
REXCVAR_DEFINE_DOUBLE(screenshot_seconds, 0.0, "Port",
                      "Segundos minimos desde o inicio do jogo antes da captura");
REXCVAR_DEFINE_DOUBLE(smoke_seconds, 0.0, "Port", "Roda N segundos e sai (teste de fumaca)");
REXCVAR_DEFINE_DOUBLE(smoke_timeout_seconds, 240.0, "Port",
                      "Limite de tempo dos modos automaticos");
REXCVAR_DEFINE_BOOL(background_window, false, "Port",
                    "Abre a janela atras das outras sem roubar o foco");
REXCVAR_DEFINE_BOOL(port_silent, false, "Port", "Nao mostra caixas de mensagem");
REXCVAR_DEFINE_BOOL(port_test_crash, false, "Port",
                    "SO TESTE: provoca a excecao 0xE0425442 numa thread 1 s apos iniciar o jogo, "
                    "para provar que o relatorio de falha e o minidump funcionam");

REXCVAR_DEFINE_STRING(port_dump_db, "", "Port",
                      "DIAGNOSTICO (Missao 02): no momento da captura, grava neste arquivo o buffer "
                      "do db360.cmp como o jogo o tem na memoria (so leitura da memoria do jogo)");
REXCVAR_DEFINE_STRING(port_dump_ranges, "", "Port",
                      "DIAGNOSTICO (Missao 02): na captura final, grava faixas da memoria do jogo "
                      "('0xENDERECO:0xTAMANHO=arquivo;...'), so leitura");
REXCVAR_DEFINE_STRING(port_test_input, "", "Port",
                      "SO TESTE (Missao 02): controle virtual com roteiro 'botao@segundos[:ms],...' "
                      "contado do inicio do jogo; substitui os controles reais");
REXCVAR_DEFINE_STRING(port_test_shots, "", "Port",
                      "SO TESTE (Missao 02): capturas extras sem sair, 'segundos=arquivo.png;...'");

namespace {
constexpr DWORD kTestCrashCode = 0xE0425442;  // codigo de cliente (bit 29), 'BTB'
constexpr int kExitBadTestOption = 6;         // opcao de teste invalida (antes da janela)
// Globais do jogo (MISSAO 02, Ghidra + codigo gerado): 0x82220790 grava o ponteiro do buffer
// lido de db360.cmp em 0x8269EE80 e (buffer + 8) em 0x826CC034, usado como tabela de assets.
constexpr uint32_t kGuestDbRawPtr = 0x8269EE80;
constexpr uint32_t kGuestDbTablePtr = 0x826CC034;
constexpr uint32_t kGuestDbDataBasePtr = 0x826746E0;
}

namespace fs = std::filesystem;

namespace {

std::string Utf8CommandLine() {
  const wchar_t* w = GetCommandLineW();
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

port::PreflightResult g_preflight;

// "a=b;c=d" -> pares (a, b). Itens vazios sao ignorados.
std::vector<std::pair<std::string, std::string>> SplitPairs(const std::string& s) {
  std::vector<std::pair<std::string, std::string>> out;
  size_t start = 0;
  while (start < s.size()) {
    size_t semi = s.find(';', start);
    if (semi == std::string::npos) semi = s.size();
    const std::string item = s.substr(start, semi - start);
    start = semi + 1;
    const size_t eq = item.find('=');
    if (item.empty()) continue;
    out.emplace_back(item.substr(0, eq), eq == std::string::npos ? "" : item.substr(eq + 1));
  }
  return out;
}

[[noreturn]] void ExitBadTestOption(const std::string& what) {
  auto& rep = port::BootReport::Get();
  rep.Stage("test_option_invalid", what);
  rep.SetExit(kExitBadTestOption, "opcao de teste invalida: " + what);
  std::fprintf(stderr, "[banjotooie] opcao de teste invalida: %s\n", what.c_str());
  std::fflush(stderr);
  std::exit(kExitBadTestOption);
}

}  // namespace

BanjotooieApp::~BanjotooieApp() { StopMonitor(); }

void BanjotooieApp::OnConfigurePaths(rex::PathConfig& paths) {
  auto& rep = port::BootReport::Get();
  // M04 (C-21..C-29, C-87): DPI por monitor ANTES de criar a janela. Sem isso, num monitor com
  // escala do Windows (ex.: 4K a 150%, o desta pessoa) o runtime lia o tamanho LÓGICO da janela
  // (GetClientRect) e apresentava numa superfície 1,5x menor (4K saía em 2560x1440 esticada pelo
  // Windows; janela 1080p em 1280x720). Agora a superfície tem os pixels físicos da janela.
  {
    using SetCtxFn = BOOL(WINAPI*)(HANDLE);
    if (auto fn = reinterpret_cast<SetCtxFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")))
      fn(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));  // PER_MONITOR_AWARE_V2
    dpi_aware_ = IsProcessDPIAware() != FALSE;
    rep.SetInfo("dpi", dpi_aware_ ? "por monitor (pixels fisicos)" : "sem reconhecimento de DPI");
  }
  const fs::path exe_dir = rex::filesystem::GetExecutableFolder();
  const std::string report_cvar = REXCVAR_GET(boot_report);
  rep.SetPath(report_cvar.empty() ? exe_dir / "logs" / "banjotooie_boot.json"
                                  : fs::path(report_cvar));
  port::InstallCrashHandler(rep.path().parent_path() / "crashdumps");

  // --self-test e as sondas da camada de PC tambem sao automaticas (janela atras, sem caixas).
  automated_ = REXCVAR_GET(preflight_only) || !REXCVAR_GET(screenshot).empty() ||
               REXCVAR_GET(smoke_seconds) > 0.0 || REXCVAR_GET(port_test_crash) ||
               !REXCVAR_GET(port_test_input).empty() || pc::automation::ParseCommandLine().active();
  const bool silent = REXCVAR_GET(port_silent) || automated_;

  // Pedido de 2026-11-13: saves e opções de PC de cada conta do Windows em
  // Documentos\My Games\Banjo-Tooie XBLA (pc_userdir.h); na primeira vez o save antigo é copiado
  // para lá. --user_data_root continua valendo (testes, pastas isoladas). Execuções automáticas
  // sem ele (auto-teste, capturas, sondas) usam uma pasta de teste do app e nunca o save da
  // pessoa (achado do check 15). O cache de shaders não é dado pessoal e fica no app (fora do
  // OneDrive), como no lançador do Craft.
  std::string user_data_source = "--user_data_root";
  if (rex::cvar::GetFlagByName("user_data_root").empty()) {
    const fs::path out_dir = exe_dir.parent_path();  // app\out (o exe fica em app\out\native)
    const fs::path mine = pc::userdir::DefaultRoot();
    if (automated_ && !REXCVAR_GET(preflight_only)) {
      paths.user_data_root = out_dir / "craft-check" / "user-direct";
      user_data_source = "pasta de teste (automatico sem --user_data_root)";
    } else if (!mine.empty()) {
      paths.user_data_root = mine;
      user_data_source = "Documentos\\My Games (conta atual)";
      if (!REXCVAR_GET(preflight_only)) {  // a verificação de arquivos não grava nada
        std::error_code ec;
        fs::create_directories(mine, ec);
        const pc::userdir::Migration m = pc::userdir::MigrateLegacy(exe_dir, mine);
        if (m.copied) {
          user_data_source += fmt::format("; {} arquivos copiados de {}", m.files, m.from.string());
        } else if (!m.error.empty()) {
          user_data_source += "; copia do save antigo falhou: " + m.error;
        }
        std::fprintf(stderr, "[banjotooie] dados do usuario: %s\n", user_data_source.c_str());
      }
    }
    if (rex::cvar::GetFlagByName("cache_path").empty())
      paths.cache_root = out_dir / "craft-runtime" / "cache";
  }
  rep.SetInfo("user_data_source", user_data_source);
  user_data_source_ = user_data_source;
  game_data_root_ = paths.game_data_root;

  rep.SetInfo("command_line", Utf8CommandLine());
  rep.SetInfo("exe_dir", exe_dir.string());
  rep.SetInfo("game_data_root", paths.game_data_root.string());
  rep.SetInfo("user_data_root", paths.user_data_root.string());
  rep.SetInfo("cache_root", paths.cache_root.string());
  rep.SetInfo("config_path", paths.config_path.string());
  rep.SetInfo("mode", automated_ ? "automatico" : "normal");
  rep.Stage("environment");

  g_preflight = port::RunPreflight(paths.game_data_root);
  rep.SetPreflight(g_preflight);
  const std::string summary = g_preflight.SummaryPtBr();
  std::fprintf(stderr, "[banjotooie] preflight: %s\n", summary.c_str());

  int fail_code = 0;
  if (paths.game_data_root.empty() || !g_preflight.root_ok) {
    fail_code = port::kExitNoGameDataRoot;
  } else if (!g_preflight.passed()) {
    fail_code = port::kExitMissingRequiredFiles;
  }
  if (fail_code) {
    rep.Stage("preflight_failed", summary);
    rep.SetExit(fail_code, "preflight: arquivos do jogo ausentes ou pasta invalida");
    if (!silent) port::ShowErrorPtBr(summary);
    std::fflush(stderr);
    std::exit(fail_code);  // antes de criar janela/runtime: nada a desmontar
  }
  rep.Stage("preflight_ok", summary);
  if (REXCVAR_GET(preflight_only)) {
    rep.SetExit(port::kExitOk, "preflight_only");
    std::fflush(stderr);
    std::exit(port::kExitOk);
  }
  ConfigurePcLayer(paths);
  rep.SetInfo("pc_settings", pc::Path().string());
  rep.SetInfo("pc_video", fmt::format("{} escala {}x fps60={} distancia={}",
                                      rex::cvar::GetFlagByName("resolution"),
                                      rex::cvar::GetFlagByName("draw_resolution_scale_x"),
                                      pc::Get().fps60, pc::DrawDistanceName(pc::Get().draw_distance)));
}

// Camada de PC (do projeto W:\Decomp\Banjo): Configuracoes de PC num arquivo proprio, separado
// dos saves; resolucao de saida e escala interna (4K = 3x sobre os 1280x720 do jogo); janela em
// pixels fisicos apesar da escala do Windows; opcoes do runtime (controle, volume).
void BanjotooieApp::ConfigurePcLayer(rex::PathConfig& paths) {
  // O VBlank do runtime e as esperas da GPU usam Sleep(1 ms): com a resolucao padrao do Windows
  // (15,6 ms) cada quadro arredonda para ~31 ms. 1 ms permite os 60 FPS reais.
  timeBeginPeriod(1);
  const fs::path base = paths.user_data_root.empty()
                            ? rex::filesystem::GetExecutableFolder() / "userdata"
                            : paths.user_data_root;
  user_data_root_ = base;
  pc::SetPath(base / "pc_settings.toml");
  const bool had_settings = pc::Load();
  // M04 (C-29): o monitor em que o jogo vai abrir (o escolhido nas opções ou, no automático, o do
  // cursor). Antes a resolução automática olhava sempre o monitor PRINCIPAL, mesmo com a janela
  // aberta em outro.
  if (had_settings && pc::MigratedFromOldMonitorRule() && pc::Get().monitor.empty()) {
    for (const auto& m : pc::display::List())
      if (m.primary) pc::Get().monitor = m.device;  // migração: continua no principal
    pc::Save();
  }
  std::string monitor_why;
  const pc::display::Monitor target = pc::display::Target(&monitor_why);
  pc::display::SetChosen(target);
  port::BootReport::Get().SetInfo(
      "monitor", fmt::format("{} {}x{} a {} Hz ({})", target.device, target.width, target.height,
                             target.hz, monitor_why));
  if (!had_settings || pc::Get().resolution_auto) {
    // Resolucao fisica do monitor escolhido (GetSystemMetrics e reduzida pela escala do Windows
    // num processo sem reconhecimento de DPI e escolheria 1080p num monitor 4K).
    int w = target.width;
    int h = target.height;
    if (w <= 0 || h <= 0) {
      DEVMODEW mode{};
      mode.dmSize = sizeof(mode);
      w = GetSystemMetrics(SM_CXSCREEN);
      h = GetSystemMetrics(SM_CYSCREEN);
      if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmPelsWidth > 0) {
        w = int(mode.dmPelsWidth);
        h = int(mode.dmPelsHeight);
      }
    }
    pc::Get().resolution = (w >= 3840 && h >= 2160)   ? pc::Resolution::R2160p
                           : (w >= 2560 && h >= 1440) ? pc::Resolution::R1440p
                                                      : pc::Resolution::R1080p;
    pc::Save();
  }
  {
    // Janela em pixels fisicos: o runtime dimensiona em unidades logicas (DPI); a 150% a janela
    // ficaria 1,5x maior que a tela e o presenter recortaria a imagem.
    const auto r = pc::Get().resolution;
    int pw = r == pc::Resolution::R1080p ? 1920 : r == pc::Resolution::R1440p ? 2560 : 3840;
    int ph = r == pc::Resolution::R1080p ? 1080 : r == pc::Resolution::R1440p ? 1440 : 2160;
    RECT work = target.rc_work;  // área útil física do monitor escolhido (C-29)
    if (work.right > work.left || SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
      const int maxw = (work.right - work.left) - 16;
      const int maxh = (work.bottom - work.top) - 40;
      if (pw > maxw) {  // mantem 16:9 ao limitar a area de trabalho
        pw = maxw;
        ph = pw * 9 / 16;
      }
      if (ph > maxh) {
        ph = maxh;
        pw = ph * 16 / 9;
      }
    }
    UINT dpi = 96;
    HKEY key = nullptr;  // o processo nao e DPI-aware: GetDpiForSystem diria 96
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics", 0, KEY_READ,
                      &key) == ERROR_SUCCESS) {
      DWORD value = 0, size = sizeof(value), type = 0;
      if (RegQueryValueExW(key, L"AppliedDPI", nullptr, &type, reinterpret_cast<LPBYTE>(&value),
                           &size) == ERROR_SUCCESS &&
          type == REG_DWORD && value >= 96) {
        dpi = value;
      }
      RegCloseKey(key);
    }
    // Com DPI por monitor o runtime já trabalha em pixels físicos (sem divisão pela escala).
    if (dpi_aware_) dpi = 96;
    pc_video_cvars_["window_width"] = std::to_string(pw * 96 / int(dpi));
    pc_video_cvars_["window_height"] = std::to_string(ph * 96 / int(dpi));
  }
  pc::ApplyRuntimeOptions();
  runtime_config_ = paths.config_path;
  {
    // Saida e escala interna sempre pelas Configuracoes de PC. A imagem do jogo tem 1280x720 e
    // o runtime so aceita escala inteira: 1440p = 2x e 4K = 3x exatos. Em 1080p (M04, C-22) a
    // escala 1x deixava a imagem interna em 1280x720 ampliada para 1920x1080; agora o jogo
    // desenha a 2x (2560x1440) e a apresentacao reduz para 1920x1080 (supersampling): mais de
    // 1080 linhas reais, sem perder nitidez.
    const auto r = pc::Get().resolution;
    const char* preset = r == pc::Resolution::R1080p ? "1080p" : r == pc::Resolution::R1440p ? "1440p" : "4k";
    const char* scale = pc::InternalScaleText(r);
    pc_video_cvars_["resolution"] = preset;
    pc_video_cvars_["draw_resolution_scale_x"] = scale;
    pc_video_cvars_["draw_resolution_scale_y"] = scale;
    pc_video_cvars_["fullscreen"] =
        pc::Get().display_mode == pc::DisplayMode::TelaCheia ? "true" : "false";
    // M04: o modo de vídeo do console fica fixo em 1280x720 (o do jogo), não o tamanho da
    // janela: com DPI por monitor a janela 1080p mede 1920 e o jogo passaria a desenhar um quadro
    // 1080p (rexglue-sdk video_mode_fixed).
    pc_video_cvars_["video_mode_fixed"] = "true";
  }
  ApplyPcVideoCvars(nullptr);
}

// M04 (C-82): resolucao, escala interna, tamanho da janela e tela cheia sao das Configuracoes
// de PC da conta (pc_settings.toml). O arquivo do runtime ao lado do exe (banjotooie.toml) e lido
// DEPOIS de OnConfigurePaths e trazia os mesmos valores de outra vez (ex.: resolution = "4k" com
// a conta em 1080p): a escolha do menu era ignorada. Reaplicados em OnPreSetup; a divergencia
// vai para o log.
void BanjotooieApp::ApplyPcVideoCvars(const char* when) {
  for (const auto& [k, v] : pc_video_cvars_) {
    const std::string now = rex::cvar::GetFlagByName(k);
    if (when && now != v)
      REXLOG_WARN("[port] {}: {} = '{}' do arquivo do runtime ignorado; Configuracoes de PC: '{}'",
                  when, k, now, v);
    rex::cvar::SetFlagByName(k, v);
  }
}

void BanjotooieApp::OnPostInitLogging() {
  REXLOG_INFO("[port] Banjo-Tooie XBLA PT-BR Recompiled (MISSAO 01) - relatorio: {}",
              port::BootReport::Get().path().string());
  for (const auto& f : g_preflight.files) {
    REXLOG_INFO("[port] preflight {:<12} {:<34} {}", f.role, f.rel,
                f.found_at.empty() ? std::string("AUSENTE")
                                   : fmt::format("ok ({} bytes)", f.size));
  }
  port::BootReport::Get().Stage("logging");
  // M04 (C-10): registro de sessão sempre ligado na pasta de dados do usuário.
  {
    auto& rep = port::BootReport::Get();
    pc::log::StartInfo si;
    si.user_data_root = user_data_root_;
    si.game_data_root = game_data_root_;
    si.preflight = g_preflight.SummaryPtBr();
    si.user_data_source = user_data_source_;
    si.automated = automated_;
    if (pc::log::Start(si)) {
      rep.SetInfo("session_log", pc::log::Path().string());
      rep.SetStageListener([](const std::string& name, const std::string& detail, double t_ms) {
        char b[48];
        std::snprintf(b, sizeof b, " (%.0f ms)", t_ms);
        pc::log::Event("etapa", name + (detail.empty() ? "" : ": " + detail) + b);
      });
    } else {
      rep.SetInfo("session_log", "indisponivel: " + pc::log::Error());
      REXLOG_WARN("[sessao] registro de sessão indisponível: {}", pc::log::Error());
    }
  }
}

void BanjotooieApp::OnPreSetup(rex::RuntimeConfig& config) {
  auto& rep = port::BootReport::Get();
  ApplyPcVideoCvars("opcoes de video");  // depois do banjotooie.toml (C-82)
  // MISSAO 02: opcoes de teste conferidas antes da janela; invalida = sai com codigo 6.
  for (const auto& [when, file] : SplitPairs(REXCVAR_GET(port_test_shots))) {
    char* end = nullptr;
    const double s = std::strtod(when.c_str(), &end);
    if (when.empty() || !end || *end || s < 0 || file.empty()) {
      ExitBadTestOption("port_test_shots: '" + when + "=" + file + "'");
    }
    extra_shots_.push_back({s, file, false});
  }
  for (const auto& [range, file] : SplitPairs(REXCVAR_GET(port_dump_ranges))) {
    const size_t colon = range.find(':');
    char* e1 = nullptr;
    char* e2 = nullptr;
    const unsigned long long addr = std::strtoull(range.substr(0, colon).c_str(), &e1, 0);
    const unsigned long long size =
        colon == std::string::npos ? 0 : std::strtoull(range.substr(colon + 1).c_str(), &e2, 0);
    if (!e1 || *e1 || !e2 || *e2 || !size || addr + size > 0x100000000ull || file.empty()) {
      ExitBadTestOption("port_dump_ranges: '" + range + "=" + file + "'");
    }
    dump_ranges_.push_back({uint32_t(addr), uint32_t(size), file});
  }
  const std::string script = REXCVAR_GET(port_test_input);
  if (!script.empty()) {
    std::string error;
    if (!port::ParseTestInputScript(script, &port::GetTestInputState().presses, &error)) {
      ExitBadTestOption("port_test_input: " + error);
    }
    // Driver de entrada oficial do SDK: o jogo le o controle pelo caminho normal.
    config.input_factory = [](bool) -> std::unique_ptr<rex::system::IInputSystem> {
      auto input = std::make_unique<rex::input::InputSystem>(nullptr);
      input->AddDriver(port::CreateTestInputDriver());
      return input;
    };
    rep.SetInfo("test_input", script);
    rep.Stage("test_input_armed", fmt::format("{} apertos roteirizados; controles reais "
                                              "desligados nesta execucao",
                                              port::GetTestInputState().presses.size()));
  }
  if (REXCVAR_GET(background_window) || automated_) {
    previous_foreground_ = GetForegroundWindow();
  }
  rep.Stage("presentation_setup");
}

void BanjotooieApp::OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) {
  // Menu CONFIGURACOES DE PC (aberto pela entrada nos menus do jogo ou por F1).
  pc::menu::Create(drawer, window(), runtime_config_);
  const bool display_probe = !pc::automation::ParseCommandLine().probe_display.empty();
  if (!automated_ || display_probe) pc::menu::ApplyDisplayMode(window());
  if (wcsstr(GetCommandLineW(), L"--open-pc-menu")) pc::menu::Open();
  if ((REXCVAR_GET(background_window) || automated_) && window()) {
    HWND hwnd = static_cast<rex::ui::Win32Window*>(window())->hwnd();
    // Fica atras das outras janelas e devolve o foco a quem o tinha (testes sem atrapalhar).
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (previous_foreground_ && previous_foreground_ != hwnd) {
      SetForegroundWindow(static_cast<HWND>(previous_foreground_));
    }
  }
  port::BootReport::Get().Stage("window_open");
}

void BanjotooieApp::OnPostLoadXexImage() {
  port::BootReport::Get().Stage("xex_loaded", "game:\\default.xex");
  REXLOG_INFO("[port] XEX carregado");
}

void BanjotooieApp::OnPreLaunchModule() {
  // Pacote de shaders do port (se houver): completa o cache antes de o runtime carrega-lo.
  shader_pack_copied_ =
      pc::shaders::InstallPack(rex::filesystem::GetExecutableFolder(), runtime()->cache_root());
  // Saves (M04, etapa B): conferir e, se o jogo fosse descartar algo, restaurar ANTES de o jogo
  // comecar. Antes isto rodava depois do inicio do jogo, em paralelo com a leitura dele.
  pc::saves::Initialize(user_data_root_);
  // CONQUISTAS no PC (M04, C-6): obtidas guardadas por conta + aviso do runtime.
  pc::achievements::Install(user_data_root_);
}

void BanjotooieApp::OnPostLaunchModule(rex::system::XThread* thread) {
  (void)thread;
  launch_time_ = std::chrono::steady_clock::now();
  // Origem do roteiro de teste (MISSAO 02): os tempos contam daqui, como --screenshot_seconds.
  port::GetTestInputState().origin_ns.store(launch_time_.time_since_epoch().count(),
                                            std::memory_order_release);
  port::BootReport::Get().Stage("module_launched");
  REXLOG_INFO("[port] modulo iniciado; monitor de quadros ativo");
  stop_monitor_ = false;
  monitor_ = std::thread([this] { MonitorLoop(); });
  if (REXCVAR_GET(port_test_crash)) {
    port::BootReport::Get().Stage("test_crash_armed", "excecao de teste em 1 s");
    std::thread([] {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      RaiseException(kTestCrashCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }).detach();
  }

  // ---- Camada de PC
  // --prepare-shaders: os pipelines guardados ja foram criados; grava o relatorio e sai.
  if (const auto report = pc::shaders::RequestedReport(); !report.empty())
    pc::shaders::FinishPreparation(report, runtime()->cache_root(), shader_pack_copied_);
  // Relatorio de falhas com o endereco do jogo (encadeia o tratador da Missao 01).
  pc::crash::Install(runtime()->function_dispatcher(), user_data_root_ / "crashes");
  pc::input::Install(runtime()->input_system());
  // Gamepads reais (SDL/XInput) para o menu de PC, com zona morta e vibracao dos drivers.
  if (auto* in = dynamic_cast<rex::input::InputSystem*>(runtime()->input_system())) {
    pc::menu::SetInputSystem(in);
    // Com o menu de PC aberto, o jogo recebe o controle parado (A/B/Start nao vazam).
    in->SetBlockedCallback([] { return pc::menu::BlocksGameInput(); });
  }
  // Teclado e mouse sao a entrada padrao do PC; o controle real continua somado pelo runtime.
  // (Com --port_test_input o roteiro de teste continua sendo a unica entrada.)
  if (REXCVAR_GET(port_test_input).empty()) {
    rex::cvar::SetFlagByName("mnk_mode", "true");
    static const char* const kDefaults[][2] = {
        {"keybind_start", "Return"},  {"keybind_a", "Space"},       {"keybind_b", "Backspace"},
        {"keybind_x", "R"},           {"keybind_y", "E"},           {"keybind_lstick_up", "W"},
        {"keybind_lstick_down", "S"}, {"keybind_lstick_left", "A"}, {"keybind_lstick_right", "D"}};
    for (const auto& kv : kDefaults)
      if (!rex::cvar::HasNonDefaultValue(kv[0])) rex::cvar::SetFlagByName(kv[0], kv[1]);
  }
  REXLOG_INFO("PC video: resolution={} scale={}x{} window={}x{} fps60={}",
              rex::cvar::GetFlagByName("resolution"),
              rex::cvar::GetFlagByName("draw_resolution_scale_x"),
              rex::cvar::GetFlagByName("draw_resolution_scale_y"),
              rex::cvar::GetFlagByName("window_width"), rex::cvar::GetFlagByName("window_height"),
              pc::Get().fps60);
  pc::input::SetMouseLook(pc::Get().mouse_look);
  if (pc::Get().mouse_look) pc::input::SetCaptureEnabled(true);
  StartFocusWatcher();
  auto opts = pc::automation::ParseCommandLine();
  if (opts.active()) {
    opts.user_data_root = user_data_root_;
    pc::automation::Start(opts, &app_context(), [this]() -> rex::ui::Presenter* {
      auto* gs = static_cast<rex::graphics::GraphicsSystem*>(runtime()->graphics_system());
      return gs ? gs->presenter() : nullptr;
    }, window(), runtime()->input_system());
  }
}

namespace {
float ReadBeFloat(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  const uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  float f;
  std::memcpy(&f, &v, sizeof(f));
  return f;
}
// Posicao do jogador (0x8266BB74..7C, floats BE) valida = ha um jogo em andamento.
bool PlayerInGame() {
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return false;
  const uint8_t* base = ks->memory()->virtual_membase();
  if (!base) return false;
  const float x = ReadBeFloat(base, 0x8266BB74);
  const float y = ReadBeFloat(base, 0x8266BB78);
  const float z = ReadBeFloat(base, 0x8266BB7C);
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
  if (std::fabs(x) < 0.01f && std::fabs(y) < 0.01f && std::fabs(z) < 0.01f) return false;
  return std::fabs(x) < 100000.0f && std::fabs(y) < 100000.0f && std::fabs(z) < 100000.0f;
}
// 0x8266EF55 == 1: menu de pausa aberto.
bool PauseOpen() {
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return false;
  const uint8_t* base = ks->memory()->virtual_membase();
  return base && base[0x8266EF55] == 1;
}
}  // namespace

// Pausa (Start) ao perder o foco e libera/recaptura o mouse. Nao age em testes automaticos.
void BanjotooieApp::StartFocusWatcher() {
  if (focus_thread_.joinable() || !window() || automated_) return;
  HWND hwnd = static_cast<rex::ui::Win32Window*>(window())->hwnd();
  focus_running_.store(true);
  focus_thread_ = std::thread([this, hwnd]() {
    bool was_focused = true;
    while (focus_running_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      if (!hwnd) continue;
      const bool focused = GetForegroundWindow() == hwnd;
      if (pc::automation::IsRunning() || pc::state::guest_swaps.load() < 60) {
        was_focused = focused;
        continue;
      }
      if (was_focused && !focused) {
        if (pc::Get().mouse_look) pc::input::SetCaptureEnabled(false);
        if (pc::Get().pause_on_focus_loss && PlayerInGame() && !PauseOpen())
          pc::input::RequestTap(pc::input::kStart);
      } else if (!was_focused && focused) {
        if (pc::Get().mouse_look) pc::input::SetCaptureEnabled(true);
      }
      was_focused = focused;
    }
  });
}

void BanjotooieApp::OnGuestThreadExit(rex::system::XThread* thread) {
  (void)thread;
  port::BootReport::Get().Stage("guest_thread_exit");
  REXLOG_INFO("[port] thread principal do jogo terminou");
}

void BanjotooieApp::OnShutdown() {
  StopMonitor();
  focus_running_.store(false);
  if (focus_thread_.joinable()) focus_thread_.join();
  pc::saves::Shutdown();
  pc::menu::Destroy();
  auto& rep = port::BootReport::Get();
  if (!REXCVAR_GET(port_test_input).empty()) {
    rep.Stage("test_input", port::TestInputSummary());
  }
  rep.Stage("shutdown");
  if (!exit_requested_) {
    rep.SetExit(port::kExitOk, "encerrado normalmente (janela fechada ou fim do jogo)");
  }
  pc::log::Stop(exit_requested_ ? exit_reason_ : std::string("janela fechada ou fim do jogo"));
}

void BanjotooieApp::StopMonitor() {
  stop_monitor_ = true;
  if (monitor_.joinable()) monitor_.join();
}

void BanjotooieApp::MonitorLoop() {
  using clock = std::chrono::steady_clock;
  auto& rep = port::BootReport::Get();
  const std::string shot = REXCVAR_GET(screenshot);
  const uint64_t shot_frames = uint64_t(std::max(0, REXCVAR_GET(screenshot_frames)));
  const double shot_seconds = REXCVAR_GET(screenshot_seconds);
  const double smoke_seconds = REXCVAR_GET(smoke_seconds);
  const double timeout = REXCVAR_GET(smoke_timeout_seconds);
  bool first_frame = false;
  bool exit_posted = false;
  uint64_t window_frames = 0;
  auto window_start = clock::now();
  double fps = 0.0;
  while (!stop_monitor_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pc::log::Tick();  // mapa, cena e bloco de desempenho do registro de sessão
    const uint64_t frames = port::GuestPresentCount().load(std::memory_order_relaxed);
    const auto now = clock::now();
    const double elapsed = std::chrono::duration<double>(now - launch_time_).count();
    if (!first_frame && frames > 0) {
      first_frame = true;
      rep.Stage("first_guest_frame", fmt::format("{:.2f} s apos o inicio do modulo", elapsed));
      REXLOG_INFO("[port] primeiro quadro do jogo apos {:.2f} s", elapsed);
    }
    const double window_s = std::chrono::duration<double>(now - window_start).count();
    if (window_s >= 1.0) {
      fps = double(frames - window_frames) / window_s;
      window_frames = frames;
      window_start = now;
      rep.SetFrames(frames, fps, elapsed);
    }
    if (exit_posted) continue;
    // Menu principal: SIM na confirmacao de SAIR PARA O DESKTOP (gancho em 0x822DD720) fecha pelo mesmo
    // caminho do botão de fechar a janela (desligamento normal, saves e opções gravados).
    if (pc::state::exit_to_desktop_request.exchange(false, std::memory_order_acq_rel)) {
      exit_posted = true;
      app_context().CallInUIThread([this] {
        RequestExitOnUiThread(port::kExitOk, "SAIR PARA O DESKTOP confirmada (SIM) no menu principal");
      });
      continue;
    }
    // MISSAO 02: capturas extras (sem sair), na ordem; uma por vez na thread da interface.
    for (auto& es : extra_shots_) {
      if (!es.taken && !extra_shot_pending_ && frames > 0 && elapsed >= es.at_s) {
        es.taken = true;
        extra_shot_pending_ = true;
        const std::string path = es.path;
        app_context().CallInUIThread([this, path] {
          const CaptureResult r = CaptureGuestPng(path);
          auto& report = port::BootReport::Get();
          report.Stage("test_shot", fmt::format("{} ({}x{}) {}", path, r.width, r.height, r.detail));
          REXLOG_INFO("[port] captura extra: {} ({}x{}) {}", path, r.width, r.height, r.detail);
          extra_shot_pending_ = false;
        });
        break;
      }
    }
    if (!shot.empty() && !screenshot_pending_ && !screenshot_done_ && frames >= shot_frames &&
        elapsed >= shot_seconds) {
      screenshot_pending_ = true;
      app_context().CallInUIThread([this] { TakeScreenshotOnUiThread(); });
    }
    if (smoke_seconds > 0.0 && elapsed >= smoke_seconds && (shot.empty() || screenshot_done_)) {
      exit_posted = true;
      const int code = frames > 0 ? port::kExitOk : port::kExitSmokeNoFrames;
      app_context().CallInUIThread([this, code, frames, elapsed] {
        RequestExitOnUiThread(code, fmt::format("smoke_seconds: {} quadros em {:.1f} s", frames,
                                                elapsed));
      });
    } else if (automated_ && elapsed >= timeout) {
      exit_posted = true;
      const int code = !shot.empty() && !screenshot_done_ ? port::kExitScreenshotFailed
                       : frames > 0                       ? port::kExitOk
                                                          : port::kExitSmokeNoFrames;
      app_context().CallInUIThread([this, code, frames, elapsed] {
        RequestExitOnUiThread(code, fmt::format("tempo limite: {} quadros em {:.1f} s", frames,
                                                elapsed));
      });
    }
  }
  rep.SetFrames(port::GuestPresentCount().load(), fps,
                std::chrono::duration<double>(clock::now() - launch_time_).count());
}

void BanjotooieApp::DumpDbOnUiThread() {
  const std::string out = REXCVAR_GET(port_dump_db);
  if (out.empty() || !runtime() || !runtime()->memory()) return;
  auto& rep = port::BootReport::Get();
  auto* mem = runtime()->memory();
  auto be32 = [&](uint32_t guest) {
    const uint8_t* p = mem->TranslateVirtual<const uint8_t*>(guest);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  };
  const uint32_t raw = be32(kGuestDbRawPtr);
  const uint32_t table = be32(kGuestDbTablePtr);
  // Base dos dados (0x82265DE0 grava raw + 0xDB40 em 0x826746E0; 0x82265B98 soma (entrada>>6)).
  const uint32_t data_base = be32(kGuestDbDataBasePtr);
  std::error_code ec;
  const uint64_t file_size = fs::file_size(game_data_root() / "RAWFiles" / "db360.cmp", ec);
  std::string detail;
  bool ok = false;
  if (!raw || !table || ec || !file_size) {
    detail = fmt::format("sem buffer (raw=0x{:08X}, tamanho={})", raw, ec ? 0 : file_size);
  } else {
    // Tabela: entradas u32 BE em table enquanto (entrada>>6) nao diminui, ate a base dos dados.
    uint32_t count = 0, last = 0;
    const uint32_t max_entries = data_base > table ? (data_base - table) / 4 : 0;
    for (; count < max_entries; ++count) {
      const uint32_t v = be32(table + count * 4) >> 6;
      if (count && v < last) break;
      last = v;
    }
    // Copia de raw ate o fim do ultimo asset (ou o tamanho do arquivo), so paginas confirmadas
    // como mapeadas no host (VirtualQuery), para nunca ler memoria invalida.
    uint64_t want = std::max<uint64_t>(file_size, uint64_t(data_base - raw) + last + 0x10000);
    const uint8_t* host = mem->TranslateVirtual<const uint8_t*>(raw);
    uint64_t readable = 0;
    while (readable < want) {
      MEMORY_BASIC_INFORMATION mbi{};
      if (!VirtualQuery(host + readable, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
          (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        break;
      }
      readable = uint64_t(static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize - host);
    }
    const uint64_t size = std::min(want, readable);
    std::ofstream f(fs::path(out), std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(host), std::streamsize(size));
    ok = bool(f);
    detail = fmt::format(
        "raw=0x{:08X} tabela=0x{:08X} (raw+{}) dados=0x{:08X} (raw+0x{:X}) entradas={} "
        "ultimo_desloc=0x{:X} tamanho_arquivo={} bytes_copiados={} gravado={}",
        raw, table, int64_t(table) - int64_t(raw), data_base, data_base - raw, count, last,
        file_size, size, ok);
  }
  rep.SetInfo("db_dump", out);
  rep.Stage("db_dump", detail);
  REXLOG_INFO("[port] dump do db360 na memoria: {}", detail);
}

void BanjotooieApp::DumpRangesOnUiThread() {
  if (dump_ranges_.empty() || !runtime() || !runtime()->memory()) return;
  auto& rep = port::BootReport::Get();
  auto* mem = runtime()->memory();
  for (const auto& r : dump_ranges_) {
    // So paginas confirmadas como mapeadas no host (VirtualQuery): nunca le memoria invalida.
    const uint8_t* host = mem->TranslateVirtual<const uint8_t*>(r.address);
    uint64_t readable = 0;
    while (readable < r.size) {
      MEMORY_BASIC_INFORMATION mbi{};
      if (!VirtualQuery(host + readable, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
          (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        break;
      }
      readable = uint64_t(static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize - host);
    }
    const uint64_t size = std::min<uint64_t>(r.size, readable);
    std::ofstream f(fs::path(r.path), std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(host), std::streamsize(size));
    const std::string detail = fmt::format("0x{:08X}+0x{:X} -> {} ({} bytes copiados, gravado={})",
                                           r.address, r.size, r.path, size, bool(f));
    rep.Stage("range_dump", detail);
    REXLOG_INFO("[port] dump de memoria: {}", detail);
  }
}

BanjotooieApp::CaptureResult BanjotooieApp::CaptureGuestPng(const std::string& path) {
  CaptureResult r;
  rex::ui::RawImage image;
  auto* gs = runtime() ? static_cast<rex::graphics::GraphicsSystem*>(runtime()->graphics_system())
                       : nullptr;
  if (!gs || !gs->presenter()) {
    r.detail = "sem presenter";
    return r;
  }
  if (!gs->presenter()->CaptureGuestOutput(image) || image.data.empty()) {
    r.detail = "CaptureGuestOutput falhou (ainda sem imagem do jogo?)";
    return r;
  }
  r.width = image.width;
  r.height = image.height;
  // Brilho medio e fracao de pixels nao pretos: prova de que a imagem nao esta vazia.
  uint64_t sum = 0, lit = 0;
  for (uint32_t y = 0; y < image.height; ++y) {
    const uint8_t* row = image.data.data() + size_t(y) * image.stride;
    for (uint32_t x = 0; x < image.width; ++x) {
      const uint32_t l = (uint32_t(row[x * 4]) * 2126 + uint32_t(row[x * 4 + 1]) * 7152 +
                          uint32_t(row[x * 4 + 2]) * 722) / 10000;
      sum += l;
      lit += l > 16 ? 1 : 0;
    }
  }
  const double px = double(image.width) * image.height;
  r.ok = port::WritePngRgbx(path, image.data.data(), image.width, image.height, image.stride);
  r.detail = fmt::format("{}; quadros={}; brilho_medio={:.1f}; pixels_acesos={:.1f}%",
                         r.ok ? "PNG gravado" : "falha ao gravar PNG",
                         port::GuestPresentCount().load(), sum / px, 100.0 * lit / px);
  return r;
}

void BanjotooieApp::TakeScreenshotOnUiThread() {
  DumpDbOnUiThread();
  DumpRangesOnUiThread();
  auto& rep = port::BootReport::Get();
  const std::string path = REXCVAR_GET(screenshot);
  const CaptureResult cap = CaptureGuestPng(path);
  rep.SetScreenshot(path, cap.width, cap.height, cap.ok, cap.detail);
  REXLOG_INFO("[port] captura: {} ({}x{}) {}", path, cap.width, cap.height, cap.detail);
  screenshot_done_ = true;
  screenshot_pending_ = false;
  if (REXCVAR_GET(smoke_seconds) <= 0.0) {
    RequestExitOnUiThread(cap.ok ? port::kExitOk : port::kExitScreenshotFailed,
                          "captura de tela concluida");
  }
}

void BanjotooieApp::RequestExitOnUiThread(int code, const std::string& reason) {
  if (exit_requested_.exchange(true)) return;
  exit_reason_ = fmt::format("saída pedida (código {}): {}", code, reason);
  auto& rep = port::BootReport::Get();
  rep.SetExit(code, reason);
  rep.Stage("exit_requested", fmt::format("codigo {}: {}", code, reason));
  REXLOG_INFO("[port] saindo com codigo {}: {}", code, reason);
  if (window()) window()->RequestClose();  // OnClosing: TerminateTitle + QuitFromUIThread
  PostQuitMessage(code);                   // o loop devolve este codigo (WM_QUIT.wParam)
  // Rede de seguranca (2026-11-11): o desligamento do runtime as vezes nao termina (regressao
  // PT-BR da rodada 9: 1 processo parado 150 s depois do estagio "shutdown"; 0 em 4 repeticoes).
  // Com "SAIR PARA O DESKTOP" o programa tem de fechar de verdade: se ainda estiver vivo 15 s
  // depois do pedido, encerra com o mesmo codigo. Sem log aqui: o logger pode ja ter sido
  // destruido; so stderr.
  std::thread([code] {
    std::this_thread::sleep_for(std::chrono::seconds(15));
    std::fprintf(stderr, "[banjotooie] saida demorou mais de 15 s; encerrando com codigo %d\n",
                 code);
    std::fflush(stderr);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
  }).detach();
}
