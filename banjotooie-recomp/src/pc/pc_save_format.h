// Formato do save (savegame.dat, 3072 bytes) e as regras com que o PRÓPRIO jogo o aceita.
//
// Lido no default.xex PT-BR (M04, etapa B, 2026-11-13; MECHANICS.md §19):
//  - 2 cópias dos dados globais, 256 bytes cada, em 0x000 e 0x100 (0x8226A790 lê `idx << 8`).
//    Uma cópia vale se o byte 0 é 9 (0x8226AFA8) e a soma confere (0x82265030). O jogo grava
//    as duas alternadamente: a cópia antiga pode ter qualquer conteúdo.
//  - 4 blocos de jogo, 512 bytes cada, em 0x200, 0x400, 0x600 e 0x800 (0x8226A730 lê
//    `(bloco + 1) << 9`). Um bloco vale se a soma confere, começa com "KHJC" e a lista de seções
//    (tipo, tamanho, dados; termina no tipo 0) só tem tipos < 7 e a seção 1 começa com a versão
//    0x1F (0x8226A558, 0x8226A500). Se não vale, 0x82271238 ZERA o bloco na memória: o jogo
//    some. Apagar um bloco (0x8226A8C0) zera só os 8 primeiros bytes. Nos saves reais o jogo
//    grava cada partida alternando entre dois blocos e apaga o anterior (a posição muda a cada
//    gravação; a quantidade de blocos válidos não).
//  - Soma (0x82264F28): dois u32 no fim do bloco = XOR das saídas de 0x8235D288, que nesta
//    versão XBLA é `li r3,0; blr` (devolve 0). As somas gravadas valem sempre 0.
//  - 0xA00..0xBFF não é lido como save: é sobra do buffer (conteúdo qualquer).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pc::savefmt {

constexpr size_t kFileSize = 3072;
constexpr size_t kGlobalSize = 256;
constexpr size_t kSlotSize = 512;
constexpr int kSlots = 4;

enum class Block : uint8_t { Empty, Valid, Invalid };

struct Report {
  bool size_ok = false;
  bool blank = false;           // tudo zero: nada gravado ainda (válido)
  Block global[2] = {Block::Empty, Block::Empty};
  Block slot[kSlots] = {Block::Empty, Block::Empty, Block::Empty, Block::Empty};
  bool usable = false;          // o jogo carrega tudo sem perder nada
  std::string reason;           // por que não é utilizável (vazio quando é)
  int ValidSlotMask() const;    // bit n = bloco de jogo n válido
  int ValidSlotCount() const;   // quantos blocos de jogo válidos (= partidas gravadas)
};

Report Inspect(const uint8_t* data, size_t size);
inline Report Inspect(const std::vector<uint8_t>& data) { return Inspect(data.data(), data.size()); }

const char* BlockName(Block b);  // "vazio", "valido", "invalido"

}  // namespace pc::savefmt
