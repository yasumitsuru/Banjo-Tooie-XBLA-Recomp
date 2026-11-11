// Preparação de shaders da placa de vídeo (ver pc_shaders.h).

#include "pc_shaders.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <dxgi1_6.h>

#include <rex/logging.h>

namespace pc::shaders {
namespace fs = std::filesystem;

namespace {

constexpr const char* kTitle = "58410955";

std::string Escape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o.push_back('\\');
    if (static_cast<unsigned char>(c) < 0x20) continue;
    o.push_back(c);
  }
  return o;
}

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

struct Adapter {
  std::string name;
  std::string driver;
  uint32_t vendor = 0, device = 0;
  uint64_t vram_mb = 0;
};

Adapter QueryAdapter() {
  Adapter a;
  IDXGIFactory6* f6 = nullptr;
  IDXGIFactory1* f1 = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&f1))) || !f1)
    return a;
  IDXGIAdapter1* ad = nullptr;
  if (SUCCEEDED(f1->QueryInterface(__uuidof(IDXGIFactory6), reinterpret_cast<void**>(&f6))) && f6) {
    f6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1),
                                   reinterpret_cast<void**>(&ad));
    f6->Release();
  }
  if (!ad) f1->EnumAdapters1(0, &ad);
  if (ad) {
    DXGI_ADAPTER_DESC1 d{};
    if (SUCCEEDED(ad->GetDesc1(&d))) {
      a.name = Utf8(d.Description);
      a.vendor = d.VendorId;
      a.device = d.DeviceId;
      a.vram_mb = uint64_t(d.DedicatedVideoMemory) / (1024 * 1024);
    }
    LARGE_INTEGER umd{};
    if (SUCCEEDED(ad->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
      const uint64_t v = uint64_t(umd.QuadPart);
      std::ostringstream s;
      s << (v >> 48) << '.' << ((v >> 32) & 0xFFFF) << '.' << ((v >> 16) & 0xFFFF) << '.'
        << (v & 0xFFFF);
      a.driver = s.str();
    }
    ad->Release();
  }
  f1->Release();
  return a;
}

uint64_t ProcessMilliseconds() {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
  FILETIME now{};
  GetSystemTimeAsFileTime(&now);
  const auto as64 = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
  return (as64(now) - as64(created)) / 10000;
}

}  // namespace

fs::path RequestedReport() {
  fs::path out;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i + 1 < argc; ++i) {
    if (std::wstring(argv[i]) == L"--prepare-shaders") {
      out = argv[i + 1];
      break;
    }
  }
  if (argv) LocalFree(argv);
  return out;
}

int InstallPack(const fs::path& exe_dir, const fs::path& cache_root) {
  std::error_code ec;
  const fs::path pack = exe_dir / "shaderpack";
  if (cache_root.empty() || !fs::is_directory(pack, ec)) return 0;
  const fs::path dest = cache_root / "shaders" / "shareable";
  int copied = 0;
  for (const auto& entry : fs::directory_iterator(pack, ec)) {
    if (!entry.is_regular_file()) continue;
    const std::string name = entry.path().filename().string();
    if (name.rfind(kTitle, 0) != 0) continue;  // só arquivos deste jogo
    const fs::path target = dest / name;
    if (fs::exists(target, ec)) continue;  // nunca substitui o cache do jogador
    fs::create_directories(dest, ec);
    const fs::path tmp = target.string() + ".tmp";
    if (fs::copy_file(entry.path(), tmp, fs::copy_options::overwrite_existing, ec) && !ec) {
      fs::rename(tmp, target, ec);
      if (!ec) {
        ++copied;
        REXLOG_INFO("Pacote de shaders: {} copiado para o cache", name);
      } else {
        fs::remove(tmp, ec);
      }
    }
  }
  return copied;
}

void FinishPreparation(const fs::path& report, const fs::path& cache_root, int pack_copied) {
  std::error_code ec;
  const Adapter a = QueryAdapter();
  const fs::path dir = cache_root / "shaders" / "shareable";
  std::ostringstream j;
  j << "{\n  \"version\": 1,\n  \"ok\": true,\n  \"title\": \"" << kTitle << "\",\n";
  j << "  \"api\": \"d3d12\",\n";
  j << "  \"adapter\": \"" << Escape(a.name) << "\",\n";
  j << "  \"vendor\": " << a.vendor << ",\n  \"device\": " << a.device << ",\n";
  j << "  \"driver\": \"" << a.driver << "\",\n  \"vram_mb\": " << a.vram_mb << ",\n";
  j << "  \"pack_files_copied\": " << pack_copied << ",\n";
  j << "  \"process_ms\": " << ProcessMilliseconds() << ",\n";
  j << "  \"cache\": \"" << Escape(Utf8(dir.wstring())) << "\",\n  \"files\": [";
  bool first = true;
  uint64_t total = 0;
  if (fs::is_directory(dir, ec)) {
    for (const auto& e : fs::directory_iterator(dir, ec)) {
      if (!e.is_regular_file()) continue;
      const auto size = e.file_size(ec);
      total += size;
      j << (first ? "\n" : ",\n") << "    {\"name\": \"" << Escape(e.path().filename().string())
        << "\", \"bytes\": " << size << "}";
      first = false;
    }
  }
  j << (first ? "]" : "\n  ]") << ",\n  \"total_bytes\": " << total << "\n}\n";
  const std::string text = j.str();
  if (!report.empty()) {
    if (report.has_parent_path()) fs::create_directories(report.parent_path(), ec);
    const fs::path tmp = report.string() + ".tmp";
    {
      std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
      o << text;
    }
    fs::rename(tmp, report, ec);
    if (ec) {
      fs::remove(report, ec);
      fs::rename(tmp, report, ec);
    }
  }
  REXLOG_INFO("Preparação de shaders concluída: {} bytes no cache ({}), relatório {}", total,
              a.name, report.string());
  Sleep(300);  // deixa o log chegar ao arquivo
  // Sai antes do código do jogo rodar: nada além do relatório e do cache é gravado.
  TerminateProcess(GetCurrentProcess(), 0);
  for (;;) Sleep(1000);
}

}  // namespace pc::shaders
