// Progresso por mundo (Jiggies, Jinjos, favos, páginas Cheato, glowbos, notas) lido da
// memória do próprio jogo, do mesmo jeito que a tela "Totais" do menu de pausa conta.
//
// Origem (engenharia reversa do default.xex PT-BR, ver docs/PORTING.md):
//  - 0x82269768 lê uma bandeira de progresso; bandeiras 0x29..0x588 ficam no bloco apontado
//    por 0x8267513C, bit (f - 0x28) com o bit menos significativo primeiro.
//  - 0x8223D928 diz se o item n (1..N) do tipo k foi pego: bandeira = base[k] + n - 1, com
//    base[k] em 0x8262DA38.
//  - 0x8223E300 conta os itens de um tipo num mundo usando o byte de mundo de cada item
//    (tabelas a partir de 0x8262D678); 0x8223D6B0 dá a quantidade de cada tipo.
//  - Notas: ninho = 5, clave = 20 (tabela 0x826223B8 via 0x821DA658).
#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace pc::progress {

enum Kind : int { kJinjo = 0, kJiggy, kHoneycomb, kGlowbo, kCheato, kNotes, kKindCount };

constexpr int kWorldCount = 9;  // 8 mundos + Ilha O' Hags

struct WorldTotals {
  const char* name;
  std::array<uint16_t, kKindCount> got{};
  std::array<uint16_t, kKindCount> total{};
};

struct Snapshot {
  bool valid = false;  // falso antes de o jogo carregar o bloco de bandeiras
  std::array<WorldTotals, kWorldCount> worlds{};
  std::array<uint16_t, kKindCount> got{};
  std::array<uint16_t, kKindCount> total{};
};

const char* KindName(Kind k);

// Lê o estado atual. Seguro de chamar de qualquer thread (só leitura da memória do jogo).
Snapshot Read();

// Só para o auto-teste: inverte por um instante a bandeira do item n do tipo k, lê o
// rastreador, desfaz a inversão e devolve quanto a contagem do mundo do item mudou
// (+1/-1 esperado). world_out recebe o índice do mundo do item.
int ProbeItemFlag(Kind k, int n, int* world_out);

// -------------------------------------------------- simulação de "save avançado" (sonda)
// Lista os itens (tipo, n) cujo mundo (byte da tabela) está em world_ids. Só leitura.
std::vector<std::pair<int, int>> ItemsInWorlds(const std::vector<uint8_t>& world_ids);

// Liga (set=true) ou desliga a bandeira do item n do tipo k e devolve o valor anterior
// (0 ou 1); -1 se o item não tem mundo conhecido ou a memória não está pronta. É a mesma
// bandeira que a tela "Totais" do jogo e o rastreador leem; o jogo nunca grava o save
// sozinho por causa disso (a sonda desfaz tudo em seguida).
int SetItemFlag(Kind k, int n, bool set);

// Mundo do item (índice em kWorlds) ou -1. Só leitura, para conferências da sonda.
int ItemWorld(Kind k, int n);

// Pontos que o item vale no rastreador (ninho de notas 5, clave 20, demais 1).
int ItemValue(Kind k, int n);

// Endereço e tamanho do bloco de bandeiras 0x29..0x588 (para despejo antes/depois).
// Devolve false se a memória do jogo não está pronta.
bool FlagBlockRange(uint32_t* addr_out, size_t* len_out);

}  // namespace pc::progress
