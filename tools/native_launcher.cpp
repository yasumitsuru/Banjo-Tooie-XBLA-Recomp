// Craft's packaged entry point. The game remains the real CMake-built native port.
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {
// Quote one Windows argv element, including trailing backslashes and embedded quotes.
std::wstring Quote(const std::wstring& arg) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (const wchar_t c : arg) {
    if (c == L'\\') {
      ++slashes;
      continue;
    }
    result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    result += c;
    slashes = 0;
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}

bool HasOption(const std::vector<std::wstring>& args, std::wstring_view option) {
  for (const auto& arg : args) {
    if (arg == option || (arg.starts_with(option) && arg.size() > option.size() &&
                          arg[option.size()] == L'=')) return true;
  }
  return false;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  wchar_t module[MAX_PATH * 128];
  const DWORD length = GetModuleFileNameW(nullptr, module, DWORD(std::size(module)));
  if (!length || length >= std::size(module)) return 2;
  const auto app = std::filesystem::path(std::wstring(module, length)).parent_path().parent_path();
  const auto game = app / L"out" / L"native" / L"banjotooie.exe";

  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return 2;
  std::vector<std::wstring> args;
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
  LocalFree(argv);

  const auto add_default = [&](std::wstring_view option, const std::filesystem::path& value) {
    if (!HasOption(args, option)) {
      args.emplace_back(option);
      args.emplace_back(value.wstring());
    }
  };
  add_default(L"--game_data_root", app.parent_path() / L"Banjo Tooie BR");
  // Keep checker runs separate from the person's ordinary saves and shader cache.
  add_default(L"--user_data_root", app / L"out" / L"craft-runtime" / L"user");
  add_default(L"--cache_path", app / L"out" / L"craft-runtime" / L"cache");

  std::wstring command = Quote(game.wstring());
  for (const auto& arg : args) command += L" " + Quote(arg);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(game.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                      nullptr, app.c_str(), &startup, &process)) {
    std::fprintf(stderr, "Native game launch failed (Windows error %lu). Run npm run build first.\n",
                 GetLastError());
    return 3;
  }
  CloseHandle(process.hThread);
  const DWORD wait = WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 4;
  if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  return static_cast<int>(code);
}
