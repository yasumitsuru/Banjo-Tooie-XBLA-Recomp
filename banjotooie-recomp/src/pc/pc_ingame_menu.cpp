// "CONFIGURAÇÕES DE PC" dentro do menu do PRÓPRIO jogo (pedido de 2026-11-12). Ver o cabeçalho
// e MECHANICS.md §18. Resumo do que foi lido no default.xex e é usado aqui:
//
//  - A tela de menus (título e pausa) é o objeto 0x82648110. tela+0x2C = página atual,
//    tela+7 = cursor, tela+4 = estado (6 = esperando entrada), tela+0x154 = vetor de 12 páginas
//    de 44 bytes (0x822DB640). O montador 0x822DEE60 monta 0-6 e 8-11: a 7 fica livre.
//  - Página: +0 entradas (136 bytes cada), +4 contagem, +0x10 última selecionável, +0x15 rola,
//    +0x24 callback por quadro, +0x28 callback ao sair com B. 0x822E1188 zera a página e os
//    globais de layout; 0x822E1230 acrescenta (texto copiado para entrada+0x3B, 64 bytes);
//    0x822E11E8 liga a rolagem com n linhas visíveis.
//  - Entrada: +0x28 página-alvo (A), +0x2C página de volta (B, do global 0x826A5AB8),
//    +0x7C ação (A chama a ação no lugar de navegar).
//  - Troca de página (estado 8, 0x822E3230): tela+0x2C = página-alvo (A) ou de volta (B); as
//    linhas novas são criadas e o estado 4 põe o cursor em tela[(página+18)*4].
//  - A página CONFIGURAÇÕES do jogo (2) usa o callback 0x822DC728: estado 6, avisos de botão
//    0x822E50C0(tela, bits), esquerda/direita pelo analógico (tela+0x1E8 > 0,75) ou direcional
//    (tela+0x1C4/0x1C8 == 1), repetição tela+0x25 = 6 quadros, texto novo da linha por
//    0x822E13D8 + 0x8225CD08/0x8225C9F0. A página 7 faz o mesmo, com as opções de PC.
//
// Tudo aqui roda na THREAD DO JOGO (ganchos do código recompilado). Nada do jogo é copiado:
// o código chama as rotinas do próprio jogo e só escreve os campos que elas leem.
#include "pc_ingame_menu.h"

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "mechanics_values.h"
#include "pc_menu.h"
#include "pc_settings.h"
#include "scripted_input.h"

namespace {

namespace mm = pc::mechanics;
namespace ig = pc::mechanics::ingame;
namespace opt = pc::menu::options;

constexpr int kRootView = -1;
constexpr int kNoView = -2;
constexpr uint32_t kNoTargetPage = 0xFFFFFFFFu;
// Linhas visíveis numa seção que rola: 5, como a página COMO JOGAR do jogo (0x822DEE60
// 'li r4,5' antes de 0x822E11E8), no título e na pausa. Com 6 na pausa a 7ª linha esmaecida
// (a "próxima", y 188) encostava nos avisos de botão de baixo (captura st1, 2026-11-12).
constexpr uint32_t kVisibleRowsPause = ig::kHelpScrollRows;
constexpr uint32_t kVisibleRowsTitle = ig::kHelpScrollRows;

// ---------------------------------------------------------------- memória do jogo (big-endian)
uint8_t* Base() { return rex::system::kernel_state()->memory()->virtual_membase(); }
uint8_t R8(uint32_t a) { return Base()[a]; }
uint32_t R32(uint32_t a) {
  const uint8_t* p = Base() + a;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
float F32(uint32_t a) { return std::bit_cast<float>(R32(a)); }
void W8(uint32_t a, uint8_t v) { Base()[a] = v; }
void W32(uint32_t a, uint32_t v) {
  uint8_t* p = Base() + a;
  p[0] = uint8_t(v >> 24);
  p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);
  p[3] = uint8_t(v);
}
bool GuestPtr(uint32_t a) { return a >= 0x40000000u && a < 0xA0000000u; }

std::string ReadCStr(uint32_t a, uint32_t max) {
  std::string s;
  for (uint32_t i = 0; i < max; ++i) {
    const uint8_t c = R8(a + i);
    if (!c) break;
    s += char(c);
  }
  return s;
}

// UTF-8 -> Latin-1 (a codificação dos textos do jogo, X360_strings.dat). Corta em 63 bytes:
// o jogo copia no máximo 64 com o zero final (0x822E5740 / 0x822E5868).
std::string ToLatin1(const std::string& u8) {
  std::string out;
  for (size_t i = 0; i < u8.size();) {
    const unsigned char c = static_cast<unsigned char>(u8[i]);
    if (c < 0x80) {
      out += char(c);
      ++i;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < u8.size()) {
      const unsigned cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(u8[i + 1]) & 0x3Fu);
      out += cp <= 0xFF ? char(cp) : '?';
      i += 2;
    } else {
      // Pontuação tipográfica de 3 bytes (textos da tabela XDBF das CONQUISTAS): aspas e
      // apóstrofo curvos, travessão e reticências viram os equivalentes simples da fonte.
      const unsigned cp = (c & 0xF0) == 0xE0 && i + 2 < u8.size()
                              ? ((c & 0x0Fu) << 12) |
                                    ((static_cast<unsigned char>(u8[i + 1]) & 0x3Fu) << 6) |
                                    (static_cast<unsigned char>(u8[i + 2]) & 0x3Fu)
                              : 0;
      if (cp == 0x2018 || cp == 0x2019) out += '\'';
      else if (cp == 0x201C || cp == 0x201D) out += '"';
      else if (cp == 0x2013 || cp == 0x2014) out += '-';
      else if (cp == 0x2026) out += "...";
      else out += '?';
      i += (c & 0xF0) == 0xE0 ? 3 : 4;
    }
  }
  if (out.size() > mm::kEntryLabelMax - 1) out.resize(mm::kEntryLabelMax - 1);
  return out;
}

// ---------------------------------------------------------------- chamar rotinas do jogo
// Mesma técnica dos outros ganchos (pc_hooks.cpp): os registradores VOLÁTEIS do ABI PowerPC
// são salvos e restaurados em volta da chamada, porque o código em volta do gancho ainda usa.
struct VolatileSnapshot {
  PPCRegister r0, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12;
  PPCRegister f[14];
  PPCVRegister v[14];
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

// Chama uma função do jogo na thread atual (r3.. = args). Exceções viram 'ok = false'.
uint64_t CallGuestRaw(uint32_t fn, const uint64_t* args, size_t n, bool* ok) {
  auto* ks = rex::system::kernel_state();
  auto* dispatcher = ks ? ks->function_dispatcher() : nullptr;
  auto* ts = rex::runtime::ThreadState::Get();
  if (!dispatcher || !ts) {
    if (ok) *ok = false;
    return 0;
  }
  uint64_t result = 0;
  __try {
    PPCContext* ctx = ts->context();
    VolatileSnapshot snap{};
    SaveVolatile(*ctx, snap);
    result = dispatcher->Execute(ts, fn, const_cast<uint64_t*>(args), n);
    RestoreVolatile(*ctx, snap);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    if (ok) *ok = false;
    return 0;
  }
  return result;
}

uint64_t CallGuest(uint32_t fn, std::initializer_list<uint64_t> args, bool* ok = nullptr) {
  uint64_t a[8] = {};
  size_t n = 0;
  for (uint64_t v : args)
    if (n < 8) a[n++] = v;
  return CallGuestRaw(fn, a, n, ok);
}

// ---------------------------------------------------------------- estado
struct Layout {
  bool valid = false;
  bool pause = false;
  int32_t y0 = 0, step = 0, x = 0;
  uint32_t scale_bits = 0, scale2_bits = 0, delay_bits = 0;
};

Layout g_layout;
int g_view = kNoView;             // vista montada na página 7
uint32_t g_root_parent = 1;       // página de volta da raiz (B)
uint32_t g_scratch = 0;           // 64 bytes na memória do jogo para passar textos
uint32_t g_last_cursor = 0xFF;    // cursor no último quadro tratado
uint32_t g_seen_generation = 0;   // geração das opções já desenhada

std::atomic<bool> g_toggle_request{false};
std::atomic<int> g_open_view{kNoView};  // vista pedida por RequestOpenView (CONQUISTAS)
bool g_direct_view = false;             // vista aberta direto: B volta à página de origem
std::atomic<int> g_jump_armed{0};  // quadros restantes para a troca pedida pelo F1 acontecer
int g_request_frames = 0;          // quadros restantes para atender o F1
bool g_request_first = false;
bool g_request_tapped = false;
int g_request_stable = 0;          // quadros seguidos com o menu no estado 6

std::atomic<uint32_t> s_built{0}, s_rows{0}, s_switches{0}, s_changes{0}, s_refreshes{0},
    s_f1_jumps{0}, s_f1_taps{0}, s_ticks{0};
std::atomic<int32_t> s_view{kNoView};

Layout FallbackLayout(uint32_t screen) {
  // Os valores que 0x822DEE60 escolhe (MECHANICS.md §13), se o montador ainda não passou.
  Layout l;
  l.valid = true;
  l.pause = R8(screen + mm::kScreenIsPauseOff) != 0;
  l.y0 = int32_t(l.pause ? mm::kPauseStartY : mm::kTitleStartY);
  l.step = int32_t(l.pause ? mm::kPauseStepY : mm::kTitleStepY);
  l.x = l.pause ? 58 : 25;  // 0x82005AC4 / 0x82005AB0
  l.scale_bits = std::bit_cast<uint32_t>(l.pause ? 0.5f : 0.65f);    // 0x82005ACC / 0x82005AD4
  l.scale2_bits = std::bit_cast<uint32_t>(l.pause ? 0.45f : 0.6f);   // 0x82005AD0 / 0x82005AD8
  l.delay_bits = std::bit_cast<uint32_t>(ig::kLetterDelay);
  return l;
}

uint32_t PageAddr(uint32_t screen) {
  const uint32_t pages = R32(screen + ig::kScreenPagesOff);
  return GuestPtr(pages) ? pages + ig::kPcPageIndex * ig::kPageSize : 0;
}

void WriteScratch(const std::string& latin1) {
  if (!g_scratch) {
    g_scratch = rex::system::kernel_state()->memory()->SystemHeapAlloc(mm::kEntryLabelMax);
    if (!g_scratch) return;
  }
  std::memset(Base() + g_scratch, 0, mm::kEntryLabelMax);
  std::memcpy(Base() + g_scratch, latin1.data(), std::min<size_t>(latin1.size(), 63));
}

// Monta a página 7 com a vista pedida (raiz ou seção), pelas rotinas do próprio jogo.
bool BuildPage(uint32_t screen, int view, uint32_t parent, bool direct = false) {
  const uint32_t page = PageAddr(screen);
  if (!page) return false;
  std::vector<std::string> texts;
  const bool section = view >= 0;
  if (section) {
    for (int i = 0; i < opt::RowCount(view); ++i) texts.push_back(ToLatin1(opt::RowText(view, i)));
  } else {
    for (int i = 0; i < opt::SectionCount(); ++i) texts.push_back(ToLatin1(opt::SectionTitle(i)));
  }
  if (texts.empty()) return false;
  WriteScratch(std::string());
  if (!g_scratch) return false;
  const Layout l = g_layout.valid ? g_layout : FallbackLayout(screen);
  bool ok = true;
  CallGuest(ig::kPageResetFn, {page, 1}, &ok);  // zera a página e os globais (0x822E5660)
  if (!ok) return false;
  // Mesmo preâmbulo que 0x822DEE60 grava antes de cada página.
  W32(ig::kLayoutDelay, l.delay_bits);
  W32(ig::kLayoutScale, l.scale_bits);
  W32(ig::kLayoutX, uint32_t(l.x));
  W32(ig::kLayoutY, uint32_t(l.y0));
  W32(ig::kLayoutStep, uint32_t(l.step));
  W32(ig::kLayoutScale2, l.scale2_bits);
  W32(ig::kLayoutParent, section && !direct ? ig::kPcPageIndex : parent);
  for (size_t i = 0; i < texts.size() && ok; ++i) {
    WriteScratch(texts[i]);
    CallGuest(ig::kAppendEntryFn, {page, g_scratch, mm::kKazooieIcon}, &ok);
    const uint32_t entries = R32(page);
    if (!ok || !GuestPtr(entries) || R32(page + 4) != i + 1) {
      ok = false;
      break;
    }
    const uint32_t e = entries + uint32_t(i) * mm::kEntrySize;
    // Raiz: A navega para a própria página 7 (o gancho de troca monta a seção).
    // Seção: A chama a ação compartilhada 0x822DEC40, que PcHook_LeaderboardsSelect manda para
    // OnSelect (o jogo não navega quando a entrada tem ação).
    W32(e + mm::kEntryTargetPageOff, section ? kNoTargetPage : ig::kPcPageIndex);
    W32(e + mm::kEntryActionOff, section ? mm::kLeaderboardsAction : 0u);
  }
  if (!ok) {
    REXLOG_ERROR("menu de PC no jogo: falha ao montar a página {} (vista {})", ig::kPcPageIndex,
                 view);
    CallGuest(ig::kPageResetFn, {page, 1});
    return false;
  }
  const uint32_t visible = l.pause ? kVisibleRowsPause : kVisibleRowsTitle;
  if (texts.size() > visible) CallGuest(ig::kPageScrollFn, {page, visible});
  W32(page + ig::kPageTickOff, ig::kHookPageTick);  // 0x822DC728 -> PcHook_PcPageTick
  W32(page + ig::kPageExitOff, 0);
  g_view = view;
  g_direct_view = direct;
  g_last_cursor = 0xFF;
  g_seen_generation = opt::Generation();
  s_view.store(view);
  s_rows.store(uint32_t(texts.size()));
  s_built.fetch_add(1);
  return true;
}

// Reescreve na tela as linhas cujo texto mudou (como 0x822DC728 faz com INVERTER EIXO Y).
void RefreshRows(uint32_t screen, uint32_t page) {
  if (g_view < 0) return;
  const uint32_t entries = R32(page);
  const uint32_t count = R32(page + 4);
  if (!GuestPtr(entries)) return;
  for (uint32_t i = 0; i < count && int(i) < opt::RowCount(g_view); ++i) {
    const uint32_t e = entries + i * mm::kEntrySize;
    const std::string want = ToLatin1(opt::RowText(g_view, int(i)));
    if (ReadCStr(e + mm::kEntryLabelOff, mm::kEntryLabelMax) == want) continue;
    WriteScratch(want);
    CallGuest(ig::kSetEntryTextFn, {page, i, g_scratch});
    const uint32_t row = R32(screen + ig::kScreenRowsOff + i * 4);
    if (GuestPtr(row) && R8(e + ig::kEntryShownOff)) {
      CallGuest(ig::kRowResetFn, {row});
      CallGuest(ig::kRowTextFn, {row, e + mm::kEntryLabelOff});
    }
    s_refreshes.fetch_add(1);
  }
}

void BounceRow(uint32_t screen, uint32_t cursor) {
  const uint32_t row = R32(screen + ig::kScreenRowsOff + cursor * 4);
  if (GuestPtr(row)) CallGuest(ig::kRowBounceFn, {row});
}

// Um quadro da página 7 no estado 6 (mesmo papel de 0x822DC728 na página 2).
void Tick(uint32_t screen) {
  const uint32_t page = PageAddr(screen);
  if (!page) return;
  const uint32_t cursor = R8(screen + mm::kScreenCursorOff);
  bool refresh = false;
  if (cursor != g_last_cursor) {
    if (g_last_cursor != 0xFF) {
      opt::RowLeft();
      refresh = true;
    }
    g_last_cursor = cursor;
  }
  if (opt::Generation() != g_seen_generation) {
    g_seen_generation = opt::Generation();
    refresh = true;
  }
  const bool section = g_view >= 0;
  const bool value = section && opt::RowIsValue(g_view, int(cursor));
  // Avisos de botão do jogo: VOLTAR sempre; ANTERIOR/PRÓXIMO nas linhas de valor, SELECIONAR
  // nas outras; ROLAR PARA CIMA/BAIXO quando a página rola (como o padrão de 0x822E2860).
  uint64_t bits = 1ull << ig::kPromptBackBit;
  bits |= value ? ((1ull << ig::kPromptPrevBit) | (1ull << ig::kPromptNextBit))
                : (1ull << ig::kPromptSelectBit);
  if (R8(page + ig::kPageScrollOff)) {
    if (cursor != 0) bits |= 1ull << ig::kPromptScrollUpBit;
    if (int32_t(cursor) < int32_t(R32(page + ig::kPageLastOff)))
      bits |= 1ull << ig::kPromptScrollDownBit;
  }
  if (R8(screen + mm::kScreenIsPauseOff) == 0 && CallGuest(ig::kLicenseFn, {}) != 0)
    bits |= 1ull << ig::kPromptStorageBit;
  CallGuest(ig::kPromptsFn, {screen, bits});

  if (value && R8(screen + ig::kScreenRepeatOff) == 0 && !opt::CaptureActive()) {
    const float sx = F32(screen + ig::kScreenStickXOff);
    int dir = 0;
    if (sx > ig::kStickThreshold || R32(screen + ig::kScreenDpadRightOff) == 1)
      dir = 1;
    else if (sx < -ig::kStickThreshold || R32(screen + ig::kScreenDpadLeftOff) == 1)
      dir = -1;
    if (dir != 0) {
      if (opt::RowInput(g_view, int(cursor), dir)) {
        BounceRow(screen, cursor);
        s_changes.fetch_add(1);
        refresh = true;
      }
      W8(screen + ig::kScreenFlags21Off, uint8_t(R8(screen + ig::kScreenFlags21Off) & 0xBF));
      W8(screen + ig::kScreenRepeatOff, uint8_t(ig::kRepeatFrames));
    }
  }
  if (refresh) RefreshRows(screen, page);
}

}  // namespace

namespace pc::ingame {

uint32_t PageIndex() { return ig::kPcPageIndex; }

void RequestToggle() { g_toggle_request.store(true); }

void RequestOpenView(int view) {
  g_open_view.store(view);
  g_toggle_request.store(true);
}

void OnPagesBuilt(uint32_t screen, int32_t y0, int32_t step, int32_t x, float scale,
                  float delay) {
  if (!GuestPtr(screen)) return;
  Layout l;
  l.valid = true;
  l.pause = R8(screen + mm::kScreenIsPauseOff) != 0;
  l.y0 = y0;
  l.step = step;
  l.x = x;
  l.scale_bits = std::bit_cast<uint32_t>(scale);
  l.scale2_bits = R32(ig::kLayoutScale2);  // o montador acabou de gravar para a página 1
  l.delay_bits = std::bit_cast<uint32_t>(delay);
  if (l.step <= 0 || l.step > 64 || l.y0 <= 0 || l.y0 > 240) l = FallbackLayout(screen);
  g_layout = l;
  g_root_parent = l.pause ? mm::kHelpPageIndex : 0u;
  if (BuildPage(screen, kRootView, g_root_parent))
    REXLOG_INFO("menu de PC no jogo: página {} montada ({}; y {} passo {} x {})",
                ig::kPcPageIndex, l.pause ? "pausa" : "título", l.y0, l.step, l.x);
}

bool OnSelect(uint32_t screen) {
  if (!GuestPtr(screen) || R32(screen + ig::kScreenPageOff) != ig::kPcPageIndex) return false;
  if (g_view < 0) return true;  // a raiz navega (ação 0); nada a fazer aqui
  const uint32_t cursor = R8(screen + mm::kScreenCursorOff);
  if (opt::RowInput(g_view, int(cursor), 0)) {
    BounceRow(screen, cursor);
    s_changes.fetch_add(1);
  }
  if (const uint32_t page = PageAddr(screen)) RefreshRows(screen, page);
  return true;
}

void FrameTick() {
  if (int left = g_jump_armed.load(); left > 0) g_jump_armed.store(left - 1);
  if (g_toggle_request.exchange(false)) {
    g_request_frames = 240;  // 4 s a 60 quadros
    g_request_first = true;
    g_request_tapped = false;
    g_request_stable = 0;
  }
  if (g_request_frames <= 0) {
    // pedido que não foi atendido não vale para o próximo F1 (mas a troca de página já pedida
    // ainda vai acontecer nos próximos quadros: g_jump_armed)
    if (g_jump_armed.load() == 0) g_open_view.store(kNoView);
    return;
  }
  --g_request_frames;
  const uint32_t screen = ig::kScreenAddr;
  const uint8_t state = R8(screen + ig::kScreenStateOff);
  const uint32_t page = R32(screen + ig::kScreenPageOff);
  const bool pause = R8(screen + mm::kScreenIsPauseOff) != 0;
  if (state == ig::kStateActive && page < ig::kPageCount) {
    if (page == ig::kPcPageIndex) {
      // F1 de novo, já nas páginas de PC: fecha (pausa -> volta ao jogo; título -> volta).
      if (g_request_first && g_open_view.load() == kNoView)
        pc::input::RequestTap(pause ? pc::input::kStart : pc::input::kB);
      g_open_view.store(kNoView);
      g_request_frames = 0;
      return;
    }
    g_request_first = false;
    if (R8(screen + ig::kScreenBusyOff) != 0 || R32(screen + mm::kScreenCallbackOff) != 0) return;
    // Pausa aberta pelo START injetado: espera o toque acabar e o menu ficar parado alguns
    // quadros. Senão o START ainda apertado chega à página nova e o próprio menu fecha a pausa
    // (visto no auto-teste de 2026-11-12: página 0 -> 7 e a pausa fechou em seguida).
    if (g_request_tapped && (pc::input::TapPending() || ++g_request_stable < 12)) return;
    // Como o A faz (0x822E2860): guarda o cursor desta página, marca "indo" e some a página
    // (estado 7). No estado 8 o gancho de troca troca a página-alvo por 7.
    W32(screen + ig::kScreenSavedCursorOff + page * 4, R8(screen + mm::kScreenCursorOff));
    W32(screen + ig::kScreenDirOff, 0);
    g_jump_armed.store(120);
    bool ok = true;
    CallGuest(ig::kSetStateFn, {screen, ig::kStateLeavePage}, &ok);
    if (!ok) g_jump_armed.store(0);
    s_f1_jumps.fetch_add(1);
    g_request_frames = 0;
    REXLOG_INFO("menu de PC no jogo: F1 na página {} do menu ({})", page,
                pause ? "pausa" : "título");
    return;
  }
  g_request_first = false;
  g_request_stable = 0;
  // Jogo livre (sem cena) e sem pausa: abre a pausa com START; depois vai para a página 7.
  if (!g_request_tapped && R8(mm::kCutsceneStateAddr) == 0 && R8(ig::kPauseFlagAddr) == 0) {
    pc::input::RequestTap(pc::input::kStart);
    g_request_tapped = true;
    s_f1_taps.fetch_add(1);
  }
}

Status GetStatus() {
  Status s;
  s.built = s_built.load();
  s.view = s_view.load();
  s.rows = s_rows.load();
  s.switches = s_switches.load();
  s.changes = s_changes.load();
  s.refreshes = s_refreshes.load();
  s.f1_jumps = s_f1_jumps.load();
  s.f1_start_taps = s_f1_taps.load();
  s.ticks = s_ticks.load();
  return s;
}

}  // namespace pc::ingame

// ---------------------------------------------------------------- ganchos (pc_hooks.toml)

// 0x822E3298 'stw r11,44(r31)' (estado 8, troca de página): r11 = página nova, r31 = tela.
void PcHook_MenuPageSwitch(PPCRegister& r11, PPCRegister& r31) {
  const uint32_t screen = r31.u32;
  if (!GuestPtr(screen)) return;
  const uint32_t old_page = R32(screen + ig::kScreenPageOff);
  uint32_t new_page = r11.u32;
  const bool back = R32(screen + ig::kScreenDirOff) != 0;
  bool jump = false;
  if (g_jump_armed.exchange(0) > 0 && !back && old_page != ig::kPcPageIndex) {
    jump = true;
    new_page = ig::kPcPageIndex;
  }
  if (new_page != ig::kPcPageIndex) {
    if (old_page == ig::kPcPageIndex) opt::RowLeft();
    return;
  }
  int view = kRootView;
  uint32_t cursor = 0;
  uint32_t parent = g_root_parent;
  bool direct = false;
  if (jump) {
    parent = old_page;
    const int req = g_open_view.exchange(kNoView);
    if (req != kNoView) {
      view = req;  // CONQUISTAS: a vista direto, B volta para a página de onde veio
      direct = true;
    }
  } else if (old_page == ig::kPcPageIndex) {
    const uint32_t at = R8(screen + mm::kScreenCursorOff);
    if (!back && g_view == kRootView && int(at) < opt::SectionCount()) {
      view = int(at);  // A numa seção da raiz
    } else if (back && g_view >= 0) {
      cursor = uint32_t(g_view);  // B numa seção: volta à raiz com o cursor nela
    }
  } else {
    parent = old_page;  // entrou por AJUDA E OPÇÕES (pausa) ou pelo menu principal
  }
  if (!BuildPage(screen, view, parent, direct)) {
    // Falhou: pelo F1, fica na página de onde saiu (o alvo da entrada sob o cursor poderia ser
    // -1, que fecharia o menu); pela navegação normal, segue a escolha do jogo.
    if (jump) r11.u64 = old_page;
    return;
  }
  if (view == kRootView) g_root_parent = parent;
  W32(screen + ig::kScreenSavedCursorOff + ig::kPcPageIndex * 4, cursor);
  r11.u64 = new_page;
  s_switches.fetch_add(1);
  REXLOG_INFO("menu de PC no jogo: página {} -> {} ({}, vista {}, volta para {})", old_page,
              new_page, jump ? "F1" : (back ? "B" : "A"), view, parent);
}

// 0x822DC728 (callback por quadro de CONFIGURAÇÕES): trata a página 7; devolve false na 2.
bool PcHook_PcPageTick(PPCRegister& r3) {
  const uint32_t screen = r3.u32;
  if (!GuestPtr(screen) || R32(screen + ig::kScreenPageOff) != ig::kPcPageIndex) return false;
  s_ticks.fetch_add(1);
  if (R8(screen + ig::kScreenStateOff) != ig::kStateActive) return true;
  __try {
    Tick(screen);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXLOG_ERROR("menu de PC no jogo: exceção no quadro da página");
    }
  }
  return true;
}
