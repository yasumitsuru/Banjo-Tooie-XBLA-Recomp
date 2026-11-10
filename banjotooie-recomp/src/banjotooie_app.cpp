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

#include "generated/default/banjotooie_init.h"

#include "banjotooie_app.h"
#include "port_diagnostics.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/graphics/graphics_system.h>
#include <rex/logging.h>
#include <rex/system/xthread.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window_win.h>
#include <rex/version.h>

#include <fmt/format.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

namespace {
constexpr DWORD kTestCrashCode = 0xE0425442;  // codigo de cliente (bit 29), 'BTB'
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

}  // namespace

BanjotooieApp::~BanjotooieApp() { StopMonitor(); }

void BanjotooieApp::OnConfigurePaths(rex::PathConfig& paths) {
  auto& rep = port::BootReport::Get();
  const fs::path exe_dir = rex::filesystem::GetExecutableFolder();
  const std::string report_cvar = REXCVAR_GET(boot_report);
  rep.SetPath(report_cvar.empty() ? exe_dir / "logs" / "banjotooie_boot.json"
                                  : fs::path(report_cvar));
  port::InstallCrashHandler(rep.path().parent_path() / "crashdumps");

  automated_ = REXCVAR_GET(preflight_only) || !REXCVAR_GET(screenshot).empty() ||
               REXCVAR_GET(smoke_seconds) > 0.0 || REXCVAR_GET(port_test_crash);
  const bool silent = REXCVAR_GET(port_silent) || automated_;

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
}

void BanjotooieApp::OnPreSetup(rex::RuntimeConfig& config) {
  (void)config;
  if (REXCVAR_GET(background_window) || automated_) {
    previous_foreground_ = GetForegroundWindow();
  }
  port::BootReport::Get().Stage("presentation_setup");
}

void BanjotooieApp::OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) {
  (void)drawer;
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

void BanjotooieApp::OnPostLaunchModule(rex::system::XThread* thread) {
  (void)thread;
  launch_time_ = std::chrono::steady_clock::now();
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
}

void BanjotooieApp::OnGuestThreadExit(rex::system::XThread* thread) {
  (void)thread;
  port::BootReport::Get().Stage("guest_thread_exit");
  REXLOG_INFO("[port] thread principal do jogo terminou");
}

void BanjotooieApp::OnShutdown() {
  StopMonitor();
  auto& rep = port::BootReport::Get();
  rep.Stage("shutdown");
  if (!exit_requested_) {
    rep.SetExit(port::kExitOk, "encerrado normalmente (janela fechada ou fim do jogo)");
  }
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

void BanjotooieApp::TakeScreenshotOnUiThread() {
  auto& rep = port::BootReport::Get();
  const std::string path = REXCVAR_GET(screenshot);
  rex::ui::RawImage image;
  bool ok = false;
  std::string detail;
  auto* gs = runtime() ? static_cast<rex::graphics::GraphicsSystem*>(runtime()->graphics_system())
                       : nullptr;
  if (!gs || !gs->presenter()) {
    detail = "sem presenter";
  } else if (!gs->presenter()->CaptureGuestOutput(image) || image.data.empty()) {
    detail = "CaptureGuestOutput falhou (ainda sem imagem do jogo?)";
  } else {
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
    ok = port::WritePngRgbx(path, image.data.data(), image.width, image.height, image.stride);
    detail = fmt::format("{}; quadros={}; brilho_medio={:.1f}; pixels_acesos={:.1f}%",
                         ok ? "PNG gravado" : "falha ao gravar PNG",
                         port::GuestPresentCount().load(), sum / px, 100.0 * lit / px);
  }
  rep.SetScreenshot(path, image.width, image.height, ok, detail);
  REXLOG_INFO("[port] captura: {} ({}x{}) {}", path, image.width, image.height, detail);
  screenshot_done_ = true;
  screenshot_pending_ = false;
  if (REXCVAR_GET(smoke_seconds) <= 0.0) {
    RequestExitOnUiThread(ok ? port::kExitOk : port::kExitScreenshotFailed,
                          "captura de tela concluida");
  }
}

void BanjotooieApp::RequestExitOnUiThread(int code, const std::string& reason) {
  if (exit_requested_.exchange(true)) return;
  auto& rep = port::BootReport::Get();
  rep.SetExit(code, reason);
  rep.Stage("exit_requested", fmt::format("codigo {}: {}", code, reason));
  REXLOG_INFO("[port] saindo com codigo {}: {}", code, reason);
  if (window()) window()->RequestClose();  // OnClosing: TerminateTitle + QuitFromUIThread
  PostQuitMessage(code);                   // o loop devolve este codigo (WM_QUIT.wParam)
}
