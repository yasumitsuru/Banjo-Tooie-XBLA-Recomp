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
#include <chrono>
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

#include "pc_ingame_menu.h"
#include "pc_lang.h"
#include "pc_log.h"
#include "pc_menu.h"
#include "pc_settings.h"
#include "pc_state.h"
#include "mechanics_values.h"

namespace {

uint8_t* GuestBase() { return rex::system::kernel_state()->memory()->virtual_membase(); }

uint8_t ReadU8(uint32_t addr) { return GuestBase()[addr]; }
uint16_t ReadU16(uint32_t addr) {
  const uint8_t* p = GuestBase() + addr;
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
uint32_t ReadU32(uint32_t addr) {
  const uint8_t* p = GuestBase() + addr;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
// Ponteiro plausivel para a memoria dinamica do jogo (as telas e menus vivem no heap).
bool GuestHeapPtr(uint32_t a) { return a >= 0x40000000u && a < 0xA0000000u; }

// 0x8266EF30: estado de cutscene/transição (0 = jogo livre; 1 e 2 = modos de cena),
// escrito por 0x82224250 e lido por 0x82224208 (ver MECHANICS.md) — valor gerado de
// mechanics.json (tools/gen_mechanics_header.py), conferido nesta linha de base.
constexpr uint32_t kCutsceneStateAddr = pc::mechanics::kCutsceneStateAddr;
// 0x82673720: id do mapa atual (u16), lido pelo "Draw Distance Hack".
constexpr uint32_t kMapIdAddr = pc::mechanics::kMapIdAddr;

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
  uint32_t step = Fps60() ? StepFor60() : pc::mechanics::kOriginalFrameStep;
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
  pc::state::logic_updates.fetch_add(1, std::memory_order_relaxed);
  // Rastro de física (M04, etapa D): uma amostra por atualização da lógica.
  if (pc::state::phys_trace_enabled.load(std::memory_order_relaxed)) {
    const uint32_t i = pc::state::phys_trace_count.load(std::memory_order_relaxed);
    if (i < pc::state::kPhysTraceSize) {
      auto f32 = [](uint32_t a) {
        const uint32_t v = ReadU32(a);
        float f;
        std::memcpy(&f, &v, sizeof f);
        return f;
      };
      pc::state::PhysSample& s = pc::state::phys_trace[i];
      s.t_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
      s.swaps = pc::state::guest_swaps.load(std::memory_order_relaxed);
      s.step = pc::state::frame_step.load(std::memory_order_relaxed);
      s.scene = ReadU8(kCutsceneStateAddr);
      s.x = f32(0x8266BB74);
      s.y = f32(0x8266BB78);
      s.z = f32(0x8266BB7C);
      // Câmera: o jogo guarda o ponteiro da posição em 0x8266EEF0 (também em 0x8266F9E0 e
      // 0x82674680); a posição fica no heap (0x4030E3F0 nas execuções de 2026-11-15).
      const uint32_t cam = ReadU32(0x8266EEF0);
      if (cam >= 0x40000000u && cam < 0xC0000000u) {
        s.cx = f32(cam);
        s.cy = f32(cam + 4);
        s.cz = f32(cam + 8);
      } else {
        s.cx = s.cy = s.cz = 0.0f;
      }
      s.pstate = ReadU16(0x82674AD0);
      s.clock_s = f32(0x8266EC80);
      s.frames = ReadU32(0x8266C69C);
      pc::state::phys_trace_count.store(i + 1, std::memory_order_release);
    }
  }
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

  // F1: abre as Configurações de PC dentro do menu do jogo (pc_ingame_menu.cpp).
  pc::ingame::FrameTick();
}

void PcHook_PresentInterval(PPCRegister& r11) {
  pc::state::guest_swaps.fetch_add(1, std::memory_order_relaxed);
  pc::log::NotePresent();  // tempo entre quadros para o registro de sessão (pc_log.h)
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
// Os valores originais e os de 60 FPS sao as constantes f32 reais do .rdata do
// default.xex, conferidas endereco por endereco nesta linha de base; vem de
// mechanics.json -> mechanics_values.h (tools/gen_mechanics_header.py, MECHANICS.md §12).
// Estrutura: PC_K(endereço do gancho, registrador do gancho, constante 60 FPS).
// M04: cada uso conta no grupo da função dona (registro de sessão anota por mapa).
constexpr int KGroup(uint32_t a) {
  return a == 0x822C8930u ? 0 : (a >> 12) == 0x821A2u ? 1 : (a >> 12) == 0x8213Eu ? 2 : 3;
}
#define PC_K(addr, reg, v60)                                                      \
  void PcHook_K_##addr(PPCRegister& reg) {                                        \
    pc::state::k60_uses[KGroup(0x##addr##u)].fetch_add(1, std::memory_order_relaxed); \
    if (Fps60()) SetFloat(reg, v60);                                              \
  }
PC_K(822C8930, f0, pc::mechanics::K_822C8930)   // 0.05 (0x82000B48) -> 0.2 (0x8200073C)
PC_K(821A22E4, f5, pc::mechanics::K_821A22E4)   // 3.0 (0x82000800) -> 9.0 (0x82001EDC)
PC_K(821A22E8, f3, pc::mechanics::K_821A22E8)   // -0.01 (0x82000924) -> -0.03 (0x82004238)
PC_K(821A22EC, f2, pc::mechanics::K_821A22EC)   // -1.0 (0x82000748) -> -3.0 (0x82000B20)
PC_K(821A2324, f5, pc::mechanics::K_821A2324)   // 3.0 -> 9.0
PC_K(821A2328, f3, pc::mechanics::K_821A2328)   // 0.01 (0x82000778) -> 0.03 (0x82000B30)
PC_K(821A232C, f2, pc::mechanics::K_821A232C)   // 1.0 (0x82000738) -> 3.0 (0x82000800)
PC_K(821A2358, f0, pc::mechanics::K_821A2358)   // 90.0 (0x820007C0) -> 100.0 (0x82000774)
PC_K(8213E2EC, f5, pc::mechanics::K_8213E2EC)   // 3.0 -> 11.0 (0x82002060)
PC_K(8213E2F0, f3, pc::mechanics::K_8213E2F0)   // -0.01 -> -0.03
PC_K(8213E2F4, f2, pc::mechanics::K_8213E2F4)   // -1.0 -> -3.90625 (0x820028F0)
PC_K(8213E32C, f5, pc::mechanics::K_8213E32C)   // 3.0 -> 11.0
PC_K(8213E330, f3, pc::mechanics::K_8213E330)   // 0.01 -> 0.03
PC_K(8213E334, f2, pc::mechanics::K_8213E334)   // 1.0 -> 3.75 (0x820013F4)
PC_K(8213E340, f0, pc::mechanics::K_8213E340)   // -0.01 -> -0.03
PC_K(8213E360, f0, pc::mechanics::K_8213E360)   // 200.0 (0x820008F4) -> 220.0 (0x82000B0C)
PC_K(821129B8, f0, pc::mechanics::K_821129B8)   // 0.028 (0x82000974) -> 1/70 (0x82001EF8)
#undef PC_K

// ---------------------------------------------------------------- velocidade dos diálogos
// Depois do "ps_add f13,f13,f29" real do jogo em 0x8225D8A8 (0xEDADE82A): ele acumula o
// tempo do quadro (f29) no par de cronômetros da caixa (+0x22C). Somamos
// (fator - 1) x tempo do quadro; as pausas de pontuação encolhem na mesma proporção.
void PcHook_DialogSpeed(PPCRegister& f13, PPCRegister& f29, PPCRegister& r31) {
  pc::state::dialog_type_calls.fetch_add(1, std::memory_order_relaxed);
  pc::state::dialog_box.store(r31.u32, std::memory_order_relaxed);
  const float k = pc::DialogSpeedFactor(pc::Get().dialog_speed);
  if (k == 1.0f) return;
  SetFloat(f13, static_cast<float>(f13.f64 + f29.f64 * static_cast<double>(k - 1.0f)));
}

// Depois do "fsubs f0,f0,f29" real do jogo em 0x8225DB64 (estado 3 da caixa): caixas com tempo
// (legendas que avançam sozinhas) esperam caixa+0x21C segundos (= caixa+0x224 quadros / 60)
// depois de mostrar a página toda. Nos modos lentos o jogo desconta só 1/fator do tempo do quadro:
// a legenda fica na tela 2x (Lenta) ou 3x (Muito lenta) mais tempo. A pode pular, como antes.
void PcHook_DialogHold(PPCRegister& f0, PPCRegister& f29) {
  pc::state::dialog_hold_calls.fetch_add(1, std::memory_order_relaxed);
  const float h = pc::DialogHoldFactor(pc::Get().dialog_speed);
  if (h <= 1.0f) return;
  SetFloat(f0, static_cast<float>(f0.f64 + f29.f64 * (1.0 - 1.0 / static_cast<double>(h))));
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

// ---------------------------------------------------------------- menu de pausa
// Pedido da pessoa (2026-11-12): a pausa fica CONTINUAR JOGO, VER TOTAIS, AJUDA E OPÇÕES,
// CONQUISTAS e SAIR DO JOGO (sair sempre por último), e "CONFIGURAÇÕES DE PC" fica DENTRO de
// AJUDA E OPÇÕES, com o ícone do Kazooie (o que era de CLASSIFICAÇÕES). MECHANICS.md §17.
// Lido no default.xex (Ghidra + código gerado):
//  - 0x822DEE60 monta as páginas; com tela+0x198 != 0 (pausa) a página 0 recebe CONTINUAR JOGO
//    (22), VER TOTAIS (23), AJUDA E OPÇÕES (3, página-alvo 1), CLASSIFICAÇÕES (1, em 0x822DF050),
//    CONQUISTAS (2) e SAIR DO JOGO (61); a página 1 (AJUDA E OPÇÕES) recebe COMO JOGAR (6),
//    CONTROLES (7), CONFIGURAÇÕES (8) e, conforme o caso, CRÉDITOS / DICAS DE JIGGIES /
//    REINSTALAR EXTRAS; os dois caminhos se juntam em 0x822DF2D0 antes da página seguinte.
//  - 0x822E1380(página, texto, ícone, página_alvo, ação): 0x822E1230 realoca o vetor de entradas
//    de 136 bytes e 0x822E5740 grava altura, x, ícone (entrada+8) e o rótulo (entrada+0x3B);
//    0x822E1380 grava página_alvo em entrada+0x28 e a ação em entrada+0x7C.
//  - Ao apertar A, 0x822E2AB8 chama entrada+0x7C com r3 = tela; a entrada escolhida é
//    entradas_da_página_atual (0x822DBB88: *(*(tela+0x154) + *(tela+0x2C)*0x2C))[byte tela+7].
// A entrada nova usa a ação de CLASSIFICAÇÕES (0x822DEC40) com página-alvo -1;
// PcHook_LeaderboardsSelect a reconhece e abre o menu de PC (o do F1).
namespace {
constexpr uint32_t kStrPcSettings = pc::state::kPcMenuEntryStringId;  // id novo (o X360_strings.dat tem 1521 textos)
// Valores lidos no default.xex, gerados de mechanics.json (pc.menu) em mechanics_values.h e
// conferidos contra pc_hooks.toml no build (tools/gen_mechanics_header.py; MECHANICS.md §15/§17).
namespace mm = pc::mechanics;
constexpr uint32_t kIconKazooie = mm::kKazooieIcon;            // 13: o pássaro (era de CLASSIFICAÇÕES)
constexpr uint32_t kAddMenuEntry = mm::kAddEntryFn;            // 0x822E1380
constexpr uint32_t kLeaderboardsAction = mm::kLeaderboardsAction;  // 0x822DEC40
constexpr uint32_t kNoTargetPage = 0xFFFFFFFFu;  // "li r6,-1" (entradas sem página própria)
}  // namespace

// Em 0x822DF04C (só o caminho da pausa passa aqui): true pula para 0x822DF054 e CLASSIFICAÇÕES
// não entra na pausa. As seguintes sobem uma linha (a altura é dada na criação, §13).
bool PcHook_PauseSkipLeaderboards() { return pc::Get().pc_menu_entry; }
static_assert(mm::kPauseSkipTo == mm::kHookPauseSkipLeaderboards + 8,
              "0x822DF04C 'mr r3,r31' + 0x822DF050 'bl 0x822E1380' (CLASSIFICAÇÕES da pausa)");

// Antes de 0x822DF2D0 (lwz r11,340(r30)), no fim da página AJUDA E OPÇÕES (r31 = página 1).
// r22/r23/r24/f27/f31 = layout que o montador usa nesta tela (y inicial, passo, x, escala,
// atraso por entrada); com eles a página 7 (Configurações de PC no menu do jogo) é montada igual.
namespace {
// Acrescenta CONFIGURAÇÕES DE PC no fim da página 1 da pausa. Desde 2026-11-12 a entrada
// NAVEGA para a página 7 (Configurações de PC no menu do jogo, ação 0), como AJUDA E OPÇÕES
// navega para a 1, em vez de abrir a janela sobreposta.
bool AddPausePcEntry(uint32_t page1) {
  auto* ks = rex::system::kernel_state();
  auto* dispatcher = ks ? ks->function_dispatcher() : nullptr;
  auto* ts = rex::runtime::ThreadState::Get();
  if (!dispatcher || !ts) return false;
  __try {
    PPCContext* ctx = ts->context();
    VolatileSnapshot snap{};
    SaveVolatile(*ctx, snap);
    uint64_t args[5] = {page1, kStrPcSettings, kIconKazooie, pc::ingame::PageIndex(), 0};
    dispatcher->Execute(ts, kAddMenuEntry, args, 5);
    RestoreVolatile(*ctx, snap);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}
}  // namespace

void PcHook_HelpPageBuilt(PPCRegister& r30, PPCRegister& r31, PPCRegister& r22, PPCRegister& r23,
                          PPCRegister& r24, PPCRegister& f27, PPCRegister& f31) {
  const uint32_t screen = r30.u32;
  const uint32_t page1 = r31.u32;
  if (!pc::Get().pc_menu_entry || !GuestHeapPtr(screen) || !GuestHeapPtr(page1)) return;
  // Só na pausa: no título a entrada própria fica no menu principal (PcHook_TitleExitEntry).
  if (ReadU8(screen + mm::kScreenIsPauseOff) != 0) {
    const uint32_t before = ReadU32(page1 + 4);
    if (AddPausePcEntry(page1)) {
      pc::state::pause_entry_added.fetch_add(1, std::memory_order_relaxed);
      pc::state::pause_entry_count.store(ReadU32(page1 + 4), std::memory_order_relaxed);
      REXLOG_INFO("pausa: CONFIGURAÇÕES DE PC acrescentada em AJUDA E OPÇÕES ({} -> {} entradas)",
                  before, ReadU32(page1 + 4));
    } else {
      REXLOG_ERROR("pausa: falha ao acrescentar CONFIGURAÇÕES DE PC em AJUDA E OPÇÕES");
    }
  }
  // Raiz das Configurações de PC na página 7 (título e pausa). Vem DEPOIS da entrada da página 1,
  // porque montar a página 7 zera os globais de layout que a entrada nova ainda usa; o montador
  // grava todos de novo no preâmbulo da página seguinte (11).
  pc::ingame::OnPagesBuilt(screen, static_cast<int32_t>(r22.u32), static_cast<int32_t>(r23.u32),
                           static_cast<int32_t>(r24.u32), static_cast<float>(f27.f64),
                           static_cast<float>(f31.f64));
}

// ---------------------------------------------------------------- textos do jogo
// 0x822E9C28 = GetString(id): devolve a tabela de ponteiros montada por 0x822E9C88 ao ler
// RAWFiles\X360_strings.dat (ponteiros em 0x826A60E8, quantidade em 0x826A60F0). Só o id novo
// kStrPcSettings (pedido pelo gancho acima) recebe "CONFIGURAÇÕES DE PC"; os textos do jogo,
// inclusive "CLASSIFICAÇÕES", vêm da tradução sem mudança.
// O gancho também registra pares (id, chamador) para a sonda --probe-menu.
namespace {
// Latin-1, como o arquivo da tradução (rotina de acentos 0x825F3940): Ç = 0xC7, Ã = 0xC3,
// Õ = 0xD5 (os três já aparecem nos textos dos menus, ex.: "RESTAURAR CONFIGURAÇÕES PADRÃO").
// O jogo copia o rótulo para a entrada (até 0x40 bytes, 0x822E5740).
constexpr uint8_t kLabelPc[] = {'C', 'O', 'N', 'F', 'I', 'G', 'U', 'R', 'A', 0xC7,
                                0xD5, 'E', 'S', ' ', 'D', 'E', ' ', 'P', 'C', 0};
constexpr uint8_t kLabelRecomp[] = {'C', 'O', 'N', 'F', 'I', 'G', 'U', 'R', 'A', 0xC7,
                                    0xC3, 'O', ' ', 'D', 'A', ' ', 'R', 'E', 'C', 'O',
                                    'M', 'P', 'I', 'L', 'A', 0xC7, 0xC3, 'O', 0};
constexpr uint8_t kLabelExit[] = {'S', 'A', 'I', 'R', ' ', 'P', 'A', 'R', 'A', ' ',
                                  'O', ' ', 'D', 'E', 'S', 'K', 'T', 'O', 'P', 0};
static_assert(sizeof(kLabelRecomp) <= 0x40 && sizeof(kLabelExit) <= 0x40);

// Cópia do texto na memória do jogo, feita uma vez por rótulo (thread do jogo).
uint32_t GuestLabel(uint32_t& slot, const uint8_t* text, size_t size) {
  if (slot) return slot;
  auto* mem = rex::system::kernel_state()->memory();
  const uint32_t a = mem->SystemHeapAlloc(0x40);
  if (!a) return 0;
  std::memcpy(GuestBase() + a, text, size);
  slot = a;
  return slot;
}

// Versão global (inglês, pedido de 2026-11-13): os mesmos rótulos no idioma do jogo.
constexpr uint8_t kLabelPcEn[] = "PC SETTINGS";
constexpr uint8_t kLabelRecompEn[] = "RECOMPILATION SETTINGS";
constexpr uint8_t kLabelExitEn[] = "EXIT TO DESKTOP";

uint32_t LabelFor(uint32_t id) {
  // O idioma é o dos textos que o jogo já carregou (pc_lang.cpp); cada processo roda uma só
  // pasta do jogo, então o rótulo guardado na memória do jogo não muda depois.
  static uint32_t slot_pc = 0, slot_recomp = 0, slot_exit = 0;
  const bool en = pc::lang::English();
  if (id == kStrPcSettings)
    return en ? GuestLabel(slot_pc, kLabelPcEn, sizeof(kLabelPcEn))
              : GuestLabel(slot_pc, kLabelPc, sizeof(kLabelPc));
  if (id == pc::state::kTitleRecompEntryStringId)
    return en ? GuestLabel(slot_recomp, kLabelRecompEn, sizeof(kLabelRecompEn))
              : GuestLabel(slot_recomp, kLabelRecomp, sizeof(kLabelRecomp));
  if (id == pc::state::kTitleExitEntryStringId)
    return en ? GuestLabel(slot_exit, kLabelExitEn, sizeof(kLabelExitEn))
              : GuestLabel(slot_exit, kLabelExit, sizeof(kLabelExit));
  return 0;
}
}  // namespace

bool PcHook_GetString(PPCRegister& r3, uint64_t& lr) {
  const uint32_t id = r3.u32;
  pc::state::NoteStringRequest(id, static_cast<uint32_t>(lr));
  // Título da confirmação de saída: 0x822DE260 pede GetString(5) com retorno em 0x822DE290
  // (a pergunta, GetString(53), fica "VOCÊ TEM CERTEZA?" como no jogo).
  if (id == mm::kExitTextId && static_cast<uint32_t>(lr) == mm::kExitDialogTitleRet &&
      pc::state::title_exit_armed.load(std::memory_order_acquire)) {
    if (const uint32_t label = LabelFor(pc::state::kTitleExitEntryStringId)) {
      pc::state::title_exit_dialog_labels.fetch_add(1, std::memory_order_relaxed);
      r3.u64 = label;
      return true;
    }
  }
  if (const uint32_t label = LabelFor(id)) {
    r3.u64 = label;
    return true;  // devolve o texto novo sem consultar a tabela (o id não existe nela)
  }
  return false;
}

// ---------------------------------------------------------------- menu principal (título)
// Pedido da pessoa (2026-11-11): ordem final JOGAR, CONQUISTAS, AJUDA E OPÇÕES,
// CONFIGURAÇÃO DA RECOMPILAÇÃO, SAIR PARA O DESKTOP.
//
// Em 0x822DF0A0 (caminho do título de 0x822DEE60) a chamada original registraria
// CLASSIFICAÇÕES (texto 1, ícone 13, página-alvo 3); true pula para 0x822DF0A8 e ela não entra
// no menu. A altura de cada entrada é dada na criação (0x822E5740), então as seguintes sobem.
bool PcHook_TitleSkipLeaderboards() { return pc::Get().pc_menu_entry; }
static_assert(mm::kTitleSkipTo == mm::kHookTitleSkipLeaderboards + 8,
              "0x822DF0A0 'mr r3,r31' + 0x822DF0A4 'bl 0x822E1380' (CLASSIFICAÇÕES)");

// Depois de 0x822DF110 ("li r4,5", só o caminho do título passa aqui; r31 = página 0):
//  1) registra "CONFIGURAÇÃO DA RECOMPILAÇÃO" com página-alvo 7 e sem ação: o próprio menu do
//     jogo navega para as Configurações de PC (pc_ingame_menu.cpp, pedido de 2026-11-12); r4/r6/r7
//     já estão carregados para a saída, por isso os voláteis são salvos e restaurados em volta
//     da chamada (como em PcHook_HelpPageBuilt);
//  2) troca o texto de VOLTAR À BIBLIOTECA DE JOGOS por SAIR PARA O DESKTOP (id novo; o id 5
//     dos textos não muda, e o jogo também tem o id 0x112 com o mesmo texto em outra tela).
void PcHook_TitleExitEntry(PPCRegister& r4, PPCRegister& r31) {
  if (r4.u32 != mm::kExitTextId) return;
  const uint32_t page0 = r31.u32;
  if (pc::Get().pc_menu_entry && GuestHeapPtr(page0)) {
    auto* ks = rex::system::kernel_state();
    auto* dispatcher = ks ? ks->function_dispatcher() : nullptr;
    auto* ts = rex::runtime::ThreadState::Get();
    if (dispatcher && ts) {
      const uint32_t before = ReadU32(page0 + 4);
      __try {
        PPCContext* ctx = ts->context();
        VolatileSnapshot snap{};
        SaveVolatile(*ctx, snap);
        // Navega para a página 7 (Configurações de PC no menu do jogo), sem ação própria.
        uint64_t args[5] = {page0, pc::state::kTitleRecompEntryStringId, kIconKazooie,
                            pc::ingame::PageIndex(), 0};
        dispatcher->Execute(ts, kAddMenuEntry, args, 5);
        RestoreVolatile(*ctx, snap);
        pc::state::title_recomp_entry_built.fetch_add(1, std::memory_order_relaxed);
        REXLOG_INFO("menu principal: CONFIGURAÇÃO DA RECOMPILAÇÃO acrescentada ({} -> {} entradas)",
                    before, ReadU32(page0 + 4));
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        REXLOG_ERROR("menu principal: falha ao acrescentar CONFIGURAÇÃO DA RECOMPILAÇÃO");
      }
    }
  }
  r4.u64 = pc::state::kTitleExitEntryStringId;
  pc::state::title_exit_entry_built.fetch_add(1, std::memory_order_relaxed);
}

// Regra do jogo original (MECHANICS.md §15): a ação 0x822DED40 (0x822E2860 a chama sem exigir
// perfil) não sai na hora; no jogo completo ela abre a confirmação do próprio jogo
// (0x822DE260: título = texto 5, pergunta = texto 53 "VOCÊ TEM CERTEZA?"). O port mantém essa
// confirmação: aqui só marca que ela veio do menu principal (tela+0x198 = 0) e deixa o original
// seguir. O título vira SAIR PARA O DESKTOP (PcHook_GetString), o SIM fecha o programa
// (PcHook_ExitConfirmed) e o NÃO volta ao menu como no original (PcHook_ExitCancelled).
bool PcHook_TitleExitSelect(PPCRegister& r3) {
  const uint32_t screen = r3.u32;
  if (!GuestHeapPtr(screen) || ReadU8(screen + mm::kScreenIsPauseOff) != 0) return false;
  pc::state::title_exit_activations.fetch_add(1, std::memory_order_relaxed);
  pc::state::title_exit_armed.store(true, std::memory_order_release);
  REXLOG_INFO("menu principal: SAIR PARA O DESKTOP escolhida; confirmação do jogo aberta");
  return false;  // o original abre a confirmação
}

// 0x822DD720 = SIM confirmado (callback em tela+0x22C, chamado por 0x822E2538 com r3 = tela).
// No original chama 0x825B1EB8("XLNI_DASH_ARCADE") para ir ao painel do console. Só quando a
// confirmação veio de SAIR PARA O DESKTOP: fecha o programa pelo caminho normal (o monitor do
// app chama o mesmo caminho do botão de fechar a janela) e não executa o original. Outros usos
// desse callback (0x822DDBA0 também é registrado por 0x822DE9A8 e 0x82473048) seguem o jogo.
bool PcHook_ExitConfirmed(PPCRegister& r3) {
  if (!pc::state::title_exit_armed.exchange(false, std::memory_order_acq_rel)) return false;
  const uint32_t screen = r3.u32;
  if (!GuestHeapPtr(screen) || ReadU8(screen + mm::kScreenIsPauseOff) != 0) return false;
  pc::state::title_exit_confirmed.fetch_add(1, std::memory_order_relaxed);
  pc::state::exit_to_desktop_request.store(true, std::memory_order_release);
  REXLOG_INFO("menu principal: SAIR PARA O DESKTOP confirmada (SIM); encerrando o programa");
  return true;
}

// 0x822DDA70 = voltar da confirmação (NÃO). Usado por várias caixas do jogo; só desarma.
void PcHook_ExitCancelled(PPCRegister& r3) {
  (void)r3;
  if (pc::state::title_exit_armed.exchange(false, std::memory_order_acq_rel)) {
    pc::state::title_exit_cancelled.fetch_add(1, std::memory_order_relaxed);
    REXLOG_INFO("menu principal: SAIR PARA O DESKTOP cancelada (NÃO); volta ao menu");
  }
}

// CONQUISTAS (M04, C-6): ação 0x822DD628 do jogo, antes de criar a pilha. Com a camada de PC
// abre a lista de conquistas do PC no menu do jogo (a página 7 na vista CONQUISTAS; B volta).
bool PcHook_AchievementsSelect(PPCRegister& r3) {
  (void)r3;
  if (!pc::Get().pc_menu_entry) return false;  // original: XamShowAchievementsUI (sem efeito)
  pc::state::achievements_opened.fetch_add(1, std::memory_order_relaxed);
  pc::ingame::RequestOpenView(pc::menu::options::kAchievementsView);
  pc::log::Event("menus", "CONQUISTAS: lista de conquistas do PC (no lugar do painel do Xbox)");
  return true;
}

// Ação compartilhada (0x822DEC40), antes de o handler criar a pilha: retornar aqui não deixa
// estado pela metade. Só a entrada nova (página-alvo -1) abre o menu de PC; CLASSIFICAÇÕES
// (página-alvo 3) segue o código original (perfil Xbox LIVE). Repete o único efeito do início
// do original que não depende do Xbox LIVE: o campo +0x220 da tela volta de 1 para 0.
bool PcHook_LeaderboardsSelect(PPCRegister& r3) {
  if (!pc::Get().pc_menu_entry) return false;
  const uint32_t screen = r3.u32;
  if (!GuestHeapPtr(screen)) return false;
  const uint32_t pages = ReadU32(screen + 0x154);
  const uint32_t page = ReadU32(screen + 0x2C);
  if (!GuestHeapPtr(pages) || page > 16) return false;
  // Linhas das Configurações de PC no menu do jogo (página 7): A troca o valor da linha.
  if (page == pc::ingame::PageIndex()) return pc::ingame::OnSelect(screen);
  const uint32_t entries = ReadU32(pages + page * 0x2C);
  const uint32_t count = ReadU32(pages + page * 0x2C + 4);
  const uint32_t cursor = ReadU8(screen + mm::kScreenCursorOff);
  if (!GuestHeapPtr(entries) || cursor >= count) return false;
  const uint32_t entry = entries + cursor * mm::kEntrySize;
  if (ReadU32(entry + mm::kEntryActionOff) != kLeaderboardsAction ||
      ReadU32(entry + mm::kEntryTargetPageOff) != kNoTargetPage)
    return false;  // CLASSIFICAÇÕES: comportamento original
  uint8_t* f = GuestBase() + screen + 0x220;
  if (f[0] == 0 && f[1] == 0 && f[2] == 0 && f[3] == 1) f[3] = 0;
  pc::state::pc_entry_activations.fetch_add(1, std::memory_order_relaxed);
  pc::state::menu_open_request.store(true, std::memory_order_relaxed);
  return true;
}
