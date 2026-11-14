// Monitores (M04, etapa D: C-29 detecção do monitor certo e vários monitores; C-25 sem bordas).
//
// O jogo abre no monitor escolhido nas Configurações de PC (linha MONITOR) ou, no automático, no
// monitor onde está o cursor quando o programa começa (o da tela em que a pessoa clicou). A
// resolução automática e o tamanho inicial da janela usam ESSE monitor; antes usavam sempre o
// principal, mesmo com a janela aberta em outro. Coordenadas em pixels físicos (contexto de DPI
// por monitor só durante as chamadas), porque o processo não declara reconhecimento de DPI.
#pragma once

#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace pc::display {

struct Monitor {
  std::string device;  // \\.\DISPLAYn (UTF-8)
  int number = 0;      // 1 = principal, depois na ordem do Windows
  RECT rc_monitor{};   // físico
  RECT rc_work{};      // físico (sem a barra de tarefas)
  int width = 0, height = 0, hz = 0;  // modo atual do monitor
  bool primary = false;
};

std::vector<Monitor> List();

// Monitor em que o jogo deve abrir e por quê ("escolhido nas opções", "cursor", "principal").
Monitor Target(std::string* why = nullptr);

// Guardado na abertura (ConfigurePcLayer) e usado ao posicionar a janela.
void SetChosen(const Monitor& m);
const Monitor& Chosen();

// Monitor físico da janela (o mais próximo).
Monitor ForWindow(HWND hwnd);

// Executa fn com a thread em DPI por monitor (coordenadas físicas).
template <typename F>
auto WithPhysicalDpi(F&& fn) {
  using SetContextFn = HANDLE(WINAPI*)(HANDLE);
  static auto set_context = reinterpret_cast<SetContextFn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext"));
  HANDLE previous =
      set_context ? set_context(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4))) : nullptr;
  struct Restore {
    HANDLE p;
    ~Restore() {
      if (p && set_context) set_context(p);
    }
  } restore{previous};
  return fn();
}

}  // namespace pc::display
