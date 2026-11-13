#include "pc_lang.h"

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include <atomic>

namespace pc::lang {
namespace {

constexpr uint32_t kStringTablePtr = 0x826A60E8;    // [ ] = vetor de ponteiros dos textos
constexpr uint32_t kStringTableCount = 0x826A60F0;  // [ ] = quantidade (1521)

// 0 = ainda não decidido, 1 = PT-BR, 2 = inglês
std::atomic<int> g_state{0};

const uint8_t* Base() {
  auto* ks = rex::system::kernel_state();
  return ks && ks->memory() ? ks->memory()->virtual_membase() : nullptr;
}

uint32_t R32(const uint8_t* b, uint32_t a) {
  return (uint32_t(b[a]) << 24) | (uint32_t(b[a + 1]) << 16) | (uint32_t(b[a + 2]) << 8) | b[a + 3];
}

bool Plausible(uint32_t a) { return a >= 0x40000000u && a < 0xC0000000u; }

}  // namespace

std::string GameString(uint32_t id) {
  const uint8_t* b = Base();
  if (!b) return {};
  const uint32_t table = R32(b, kStringTablePtr);
  const uint32_t count = R32(b, kStringTableCount);
  if (!Plausible(table) || count == 0 || count > 100000 || id >= count) return {};
  const uint32_t s = R32(b, table + id * 4);
  if (!Plausible(s)) return {};
  std::string out;
  for (uint32_t i = 0; i < 512; ++i) {
    const uint8_t c = b[s + i];
    if (!c) break;
    out += char(c);
  }
  return out;
}

bool English() {
  const int st = g_state.load(std::memory_order_relaxed);
  if (st) return st == 2;
  const std::string s0 = GameString(0);
  if (s0.empty()) return false;
  const bool en = s0 != "JOGAR";
  int expected = 0;
  if (g_state.compare_exchange_strong(expected, en ? 2 : 1)) {
    REXLOG_INFO("PC: idioma do jogo = {} (texto 0 = '{}')", en ? "inglês" : "PT-BR", s0);
  }
  return en;
}

bool Known() { return g_state.load(std::memory_order_relaxed) != 0; }

}  // namespace pc::lang
