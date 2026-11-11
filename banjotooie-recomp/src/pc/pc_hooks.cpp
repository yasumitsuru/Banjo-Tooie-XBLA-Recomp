// Ganchos chamados pelo código recompilado (ver pc_hooks.toml e MECHANICS.md).
// Os endereços e valores vêm da descompilação do default.xex do usuário, que tem o
// mesmo layout da versão do patch Xenia 58410955 (60 FPS / Draw Distance Hack).
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "pc_settings.h"
#include "pc_state.h"

namespace {

uint8_t* GuestBase() { return rex::system::kernel_state()->memory()->virtual_membase(); }

uint8_t ReadU8(uint32_t addr) { return GuestBase()[addr]; }
uint16_t ReadU16(uint32_t addr) {
  const uint8_t* p = GuestBase() + addr;
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

// 0x8266EF30: estado de cutscene/transição (0 = jogo livre; 1 e 2 = modos de cena),
// escrito por 0x82224250 e lido por 0x82224208 (ver MECHANICS.md).
constexpr uint32_t kCutsceneStateAddr = 0x8266EF30;
// 0x82673720: id do mapa atual (u16), lido pelo "Draw Distance Hack".
constexpr uint32_t kMapIdAddr = 0x82673720;

bool Fps60() { return pc::Get().fps60; }

// --scenes30: só nesta execução, cenas a 30 FPS como no patch Xenia (para comparação).
const bool g_force_scene30 = wcsstr(GetCommandLineW(), L"--scenes30") != nullptr;

// 1 VBlank por quadro (60 FPS) no jogo livre; 2 (30 FPS, como o patch Xenia) durante cenas,
// que são cronometradas por quadros: com a lógica a 60 o sobrevoo da tela de título terminou
// em 35,8 s em vez de > 55,8 s (shots/scn_l60.json x scn_s30.json). Com "Ritmo travado em 60"
// o runtime mostra cada quadro dessas cenas duas vezes, então a tela continua a 60 constantes.
// scenes60 (experimental) força a lógica a 60 também nas cenas.
uint32_t StepFor60() {
  if (pc::Get().scenes60 && !g_force_scene30) return 1u;
  return ReadU8(kCutsceneStateAddr) != 0 ? 2u : 1u;
}

// "Início rápido": acelera os logos de abertura (antes de chegar ao menu) sem
// alterar o ritmo do jogo. O delta do jogo é limitado a 3 quadros por quadro.
uint32_t BootFastForward() {
  if (!pc::Get().skip_intro_logos) return 0;
  if (pc::state::guest_swaps.load() > 1200) return 0;
  return 4;
}

void SetFloat(PPCRegister& f, float v) { f.f64 = static_cast<double>(v); }

}  // namespace

// ---------------------------------------------------------------- tempo de quadro
namespace {
// Snapshot dos registradores VOLÁTEIS do ABI PowerPC (r0,r2,r3-r12,f0-f13,v0-v13,
// cr0,cr1,cr5-cr7,ctr,xer,fpscr). Chamar uma função do jogo de dentro de um gancho
// os altera; o código recompilado em volta do gancho ainda usa esses registradores,
// então salvamos antes e restauramos depois.
struct VolatileSnapshot {
  PPCRegister r0, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12;
  PPCRegister f[14];   // f0..f13
  PPCVRegister v[14];  // v0..v13
  PPCCRRegister cr0, cr1, cr5, cr6, cr7;
  PPCRegister ctr;
  PPCXERRegister xer;
  PPCFPSCRRegister fpscr;
};

void SaveVolatile(const PPCContext& c, VolatileSnapshot& s) {
  s.r0 = c.r0;
  s.r2 = c.r2;
  s.r3 = c.r3;
  s.r4 = c.r4;
  s.r5 = c.r5;
  s.r6 = c.r6;
  s.r7 = c.r7;
  s.r8 = c.r8;
  s.r9 = c.r9;
  s.r10 = c.r10;
  s.r11 = c.r11;
  s.r12 = c.r12;
  // f0..f13 e v0..v13 são contíguos na estrutura
  for (int i = 0; i < 14; ++i) *(&s.f[0] + i) = *(&c.f0 + i);
  for (int i = 0; i < 14; ++i) *(&s.v[0] + i) = *(&c.v0 + i);
  s.cr0 = c.cr0;
  s.cr1 = c.cr1;
  s.cr5 = c.cr5;
  s.cr6 = c.cr6;
  s.cr7 = c.cr7;
  s.ctr = c.ctr;
  s.xer = c.xer;
  s.fpscr = c.fpscr;
}

void RestoreVolatile(PPCContext& c, const VolatileSnapshot& s) {
  c.r0 = s.r0;
  c.r2 = s.r2;
  c.r3 = s.r3;
  c.r4 = s.r4;
  c.r5 = s.r5;
  c.r6 = s.r6;
  c.r7 = s.r7;
  c.r8 = s.r8;
  c.r9 = s.r9;
  c.r10 = s.r10;
  c.r11 = s.r11;
  c.r12 = s.r12;
  for (int i = 0; i < 14; ++i) *(&c.f0 + i) = *(&s.f[0] + i);
  for (int i = 0; i < 14; ++i) *(&c.v0 + i) = *(&s.v[0] + i);
  c.cr0 = s.cr0;
  c.cr1 = s.cr1;
  c.cr5 = s.cr5;
  c.cr6 = s.cr6;
  c.cr7 = s.cr7;
  c.ctr = s.ctr;
  c.xer = s.xer;
  c.fpscr = s.fpscr;
}
}  // namespace

void PcHook_FrameStep(PPCRegister& r9) {
  uint32_t step = Fps60() ? StepFor60() : 2u;
  if (Fps60()) {
    const uint32_t boot = BootFastForward();
    if (boot) step = boot;
  }
  r9.u64 = step;
  pc::state::frame_step.store(step, std::memory_order_relaxed);
  pc::state::frame_step_calls.fetch_add(1, std::memory_order_relaxed);
}

// Início do quadro principal do jogo (0x8222165C, dentro de 0x82221648).
// Viagem pedida pela sonda --probe-dd-worlds: chama a rotina de viagem do próprio jogo
// (0x82248810: r3 = mapa, r4 = entrada) na thread do jogo, com o contexto atual, antes das
// atualizações dos objetos. O jogo processa o pedido nos quadros seguintes.
void PcHook_FrameStart() {
  if (const uint32_t req = pc::state::warp_requested.exchange(0, std::memory_order_relaxed)) {
    const uint16_t map = uint16_t(req >> 8);
    const uint8_t entry = uint8_t(req & 0xFF);
    const bool busy = ReadU8(0x826CC2B8) != 0;
    pc::state::warp_was_blocked.store(busy ? 1u : 0u, std::memory_order_relaxed);
    auto* ks = rex::system::kernel_state();
    auto* dispatcher = ks ? ks->function_dispatcher() : nullptr;
    auto* ts = rex::runtime::ThreadState::Get();
    if (dispatcher && ts && !busy) {
      auto log_exception = [](DWORD code, uintptr_t addr, uintptr_t fault, const char* where) {
        REXLOG_ERROR("warp: exceção 0x{:08X} em 0x{:016X} (acesso 0x{:016X}) em {}", code, addr,
                     fault, where);
      };
      __try {
        REXLOG_INFO("warp: início mapa=0x{:X} entrada={}", map, entry);
        PPCContext* ctx = ts->context();
        VolatileSnapshot snap{};
        SaveVolatile(*ctx, snap);
        uint64_t args[2] = {map, entry};
        dispatcher->Execute(ts, 0x82248810, args, 2);
        RestoreVolatile(*ctx, snap);
        REXLOG_INFO("warp: fim mapa=0x{:X}", map);
      } __except (log_exception(GetExceptionCode(),
                                reinterpret_cast<uintptr_t>(
                                    GetExceptionInformation()->ExceptionRecord->ExceptionAddress),
                                reinterpret_cast<uintptr_t>(
                                    GetExceptionInformation()->ExceptionRecord->ExceptionInformation[1]),
                                "hook"),
                  EXCEPTION_EXECUTE_HANDLER) {
      }
      pc::state::warp_result.store(1u, std::memory_order_relaxed);
    } else {
      pc::state::warp_result.store(0u, std::memory_order_relaxed);
    }
    pc::state::warp_invoked.store(req, std::memory_order_relaxed);
  }

  // Contagem pedida pela sonda --probe-save-sim: chama a função de contagem da tela
  // "Totais" do próprio jogo (0x8223E300: r3 = tipo, r4 = id do mundo; devolve a soma em
  // r3) na thread do jogo. Só lê bandeiras e tabelas, como quando a tela de pausa abre.
  if (const uint32_t req = pc::state::count_requested.exchange(0, std::memory_order_relaxed)) {
    const uint32_t kind = req >> 16;
    const uint32_t world = req & 0xFFFF;
    auto* ks = rex::system::kernel_state();
    auto* dispatcher = ks ? ks->function_dispatcher() : nullptr;
    auto* ts = rex::runtime::ThreadState::Get();
    uint64_t result = ~0ull;
    if (dispatcher && ts) {
      __try {
        PPCContext* ctx = ts->context();
        VolatileSnapshot snap{};
        SaveVolatile(*ctx, snap);
        uint64_t args[2] = {kind, world};
        result = dispatcher->Execute(ts, 0x8223E300, args, 2);
        RestoreVolatile(*ctx, snap);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = ~0ull;
      }
    }
    pc::state::count_result.store(result, std::memory_order_relaxed);
    pc::state::count_invoked.store(req, std::memory_order_relaxed);
  }
}

void PcHook_PresentInterval(PPCRegister& r11) {
  pc::state::guest_swaps.fetch_add(1, std::memory_order_relaxed);
  // Ritmo da tela = ritmo do jogo: o runtime apresenta cada quadro do jogo uma vez e descarta
  // redesenhos extras só da interface (que seguiam a taxa do monitor, 90-120 FPS irregulares).
  {
    static int applied_cap = -1;
    const int cap = pc::Get().lock60 ? 60 : 0;  // 0 = sem ritmo fixo (como o runtime original)
    if (cap != applied_cap) {
      applied_cap = cap;
      rex::cvar::SetFlagByName("host_present_fps_cap", std::to_string(cap));
    }
  }
  pc::state::cutscene_state.store(ReadU8(kCutsceneStateAddr), std::memory_order_relaxed);
  if (Fps60()) r11.u64 = StepFor60();
}

// Constantes por quadro trocadas pelo patch de 60 FPS (original -> 60 FPS).
// Valores lidos do .rdata (endereço da constante entre parênteses).
#define PC_K(addr, reg, v60)                       \
  void PcHook_K_##addr(PPCRegister& reg) {         \
    if (Fps60()) SetFloat(reg, v60);               \
  }
PC_K(822C8930, f0, 0.2f)             // 0.05 (0x82000B48) -> 0.2 (0x8200073C)
PC_K(821A22E4, f5, 9.0f)             // 3.0 (0x82000800) -> 9.0 (0x82001EDC)
PC_K(821A22E8, f3, -0.03f)           // -0.01 (0x82000924) -> -0.03 (0x82004238)
PC_K(821A22EC, f2, -3.0f)            // -1.0 (0x82000748) -> -3.0 (0x82000B20)
PC_K(821A2324, f5, 9.0f)             // 3.0 -> 9.0
PC_K(821A2328, f3, 0.03f)            // 0.01 (0x82000778) -> 0.03 (0x82000B30)
PC_K(821A232C, f2, 3.0f)             // 1.0 (0x82000738) -> 3.0 (0x82000800)
PC_K(821A2358, f0, 100.0f)           // 90.0 (0x820007C0) -> 100.0 (0x82000774)
PC_K(8213E2EC, f5, 11.0f)            // 3.0 -> 11.0 (0x82002060)
PC_K(8213E2F0, f3, -0.03f)           // -0.01 -> -0.03
PC_K(8213E2F4, f2, -3.90625f)        // -1.0 -> -3.90625 (0x820028F0)
PC_K(8213E32C, f5, 11.0f)            // 3.0 -> 11.0
PC_K(8213E330, f3, 0.03f)            // 0.01 -> 0.03
PC_K(8213E334, f2, 3.75f)            // 1.0 -> 3.75 (0x820013F4)
PC_K(8213E340, f0, -0.03f)           // -0.01 -> -0.03
PC_K(8213E360, f0, 220.0f)           // 200.0 (0x820008F4) -> 220.0 (0x82000B0C)
PC_K(821129B8, f0, 0.014285714f)     // 0.028 (0x82000974) -> 1/70 (0x82001EF8)
#undef PC_K

// ---------------------------------------------------------------- velocidade dos diálogos
// Depois de "fadds f13,f13,f29" (0x8225D8A8): f13 = cronômetro + tempo do quadro. Somamos
// (fator - 1) x tempo do quadro; as pausas de pontuação encolhem na mesma proporção.
void PcHook_DialogSpeed(PPCRegister& f13, PPCRegister& f29, PPCRegister& r31) {
  pc::state::dialog_type_calls.fetch_add(1, std::memory_order_relaxed);
  pc::state::dialog_box.store(r31.u32, std::memory_order_relaxed);
  const float k = pc::DialogSpeedFactor(pc::Get().dialog_speed);
  if (k == 1.0f) return;
  SetFloat(f13, static_cast<float>(f13.f64 + f29.f64 * static_cast<double>(k - 1.0f)));
}

// ---------------------------------------------------------------- distância de desenho
// Alcance "Total" por mapa = valores do Draw Distance Hack (maior alcance já testado
// pela comunidade Xenia); mapas 0x112/0x128 têm alcance menor por segurança.
static uint32_t TotalRangeForMap(uint16_t map) {
  switch (map) {
    case 0x112:
    case 0x128: return 0x28A0;  // 10400
    case 0x127: return 0x3200;  // 12800
    case 0x136: return 0x5400;  // 21504
    default: return 0x6400;     // 25600
  }
}

void PcHook_DrawDistance(PPCRegister& r3) {
  const uint32_t original = r3.u32;
  pc::state::last_draw_range_original.store(original, std::memory_order_relaxed);
  pc::state::draw_range_calls.fetch_add(1, std::memory_order_relaxed);
  // maior alcance original pedido nesta execução (para a sonda --probe-drawdistance)
  uint32_t prev_max = pc::state::max_draw_range_original.load(std::memory_order_relaxed);
  while (original > prev_max &&
         !pc::state::max_draw_range_original.compare_exchange_weak(
             prev_max, original, std::memory_order_relaxed)) {
  }
  switch (pc::Get().draw_distance) {
    case pc::DrawDistance::Original:
      return;
    case pc::DrawDistance::Longe: {
      // Dobro do alcance original, sem passar do perfil Total do mapa.
      uint32_t total = TotalRangeForMap(ReadU16(kMapIdAddr));
      r3.u64 = std::max(original, std::min(original * 2u, total));
      return;
    }
    case pc::DrawDistance::Total:
      r3.u64 = std::max(original, TotalRangeForMap(ReadU16(kMapIdAddr)));
      return;
  }
}

// true = pula o desvio condicional original (equivale ao NOP do patch).
bool PcHook_SkipRangeCheckA() { return pc::Get().draw_distance == pc::DrawDistance::Total; }
bool PcHook_SkipRangeCheckB() { return pc::Get().draw_distance == pc::DrawDistance::Total; }

// ---------------------------------------------------------------- textos do jogo
// 0x822E9C28 = GetString(id): devolve a tabela de ponteiros montada por 0x822E9C88 ao ler
// RAWFiles\X360_strings.dat (ponteiros em 0x826A60E8, quantidade em 0x826A60F0). O id 1 é
// "CLASSIFICAÇÕES", a entrada de placares online do menu de título e do menu de pausa, que
// no PC só mostra "ESTE RECURSO EXIGE UM PERFIL ... XBOX LIVE". Com a opção ligada, essa
// entrada passa a se chamar "CONFIGURAÇÕES DE PC" e abre o menu de PC (ver PcHook_*Select).
// O gancho também registra pares (id, chamador) para a sonda --probe-menu.
namespace {
constexpr uint32_t kStrLeaderboards = 1;
uint32_t PcMenuLabel() {
  static uint32_t addr = 0;
  if (addr) return addr;
  // Latin-1, como o arquivo da tradução: Ç = 0xC7, Õ = 0xD5 (rotina de acentos 0x825F3940)
  static const uint8_t kText[] = {'C', 'O', 'N', 'F', 'I', 'G', 'U', 'R', 'A', 0xC7,
                                  0xD5, 'E', 'S', ' ', 'D', 'E', ' ', 'P', 'C', 0};
  auto* mem = rex::system::kernel_state()->memory();
  const uint32_t a = mem->SystemHeapAlloc(0x40);
  if (!a) return 0;
  std::memcpy(GuestBase() + a, kText, sizeof(kText));
  addr = a;
  return addr;
}
}  // namespace

bool PcHook_GetString(PPCRegister& r3, uint64_t& lr) {
  const uint32_t id = r3.u32;
  pc::state::NoteStringRequest(id, static_cast<uint32_t>(lr));
  if (id == kStrLeaderboards && pc::Get().pc_menu_entry) {
    const uint32_t label = PcMenuLabel();
    if (label) {
      r3.u64 = label;
      return true;  // devolve o texto novo sem consultar a tabela
    }
  }
  return false;
}

// Ação da entrada (antes de o handler criar a pilha: retornar aqui não deixa estado pela
// metade). Repete o único efeito do início do original que não depende do Xbox Live: o
// campo +0x220 do menu volta de 1 para 0.
bool PcHook_LeaderboardsSelect(PPCRegister& r3) {
  if (!pc::Get().pc_menu_entry) return false;
  uint8_t* g = GuestBase();
  const uint32_t menu = r3.u32;
  if (menu >= 0x40000000u && menu < 0xA0000000u) {
    uint8_t* f = g + menu + 0x220;
    if (f[0] == 0 && f[1] == 0 && f[2] == 0 && f[3] == 1) f[3] = 0;
  }
  pc::state::pc_entry_activations.fetch_add(1, std::memory_order_relaxed);
  pc::state::menu_open_request.store(true, std::memory_order_relaxed);
  return true;
}
