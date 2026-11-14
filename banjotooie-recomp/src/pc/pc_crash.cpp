// Registro de falhas do port PT-BR (ver pc_crash.h).

#include "pc_log.h"
#include "pc_crash.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <rex/logging.h>
#include <rex/system/function_dispatcher.h>

namespace pc::crash {
namespace {

// Faixa de código do default.xex PT-BR (log do runtime: "code=820A0000-825F39C8"); inclui a
// rotina de acentos acrescentada pela tradução em 0x825F3940.
constexpr uint32_t kCodeBegin = 0x820A0000;
constexpr uint32_t kCodeEnd = 0x825F39C8;
// Maior distância plausível entre a entrada de uma função recompilada e um endereço dentro dela.
constexpr uintptr_t kMaxFunctionSpan = 0x80000;

std::vector<std::pair<uintptr_t, uint32_t>> g_map;  // (endereço host, endereço do jogo), ordenado
std::wstring g_folder;
LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;
std::atomic<bool> g_reported{false};

const char* ExceptionName(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "acesso inválido à memória";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "instrução ilegal";
    case EXCEPTION_STACK_OVERFLOW: return "estouro de pilha";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "divisão inteira por zero";
    case EXCEPTION_PRIV_INSTRUCTION: return "instrução privilegiada";
    case EXCEPTION_IN_PAGE_ERROR: return "erro de página";
    case 0xC0000409: return "verificação de pilha (fail-fast)";
    default: return "exceção";
  }
}

void Append(std::string& out, const char* fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n > 0) out.append(buf, size_t(std::min(n, int(sizeof(buf) - 1))));
}

LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* info) {
  if (g_reported.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;
  const EXCEPTION_RECORD* rec = info->ExceptionRecord;
  SYSTEMTIME now{};
  GetLocalTime(&now);
  std::string text;
  Append(text, "Banjo-Tooie PT-BR — relatório de falha\r\n");
  Append(text, "Data: %04u-%02u-%02u %02u:%02u:%02u\r\n", now.wYear, now.wMonth, now.wDay,
         now.wHour, now.wMinute, now.wSecond);
  Append(text, "Tipo: %s (0x%08lX)\r\n", ExceptionName(rec->ExceptionCode),
         static_cast<unsigned long>(rec->ExceptionCode));
  Append(text, "Endereço da instrução: 0x%016llX\r\n",
         static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(rec->ExceptionAddress)));
  if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
    const uintptr_t fault = rec->ExceptionInformation[1];
    Append(text, "Acesso: %s em 0x%016llX\r\n",
           rec->ExceptionInformation[0] == 1 ? "escrita" : rec->ExceptionInformation[0] == 8
                                                             ? "execução"
                                                             : "leitura",
           static_cast<unsigned long long>(fault));
    // A memória do jogo é mapeada a partir de 4 GiB no processo (endereço do jogo = host - 4 GiB).
    if (fault >= 0x100000000ull && fault < 0x200000000ull)
      Append(text, "  = endereço do jogo 0x%08X\r\n", static_cast<uint32_t>(fault - 0x100000000ull));
  }
  Append(text, "Thread: %lu\r\nPilha (mais interna primeiro):\r\n",
         static_cast<unsigned long>(GetCurrentThreadId()));
  CONTEXT ctx = *info->ContextRecord;
  for (int frame = 0; frame < 48 && ctx.Rip; ++frame) {
    const uintptr_t pc = ctx.Rip;
    if (const uint32_t guest = GuestFunctionFor(pc)) {
      Append(text, "  #%-2d sub_%08X (função do jogo, host 0x%016llX)\r\n", frame, guest,
             static_cast<unsigned long long>(pc));
    } else {
      HMODULE module = nullptr;
      char name[MAX_PATH] = "?";
      if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCSTR>(pc), &module) &&
          module) {
        char full[MAX_PATH];
        if (GetModuleFileNameA(module, full, MAX_PATH)) {
          const char* slash = strrchr(full, '\\');
          snprintf(name, sizeof(name), "%s", slash ? slash + 1 : full);
        }
      }
      Append(text, "  #%-2d %s+0x%llX\r\n", frame, name,
             static_cast<unsigned long long>(pc - reinterpret_cast<uintptr_t>(module)));
    }
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image_base, nullptr);
    if (!fn) {
      // função folha: o endereço de retorno está no topo da pilha
      if (!ctx.Rsp || IsBadReadPtr(reinterpret_cast<void*>(ctx.Rsp), 8)) break;
      ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
      ctx.Rsp += 8;
      continue;
    }
    void* handler_data = nullptr;
    DWORD64 establisher = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, ctx.Rip, fn, &ctx, &handler_data, &establisher,
                     nullptr);
  }
  Append(text, "\r\nEnvie este arquivo e a pasta logs ao relatar o problema.\r\n");

  wchar_t file[MAX_PATH * 2];
  swprintf(file, MAX_PATH * 2, L"%s\\crash-%04u%02u%02u-%02u%02u%02u.txt", g_folder.c_str(),
           now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
  CreateDirectoryW(g_folder.c_str(), nullptr);
  HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    static const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    WriteFile(h, bom, sizeof(bom), &written, nullptr);
    WriteFile(h, text.data(), DWORD(text.size()), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
  }
  OutputDebugStringA(text.c_str());
  // Registro de sessão (M04, C-10): o relatório inteiro também vai para logs\sessao-*.txt.
  pc::log::Emergency(text.c_str());
  return g_previous ? g_previous(info) : EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

uint32_t GuestFunctionFor(uintptr_t host_pc) {
  auto it = std::upper_bound(g_map.begin(), g_map.end(), std::make_pair(host_pc, UINT32_MAX));
  if (it == g_map.begin()) return 0;
  --it;
  if (host_pc - it->first > kMaxFunctionSpan) return 0;
  return it->second;
}

void Install(rex::runtime::FunctionDispatcher* dispatcher, const std::filesystem::path& folder) {
  g_folder = folder.wstring();
  g_map.clear();
  if (dispatcher) {
    for (uint32_t a = kCodeBegin; a < kCodeEnd; a += 4) {
      if (auto* fn = dispatcher->GetFunction(a))
        g_map.emplace_back(reinterpret_cast<uintptr_t>(fn), a);
    }
    std::sort(g_map.begin(), g_map.end());
  }
  g_previous = SetUnhandledExceptionFilter(OnUnhandled);
  REXLOG_INFO("Registro de falhas: {} funções do jogo mapeadas; relatórios em {}", g_map.size(),
              folder.string());
}

}  // namespace pc::crash
