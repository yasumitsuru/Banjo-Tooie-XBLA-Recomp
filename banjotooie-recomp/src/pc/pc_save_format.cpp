// Regras do save lidas no default.xex (ver pc_save_format.h e MECHANICS.md §19).
#include "pc/pc_save_format.h"

namespace pc::savefmt {
namespace {

constexpr uint32_t kSlotMagic = 0x4B484A43;  // "KHJC"
constexpr uint8_t kGlobalVersion = 9;         // 0x8226AFA8: byte 0 == 9
constexpr uint8_t kSlotVersion = 0x1F;        // 0x8226A500: 1º byte da seção 1 == 31
constexpr uint8_t kSectionTypes = 7;          // 0x8226A558: tipo >= 7 -> inválido

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool AllZero(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (p[i]) return false;
  return true;
}

// 0x82265030: os dois u32 do fim do bloco têm de ser iguais aos que 0x82264F28 calcula; nesta
// versão o cálculo dá sempre (0, 0) (ver o cabeçalho).
bool ChecksumOk(const uint8_t* block, size_t size) {
  return Be32(block + size - 8) == 0 && Be32(block + size - 4) == 0;
}

Block InspectGlobal(const uint8_t* b) {
  if (AllZero(b, kGlobalSize)) return Block::Empty;
  if (b[0] == kGlobalVersion && ChecksumOk(b, kGlobalSize)) return Block::Valid;
  return Block::Invalid;  // cópia antiga ou estragada: o jogo usa a outra
}

Block InspectSlot(const uint8_t* b, std::string* why) {
  if (AllZero(b, 8)) return Block::Empty;  // nunca usado ou apagado pelo jogo
  if (!ChecksumOk(b, kSlotSize)) {
    *why = "soma do bloco diferente de 0";
    return Block::Invalid;
  }
  if (Be32(b) != kSlotMagic) {
    *why = "sem a assinatura KHJC";
    return Block::Invalid;
  }
  const size_t end = kSlotSize - 8;  // as seções não passam da soma
  size_t o = 4;
  bool version_ok = false;
  for (int guard = 0; guard < 256; ++guard) {
    if (o + 2 > end) {
      *why = "lista de seções passa do fim do bloco";
      return Block::Invalid;
    }
    const uint8_t type = b[o], len = b[o + 1];
    if (type >= kSectionTypes) {
      *why = "seção de tipo " + std::to_string(type) + " (o jogo só aceita 0..6)";
      return Block::Invalid;
    }
    if (o + 2 + len > end) {
      *why = "seção passa do fim do bloco";
      return Block::Invalid;
    }
    if (type == 1) version_ok = len >= 1 && b[o + 2] == kSlotVersion;
    o += 2 + size_t(len);
    if (type == 0) break;
  }
  if (!version_ok) {
    *why = "seção 1 ausente ou versão diferente de 0x1F";
    return Block::Invalid;
  }
  return Block::Valid;
}

}  // namespace

int Report::ValidSlotMask() const {
  int m = 0;
  for (int i = 0; i < kSlots; ++i)
    if (slot[i] == Block::Valid) m |= 1 << i;
  return m;
}

int Report::ValidSlotCount() const {
  int n = 0;
  for (int i = 0; i < kSlots; ++i) n += slot[i] == Block::Valid;
  return n;
}

const char* BlockName(Block b) {
  switch (b) {
    case Block::Valid: return "valido";
    case Block::Invalid: return "invalido";
    default: return "vazio";
  }
}

Report Inspect(const uint8_t* data, size_t size) {
  Report r;
  r.size_ok = size == kFileSize;
  if (!r.size_ok) {
    r.reason = "tamanho " + std::to_string(size) + " bytes (o save tem " +
               std::to_string(kFileSize) + ")";
    return r;
  }
  r.blank = AllZero(data, kFileSize);
  if (r.blank) {
    r.usable = true;
    return r;
  }
  for (int g = 0; g < 2; ++g) r.global[g] = InspectGlobal(data + g * kGlobalSize);
  std::string first_bad;
  for (int s = 0; s < kSlots; ++s) {
    std::string why;
    r.slot[s] = InspectSlot(data + 2 * kGlobalSize + s * kSlotSize, &why);
    if (r.slot[s] == Block::Invalid && first_bad.empty()) {
      static const char* const kOffsets[kSlots] = {"0x200", "0x400", "0x600", "0x800"};
      first_bad = std::string("bloco de jogo em ") + kOffsets[s] + ": " + why;
    }
  }
  if (!first_bad.empty()) {
    r.reason = first_bad;  // o jogo zeraria esse jogo ao ler
    return r;
  }
  if (r.global[0] != Block::Valid && r.global[1] != Block::Valid) {
    r.reason = "nenhuma das 2 cópias dos dados globais é válida";
    return r;
  }
  r.usable = true;
  return r;
}

}  // namespace pc::savefmt
