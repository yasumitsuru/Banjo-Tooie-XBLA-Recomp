// "CONFIGURAÇÕES DE PC" dentro do menu do PRÓPRIO jogo (pedido de 2026-11-12).
//
// As opções que antes ficavam numa janela sobreposta (F1) agora são páginas do motor de menus
// do jogo: mesma fonte, cursor, animação de entrada letra a letra, rolagem e avisos de botão.
// O vetor de páginas da tela de menus tem 12 posições e o jogo não monta a 7 — é nela que
// ficam a raiz (GRÁFICOS, JOGO, CONTROLE, TECLADO E MOUSE, ÁUDIO E SAVES) e cada seção.
// Engenharia reversa e endereços: MECHANICS.md §18; valores em mechanics.json (pc.ingame_menu).
#pragma once

#include <cstdint>

namespace pc::ingame {

// F1 (qualquer thread): abre as páginas de PC. No título/pausa vai direto para elas; durante o
// jogo livre abre a pausa (toque de START) e depois vai para elas. Já nelas, fecha.
void RequestToggle();

// Thread do jogo, início de cada quadro (PcHook_FrameStart): executa o pedido do F1.
void FrameTick();

// Chamado pelo gancho do montador de páginas (0x822DF2D0, título e pausa): guarda o layout que o
// jogo usa nesta tela e monta a raiz da página 7. 'scale'/'delay' são os bits f32.
void OnPagesBuilt(uint32_t screen, int32_t y0, int32_t step, int32_t x, float scale, float delay);

// A numa linha de uma seção (ação 0x822DEC40 na página 7). true = tratado aqui.
bool OnSelect(uint32_t screen);

// Página do jogo usada (7).
uint32_t PageIndex();

// Diagnóstico (auto-teste e sondas).
struct Status {
  uint32_t built = 0;         // vezes que a página 7 foi montada
  int32_t view = -2;          // -1 = raiz, 0.. = seção, -2 = ainda não montada
  uint32_t rows = 0;          // linhas da vista atual
  uint32_t switches = 0;      // vezes que o jogo entrou na página 7
  uint32_t changes = 0;       // valores trocados por esquerda/direita/A
  uint32_t refreshes = 0;     // linhas reescritas na tela
  uint32_t f1_jumps = 0;      // aberturas pelo F1 a partir de outra página
  uint32_t f1_start_taps = 0; // F1 durante o jogo livre (START injetado)
  uint32_t ticks = 0;         // quadros tratados pelo gancho da página
};
Status GetStatus();

}  // namespace pc::ingame
