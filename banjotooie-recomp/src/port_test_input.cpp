// Banjo-Tooie XBLA PT-BR Recompiled - entrada roteirizada SO PARA TESTES (MISSAO 02).
// Ver port_test_input.h. O jogo le o controle pelo caminho normal (XamInputGetState ->
// InputSystem::GetState); este driver so responde no lugar de um controle fisico.

#include "port_test_input.h"

#include <rex/input/input.h>
#include <rex/input/input_driver.h>

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace port {

using namespace rex;
using namespace rex::input;

namespace {

uint16_t ButtonFromName(const std::string& n) {
  if (n == "start") return X_INPUT_GAMEPAD_START;
  if (n == "back") return X_INPUT_GAMEPAD_BACK;
  if (n == "a") return X_INPUT_GAMEPAD_A;
  if (n == "b") return X_INPUT_GAMEPAD_B;
  if (n == "x") return X_INPUT_GAMEPAD_X;
  if (n == "y") return X_INPUT_GAMEPAD_Y;
  if (n == "up") return X_INPUT_GAMEPAD_DPAD_UP;
  if (n == "down") return X_INPUT_GAMEPAD_DPAD_DOWN;
  if (n == "left") return X_INPUT_GAMEPAD_DPAD_LEFT;
  if (n == "right") return X_INPUT_GAMEPAD_DPAD_RIGHT;
  if (n == "lb") return X_INPUT_GAMEPAD_LEFT_SHOULDER;
  if (n == "rb") return X_INPUT_GAMEPAD_RIGHT_SHOULDER;
  if (n == "ls") return X_INPUT_GAMEPAD_LEFT_THUMB;
  if (n == "rs") return X_INPUT_GAMEPAD_RIGHT_THUMB;
  return 0;
}

std::string Trim(std::string s) {
  auto sp = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && sp(s.back())) s.pop_back();
  size_t i = 0;
  while (i < s.size() && sp(s[i])) ++i;
  return s.substr(i);
}

class ScriptedInputDriver final : public InputDriver {
 public:
  ScriptedInputDriver() : InputDriver(nullptr, 0) {}

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t, X_INPUT_CAPABILITIES* caps) override {
    if (user_index != 0) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (caps) {
      *caps = {};
      caps->type = 1;      // XINPUT_DEVTYPE_GAMEPAD
      caps->sub_type = 1;  // XINPUT_DEVSUBTYPE_GAMEPAD
      caps->gamepad.buttons = 0xFFFF;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* state) override {
    if (user_index != 0) return X_ERROR_DEVICE_NOT_CONNECTED;
    auto& st = GetTestInputState();
    const uint64_t n = ++st.polls;
    uint16_t buttons = 0;
    const int64_t origin = st.origin_ns.load(std::memory_order_acquire);
    if (origin != 0) {
      const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
      const double t_s = double(now - origin) / 1e9;
      for (size_t i = 0; i < st.presses.size(); ++i) {
        const auto& p = st.presses[i];
        if (t_s >= p.at_s && t_s < p.at_s + p.hold_ms / 1000.0) {
          buttons |= p.buttons;
          if (i < 32) st.delivered_mask.fetch_or(1u << i, std::memory_order_relaxed);
        }
      }
    }
    if (buttons) ++st.pressed_polls;
    if (state) {
      *state = {};
      state->packet_number = static_cast<uint32_t>(n);
      state->gamepad.buttons = buttons;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION*) override {
    return user_index ? X_ERROR_DEVICE_NOT_CONNECTED : X_ERROR_SUCCESS;
  }

  X_RESULT GetKeystroke(uint32_t user_index, uint32_t, X_INPUT_KEYSTROKE*) override {
    return user_index ? X_ERROR_DEVICE_NOT_CONNECTED : X_ERROR_EMPTY;
  }
};

}  // namespace

bool ParseTestInputScript(const std::string& script, std::vector<TestPress>* out,
                          std::string* error) {
  out->clear();
  size_t start = 0;
  while (start <= script.size()) {
    size_t comma = script.find(',', start);
    if (comma == std::string::npos) comma = script.size();
    std::string item = Trim(script.substr(start, comma - start));
    start = comma + 1;
    if (item.empty()) {
      if (comma >= script.size()) break;
      continue;
    }
    const size_t at = item.find('@');
    if (at == std::string::npos) {
      *error = fmt::format("item sem '@': '{}'", item);
      return false;
    }
    TestPress p;
    p.name = Trim(item.substr(0, at));
    std::transform(p.name.begin(), p.name.end(), p.name.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    p.buttons = ButtonFromName(p.name);
    if (!p.buttons) {
      *error = fmt::format("botao desconhecido: '{}'", p.name);
      return false;
    }
    std::string when = item.substr(at + 1);
    const size_t colon = when.find(':');
    char* end = nullptr;
    p.at_s = std::strtod(when.substr(0, colon).c_str(), &end);
    if (!end || *end != '\0' || p.at_s < 0) {
      *error = fmt::format("tempo invalido em '{}'", item);
      return false;
    }
    if (colon != std::string::npos) {
      p.hold_ms = std::strtod(when.substr(colon + 1).c_str(), &end);
      if (!end || *end != '\0' || p.hold_ms <= 0 || p.hold_ms > 10000) {
        *error = fmt::format("duracao invalida em '{}'", item);
        return false;
      }
    }
    out->push_back(p);
    if (comma >= script.size()) break;
  }
  if (out->empty()) {
    *error = "roteiro vazio";
    return false;
  }
  if (out->size() > 32) {
    *error = "roteiro com mais de 32 apertos";
    return false;
  }
  return true;
}

TestInputState& GetTestInputState() {
  static TestInputState state;
  return state;
}

std::unique_ptr<InputDriver> CreateTestInputDriver() {
  return std::make_unique<ScriptedInputDriver>();
}

std::string TestInputSummary() {
  auto& st = GetTestInputState();
  const uint32_t mask = st.delivered_mask.load();
  std::string items;
  for (size_t i = 0; i < st.presses.size(); ++i) {
    const auto& p = st.presses[i];
    items += fmt::format("{}{}@{:g}s={}", items.empty() ? "" : " ", p.name, p.at_s,
                         (i < 32 && (mask & (1u << i))) ? "entregue" : "NAO_ENTREGUE");
  }
  return fmt::format("leituras_do_controle={} leituras_com_botao={} apertos: {}", st.polls.load(),
                     st.pressed_polls.load(), items);
}

}  // namespace port
