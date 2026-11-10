// banjotooie - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.
//
// MISSAO 01 (Banjo-Tooie XBLA PT-BR Recompiled): a base iron-jay/banjo_tooie_xbla_recomp@72c2397
// deixava todos os ganchos comentados. Este projeto usa os ganchos apenas para diagnostico e
// testes automatizados (preflight dos arquivos, relatorio de boot, falhas, quadros, captura de
// tela, teste de fumaca). Nenhum deles muda o comportamento do jogo. Ver banjotooie_app.cpp.

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <rex/rex_app.h>

class BanjotooieApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  ~BanjotooieApp() override;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BanjotooieApp>(new BanjotooieApp(ctx, "banjotooie",
        PPCImageConfig));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override;
  void OnPostInitLogging() override;
  void OnPreSetup(rex::RuntimeConfig& config) override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;
  void OnPostLoadXexImage() override;
  void OnPostLaunchModule(rex::system::XThread* thread) override;
  void OnGuestThreadExit(rex::system::XThread* thread) override;
  void OnShutdown() override;

 private:
  struct CaptureResult {
    bool ok = false;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string detail;
  };
  struct ExtraShot {  // MISSAO 02: --port_test_shots
    double at_s = 0.0;
    std::string path;
    bool taken = false;
  };
  struct DumpRange {  // MISSAO 02: --port_dump_ranges
    uint32_t address = 0;
    uint32_t size = 0;
    std::string path;
  };

  void MonitorLoop();
  void TakeScreenshotOnUiThread();
  CaptureResult CaptureGuestPng(const std::string& path);
  void DumpDbOnUiThread();      // MISSAO 02: diagnostico (--port_dump_db)
  void DumpRangesOnUiThread();  // MISSAO 02: diagnostico (--port_dump_ranges)
  void RequestExitOnUiThread(int code, const std::string& reason);
  void StopMonitor();

  std::vector<ExtraShot> extra_shots_;  // so lido/escrito pela thread do monitor
  std::vector<DumpRange> dump_ranges_;
  std::atomic<bool> extra_shot_pending_{false};
  bool automated_ = false;           // --screenshot / --smoke_seconds / --preflight_only
  void* previous_foreground_ = nullptr;
  std::thread monitor_;
  std::atomic<bool> stop_monitor_{false};
  std::atomic<bool> screenshot_pending_{false};
  std::atomic<bool> screenshot_done_{false};
  std::atomic<bool> exit_requested_{false};
  std::chrono::steady_clock::time_point launch_time_{};
};
