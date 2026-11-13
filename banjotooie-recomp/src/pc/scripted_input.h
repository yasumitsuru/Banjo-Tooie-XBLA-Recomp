// Entrada simulada e câmera por mouse do port (auto-teste e Configurações de PC).
//
// O runtime lê o controle do jogo por drivers (SDL/XInput/teclado e mouse). Este driver
// entra na mesma lista e responde quando:
//  - a automação está ativa (--self-test), com botões roteirizados; ou
//  - a câmera por mouse está ligada, fornecendo o analógico direito.
// Fora disso devolve "controle não conectado" e não interfere em nada.
#pragma once

#include <cstdint>

namespace rex::system {
class IInputSystem;
}

namespace pc::input {

// Estado do controle virtual. Pode ser escrito de qualquer thread.
struct PadState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;
};

PadState& Get();

// Liga/desliga o controle virtual de automação (desligado = driver não conectado).
void SetActive(bool active);
bool Active();

// Câmera por mouse/teclado: captura o cursor e fornece o analógico direito.
void SetMouseLook(bool enabled);
bool MouseLook();
void SetCaptureEnabled(bool enabled);
bool CaptureEnabled();

// Injeta um toque de botão por alguns quadros (ex.: pausar ao perder o foco).
void RequestTap(uint16_t button);
// Ainda há um toque pendente (o driver ainda o entrega ao jogo).
bool TapPending();

// Cria o driver para ser adicionado ao InputSystem do runtime.
void Install(rex::system::IInputSystem* system);

// Botões no formato X_INPUT_GAMEPAD_* (o driver converte para o estado do runtime).
enum : uint16_t {
  kDpadUp = 0x0001,
  kDpadDown = 0x0002,
  kDpadLeft = 0x0004,
  kDpadRight = 0x0008,
  kStart = 0x0010,
  kBack = 0x0020,
  kLeftThumb = 0x0040,
  kRightThumb = 0x0080,
  kLeftShoulder = 0x0100,
  kRightShoulder = 0x0200,
  kA = 0x1000,
  kB = 0x2000,
  kX = 0x4000,
  kY = 0x8000,
};

// Define um botão (down=true pressiona).
void SetButton(uint16_t button, bool down);
void ClearButtons();

}  // namespace pc::input
