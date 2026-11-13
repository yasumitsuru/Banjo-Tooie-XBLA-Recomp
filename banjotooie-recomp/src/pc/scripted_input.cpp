#include "scripted_input.h"

#include <rex/cvar.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/system/interfaces/input.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <queue>
#include <utility>

#include "pc_settings.h"

namespace pc::input {
namespace {

using namespace rex;
using namespace rex::input;

PadState g_pad;
std::atomic<bool> g_active{false};
std::atomic<bool> g_mouse_look{false};
std::atomic<bool> g_capture{false};
std::atomic<uint32_t> g_packet{0};
std::atomic<uint16_t> g_tap_button{0};
std::atomic<int> g_tap_left{0};

std::mutex g_queue_mutex;
std::queue<X_INPUT_KEYSTROKE> g_queue;

HWND g_hwnd = nullptr;
bool g_cursor_hidden = false;
float g_filter_rx = 0.0f, g_filter_ry = 0.0f;
POINT g_last_cursor{};

void EnqueueKeystroke(uint16_t vk_pad, bool down) {
  X_INPUT_KEYSTROKE ks = {};
  ks.virtual_key = vk_pad;
  ks.flags = down ? X_INPUT_KEYSTROKE_KEYDOWN : X_INPUT_KEYSTROKE_KEYUP;
  ks.user_index = 0;
  std::lock_guard lock(g_queue_mutex);
  g_queue.push(ks);
}

constexpr uint32_t kMenuFlagAddr = 0x8266EF55;

bool PauseMenuOpen() {
  auto* state = rex::system::kernel_state();
  if (!state || !state->memory()) return false;
  const uint8_t* p = state->memory()->virtual_membase();
  return p && p[kMenuFlagAddr] == 1;
}

void ShowGameCursor(bool show) {
  if (!g_hwnd) return;
  if (show && g_cursor_hidden) {
    ShowCursor(TRUE);
    g_cursor_hidden = false;
  } else if (!show && !g_cursor_hidden) {
    ShowCursor(FALSE);
    g_cursor_hidden = true;
  }
}

void CenterCursor() {
  if (!g_hwnd) return;
  RECT rc{};
  GetClientRect(g_hwnd, &rc);
  POINT pt{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
  ClientToScreen(g_hwnd, &pt);
  SetCursorPos(pt.x, pt.y);
  g_last_cursor = pt;
}

// Nome de tecla das Configurações de PC -> código virtual.
int VkFromName(const std::string& name) {
  if (name.empty()) return 0;
  const auto vk = rex::ui::ParseVirtualKey(name);
  return vk == rex::ui::VirtualKey::kNone ? 0 : static_cast<int>(vk);
}

bool KeyDown(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; }

class PcInputDriver final : public InputDriver {
 public:
  PcInputDriver() : InputDriver(nullptr, 0) {}

  void SetWindow(rex::ui::Window* window) {
    window_ = window;
    if (window) g_hwnd = reinterpret_cast<HWND>(window->GetNativeWindowHandle());
    if (g_hwnd) {
      RECT rc{};
      GetClientRect(g_hwnd, &rc);
      POINT pt{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
      ClientToScreen(g_hwnd, &pt);
      g_last_cursor = pt;
    }
  }

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override {
    (void)flags;
    if (!IsOn() || user_index != 0) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out_caps) {
      std::memset(out_caps, 0, sizeof(*out_caps));
      out_caps->type = 0x01;
      out_caps->sub_type = 0x01;
      out_caps->gamepad.buttons = 0xFFFF;
      out_caps->gamepad.left_trigger = 0xFF;
      out_caps->gamepad.right_trigger = 0xFF;
      out_caps->gamepad.thumb_lx = 0x7FFF;
      out_caps->gamepad.thumb_ly = 0x7FFF;
      out_caps->gamepad.thumb_rx = 0x7FFF;
      out_caps->gamepad.thumb_ry = 0x7FFF;
      out_caps->vibration.left_motor_speed = 0xFFFF;
      out_caps->vibration.right_motor_speed = 0xFFFF;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override {
    if (!IsOn() || user_index != 0) return X_ERROR_DEVICE_NOT_CONNECTED;
    PadState p = g_pad;
    // toque pendente (pausar ao perder foco)
    if (g_tap_left.load() > 0) {
      p.buttons = static_cast<uint16_t>(p.buttons | g_tap_button.load());
      g_tap_left.fetch_sub(1);
    }
    if (g_mouse_look.load()) {
      UpdateCamera(&p);
    }
    if (out_state) {
      out_state->packet_number = ++g_packet;
      out_state->gamepad.buttons = p.buttons;
      out_state->gamepad.left_trigger = p.left_trigger;
      out_state->gamepad.right_trigger = p.right_trigger;
      out_state->gamepad.thumb_lx = p.thumb_lx;
      out_state->gamepad.thumb_ly = p.thumb_ly;
      out_state->gamepad.thumb_rx = p.thumb_rx;
      out_state->gamepad.thumb_ry = p.thumb_ry;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override {
    (void)vibration;
    if (!IsOn() || user_index != 0) return X_ERROR_DEVICE_NOT_CONNECTED;
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override {
    (void)flags;
    if (!IsOn() || user_index != 0) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(g_queue_mutex);
    if (g_queue.empty()) return X_ERROR_EMPTY;
    if (out_keystroke) *out_keystroke = g_queue.front();
    g_queue.pop();
    return X_ERROR_SUCCESS;
  }

 private:
  bool IsOn() const {
    return g_active.load() || g_mouse_look.load() || g_tap_left.load() > 0;
  }

  void UpdateCamera(PadState* p) {
    const auto& s = pc::Get();
    float target_rx = 0.0f, target_ry = 0.0f;
    const bool focused = g_hwnd && GetForegroundWindow() == g_hwnd;
    if (g_capture.load() && focused) {
      if (PauseMenuOpen()) {
        ShowGameCursor(true);  // no menu de pausa o cursor é útil/clicável
      } else {
        ShowGameCursor(false);
        POINT pt{};
        if (GetCursorPos(&pt)) {
          RECT rc{};
          GetClientRect(g_hwnd, &rc);
          POINT c{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
          ClientToScreen(g_hwnd, &c);
          const float dx = float(pt.x - c.x);
          const float dy = float(pt.y - c.y);
          if (dx != 0.0f || dy != 0.0f) {
            target_rx += dx * 220.0f;
            target_ry -= dy * 220.0f;
            SetCursorPos(c.x, c.y);
          }
        }
      }
    } else {
      ShowGameCursor(true);
    }
    // câmera por teclado (teclas remapeáveis)
    if (focused) {
      if (KeyDown(VkFromName(s.cam_key_left))) target_rx -= 23000.0f;
      if (KeyDown(VkFromName(s.cam_key_right))) target_rx += 23000.0f;
      if (KeyDown(VkFromName(s.cam_key_up))) target_ry += 23000.0f;
      if (KeyDown(VkFromName(s.cam_key_down))) target_ry -= 23000.0f;
    }
    target_rx *= s.cam_sens_x * (s.cam_invert_x ? -1.0f : 1.0f);
    target_ry *= s.cam_sens_y * (s.cam_invert_y ? -1.0f : 1.0f);
    // suavização (baixa passagem): 0 = direto
    const float alpha = 1.0f - std::clamp(s.cam_smoothing, 0.0f, 0.9f);
    g_filter_rx += (target_rx - g_filter_rx) * alpha;
    g_filter_ry += (target_ry - g_filter_ry) * alpha;
    auto clamp16 = [](float v) -> int16_t {
      if (v > 32767.0f) v = 32767.0f;
      if (v < -32767.0f) v = -32767.0f;
      return static_cast<int16_t>(v);
    };
    if (std::abs(g_filter_rx) > std::abs(float(p->thumb_rx))) p->thumb_rx = clamp16(g_filter_rx);
    if (std::abs(g_filter_ry) > std::abs(float(p->thumb_ry))) p->thumb_ry = clamp16(g_filter_ry);
  }

  rex::ui::Window* window_ = nullptr;
};

constexpr std::pair<uint16_t, uint16_t> kButtonVk[] = {
    {kDpadUp, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadDpadUp)},
    {kDpadDown, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadDpadDown)},
    {kDpadLeft, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadDpadLeft)},
    {kDpadRight, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadDpadRight)},
    {kStart, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadStart)},
    {kBack, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadBack)},
    {kA, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadA)},
    {kB, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadB)},
    {kX, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadX)},
    {kY, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadY)},
    {kLeftShoulder, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadLShoulder)},
    {kRightShoulder, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadRShoulder)},
};

}  // namespace

PadState& Get() { return g_pad; }

void SetActive(bool active) { g_active.store(active); }
bool Active() { return g_active.load(); }

void SetMouseLook(bool enabled) { g_mouse_look.store(enabled); }
bool MouseLook() { return g_mouse_look.load(); }

void SetCaptureEnabled(bool enabled) {
  g_capture.store(enabled);
  if (enabled) {
    CenterCursor();
  } else {
    ShowGameCursor(true);
  }
}
bool CaptureEnabled() { return g_capture.load(); }

void RequestTap(uint16_t button) {
  g_tap_button.store(button);
  g_tap_left.store(10);
}

bool TapPending() { return g_tap_left.load() > 0; }

void Install(rex::system::IInputSystem* system) {
  static bool installed = false;
  if (installed || !system) return;
  installed = true;
  auto* input = static_cast<rex::input::InputSystem*>(system);
  auto driver = std::make_unique<PcInputDriver>();
  driver->SetWindow(input->window());
  input->AddDriver(std::move(driver));
}

void SetButton(uint16_t button, bool down) {
  const uint16_t before = g_pad.buttons;
  if (down)
    g_pad.buttons |= button;
  else
    g_pad.buttons &= ~button;
  if ((before ^ g_pad.buttons) & button) {
    for (const auto& [mask, vk] : kButtonVk) {
      if (mask == button) {
        EnqueueKeystroke(vk, down);
        break;
      }
    }
  }
}

void ClearButtons() { g_pad.buttons = 0; }

}  // namespace pc::input
