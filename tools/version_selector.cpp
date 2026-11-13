// Seletor de versão do Banjo-Tooie (pedido de 2026-11-13): escolhe qual pasta do jogo o port
// abre — a traduzida (PT-BR, "Banjo Tooie BR") ou a global (inglês, "Banjo-Tooie (World)
// (XBLA)") — para comparar as duas na tela. O jogo é sempre o mesmo port nativo
// (out\native\banjotooie.exe); só muda --game_data_root. As duas versões usam o mesmo save e as
// mesmas opções, os da conta do Windows em Documentos\My Games\Banjo-Tooie XBLA (o jogo escolhe
// a pasta, pedido de 2026-11-13), para abrir o mesmo ponto nas duas. O cache de shaders é o do
// lançador do Craft (out\craft-runtime\cache).
//
// Uso: abrir o programa e clicar numa versão. Para testes sem janela:
//   BanjoTooie_Seletor.exe --auto ptbr|global [opções extras repassadas ao jogo]
// inicia a versão, espera o jogo terminar e devolve o código de saída dele.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Version {
  const wchar_t* key;       // --auto
  const wchar_t* title;     // texto grande do botão
  const wchar_t* folder;    // pasta ao lado de app\ (irmã da pasta app)
  COLORREF color;
  fs::path dir;
  bool ok = false;
};

Version g_versions[] = {
    {L"ptbr", L"Português (PT-BR)", L"Banjo Tooie BR", RGB(46, 125, 50)},
    {L"global", L"Global (inglês)", L"Banjo-Tooie (World) (XBLA)", RGB(21, 101, 192)},
};
constexpr int kCount = int(sizeof(g_versions) / sizeof(g_versions[0]));
constexpr int kFirstButtonId = 100;
constexpr UINT_PTR kTimerId = 1;

fs::path g_app;   // pasta app\ (a deste programa é app\Seletor)
fs::path g_game;  // app\out\native\banjotooie.exe
HWND g_status = nullptr;
HWND g_buttons[kCount] = {};
HFONT g_font = nullptr, g_font_big = nullptr, g_font_small = nullptr;
HANDLE g_running = nullptr;
int g_running_index = -1;
UINT g_dpi = 96;

int Px(int v) { return MulDiv(v, int(g_dpi), 96); }

// Mesma regra de aspas do lançador do Craft (tools/native_launcher.cpp).
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

bool FolderLooksLikeGame(const fs::path& dir) {
  std::error_code ec;
  return fs::is_regular_file(dir / L"default.xex", ec) &&
         fs::is_regular_file(dir / L"RAWFiles" / L"X360_strings.dat", ec);
}

void Locate() {
  wchar_t module[MAX_PATH * 8];
  const DWORD n = GetModuleFileNameW(nullptr, module, DWORD(std::size(module)));
  g_app = fs::path(std::wstring(module, n)).parent_path().parent_path();
  g_game = g_app / L"out" / L"native" / L"banjotooie.exe";
  for (auto& v : g_versions) {
    v.dir = g_app.parent_path() / v.folder;
    v.ok = FolderLooksLikeGame(v.dir);
  }
}

// Inicia o jogo com a pasta da versão. 'extra' vai no fim da linha de comando.
HANDLE Start(const Version& v, const std::vector<std::wstring>& extra, DWORD* error) {
  std::wstring cmd = Quote(g_game.wstring());
  const std::vector<std::wstring> args = {
      L"--game_data_root", v.dir.wstring(),
      L"--cache_path", (g_app / L"out" / L"craft-runtime" / L"cache").wstring()};
  for (const auto& a : args) cmd += L" " + Quote(a);
  for (const auto& a : extra) cmd += L" " + Quote(a);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(g_game.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      g_app.c_str(), &si, &pi)) {
    if (error) *error = GetLastError();
    return nullptr;
  }
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

void SetStatus(const std::wstring& text) {
  if (g_status) SetWindowTextW(g_status, text.c_str());
}

void Launch(HWND hwnd, int index) {
  const Version& v = g_versions[index];
  if (g_running && WaitForSingleObject(g_running, 0) == WAIT_TIMEOUT) {
    MessageBoxW(hwnd,
                L"Uma versão do jogo já está aberta. Feche o jogo antes de iniciar a outra "
                L"(as duas usam os mesmos saves e configurações).",
                L"Banjo-Tooie", MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (!fs::exists(g_game)) {
    MessageBoxW(hwnd, (L"O jogo ainda não foi compilado:\n" + g_game.wstring()).c_str(),
                L"Banjo-Tooie", MB_OK | MB_ICONWARNING);
    return;
  }
  DWORD error = 0;
  HANDLE h = Start(v, {}, &error);
  if (!h) {
    SetStatus(L"Não foi possível iniciar (erro do Windows " + std::to_wstring(error) + L").");
    return;
  }
  if (g_running) CloseHandle(g_running);
  g_running = h;
  g_running_index = index;
  SetStatus(std::wstring(L"Em execução: ") + v.title + L". Feche o jogo para escolher de novo.");
}

void PollRunning() {
  if (!g_running || WaitForSingleObject(g_running, 0) == WAIT_TIMEOUT) return;
  DWORD code = 0;
  GetExitCodeProcess(g_running, &code);
  CloseHandle(g_running);
  g_running = nullptr;
  SetStatus(std::wstring(L"Pronto. A última versão aberta foi ") +
            g_versions[g_running_index].title + L" (código de saída " + std::to_wstring(code) +
            L").");
}

HFONT MakeFont(int pt, int weight) {
  return CreateFontW(-MulDiv(pt, int(g_dpi), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void DrawButton(const DRAWITEMSTRUCT* d) {
  const int index = int(d->CtlID) - kFirstButtonId;
  if (index < 0 || index >= kCount) return;
  const Version& v = g_versions[index];
  const bool pressed = (d->itemState & ODS_SELECTED) != 0;
  const bool focus = (d->itemState & ODS_FOCUS) != 0;
  RECT r = d->rcItem;
  COLORREF fill = v.ok ? v.color : RGB(150, 150, 150);
  if (pressed)
    fill = RGB(GetRValue(fill) * 3 / 4, GetGValue(fill) * 3 / 4, GetBValue(fill) * 3 / 4);
  HBRUSH b = CreateSolidBrush(fill);
  HPEN p = CreatePen(PS_SOLID, focus ? Px(3) : 1, focus ? RGB(255, 210, 60) : fill);
  HGDIOBJ ob = SelectObject(d->hDC, b), op = SelectObject(d->hDC, p);
  RoundRect(d->hDC, r.left, r.top, r.right, r.bottom, Px(14), Px(14));
  SelectObject(d->hDC, ob);
  SelectObject(d->hDC, op);
  DeleteObject(b);
  DeleteObject(p);
  SetBkMode(d->hDC, TRANSPARENT);
  SetTextColor(d->hDC, RGB(255, 255, 255));
  RECT t = r;
  t.top += Px(14);
  t.bottom = t.top + Px(34);
  SelectObject(d->hDC, g_font_big);
  DrawTextW(d->hDC, v.title, -1, &t, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
  RECT s = r;
  s.top = t.bottom;
  s.left += Px(8);
  s.right -= Px(8);
  SelectObject(d->hDC, g_font_small);
  const std::wstring sub = v.ok ? std::wstring(L"pasta “") + v.folder + L"”"
                                : std::wstring(L"pasta “") + v.folder + L"” não encontrada";
  DrawTextW(d->hDC, sub.c_str(), -1, &s, DT_CENTER | DT_WORDBREAK);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_font = MakeFont(10, FW_NORMAL);
      g_font_big = MakeFont(14, FW_SEMIBOLD);
      g_font_small = MakeFont(9, FW_NORMAL);
      HWND head = CreateWindowExW(0, L"STATIC", L"Qual versão do Banjo-Tooie você quer abrir?",
                                  WS_CHILD | WS_VISIBLE, Px(24), Px(18), Px(520), Px(26), hwnd,
                                  nullptr, nullptr, nullptr);
      SendMessageW(head, WM_SETFONT, WPARAM(g_font), TRUE);
      for (int i = 0; i < kCount; ++i) {
        g_buttons[i] = CreateWindowExW(
            0, L"BUTTON", g_versions[i].title,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | (g_versions[i].ok ? 0 : WS_DISABLED),
            Px(24 + i * 268), Px(56), Px(252), Px(116), hwnd,
            HMENU(INT_PTR(kFirstButtonId + i)), nullptr, nullptr);
      }
      HWND hint = CreateWindowExW(
          0, L"STATIC",
          L"O jogo é o mesmo port; só muda a pasta de dados. As duas versões usam o seu save e "
          L"as suas configurações (Documentos\\My Games\\Banjo-Tooie XBLA desta conta do Windows), "
          L"para comparar o mesmo ponto do jogo.",
          WS_CHILD | WS_VISIBLE, Px(24), Px(182), Px(520), Px(52), hwnd, nullptr, nullptr, nullptr);
      SendMessageW(hint, WM_SETFONT, WPARAM(g_font_small), TRUE);
      g_status = CreateWindowExW(0, L"STATIC", L"Pronto.", WS_CHILD | WS_VISIBLE, Px(24), Px(242),
                                 Px(520), Px(40), hwnd, nullptr, nullptr, nullptr);
      SendMessageW(g_status, WM_SETFONT, WPARAM(g_font), TRUE);
      if (!fs::exists(g_game)) SetStatus(L"Atenção: o jogo ainda não foi compilado (out\\native).");
      for (int i = 0; i < kCount; ++i)
        if (g_versions[i].ok) {
          SetFocus(g_buttons[i]);
          break;
        }
      SetTimer(hwnd, kTimerId, 500, nullptr);
      return 0;
    }
    case WM_DRAWITEM:
      DrawButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
      return TRUE;
    case WM_COMMAND: {
      const int index = int(LOWORD(wp)) - kFirstButtonId;
      if (index >= 0 && index < kCount && HIWORD(wp) == BN_CLICKED) Launch(hwnd, index);
      return 0;
    }
    case WM_TIMER:
      PollRunning();
      return 0;
    case WM_CTLCOLORSTATIC:
      SetBkMode(HDC(wp), TRANSPARENT);
      return LRESULT(GetSysColorBrush(COLOR_WINDOW));
    case WM_DESTROY:
      KillTimer(hwnd, kTimerId);
      if (g_running) CloseHandle(g_running);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void SetDpiAwareness() {
  using Fn = BOOL(WINAPI*)(HANDLE);
  if (HMODULE u = GetModuleHandleW(L"user32.dll"))
    if (auto f = reinterpret_cast<Fn>(GetProcAddress(u, "SetProcessDpiAwarenessContext")))
      f(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));  // PER_MONITOR_AWARE_V2
  using GetDpi = UINT(WINAPI*)();
  if (HMODULE u = GetModuleHandleW(L"user32.dll"))
    if (auto f = reinterpret_cast<GetDpi>(GetProcAddress(u, "GetDpiForSystem"))) g_dpi = f();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  Locate();
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  // Modo de teste: --auto ptbr|global [extras] (sem janela; devolve o código do jogo).
  if (argv && argc >= 3 && std::wcscmp(argv[1], L"--auto") == 0) {
    int index = -1;
    for (int i = 0; i < kCount; ++i)
      if (std::wcscmp(argv[2], g_versions[i].key) == 0) index = i;
    std::vector<std::wstring> extra;
    for (int i = 3; i < argc; ++i) extra.emplace_back(argv[i]);
    LocalFree(argv);
    if (index < 0) return 2;
    if (!g_versions[index].ok || !fs::exists(g_game)) return 3;
    HANDLE h = Start(g_versions[index], extra, nullptr);
    if (!h) return 4;
    WaitForSingleObject(h, INFINITE);
    DWORD code = 5;
    GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return int(code);
  }
  if (argv) LocalFree(argv);

  SetDpiAwareness();
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
  wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  wc.hIcon = LoadIconW(inst, L"MAINICON");
  wc.hIconSm = wc.hIcon;
  wc.lpszClassName = L"BanjoTooieSeletorDeVersao";
  RegisterClassExW(&wc);
  RECT r{0, 0, Px(568), Px(292)};
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&r, style, FALSE);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Banjo-Tooie — escolher versão", style,
                              CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                              nullptr, nullptr, inst, nullptr);
  if (!hwnd) return 1;
  ShowWindow(hwnd, show);
  UpdateWindow(hwnd);
  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    if (IsDialogMessageW(hwnd, &m)) continue;
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  return int(m.wParam);
}
