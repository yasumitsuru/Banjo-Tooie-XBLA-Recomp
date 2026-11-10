// banjotooie - ReXGlue Recompiled Project

#include "generated/default/banjotooie_init.h"

#include "banjotooie_app.h"
#include "port_diagnostics.h"

// MISSAO 01 - gancho de instrumentacao (nao muda o comportamento do jogo).
// 0x822FA358 e a funcao do jogo que incrementa o contador em +0x40B0 e chama VdSwap (ver
// MECHANICS.md e o codigo gerado). As chamadas no codigo gerado usam o simbolo fraco
// sub_822FA358; esta definicao forte conta um quadro e chama a original (__imp__).
extern "C" REX_FUNC(sub_822FA358) {
  port::GuestPresentCount().fetch_add(1, std::memory_order_relaxed);
  __imp__sub_822FA358(ctx, base);
}

REX_DEFINE_APP(banjotooie, BanjotooieApp::Create)
