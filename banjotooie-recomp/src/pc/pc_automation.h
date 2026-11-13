// Opções de linha de comando para verificação automática:
//   --screenshot <arquivo.png>  inicia normalmente, espera ~120 quadros do jogo, salva a imagem e sai
//   --self-test <arquivo.json>  roda o jogo e grava, a partir do estado real do jogo, quais etapas funcionam
#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace rex::ui {
class Presenter;
class Window;
class WindowedAppContext;
}  // namespace rex::ui

namespace rex::system {
class IInputSystem;
}  // namespace rex::system

namespace pc::automation {

struct Options {
  std::filesystem::path screenshot;
  std::filesystem::path self_test;
  std::filesystem::path probe_position;
  std::filesystem::path probe_game;
  std::filesystem::path probe_pause;
  std::filesystem::path probe_focus;
  std::filesystem::path probe_save;
  std::filesystem::path probe_totals;
  std::filesystem::path probe_quit;
  std::filesystem::path probe_items;
  std::filesystem::path probe_scan;
  std::filesystem::path probe_dialog;
  std::filesystem::path probe_scenes;  // linha do tempo das cenas sem entrada (ritmo a 30 x 60)
  std::filesystem::path probe_drawdistance;  // mesma cena nos 3 perfis de distância de desenho
  std::filesystem::path probe_menu;  // estado vivo do menu de pausa/totais (itens, tabelas, cursor)
  std::filesystem::path probe_titlepc;  // entrada de PC do menu de TÍTULO (1 abaixo de JOGAR)
  std::filesystem::path probe_titlemenu;  // menu principal: rótulos, entrada da recompilação e SAIR PARA O DESKTOP
  std::filesystem::path probe_fullscreen;  // MODO pelo menu do jogo durante o jogo (queda em tela cheia)
  std::filesystem::path probe_pcpages;  // Configurações de PC no menu do jogo: seções, rolagem, valor, F1
  std::filesystem::path probe_menucursor;  // memória do menu de título antes/depois de baixo/cima
  std::filesystem::path probe_gamepad;  // XInput real + navegação sintética do menu de PC
  std::filesystem::path probe_dd_maps;  // mapas visitados (título/intro/jogo) e alcances originais
  std::filesystem::path probe_dd_worlds;  // viagem por todos os mundos + perfis de distância
  std::filesystem::path probe_testpad;  // gamepad virtual SDL: caminho real de um controle físico
  std::filesystem::path probe_save_sim;  // "save avançado" simulado: bandeiras + tela Totais + rastreador
  std::filesystem::path probe_display;  // estado aplicado + capturas reais da janela/framebuffer
  // Save avançado DE VERDADE (use com --user_data_root=<pasta de teste>):
  //   --make-advanced-save  jogo novo, liga todos os colecionáveis e grava por "SALVAR E SAIR"
  //   --probe-advanced-save carrega esse save do disco pelo menu (JOGO 1) e confere tudo
  std::filesystem::path make_advanced_save;
  std::filesystem::path probe_advanced_save;
  // Segurança dos saves (M04, etapa B; use com --user_data_root=<pasta de teste>):
  //   --probe-saves <json>          o que a conferência da abertura fez, backups, estado do save
  //   --probe-saves-feed <pasta>    grava cada .dat da pasta no save, em ordem, como o jogo faria
  //   --probe-saves-choose <n>      linha RESTAURAR do menu: n x direita, A, A
  //   --probe-saves-inspect <pasta> confere cada .dat da pasta (e subpastas) com as regras do jogo
  //   --probe-saves-wait <s>        espera antes do relatório (para a vigia de backups)
  std::filesystem::path probe_saves;
  std::filesystem::path probe_saves_feed;
  std::filesystem::path probe_saves_inspect;
  int probe_saves_choose = -1;
  int probe_saves_wait = 0;
  std::filesystem::path user_data_root;  // preenchido pelo app (pasta dos saves e backups)
  bool self_test_f1 = false;  // --self-test-f1: o auto-teste também prova o F1 no jogo livre (até 120 s)
  int dialog_speed = -1;  // --dialog-speed N (0..3) força a velocidade só nesta execução
  std::string probe_display_set_resolution;
  int frames = 120;
  int measure_secs = 5;
  bool probe_display_transitions = false;
  bool probe_display_transition_input = false;
  bool probe_display_no_capture = false;
  bool active() const {
    return !screenshot.empty() || !self_test.empty() || !probe_position.empty() ||
           !probe_game.empty() || !probe_pause.empty() || !probe_focus.empty() ||
           !probe_save.empty() || !probe_totals.empty() || !probe_quit.empty() ||
           !probe_items.empty() || !probe_scan.empty() || !probe_dialog.empty() ||
           !probe_scenes.empty() || !probe_drawdistance.empty() || !probe_menu.empty() ||
           !probe_titlepc.empty() || !probe_titlemenu.empty() || !probe_pcpages.empty() || !probe_fullscreen.empty() || !probe_menucursor.empty() || !probe_gamepad.empty() || !probe_dd_maps.empty() ||
           !probe_dd_worlds.empty() || !probe_testpad.empty() || !probe_save_sim.empty() ||
           !make_advanced_save.empty() || !probe_advanced_save.empty() || !probe_display.empty() ||
           !probe_saves.empty();
  }
};

// Lê as opções da linha de comando do processo (GetCommandLineW).
Options ParseCommandLine();

// true enquanto o auto-teste/captura automática está no controle (o vigia de foco
// não deve pausar o jogo nesse caso).
bool IsRunning();

// Inicia a thread de verificação. 'presenter' fornece a imagem do jogo; a captura é feita
// na thread da interface via 'ui'. 'input_system' é o sistema de entrada do runtime
// (estado dos gamepads reais + vibração).
void Start(const Options& opts, rex::ui::WindowedAppContext* ui,
           std::function<rex::ui::Presenter*()> presenter,
           rex::ui::Window* window,
           rex::system::IInputSystem* input_system = nullptr);

}  // namespace pc::automation
