// Estado de execução observado pelos ganchos (para o contador de FPS, estatísticas e o auto-teste).
#pragma once

#include <atomic>
#include <cstdint>

namespace pc::state {
inline std::atomic<uint32_t> frame_step{2};                  // último passo de quadros aplicado
inline std::atomic<uint32_t> last_draw_range_original{0};    // último alcance original visto
inline std::atomic<uint32_t> max_draw_range_original{0};     // maior alcance original já pedido
inline std::atomic<uint64_t> guest_swaps{0};                 // quadros apresentados pelo jogo
inline std::atomic<uint64_t> frame_step_calls{0};            // vezes que o jogo fixou o passo
inline std::atomic<uint64_t> draw_range_calls{0};            // consultas de alcance de desenho
inline std::atomic<uint32_t> cutscene_state{0};              // byte 0x8266EF30 (0 = jogo livre)
inline std::atomic<uint64_t> dialog_type_calls{0};           // quadros em que uma caixa de texto digitou
inline std::atomic<uint32_t> dialog_box{0};                  // última caixa de texto (ponteiro convidado)

// Entrada "CONFIGURAÇÕES DE PC" (7ª entrada, acrescentada no fim do menu de pausa; id de
// texto novo kPcMenuEntryStringId, devolvido pelo gancho GetString em pc_hooks.cpp).
inline constexpr uint32_t kPcMenuEntryStringId = 0x0F00;  // fora dos 1521 textos do jogo
inline std::atomic<uint32_t> pc_entry_label_requests{0};     // vezes que o jogo pediu o rótulo
inline std::atomic<uint32_t> pc_entry_activations{0};        // vezes que a entrada foi escolhida
inline std::atomic<uint32_t> pause_entry_added{0};           // vezes que a 7ª entrada foi acrescentada
inline std::atomic<uint32_t> pause_entry_count{0};           // entradas da página 0 depois disso
inline std::atomic<bool> menu_open_request{false};           // abrir o menu de PC (consumido pela UI)
inline std::atomic<bool> menu_close_request{false};          // fechar (auto-teste)

// Menu principal (título, antes de carregar um save): CLASSIFICAÇÕES vira
// "CONFIGURAÇÃO DA RECOMPILAÇÃO" (abre o menu de PC/F1) e VOLTAR À BIBLIOTECA DE JOGOS vira
// "SAIR PARA O DESKTOP" (fecha o programa). Ids de texto novos, fora dos 1521 do jogo.
inline constexpr uint32_t kTitleRecompEntryStringId = 0x0F01;
inline constexpr uint32_t kTitleExitEntryStringId = 0x0F02;
inline std::atomic<uint32_t> title_recomp_entry_built{0};    // vezes que o título recebeu a entrada
inline std::atomic<uint32_t> title_exit_entry_built{0};      // vezes que a saída foi renomeada
inline std::atomic<uint32_t> title_exit_activations{0};      // vezes que SAIR PARA O DESKTOP foi escolhida
inline std::atomic<bool> title_exit_armed{false};            // confirmação aberta por SAIR PARA O DESKTOP
inline std::atomic<uint32_t> title_exit_dialog_labels{0};    // título da confirmação trocado
inline std::atomic<uint32_t> title_exit_confirmed{0};        // SIM na confirmação
inline std::atomic<uint32_t> title_exit_cancelled{0};        // NÃO / voltar na confirmação
inline std::atomic<bool> exit_to_desktop_request{false};     // consumido pelo monitor do app

// Registro de pedidos de texto (id, chamador) para a sonda --probe-menu: tabela aberta de
// pares distintos; contador por par. Só diagnóstico.
constexpr int kStringLogSize = 512;
inline std::atomic<uint64_t> string_log_key[kStringLogSize];   // (lr << 32) | id, 0 = vazio
inline std::atomic<uint32_t> string_log_count[kStringLogSize];
inline std::atomic<bool> string_log_enabled{false};
inline void NoteStringRequest(uint32_t id, uint32_t lr) {
  if (id == kPcMenuEntryStringId)
    pc_entry_label_requests.fetch_add(1, std::memory_order_relaxed);
  if (!string_log_enabled.load(std::memory_order_relaxed)) return;
  const uint64_t key = (uint64_t(lr) << 32) | id;
  uint32_t h = uint32_t((key * 0x9E3779B97F4A7C15ull) >> 55) % kStringLogSize;
  for (int i = 0; i < kStringLogSize; ++i, h = (h + 1) % kStringLogSize) {
    uint64_t cur = string_log_key[h].load(std::memory_order_relaxed);
    if (cur == 0) {
      if (string_log_key[h].compare_exchange_strong(cur, key)) {
        string_log_count[h].fetch_add(1, std::memory_order_relaxed);
        return;
      }
    }
    if (cur == key) {
      string_log_count[h].fetch_add(1, std::memory_order_relaxed);
      return;
    }
  }
}

// Viagem entre mapas (sonda --probe-dd-worlds): pedido do host consumido pelo gancho de
// quadro na THREAD DO JOGO, que chama a rotina de viagem do próprio jogo (0x82248810
// com r3 = mapa, r4 = entrada). Evita tocar a memória do jogo diretamente.
inline std::atomic<uint32_t> warp_requested{0};  // (mapa << 8) | entrada quando != 0
inline std::atomic<uint32_t> warp_invoked{0};    // último pedido executado (mesma codificação)
inline std::atomic<uint32_t> warp_result{0};     // 1 = função chamada, 0 = não achou
inline std::atomic<uint32_t> warp_was_blocked{0};  // 1 = jogo ainda estava em viagem (0x826CC2B8)

// Contagem do PRÓPRIO jogo (sonda --probe-save-sim): chama 0x8223E300(tipo, mundo), a
// MESMA função que a tela "Totais" do menu de pausa usa, na thread do jogo no início do
// quadro. Devolve em r3 a soma dos itens do tipo naquele mundo (ninhos de notas valem 5,
// claves 20). O rastreador precisa bater com esses números.
inline std::atomic<uint32_t> count_requested{0};  // (tipo << 16) | id do mundo, quando != 0
inline std::atomic<uint64_t> count_result{~0ull};  // valor em r3; ~0ull = não executou
inline std::atomic<uint32_t> count_invoked{0};     // último pedido executado (mesma codificação)
}  // namespace pc::state
