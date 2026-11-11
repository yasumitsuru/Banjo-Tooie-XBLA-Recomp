#include "pc_progress.h"

#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

namespace pc::progress {
namespace {

// Tipo de colecionável como o jogo o descreve (0x8223D6B0, 0x8262DA38, 0x8223E300).
struct KindInfo {
  const char* name;
  uint32_t world_table;  // byte do mundo do item 1
  uint32_t stride;       // bytes por item na tabela
  uint16_t count;        // quantidade total (0x8223D6B0)
  uint16_t flag_base;    // base[k] em 0x8262DA38 (tipo do jogo)
  bool notes;            // ninhos de notas: valor 5, clave 20
};

constexpr KindInfo kKinds[kKindCount] = {
    {"Jinjos", 0x8262D678, 3, 45, 0x1D6, false},           // tipo 0
    {"Jiggies", 0x8262D700, 2, 90, 0x232, false},          // tipo 1
    {"Favos vazios", 0x8262D7B4, 2, 25, 0x204, false},     // tipo 2
    {"Glowbos", 0x8262D7E8, 2, 17, 0x221, false},          // tipo 3
    {"Páginas Cheato", 0x8262D80C, 2, 25, 0x2BD, false},   // tipo 4
    {"Notas", 0x8262D840, 2, 153, 0x431, true},            // tipo 6 (ninhos)
};

// Ids de mundo das tabelas, na ordem do jogo (confirmada pela quantidade de golpes do
// Jamjars por mundo na tabela 0x8262FD34: 3,2,3,3,3,3,2,1).
struct WorldInfo {
  const char* name;
  uint8_t ids[2];
};

constexpr WorldInfo kWorlds[kWorldCount] = {
    {"Mayahem Temple", {16, 0}},     {"Glitter Gulch Mine", {15, 0}},
    {"Witchyworld", {18, 0}},        {"Jolly Roger's Lagoon", {20, 0}},
    {"Terrydactyland", {21, 0}},     {"Grunty Industries", {22, 0}},
    {"Hailfire Peaks", {23, 0}},     {"Cloud Cuckooland", {24, 0}},
    {"Ilha O' Hags", {26, 19}},
};

constexpr uint32_t kFlagBlockPtr = 0x8267513C;  // bandeiras 0x29..0x588
constexpr uint32_t kFlagFirst = 0x29, kFlagLast = 0x588;

uint32_t Be32(const uint8_t* p, uint32_t a) {
  return (uint32_t(p[a]) << 24) | (uint32_t(p[a + 1]) << 16) | (uint32_t(p[a + 2]) << 8) |
         p[a + 3];
}

int WorldIndex(uint8_t id) {
  for (int w = 0; w < kWorldCount; ++w)
    for (uint8_t v : kWorlds[w].ids)
      if (v && v == id) return w;
  return -1;
}

}  // namespace

const char* KindName(Kind k) { return (k >= 0 && k < kKindCount) ? kKinds[k].name : "?"; }

Snapshot Read() {
  Snapshot s;
  for (int w = 0; w < kWorldCount; ++w) s.worlds[w].name = kWorlds[w].name;

  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return s;
  const uint8_t* p = ks->memory()->virtual_membase();
  if (!p) return s;
  const uint32_t block = Be32(p, kFlagBlockPtr);
  if (block < 0x00010000u || block >= 0xE0000000u) return s;
  s.valid = true;

  auto flag = [&](uint32_t f) -> bool {
    if (f < kFlagFirst || f > kFlagLast) return false;
    const uint32_t bit = f - 0x28;
    return (p[block + (bit >> 3)] >> (bit & 7)) & 1;
  };

  for (int k = 0; k < kKindCount; ++k) {
    const KindInfo& ki = kKinds[k];
    for (uint32_t n = 1; n <= ki.count; ++n) {
      const uint32_t e = ki.world_table + ki.stride * (n - 1);
      const int w = WorldIndex(p[e]);
      const uint16_t value = ki.notes ? (p[e + 1] ? 20 : 5) : 1;
      const bool got = flag(ki.flag_base + n - 1);
      s.total[k] += value;
      if (got) s.got[k] += value;
      if (w < 0) continue;
      s.worlds[w].total[k] += value;
      if (got) s.worlds[w].got[k] += value;
    }
  }
  return s;
}

int ProbeItemFlag(Kind k, int n, int* world_out) {
  if (world_out) *world_out = -1;
  if (k < 0 || k >= kKindCount || n < 1 || n > kKinds[k].count) return 0;
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return 0;
  uint8_t* p = ks->memory()->virtual_membase();
  if (!p) return 0;
  const uint32_t block = Be32(p, kFlagBlockPtr);
  if (block < 0x00010000u || block >= 0xE0000000u) return 0;
  const KindInfo& ki = kKinds[k];
  const int w = WorldIndex(p[ki.world_table + ki.stride * (n - 1)]);
  if (world_out) *world_out = w;
  if (w < 0) return 0;
  const uint32_t bit = uint32_t(ki.flag_base + n - 1) - 0x28;
  uint8_t& byte = p[block + (bit >> 3)];
  const uint8_t saved = byte;
  const int before = Read().worlds[w].got[k];
  byte = uint8_t(saved ^ (1u << (bit & 7)));
  const int after = Read().worlds[w].got[k];
  byte = saved;  // desfaz na hora: o progresso do jogo não muda
  return after - before;
}

bool FlagBlockRange(uint32_t* addr_out, size_t* len_out) {
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory() || !addr_out || !len_out) return false;
  const uint8_t* p = ks->memory()->virtual_membase();
  if (!p) return false;
  const uint32_t block = Be32(p, kFlagBlockPtr);
  if (block < 0x00010000u || block >= 0xE0000000u) return false;
  *addr_out = block;
  *len_out = (kFlagLast - kFlagFirst + 1 + 7) / 8;
  return true;
}

int SetItemFlag(Kind k, int n, bool set) {
  if (k < 0 || k >= kKindCount || n < 1 || n > kKinds[k].count) return -1;
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return -1;
  uint8_t* p = ks->memory()->virtual_membase();
  if (!p) return -1;
  const uint32_t block = Be32(p, kFlagBlockPtr);
  if (block < 0x00010000u || block >= 0xE0000000u) return -1;
  const KindInfo& ki = kKinds[k];
  const int w = WorldIndex(p[ki.world_table + ki.stride * (n - 1)]);
  if (w < 0) return -1;
  const uint32_t bit = uint32_t(ki.flag_base + n - 1) - 0x28;
  uint8_t& byte = p[block + (bit >> 3)];
  const uint8_t saved = byte;
  if (set)
    byte = uint8_t(saved | (1u << (bit & 7)));
  else
    byte = uint8_t(saved & ~(1u << (bit & 7)));
  return (saved >> (bit & 7)) & 1;
}

int ItemWorld(Kind k, int n) {
  if (k < 0 || k >= kKindCount || n < 1 || n > kKinds[k].count) return -1;
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return -1;
  const uint8_t* p = ks->memory()->virtual_membase();
  if (!p) return -1;
  const KindInfo& ki = kKinds[k];
  return WorldIndex(p[ki.world_table + ki.stride * (n - 1)]);
}

int ItemValue(Kind k, int n) {
  if (k < 0 || k >= kKindCount || n < 1 || n > kKinds[k].count) return 0;
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return 0;
  const uint8_t* p = ks->memory()->virtual_membase();
  if (!p) return 0;
  const KindInfo& ki = kKinds[k];
  if (!ki.notes) return 1;
  const uint32_t e = ki.world_table + ki.stride * (n - 1);
  return p[e + 1] ? 20 : 5;
}

std::vector<std::pair<int, int>> ItemsInWorlds(const std::vector<uint8_t>& world_ids) {
  std::vector<std::pair<int, int>> out;
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return out;
  const uint8_t* p = ks->memory()->virtual_membase();
  if (!p) return out;
  for (int k = 0; k < kKindCount; ++k) {
    const KindInfo& ki = kKinds[k];
    for (uint32_t n = 1; n <= ki.count; ++n) {
      const uint32_t e = ki.world_table + ki.stride * (n - 1);
      const int w = WorldIndex(p[e]);
      if (w < 0) continue;
      const uint8_t id = p[e];
      for (uint8_t want : world_ids)
        if (want && want == id) out.emplace_back(k, int(n));
    }
  }
  return out;
}

}  // namespace pc::progress
