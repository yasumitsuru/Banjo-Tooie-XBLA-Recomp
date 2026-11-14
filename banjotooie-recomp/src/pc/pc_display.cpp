// Monitores (M04, C-29 e C-25). Ver pc_display.h.
#include "pc_display.h"

#include <algorithm>

#include "pc_settings.h"

namespace pc::display {
namespace {

Monitor g_chosen;

std::string W2U(const wchar_t* w) {
  if (!w || !*w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

Monitor Describe(HMONITOR h) {
  Monitor m;
  MONITORINFOEXW mi{};
  mi.cbSize = sizeof(mi);
  if (!h || !GetMonitorInfoW(h, &mi)) return m;
  m.device = W2U(mi.szDevice);
  m.rc_monitor = mi.rcMonitor;
  m.rc_work = mi.rcWork;
  m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
    m.width = int(dm.dmPelsWidth);
    m.height = int(dm.dmPelsHeight);
    m.hz = int(dm.dmDisplayFrequency);
  } else {
    m.width = mi.rcMonitor.right - mi.rcMonitor.left;
    m.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
  }
  return m;
}

BOOL CALLBACK Collect(HMONITOR h, HDC, LPRECT, LPARAM p) {
  reinterpret_cast<std::vector<Monitor>*>(p)->push_back(Describe(h));
  return TRUE;
}

}  // namespace

std::vector<Monitor> List() {
  return WithPhysicalDpi([] {
    std::vector<Monitor> v;
    EnumDisplayMonitors(nullptr, nullptr, Collect, reinterpret_cast<LPARAM>(&v));
    // número estável para a tela: principal = 1, depois na ordem do Windows
    std::stable_sort(v.begin(), v.end(),
                     [](const Monitor& a, const Monitor& b) { return a.primary && !b.primary; });
    for (size_t i = 0; i < v.size(); ++i) v[i].number = int(i) + 1;
    return v;
  });
}

Monitor Target(std::string* why) {
  const auto all = List();
  const std::string& want = pc::Get().monitor;
  if (!want.empty()) {
    for (const auto& m : all)
      if (m.device == want) {
        if (why) *why = "escolhido nas opções";
        return m;
      }
  }
  Monitor cur = WithPhysicalDpi([] {
    POINT pt{};
    GetCursorPos(&pt);
    return Describe(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY));
  });
  for (const auto& m : all)
    if (m.device == cur.device) {
      if (why)
        *why = want.empty() ? "automático: monitor do cursor"
                            : "o escolhido (" + want + ") não está ligado; monitor do cursor";
      return m;
    }
  for (const auto& m : all)
    if (m.primary) {
      if (why) *why = "principal";
      return m;
    }
  if (why) *why = "nenhum monitor listado";
  return all.empty() ? Monitor{} : all.front();
}

void SetChosen(const Monitor& m) { g_chosen = m; }
const Monitor& Chosen() { return g_chosen; }

Monitor ForWindow(HWND hwnd) {
  return WithPhysicalDpi([hwnd] {
    Monitor m = Describe(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
    for (const auto& x : List())
      if (x.device == m.device) m.number = x.number;
    return m;
  });
}

}  // namespace pc::display
