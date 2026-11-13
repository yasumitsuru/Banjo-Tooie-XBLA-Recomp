// Idioma dos textos que o jogo carregou (pedido de 2026-11-13: a versão global, em inglês,
// roda no mesmo port que a PT-BR). A camada de PC escreve os próprios rótulos (entradas novas
// nos menus, páginas das Configurações de PC) no idioma do jogo.
//
// Lido da tabela do próprio jogo: GetString (0x822E9C28) devolve
// *([0x826A60E8] + id*4) quando id < [0x826A60F0] (ev_79cdb272e32e). O texto 0 é "JOGAR" na
// pasta traduzida e "PLAY GAME" na global (X360_strings.dat, bloco 0).
#pragma once

#include <cstdint>
#include <string>

namespace pc::lang {

// true = jogo em inglês. Enquanto o jogo não carregou os textos, devolve false (PT-BR) sem
// guardar a decisão.
bool English();
// true depois que o idioma foi decidido pelos textos do jogo.
bool Known();
// Texto do jogo (Latin-1, como no X360_strings.dat); "" se ainda não carregado ou fora da tabela.
std::string GameString(uint32_t id);
// Escolhe o rótulo do idioma do jogo.
inline const char* T(const char* pt, const char* en) { return English() ? en : pt; }

}  // namespace pc::lang
