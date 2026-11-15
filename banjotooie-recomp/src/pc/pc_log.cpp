// Registro de sessão (M04, C-10). Ver pc_log.h.
#include "pc_log.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <spdlog/sinks/base_sink.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <shlobj.h>

#include "pc_settings.h"
#include "pc_state.h"

#ifndef PC_GIT_COMMIT
#define PC_GIT_COMMIT "desconhecido"
#endif
#ifndef PC_SDK_COMMIT
#define PC_SDK_COMMIT "desconhecido"
#endif
#ifndef PC_GIT_STATE
#define PC_GIT_STATE "desconhecido"
#endif

namespace fs = std::filesystem;

namespace pc::log {
namespace {

std::mutex g_mu;  // escrita no arquivo (cabeçalho, sink, eventos diretos)
FILE* g_file = nullptr;
std::atomic<bool> g_active{false};
fs::path g_path;
std::string g_error;
uint64_t g_bytes = 0;
bool g_capped = false;
std::string g_profile_u8;  // C:\Users\<nome> (vira %USERPROFILE%)
std::shared_ptr<spdlog::sinks::sink> g_sink;
std::chrono::steady_clock::time_point g_t0;
IDXGIAdapter3* g_adapter = nullptr;  // memória de vídeo no bloco de desempenho

std::string W2U(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string U8(const fs::path& p) { return W2U(p.wstring()); }

std::string Lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string Sanitize(std::string s) {
  if (g_profile_u8.empty()) return s;
  const std::string low = Lower(s), pat = Lower(g_profile_u8);
  std::string out;
  size_t i = 0;
  while (true) {
    const size_t j = low.find(pat, i);
    if (j == std::string::npos) break;
    out.append(s, i, j - i);
    out += "%USERPROFILE%";
    i = j + pat.size();
  }
  out.append(s, i, std::string::npos);
  return out;
}

// Chamado com g_mu travado.
void WriteLocked(const std::string& line) {
  if (!g_file || g_capped) return;
  std::string s = Sanitize(line);
  s += '\n';
  if (g_bytes + s.size() > kMaxBytes) {
    static const char kMsg[] =
        "[sessao] limite de 16 MB deste arquivo atingido; o resto da sessão fica só no log do "
        "runtime\n";
    std::fwrite(kMsg, 1, sizeof(kMsg) - 1, g_file);
    std::fflush(g_file);
    g_capped = true;
    return;
  }
  std::fwrite(s.data(), 1, s.size(), g_file);
  g_bytes += s.size();
}

void Write(const std::string& line) {
  std::lock_guard lk(g_mu);
  WriteLocked(line);
}

class SessionSink final : public spdlog::sinks::base_sink<std::mutex> {
 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    spdlog::memory_buf_t buf;
    formatter_->format(msg, buf);
    std::string s(buf.data(), buf.size());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    std::lock_guard lk(g_mu);
    WriteLocked(s);
    if (msg.level >= spdlog::level::warn && g_file) std::fflush(g_file);
  }
  void flush_() override {
    std::lock_guard lk(g_mu);
    if (g_file) std::fflush(g_file);
  }
};

// ------------------------------------------------------------------ cabeçalho
std::string Sha256File(const fs::path& p) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  std::string hex;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return "?";
  if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
    if (FILE* f = _wfopen(p.c_str(), L"rb")) {
      std::vector<unsigned char> buf(1 << 20);
      size_t n;
      while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0)
        BCryptHashData(h, buf.data(), ULONG(n), 0);
      std::fclose(f);
      unsigned char d[32];
      if (BCryptFinishHash(h, d, sizeof d, 0) == 0) {
        char x[3];
        for (unsigned char c : d) {
          std::snprintf(x, sizeof x, "%02x", c);
          hex += x;
        }
      }
    }
    BCryptDestroyHash(h);
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return hex.empty() ? "?" : hex;
}

std::string RegString(HKEY root, const wchar_t* key, const wchar_t* value) {
  wchar_t buf[512];
  DWORD size = sizeof(buf);
  if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
  return W2U(buf);
}

std::string WindowsVersion() {
  using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  RTL_OSVERSIONINFOW v{};
  v.dwOSVersionInfoSize = sizeof(v);
  if (auto fn = reinterpret_cast<RtlGetVersionFn>(
          GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))
    fn(&v);
  const wchar_t* k = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
  std::string name = RegString(HKEY_LOCAL_MACHINE, k, L"ProductName");
  const std::string disp = RegString(HKEY_LOCAL_MACHINE, k, L"DisplayVersion");
  // O registro ainda diz "Windows 10" no Windows 11; o build 22000+ é Windows 11.
  if (v.dwBuildNumber >= 22000 && name.rfind("Windows 10", 0) == 0) name.replace(8, 2, "11");
  char b[256];
  std::snprintf(b, sizeof b, "%s %s (%lu.%lu.%lu), x64", name.c_str(), disp.c_str(),
                v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
  return b;
}

std::string CpuAndMemory() {
  std::string cpu = RegString(HKEY_LOCAL_MACHINE,
                              L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                              L"ProcessorNameString");
  while (!cpu.empty() && cpu.back() == ' ') cpu.pop_back();
  SYSTEM_INFO si{};
  GetNativeSystemInfo(&si);
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  GlobalMemoryStatusEx(&ms);
  char b[384];
  std::snprintf(b, sizeof b, "%s, %lu processadores lógicos; memória %.1f GB", cpu.c_str(),
                si.dwNumberOfProcessors, double(ms.ullTotalPhys) / (1024.0 * 1024 * 1024));
  return b;
}

std::vector<std::string> Gpus() {
  std::vector<std::string> out;
  IDXGIFactory1* f = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&f))) || !f)
    return {"não foi possível listar (DXGI)"};
  IDXGIAdapter1* a = nullptr;
  for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 d{};
    a->GetDesc1(&d);
    if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      a->Release();
      continue;
    }
    LARGE_INTEGER umd{};
    char drv[64] = "?";
    if (SUCCEEDED(a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
      std::snprintf(drv, sizeof drv, "%u.%u.%u.%u", HIWORD(umd.HighPart), LOWORD(umd.HighPart),
                    HIWORD(umd.LowPart), LOWORD(umd.LowPart));
    char b[512];
    std::snprintf(b, sizeof b, "%s (fornecedor 0x%04X, dispositivo 0x%04X), vídeo %.1f GB, driver %s",
                  W2U(d.Description).c_str(), d.VendorId, d.DeviceId,
                  double(d.DedicatedVideoMemory) / (1024.0 * 1024 * 1024), drv);
    out.push_back(b);
    if (!g_adapter) a->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&g_adapter));
    a->Release();
  }
  f->Release();
  if (out.empty()) out.push_back("nenhuma placa de vídeo de hardware encontrada");
  return out;
}

BOOL CALLBACK MonitorProc(HMONITOR m, HDC, LPRECT, LPARAM p) {
  auto* out = reinterpret_cast<std::vector<std::string>*>(p);
  MONITORINFOEXW mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(m, &mi)) return TRUE;
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm);
  UINT dx = 0, dy = 0;
  using GetDpiFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
  static HMODULE shcore = LoadLibraryW(L"Shcore.dll");
  if (shcore)
    if (auto fn = reinterpret_cast<GetDpiFn>(GetProcAddress(shcore, "GetDpiForMonitor")))
      fn(m, 0 /*MDT_EFFECTIVE_DPI*/, &dx, &dy);
  char b[256];
  std::snprintf(b, sizeof b, "%s %lux%lu a %lu Hz, escala %u%%%s", W2U(mi.szDevice).c_str(),
                dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency,
                dx ? dx * 100 / 96 : 0, (mi.dwFlags & MONITORINFOF_PRIMARY) ? ", principal" : "");
  out->push_back(b);
  return TRUE;
}

std::string Cvar(const char* name) {
  const std::string v = rex::cvar::GetFlagByName(name);
  return v.empty() ? "-" : v;
}

std::string PcSettingsSummary() {
  const auto& s = pc::Get();
  std::ostringstream o;
  o << "fps60=" << s.fps60 << " lock60=" << s.lock60 << " scenes60=" << s.scenes60
    << " resolução=" << pc::ResolutionName(s.resolution) << (s.resolution_auto ? "(auto)" : "")
    << " modo=" << pc::DisplayModeName(s.display_mode)
    << " distância=" << pc::DrawDistanceName(s.draw_distance)
    << " diálogos=" << s.dialog_speed << " pausa_sem_foco=" << s.pause_on_focus_loss
    << " início_rápido=" << s.skip_intro_logos << " fps_na_tela=" << s.show_fps
    << " desempenho_na_tela=" << s.show_perf_stats << " rastreador=" << s.show_collectibles
    << " volume=" << s.volume_master << " mouse=" << s.mouse_look
    << " zona_morta=" << s.pad_deadzone << " vibração=" << s.pad_vibration
    << " backups=" << s.save_backup << "/" << s.backup_count
    << " registro_detalhado=" << s.log_detailed;
  return o.str();
}

// ------------------------------------------------------------------ desempenho
constexpr uint32_t kRing = 8192;
std::atomic<uint32_t> g_dt_us[kRing];
std::atomic<uint64_t> g_present_idx{0};
int64_t g_last_present_ns = 0;  // só a thread do jogo

struct Window {
  std::chrono::steady_clock::time_point t;
  uint64_t swaps = 0, logic = 0, present_idx = 0;
  uint64_t cpu_100ns = 0;
  int ticks = 0, scene_ticks = 0;
};
Window g_win;
std::mutex g_perf_mu;  // g_win: monitor (Tick) x fim da sessão (Stop)
uint16_t g_last_map = 0xFFFF;
uint32_t g_last_scene = 0xFFFFFFFF;
std::chrono::steady_clock::time_point g_last_flush;

uint64_t ProcessCpu100ns() {
  FILETIME c, e, k, u;
  if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
  return (uint64_t(k.dwHighDateTime) << 32 | k.dwLowDateTime) +
         (uint64_t(u.dwHighDateTime) << 32 | u.dwLowDateTime);
}

void ResetWindow() {
  g_win.t = std::chrono::steady_clock::now();
  g_win.swaps = pc::state::guest_swaps.load();
  g_win.logic = pc::state::logic_updates.load();
  g_win.present_idx = g_present_idx.load();
  g_win.cpu_100ns = ProcessCpu100ns();
  g_win.ticks = g_win.scene_ticks = 0;
}

void PerfBlock(const char* why) {
  const auto now = std::chrono::steady_clock::now();
  const double secs = std::chrono::duration<double>(now - g_win.t).count();
  if (secs < 0.5) return;
  const uint64_t swaps = pc::state::guest_swaps.load() - g_win.swaps;
  const uint64_t logic = pc::state::logic_updates.load() - g_win.logic;
  const uint64_t end = g_present_idx.load();
  uint64_t begin = g_win.present_idx;
  if (end - begin > kRing) begin = end - kRing;
  std::vector<uint32_t> dts;
  dts.reserve(size_t(end - begin));
  for (uint64_t i = begin; i < end; ++i) dts.push_back(g_dt_us[i % kRing].load());
  double mean = 0, p95 = 0, mx = 0;
  int slow = 0;
  if (!dts.empty()) {
    uint64_t sum = 0;
    for (uint32_t d : dts) {
      sum += d;
      if (d > 33400) ++slow;
    }
    mean = double(sum) / double(dts.size()) / 1000.0;
    std::vector<uint32_t> s = dts;
    std::sort(s.begin(), s.end());
    p95 = s[std::min(s.size() - 1, size_t(double(s.size()) * 0.95))] / 1000.0;
    mx = s.back() / 1000.0;
  }
  PROCESS_MEMORY_COUNTERS_EX pm{};
  pm.cb = sizeof(pm);
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm),
                       sizeof(pm));
  SYSTEM_INFO si{};
  GetNativeSystemInfo(&si);
  const uint64_t cpu = ProcessCpu100ns();
  const double cpu_pct = 100.0 * double(cpu - g_win.cpu_100ns) / (secs * 1e7) /
                         double(std::max<DWORD>(1, si.dwNumberOfProcessors));
  double vram_gb = -1;
  if (g_adapter) {
    DXGI_QUERY_VIDEO_MEMORY_INFO vi{};
    if (SUCCEEDED(g_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi)))
      vram_gb = double(vi.CurrentUsage) / (1024.0 * 1024 * 1024);
  }
  DWORD handles = 0;
  GetProcessHandleCount(GetCurrentProcess(), &handles);
  const int scene_pct = g_win.ticks ? 100 * g_win.scene_ticks / g_win.ticks : 0;
  char b[512];
  std::snprintf(b, sizeof b,
                "%s %.1f s | %.1f quadros/s | lógica %.1f/s | quadro %.2f ms (p95 %.2f, máx %.1f) "
                "| >33,4 ms: %d | em cena %d%% | mapa 0x%X | memória %.2f GB (pico %.2f) | "
                "CPU %.1f%% | vídeo %.2f GB | handles %lu",
                why, secs, double(swaps) / secs, double(logic) / secs, mean, p95, mx, slow,
                scene_pct, unsigned(g_last_map == 0xFFFF ? 0 : g_last_map),
                double(pm.PrivateUsage) / (1024.0 * 1024 * 1024),
                double(pm.PeakPagefileUsage) / (1024.0 * 1024 * 1024), cpu_pct, vram_gb, handles);
  Event("desempenho", b);
  ResetWindow();
}

uint8_t* GuestMem() {
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return nullptr;
  return ks->memory()->virtual_membase();
}

}  // namespace

bool Start(const StartInfo& info) {
  g_t0 = std::chrono::steady_clock::now();
  {
    PWSTR prof = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &prof)) && prof) {
      g_profile_u8 = W2U(prof);
      CoTaskMemFree(prof);
    }
  }
  const fs::path dir = info.user_data_root / "logs";
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec || !fs::is_directory(dir)) {
    g_error = "não foi possível criar a pasta de registros " + Sanitize(U8(dir)) +
              ": " + ec.message();
    return false;
  }
  // Retenção: só os arquivos de sessão deste formato; os kKeepSessions-1 mais novos ficam.
  {
    std::vector<fs::path> old;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
      const std::string n = e.path().filename().string();
      if (e.is_regular_file() && n.rfind("sessao-", 0) == 0 && e.path().extension() == ".txt")
        old.push_back(e.path());
    }
    std::sort(old.begin(), old.end());
    while (old.size() >= size_t(kKeepSessions)) {
      fs::remove(old.front(), ec);
      old.erase(old.begin());
    }
  }
  std::time_t tt = std::time(nullptr);
  std::tm lt{};
  localtime_s(&lt, &tt);
  char stamp[32], when[48];
  std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &lt);
  std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &lt);
  fs::path p = dir / (std::string("sessao-") + stamp + ".txt");
  for (int i = 2; fs::exists(p, ec) && i < 100; ++i)
    p = dir / (std::string("sessao-") + stamp + "-" + std::to_string(i) + ".txt");
  FILE* f = _wfopen(p.c_str(), L"wb");
  if (!f) {
    g_error = "não foi possível criar " + Sanitize(U8(p));
    return false;
  }
  g_file = f;
  g_path = p;
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  const fs::path exe_path(exe);
  std::vector<std::string> mons;
  EnumDisplayMonitors(nullptr, nullptr, MonitorProc, reinterpret_cast<LPARAM>(&mons));
  {
    std::lock_guard lk(g_mu);
    WriteLocked("=== Banjo-Tooie XBLA PT-BR Recompiled - registro da sessão ===");
    WriteLocked(std::string("início: ") + when + " | modo: " +
                (info.automated ? "automático (teste/sonda)" : "normal") + " | nível: " +
                (pc::Get().log_detailed ? "detalhado" : "normal"));
    WriteLocked("build: exe " + W2U(exe_path.filename().wstring()) + " sha256 " +
                Sha256File(exe_path) + " (" + std::to_string(fs::file_size(exe_path, ec)) +
                " bytes) | app " PC_GIT_COMMIT " (" PC_GIT_STATE ") | rexglue-sdk " PC_SDK_COMMIT
                " | compilado em " __DATE__ " " __TIME__);
    WriteLocked("windows: " + WindowsVersion());
    WriteLocked("cpu: " + CpuAndMemory());
    for (const auto& g : Gpus()) WriteLocked("gpu: " + g);
    WriteLocked("api gráfica: Direct3D 12 (runtime ReXGlue) | resolução " + Cvar("resolution") +
                ", escala interna " + Cvar("draw_resolution_scale_x") + "x" +
                Cvar("draw_resolution_scale_y") + ", vsync do monitor " + Cvar("host_vsync") +
                ", ritmo 60 Hz " + Cvar("vsync") + ", FXAA " + Cvar("swap_post_effect") +
                ", MSAA2x " + Cvar("native_2x_msaa") + ", anisotropia " +
                Cvar("anisotropic_override") + ", escala " + Cvar("present_effect"));
    for (const auto& m : mons) WriteLocked("monitor: " + m);
    WriteLocked("jogo: " + U8(info.game_data_root) + " | " + info.preflight);
    WriteLocked("dados do usuário: " + U8(info.user_data_root) + " (" +
                info.user_data_source + ")");
    WriteLocked("opções de PC: " + PcSettingsSummary());
    WriteLocked("=== eventos ===");
    std::fflush(g_file);
  }
  auto sink = std::make_shared<SessionSink>();
  sink->set_pattern("%H:%M:%S.%e [%l] [%n] %v");
  sink->set_level(pc::Get().log_detailed ? spdlog::level::debug : spdlog::level::info);
  g_sink = sink;
  rex::AddSink(g_sink);
  ResetWindow();
  g_last_flush = std::chrono::steady_clock::now();
  g_active.store(true);
  return true;
}

bool Active() { return g_active.load(); }

void SetDetailed(bool on) {
  if (g_sink) g_sink->set_level(on ? spdlog::level::debug : spdlog::level::info);
  Event("registro", on ? "nível detalhado" : "nível normal");
}
const fs::path& Path() { return g_path; }
std::string Error() { return g_error; }

void Event(const char* category, const std::string& text) {
  // vai para o log do runtime e, pelo sink, para o arquivo da sessão (mesma ordem)
  REXLOG_INFO("[sessao] {}: {}", category, text);
}

void NotePresent() {
  const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  if (g_last_present_ns) {
    const int64_t dt = (now - g_last_present_ns) / 1000;
    const uint64_t i = g_present_idx.load(std::memory_order_relaxed);
    g_dt_us[i % kRing].store(uint32_t(std::clamp<int64_t>(dt, 0, 0x7FFFFFFF)),
                             std::memory_order_relaxed);
    g_present_idx.store(i + 1, std::memory_order_release);
  }
  g_last_present_ns = now;
}

void Tick() {
  if (!g_active.load()) return;
  std::lock_guard perf(g_perf_mu);
  if (uint8_t* mem = GuestMem()) {
    const uint16_t map = uint16_t((mem[0x82673720] << 8) | mem[0x82673721]);
    const uint32_t scene = mem[0x8266EF30];
    if (map != g_last_map) {
      // usos das constantes do 60 FPS no mapa que terminou (onde o código delas roda)
      static uint64_t last_k[4] = {};
      uint64_t k[4];
      for (int i = 0; i < 4; ++i) k[i] = pc::state::k60_uses[i].load();
      if (g_last_map != 0xFFFF && (k[0] != last_k[0] || k[1] != last_k[1] ||
                                   k[2] != last_k[2] || k[3] != last_k[3])) {
        char kb[200];
        std::snprintf(kb, sizeof kb,
                      "mapa 0x%X: 0x822C8930=%llu 0x821A22xx=%llu 0x8213E2xx=%llu 0x821129B8=%llu",
                      unsigned(g_last_map), (unsigned long long)(k[0] - last_k[0]),
                      (unsigned long long)(k[1] - last_k[1]), (unsigned long long)(k[2] - last_k[2]),
                      (unsigned long long)(k[3] - last_k[3]));
        Event("k60", kb);
      }
      for (int i = 0; i < 4; ++i) last_k[i] = k[i];
      char b[64];
      if (g_last_map == 0xFFFF) std::snprintf(b, sizeof b, "0x%X", unsigned(map));
      else std::snprintf(b, sizeof b, "0x%X -> 0x%X", unsigned(g_last_map), unsigned(map));
      Event("mapa", b);
      g_last_map = map;
    }
    if (scene != g_last_scene) {
      char b[64];
      std::snprintf(b, sizeof b, "estado %u%s", scene, scene == 0 ? " (jogo livre)" : " (cena)");
      if (g_last_scene != 0xFFFFFFFF) Event("cena", b);
      g_last_scene = scene;
    }
    ++g_win.ticks;
    if (scene != 0) ++g_win.scene_ticks;
  }
  const auto now = std::chrono::steady_clock::now();
  const double period = pc::Get().log_detailed ? 5.0 : 30.0;
  if (std::chrono::duration<double>(now - g_win.t).count() >= period) PerfBlock("bloco");
  if (now - g_last_flush > std::chrono::seconds(2)) {
    g_last_flush = now;
    std::lock_guard lk(g_mu);
    if (g_file) std::fflush(g_file);
  }
}

void Stop(const std::string& reason) {
  std::lock_guard perf(g_perf_mu);
  if (!g_active.exchange(false)) return;  // uma vez só (sonda que encerra + desligamento)
  PerfBlock("final");
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
  char b[64];
  std::snprintf(b, sizeof b, " (sessão de %.1f s)", secs);
  Event("sessao", "fim: " + reason + b);
  std::lock_guard lk(g_mu);
  if (g_file) std::fflush(g_file);
  // o arquivo fica aberto: as últimas linhas do desligamento do runtime ainda entram nele
}

void Emergency(const char* text) {
  if (!g_file) return;
  // a thread que caiu pode estar com g_mu: tenta por pouco tempo e escreve mesmo assim
  bool locked = false;
  for (int i = 0; i < 50 && !(locked = g_mu.try_lock()); ++i) Sleep(1);
  std::string s = std::string("[sessao] FALHA: ") + text + "\n";
  std::fwrite(s.data(), 1, s.size(), g_file);
  std::fflush(g_file);
  if (locked) g_mu.unlock();
}

}  // namespace pc::log
