// Banjo-Tooie XBLA PT-BR Recompiled - entrada roteirizada SO PARA TESTES (MISSAO 02).
//
// Com --port_test_input "<roteiro>", o runtime recebe um controle virtual (driver de entrada
// oficial do SDK, RuntimeConfig::input_factory) que aperta botoes em tempos fixos, contados do
// inicio do modulo do jogo. Serve para os testes automaticos chegarem aos menus e mostrarem o
// texto PT-BR sem acao manual. Sem a opcao, nada muda: o runtime usa os controles normais.
//
// Roteiro: itens separados por virgula, "botao@segundos" ou "botao@segundos:milissegundos"
// (duracao do aperto, padrao 200 ms). Botoes: start back a b x y up down left right lb rb ls rs.
// Exemplo: "start@12,down@16,a@18".
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rex::input {
class InputDriver;
}

namespace port {

struct TestPress {
  std::string name;
  uint16_t buttons = 0;
  double at_s = 0.0;
  double hold_ms = 200.0;
};

// Interpreta o roteiro; devolve false (e o motivo em error) se algum item for invalido.
bool ParseTestInputScript(const std::string& script, std::vector<TestPress>* out,
                          std::string* error);

// Estado compartilhado entre o driver (thread do jogo) e o app (relatorio).
struct TestInputState {
  std::vector<TestPress> presses;
  std::atomic<int64_t> origin_ns{0};     // inicio do modulo (steady_clock); 0 = ainda nao
  std::atomic<uint64_t> polls{0};        // leituras do controle feitas pelo jogo
  std::atomic<uint64_t> pressed_polls{0};  // leituras que devolveram botao apertado
  std::atomic<uint32_t> delivered_mask{0};  // bit i = aperto i foi visto pelo jogo
};

TestInputState& GetTestInputState();
std::unique_ptr<rex::input::InputDriver> CreateTestInputDriver();
std::string TestInputSummary();  // texto para o relatorio de boot

}  // namespace port
