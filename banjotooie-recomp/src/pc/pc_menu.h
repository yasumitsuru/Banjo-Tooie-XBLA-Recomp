// Menu "Configurações de PC" (F1) e contador de FPS opcional.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace rex::ui {
class ImGuiDrawer;
class Window;
}  // namespace rex::ui

namespace rex::input {
class InputSystem;
}  // namespace rex::input

namespace pc::menu {

// Cria o menu e o contador de FPS, registra a tecla F1. 'runtime_config' é o arquivo de
// configurações do runtime (gráficos/entrada), também separado dos saves.
void Create(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
            const std::filesystem::path& runtime_config);
// Define o sistema de entrada do runtime: o menu passa a ler o estado FUNDIDO dos
// gamepads reais (XInput/SDL) por ele, cobrindo qualquer controle que o SDL reconheça.
void SetInputSystem(rex::input::InputSystem* system);
void Destroy();
void Open();  // abre o menu (também via --open-pc-menu na linha de comando)
bool IsOpen();
// O jogo não deve receber entrada (menu aberto, ou logo depois de fechar com B/Start).
bool BlocksGameInput();

// Aplica o modo de exibição salvo (janela / sem bordas / tela cheia) depois que a janela existe.
void ApplyDisplayMode(rex::ui::Window* window);

// ---------------------------------------------------------------- diagnóstico de gamepad
// Estado de um controle XInput (mesmo layout do XINPUT_GAMEPAD, sem o número do pacote).
struct GamepadState {
  uint16_t buttons;
  uint8_t left_trigger, right_trigger;
  int16_t lx, ly, rx, ry;
};

struct XInputDiag {
  bool loaded = false;   // XInput carregado de alguma DLL do sistema
  int connected = -1;    // 1 = conectado, 0 = não conectado, -1 = DLL ausente
  uint32_t error = 0;    // código de retorno do XInputGetState (ERROR_DEVICE_NOT_CONNECTED…)
  uint16_t buttons = 0;  // botões apertados agora (só com controle conectado)
};

// Um quadro do menu com o controle sintético ativo (registrado na thread da interface).
struct GamepadTraceEntry {
  uint64_t swaps = 0;    // quadros apresentados pelo jogo naquele momento
  int tab = -1;          // aba selecionada do menu de PC
  int nav_active = 0;    // navegação do ImGui ativa naquele quadro
  int any_active = 0;    // algum item ativo (foco de navegação em cima dele)
  uint32_t nav_id = 0;   // id do item com foco de navegação (0 = nenhum)
  uint32_t active_id = 0;  // id do item ativo (0 = nenhum)
  uint32_t pin_id = 0;   // id da caixa "60 FPS" (alvo do teste de foco)
  int dpad_down = 0;     // tecla do direcional para baixo abaixada neste quadro
  int move_dir = 0;      // direção de navegação pedida (1 cima, 2 direita, 3 baixo, 4 esquerda)
  int menu_open = 0;     // menu ainda aberto
};

// Estado do XInput real nos 4 slots (0 = não conectado inclui ERROR_DEVICE_NOT_CONNECTED).
std::array<XInputDiag, 4> QueryXInput();

// Teste sintético: enquanto um estado está definido, o menu usa ele no lugar do XInput real,
// pelo MESMO caminho (FeedGamepad -> ImGui). nullptr volta ao controle real. Só diagnóstico.
void SetSyntheticGamepad(const GamepadState* state);
std::vector<GamepadTraceEntry> TakeGamepadTrace();  // copia e limpa o rastro
int CurrentTab();                                   // aba selecionada no menu de PC
// Coloca o foco de navegação no primeiro controle da aba Jogo ("60 FPS") no próximo quadro,
// para a sonda medir direcional + A de ponta a ponta a partir de um ponto conhecido.
void SetGamepadTestFocus(bool set);

}  // namespace pc::menu
