// Banjo-Tooie XBLA PT-BR Recompiled - diagnostico de inicializacao (MISSAO 01).
// Ver port_diagnostics.h.

#include "port_diagnostics.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

namespace port {

namespace fs = std::filesystem;

namespace {

const auto kProcessStart = std::chrono::steady_clock::now();

struct Expected {
  const char* rel;
  const char* role;
  const char* note;
};

// Lista tirada do proprio XEX (strings em .rdata) e da auditoria 00:
//  - os .cmp/.tbl/.ctl sao validados/carregados no boot (0x8227D340, 0x8227D0C0);
//  - X360_strings.dat, a fonte, os .ptc e o pacote 58410955033F0001 sao citados pelo XEX, mas nao
//    foi verificado se a falta deles impede o boot: ficam como "recomendado";
//  - db360_TexFileList.txt e opcional (Ghidra: sem ela o jogo usa db360.textures.cmp).
constexpr Expected kExpected[] = {
    {"default.xex", "obrigatorio", "executavel original do XBLA (XEX2)"},
    {"RAWFiles\\db360.cmp", "obrigatorio", "banco de dados do jogo (inclui o texto)"},
    {"RAWFiles\\db360.textures.cmp", "obrigatorio", "pacote de texturas"},
    {"RAWFiles\\banjo2_360.tbl", "obrigatorio", "tabela de dados"},
    {"RAWFiles\\banjo2_360.ctl", "obrigatorio", "indice da tabela de dados"},
    {"RAWFiles\\X360_strings.dat", "recomendado", "textos de sistema do XBLA"},
    {"RAWFiles\\xarialuni.ttf", "recomendado", "fonte Unicode"},
    {"RAWFiles\\ArcadeNormal.ptc", "recomendado", "layout de tela 4:3"},
    {"RAWFiles\\ArcadeWide.ptc", "recomendado", "layout de tela 16:9"},
    {"RAWFiles\\58410955033F0001", "recomendado", "pacote PIRS do tema do painel"},
    {"RAWFiles\\db360_TexFileList.txt", "opcional",
     "lista de texturas soltas; sem ela o jogo usa db360.textures.cmp (0x8227D0C0)"},
};

std::string FileNameOnly(const char* rel) {
  std::string s(rel);
  auto p = s.find_last_of('\\');
  return p == std::string::npos ? s : s.substr(p + 1);
}

uint32_t Crc32(const uint8_t* data, size_t len, uint32_t crc = 0) {
  static std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void PutBe32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(uint8_t(x >> 24));
  v.push_back(uint8_t(x >> 16));
  v.push_back(uint8_t(x >> 8));
  v.push_back(uint8_t(x));
}

void PutChunk(std::ofstream& out, const char* type, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> buf;
  PutBe32(buf, uint32_t(payload.size()));
  size_t type_at = buf.size();
  buf.insert(buf.end(), type, type + 4);
  buf.insert(buf.end(), payload.begin(), payload.end());
  uint32_t crc = Crc32(buf.data() + type_at, buf.size() - type_at);
  PutBe32(buf, crc);
  out.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info);
fs::path g_dump_dir;

}  // namespace

std::string JsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char b[8];
          std::snprintf(b, sizeof(b), "\\u%04x", c);
          o += b;
        } else {
          o += char(c);
        }
    }
  }
  return o;
}

std::string NowIso8601() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &t);
  char buf[40];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
  return buf;
}

// ---------------------------------------------------------------------------
// Preflight
// ---------------------------------------------------------------------------

PreflightResult RunPreflight(const fs::path& game_root) {
  PreflightResult r;
  r.game_root = game_root;
  std::error_code ec;
  r.root_ok = !game_root.empty() && fs::is_directory(game_root, ec);
  for (const auto& e : kExpected) {
    PreflightFile f;
    f.rel = e.rel;
    f.role = e.role;
    f.note = e.note;
    if (r.root_ok) {
      // O jogo le de RAWFiles\ (confirmado no Xenia); o launcher da base tambem copia para a raiz.
      std::string candidates[2] = {e.rel, FileNameOnly(e.rel)};
      for (const auto& c : candidates) {
        fs::path p = game_root / fs::path(c);
        if (fs::is_regular_file(p, ec)) {
          f.found_at = c;
          f.size = fs::file_size(p, ec);
          break;
        }
      }
    }
    bool present = !f.found_at.empty() && f.size > 0;
    if (!present) {
      if (f.role == "obrigatorio") r.missing_required.push_back(f.rel);
      if (f.role == "recomendado") r.missing_recommended.push_back(f.rel);
    }
    r.files.push_back(std::move(f));
  }
  if (r.root_ok) {
    std::ifstream xex(game_root / "default.xex", std::ios::binary);
    char magic[4] = {};
    if (xex.read(magic, 4)) r.xex_magic_ok = std::string(magic, 4) == "XEX2";
  }
  return r;
}

std::string PreflightResult::SummaryPtBr() const {
  std::ostringstream s;
  if (game_root.empty()) {
    s << "A pasta do jogo não foi informada.\n\n"
         "Inicie com: banjotooie.exe --game_data_root \"<pasta com default.xex e RAWFiles>\"";
    return s.str();
  }
  if (!root_ok) {
    s << "A pasta do jogo não existe ou não pode ser lida:\n" << game_root.string();
    return s.str();
  }
  if (passed()) {
    s << "Pasta do jogo OK: " << game_root.string();
    if (!missing_recommended.empty()) {
      s << "\nAusentes (recomendados): ";
      for (size_t i = 0; i < missing_recommended.size(); ++i)
        s << (i ? ", " : "") << missing_recommended[i];
    }
    return s.str();
  }
  s << "Faltam arquivos do jogo na pasta:\n" << game_root.string() << "\n\n";
  for (const auto& m : missing_required) s << "  - " << m << "\n";
  if (!xex_magic_ok && std::find(missing_required.begin(), missing_required.end(),
                                 std::string("default.xex")) == missing_required.end()) {
    s << "  - default.xex não é um executável XEX2 válido\n";
  }
  s << "\nUse a pasta extraída do seu próprio pacote XBLA do Banjo-Tooie (default.xex + RAWFiles).\n"
       "Nenhum arquivo do jogo é distribuído com este port.";
  return s.str();
}

// ---------------------------------------------------------------------------
// BootReport
// ---------------------------------------------------------------------------

struct BootReport::Impl {
  mutable std::mutex mu;
  std::string started_at = NowIso8601();
  std::map<std::string, std::string> info;
  std::string preflight_json = "null";
  struct StageRow {
    std::string name;
    double t_ms;
    std::string detail;
  };
  std::vector<StageRow> stages;
  uint64_t frames = 0;
  double fps = 0;
  double elapsed_s = 0;
  std::string screenshot_json = "null";
  std::string crash_json = "null";
  std::string exit_json = "null";
};

BootReport::BootReport() : impl_(new Impl) {}

BootReport& BootReport::Get() {
  static BootReport* r = new BootReport();  // nunca destruido: usado ate pelo filtro de falhas
  return *r;
}

double BootReport::ElapsedMs() const {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                   kProcessStart)
      .count();
}

void BootReport::SetPath(const fs::path& path) {
  std::lock_guard lock(impl_->mu);
  path_ = path;
}

void BootReport::SetInfo(const std::string& key, const std::string& value) {
  {
    std::lock_guard lock(impl_->mu);
    impl_->info[key] = value;
  }
  Flush();
}

void BootReport::SetPreflight(const PreflightResult& r) {
  std::ostringstream j;
  j << "{\"game_root\":\"" << JsonEscape(r.game_root.string()) << "\",\"root_ok\":"
    << (r.root_ok ? "true" : "false") << ",\"xex_magic_ok\":" << (r.xex_magic_ok ? "true" : "false")
    << ",\"passed\":" << (r.passed() ? "true" : "false") << ",\"files\":[";
  for (size_t i = 0; i < r.files.size(); ++i) {
    const auto& f = r.files[i];
    j << (i ? "," : "") << "{\"rel\":\"" << JsonEscape(f.rel) << "\",\"role\":\"" << f.role
      << "\",\"present\":" << (!f.found_at.empty() && f.size ? "true" : "false")
      << ",\"found_at\":\"" << JsonEscape(f.found_at) << "\",\"size\":" << f.size
      << ",\"note\":\"" << JsonEscape(f.note) << "\"}";
  }
  j << "],\"missing_required\":[";
  for (size_t i = 0; i < r.missing_required.size(); ++i)
    j << (i ? "," : "") << "\"" << JsonEscape(r.missing_required[i]) << "\"";
  j << "],\"missing_recommended\":[";
  for (size_t i = 0; i < r.missing_recommended.size(); ++i)
    j << (i ? "," : "") << "\"" << JsonEscape(r.missing_recommended[i]) << "\"";
  j << "]}";
  {
    std::lock_guard lock(impl_->mu);
    impl_->preflight_json = j.str();
  }
  Flush();
}

void BootReport::Stage(const std::string& name, const std::string& detail) {
  {
    std::lock_guard lock(impl_->mu);
    impl_->stages.push_back({name, ElapsedMs(), detail});
  }
  Flush();
}

void BootReport::SetFrames(uint64_t frames, double fps, double elapsed_s) {
  {
    std::lock_guard lock(impl_->mu);
    impl_->frames = frames;
    impl_->fps = fps;
    impl_->elapsed_s = elapsed_s;
  }
  Flush();
}

void BootReport::SetScreenshot(const std::string& path, uint32_t width, uint32_t height, bool ok,
                               const std::string& detail) {
  std::ostringstream j;
  j << "{\"path\":\"" << JsonEscape(path) << "\",\"width\":" << width << ",\"height\":" << height
    << ",\"ok\":" << (ok ? "true" : "false") << ",\"detail\":\"" << JsonEscape(detail) << "\"}";
  {
    std::lock_guard lock(impl_->mu);
    impl_->screenshot_json = j.str();
  }
  Flush();
}

void BootReport::SetCrash(const std::string& text) {
  {
    std::lock_guard lock(impl_->mu);
    impl_->crash_json = "\"" + JsonEscape(text) + "\"";
  }
  Flush();
}

void BootReport::SetExit(int code, const std::string& reason) {
  std::ostringstream j;
  j << "{\"code\":" << code << ",\"reason\":\"" << JsonEscape(reason) << "\"}";
  {
    std::lock_guard lock(impl_->mu);
    impl_->exit_json = j.str();
  }
  Flush();
}

std::string BootReport::ToJson() const {
  std::ostringstream j;
  j << "{\n  \"schema\": 1,\n  \"app\": \"banjotooie (Banjo-Tooie XBLA PT-BR Recompiled)\",\n"
    << "  \"started_at\": \"" << impl_->started_at << "\",\n"
    << "  \"updated_at\": \"" << NowIso8601() << "\",\n  \"info\": {";
  bool first = true;
  for (const auto& [k, v] : impl_->info) {
    j << (first ? "" : ",") << "\n    \"" << JsonEscape(k) << "\": \"" << JsonEscape(v) << "\"";
    first = false;
  }
  j << "\n  },\n  \"preflight\": " << impl_->preflight_json << ",\n  \"stages\": [";
  for (size_t i = 0; i < impl_->stages.size(); ++i) {
    const auto& s = impl_->stages[i];
    char t[32];
    std::snprintf(t, sizeof(t), "%.1f", s.t_ms);
    j << (i ? "," : "") << "\n    {\"name\": \"" << JsonEscape(s.name) << "\", \"t_ms\": " << t
      << ", \"detail\": \"" << JsonEscape(s.detail) << "\"}";
  }
  char fps[32], el[32];
  std::snprintf(fps, sizeof(fps), "%.2f", impl_->fps);
  std::snprintf(el, sizeof(el), "%.2f", impl_->elapsed_s);
  j << "\n  ],\n  \"guest\": {\"frames\": " << impl_->frames << ", \"fps\": " << fps
    << ", \"elapsed_s\": " << el << ", \"counter\": \"chamadas de 0x822FA358 (VdSwap)\"},\n"
    << "  \"screenshot\": " << impl_->screenshot_json << ",\n  \"crash\": " << impl_->crash_json
    << ",\n  \"exit\": " << impl_->exit_json << "\n}\n";
  return j.str();
}

void BootReport::Flush() {
  std::string text;
  fs::path path;
  {
    std::lock_guard lock(impl_->mu);
    if (path_.empty()) return;
    path = path_;
    text = ToJson();
  }
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  fs::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(text.data(), std::streamsize(text.size()));
  }
  fs::rename(tmp, path, ec);  // troca atomica (MoveFileEx com REPLACE_EXISTING)
}

// ---------------------------------------------------------------------------
// Falhas nao tratadas
// ---------------------------------------------------------------------------

namespace {
LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
  const auto* rec = info ? info->ExceptionRecord : nullptr;
  uint32_t code = rec ? rec->ExceptionCode : 0;
  void* addr = rec ? rec->ExceptionAddress : nullptr;
  char module_name[MAX_PATH] = "?";
  uintptr_t offset = 0;
  HMODULE mod = nullptr;
  if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 static_cast<LPCSTR>(addr), &mod)) {
    GetModuleFileNameA(mod, module_name, MAX_PATH);
    offset = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(mod);
  }
  char text[MAX_PATH + 160];
  std::snprintf(text, sizeof(text), "excecao 0x%08X em %p (%s+0x%llX), thread %lu", code, addr,
                module_name, static_cast<unsigned long long>(offset), GetCurrentThreadId());
  std::string dump_note;
  if (!g_dump_dir.empty()) {
    std::error_code ec;
    fs::create_directories(g_dump_dir, ec);
    char name[64];
    std::snprintf(name, sizeof(name), "banjotooie-crash-%lu.dmp", GetCurrentProcessId());
    fs::path dump = g_dump_dir / name;
    HANDLE f = CreateFileW(dump.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
      BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                                  MiniDumpWithThreadInfo, info ? &mei : nullptr, nullptr, nullptr);
      CloseHandle(f);
      dump_note = ok ? (" minidump: " + dump.string()) : " (minidump falhou)";
    }
  }
  std::string full = std::string(text) + dump_note;
  std::fprintf(stderr, "[banjotooie] FALHA: %s\n", full.c_str());
  BootReport::Get().SetCrash(full);
  BootReport::Get().Stage("crash", full);
  return EXCEPTION_EXECUTE_HANDLER;  // encerra com o codigo da excecao
}
}  // namespace

void InstallCrashHandler(const fs::path& dump_dir) {
  g_dump_dir = dump_dir;
  SetUnhandledExceptionFilter(CrashFilter);
}

// ---------------------------------------------------------------------------
// PNG
// ---------------------------------------------------------------------------

bool WritePngRgbx(const fs::path& path, const uint8_t* data, uint32_t width, uint32_t height,
                  size_t stride) {
  if (!data || !width || !height) return false;
  std::error_code ec;
  if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  out.write(reinterpret_cast<const char*>(sig), 8);
  std::vector<uint8_t> ihdr;
  PutBe32(ihdr, width);
  PutBe32(ihdr, height);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 bits, RGB, deflate, filtro 0, sem entrelace
  PutChunk(out, "IHDR", ihdr);

  // Linhas sem filtro (byte 0) + RGB; zlib com blocos "stored" (sem compressao).
  std::vector<uint8_t> raw;
  raw.reserve(size_t(height) * (1 + size_t(width) * 3));
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    const uint8_t* row = data + size_t(y) * stride;
    for (uint32_t x = 0; x < width; ++x) {
      raw.push_back(row[x * 4 + 0]);
      raw.push_back(row[x * 4 + 1]);
      raw.push_back(row[x * 4 + 2]);
    }
  }
  std::vector<uint8_t> z;
  z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
  z.push_back(0x78);
  z.push_back(0x01);
  size_t pos = 0;
  do {
    size_t n = std::min<size_t>(65535, raw.size() - pos);
    bool last = pos + n >= raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(uint8_t(n));
    z.push_back(uint8_t(n >> 8));
    z.push_back(uint8_t(~n));
    z.push_back(uint8_t(~n >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  } while (pos < raw.size());
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  PutBe32(z, (b << 16) | a);
  PutChunk(out, "IDAT", z);
  PutChunk(out, "IEND", {});
  return bool(out);
}

void ShowErrorPtBr(const std::string& text) {
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), w.data(), n);
  MessageBoxW(nullptr, w.c_str(), L"Banjo-Tooie", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

std::atomic<uint64_t>& GuestPresentCount() {
  static std::atomic<uint64_t> count{0};
  return count;
}

}  // namespace port
