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
  // Normal launches pass no --user_data_root: the game keeps saves and PC settings in the current
  // Windows account's Documents\My Games\Banjo-Tooie XBLA (requested 2026-11-13; the game copies
  // the old out\craft-runtime\user save there once, see src/pc/pc_userdir.h).
  // Check runs (--self-test, --screenshot, --probe-*, --make-advanced-save) never touch the
  // person's saves and PC settings: they get their own folder, emptied first, so every check
  // starts a new game. Found 2026-11-13 (Craft check 15): the self-test had loaded the person's
  // save, the game wrote it back, and the FPS-counter toggle of the pcmenu step stayed "on".
  bool check_run = false;
  for (const auto& arg : args) {
    for (std::wstring_view flag : {std::wstring_view(L"--self-test"), std::wstring_view(L"--screenshot"),
                                   std::wstring_view(L"--probe"), std::wstring_view(L"--make-advanced-save")}) {
      if (arg.starts_with(flag)) check_run = true;
    }
  }
  if (check_run && !HasOption(args, L"--user_data_root")) {
    const auto check_user = app / L"out" / L"craft-check" / L"user";
    std::error_code ec;
    std::filesystem::remove_all(check_user, ec);  // only this scratch folder, inside app\out
    std::filesystem::create_directories(check_user, ec);
    args.emplace_back(L"--user_data_root");
    args.emplace_back(check_user.wstring());
  }
  // The shader cache is not personal data; checks and accounts share it (faster start).
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
