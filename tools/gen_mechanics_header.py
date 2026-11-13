"""Generate the mechanics header used by the PC layer of the port.

Reads app/mechanics.json (real values decompiled from default.xex, see
app/MECHANICS.md section 12) and writes:

    app/banjotooie-recomp/src/pc/mechanics_values.h

Before writing it validates the values against app/banjotooie-recomp/pc_hooks.toml:
every K-60 hook must exist with the decompiled address. If the hook table and the
decompiled values disagree, the build fails here instead of shipping a half-wired game.

Exit codes: 0 ok, 2 mismatch, 3 missing input files.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
MECHANICS = os.path.join(APP, "mechanics.json")
TOML_PATH = os.path.join(APP, "banjotooie-recomp", "pc_hooks.toml")
HEADER_PATH = os.path.join(APP, "banjotooie-recomp", "src", "pc", "mechanics_values.h")


def fail(msg):
    print("[mechanics] ERRO: %s" % msg)
    sys.exit(2)


def parse_toml_hooks(path):
    """Return {name: address:int} for every [[midasm_hook]] block."""
    hooks = {}
    name = addr = None

    def flush():
        nonlocal name, addr
        if name is not None and addr is not None:
            hooks[name] = addr
        name = addr = None

    for line in open(path, encoding="utf-8"):
        if line.startswith("[[midasm_hook]]"):
            flush()
            continue
        m = re.match(r'\s*address\s*=\s*(0x[0-9a-fA-F]+)\s*$', line)
        if m:
            addr = int(m.group(1), 16)
            continue
        m = re.match(r'\s*name\s*=\s*"([^"]+)"\s*$', line)
        if m:
            name = m.group(1)
            continue
    flush()
    return hooks


def main():
    if not os.path.exists(MECHANICS):
        fail("app/mechanics.json nao existe (%s)" % MECHANICS)
    if not os.path.exists(TOML_PATH):
        fail("pc_hooks.toml nao existe (%s)" % TOML_PATH)
    with open(MECHANICS, encoding="utf-8") as f:
        mech = json.load(f)

    pc = mech.get("pc") or {}
    frame = pc.get("frame") or {}
    k60 = pc.get("k60") or []

    # ---- validation against the hook table
    hooks = parse_toml_hooks(TOML_PATH)
    expect = {
        "PcHook_FrameStep": int(frame["hook_frame_step"], 16),
        "PcHook_PresentInterval": int(frame["hook_present_interval"], 16),
    }
    for name, addr in expect.items():
        if hooks.get(name) != addr:
            fail("%s: endereco no pc_hooks.toml (%s) difere do decompilado (%s)" %
                 (name, hex(hooks.get(name, 0)), hex(addr)))
    for k in k60:
        name = "PcHook_K_%s" % k["hook"][2:].upper()
        addr = int(k["hook"], 16)
        if hooks.get(name) != addr:
            fail("%s: ausente ou com endereco diferente no pc_hooks.toml (decompilado: %s)" %
                 (name, hex(addr)))
    menu = pc.get("menu") or {}
    if not menu:
        fail("mechanics.json: falta o bloco pc.menu (MECHANICS.md secao 15)")
    menu_hooks = {
        "PcHook_TitleSkipLeaderboards": menu["hook_title_skip_leaderboards"],
        "PcHook_TitleExitEntry": menu["hook_title_exit_entry"],
        "PcHook_PauseSkipLeaderboards": menu["hook_pause_skip_leaderboards"],
        "PcHook_HelpPageBuilt": menu["hook_help_page_built"],
        "PcHook_GetString": menu["hook_get_string"],
        "PcHook_LeaderboardsSelect": menu["leaderboards_action"],
        "PcHook_TitleExitSelect": menu["exit_action"],
        "PcHook_ExitConfirmed": menu["exit_yes_callback"],
        "PcHook_ExitCancelled": menu["exit_no_callback"],
    }
    for name, addr in menu_hooks.items():
        if hooks.get(name) != int(addr, 16):
            fail("%s: endereco no pc_hooks.toml (%s) difere do decompilado (%s)" %
                 (name, hex(hooks.get(name, 0)), addr))
    # Paginas de PC dentro do menu do jogo (pedido de 2026-11-12, MECHANICS.md secao 18).
    ingame = pc.get("ingame_menu") or {}
    if not ingame:
        fail("mechanics.json: falta o bloco pc.ingame_menu (MECHANICS.md secao 18)")
    for name, key in (("PcHook_MenuPageSwitch", "hook_page_switch"),
                      ("PcHook_PcPageTick", "hook_page_tick")):
        if hooks.get(name) != int(ingame[key], 16):
            fail("%s: endereco no pc_hooks.toml (%s) difere do decompilado (%s)" %
                 (name, hex(hooks.get(name, 0)), ingame[key]))
    if int(ingame["pc_page_index"]) >= int(ingame["page_count"]):
        fail("pc.ingame_menu: pc_page_index fora do vetor de paginas do jogo")

    # ---- header text
    lines = []
    w = lines.append
    w("// GERADO por tools/gen_mechanics_header.py a partir de app/mechanics.json.")
    w("// Nao edite; arquivo nao versionado. Valores reais decompilados do default.xex")
    w("// (PowerPC big-endian, base 0x82000000) na rodada 2026-11-11 -- MECHANICS.md secao 12.")
    w("#pragma once")
    w("#include <cstdint>")
    w("")
    w("namespace pc {")
    w("namespace mechanics {")
    w("")
    w("// ---- tempo de quadro e estado (enderecos reais do XEX) ----")
    w("constexpr uint32_t kHookFrameStep        = %su;  // original: 'li r9,2' (grava 2 em 0x8269B1BC)" % frame["hook_frame_step"].lower())
    w("constexpr uint32_t kOriginalFrameStep    = %du;  // 2 VBlanks = 30 FPS a 60 Hz" % int(frame["original_step"]))
    w("constexpr uint32_t kHookPresentInterval  = %su;  // original: 'lwz r11,0x351C(r31)'" % frame["hook_present_interval"].lower())
    w("constexpr uint32_t kCutsceneStateAddr    = %su;  // byte: 0 = jogo livre (FUN_00224208)" % frame["cutscene_state"].lower())
    w("constexpr uint32_t kMapIdAddr            = %su;  // u16 do mapa atual" % frame["map_id"].lower())
    w("")
    w("// ---- constantes por quadro trocadas pelo 60 FPS (f32 do .rdata, BE) ----")
    w("// original -> 60 FPS, com o endereco da constante para conferencia")
    for k in sorted(k60, key=lambda x: int(x["hook"], 16)):
        name = "K_%s" % k["hook"][2:].upper()
        w("constexpr uint32_t kHook%s    = %su;  // %s" % (name, k["hook"].lower(), k["note"]))
        w("constexpr float %s_orig      = %s;   // f32 em %s" % (name, _f32(k["orig"]), k["orig_addr"].lower()))
        w("constexpr float %s           = %s;   // f32 em %s" % (name, _f32(k["v60"]), k["v60_addr"].lower()))
    w("")
    w("// ---- menus do jogo (MECHANICS.md secoes 13-15; enderecos conferidos no pc_hooks.toml) ----")
    def hx(key, note):
        w("constexpr uint32_t %-26s = %su;  // %s" % (_camel(key), menu[key].lower(), note))
    def dec(key, note):
        w("constexpr uint32_t %-26s = %du;  // %s" % (_camel(key), int(menu[key]), note))
    hx("add_entry_fn", "acrescenta entrada (pagina, texto, icone, pagina-alvo, acao)")
    dec("entry_size", "bytes por entrada (0x822E1230)")
    hx("entry_target_page_off", "pagina-alvo na entrada")
    hx("entry_action_off", "acao na entrada")
    hx("entry_label_off", "rotulo copiado na entrada (0x822E5740)")
    dec("entry_label_max", "tamanho maximo do rotulo")
    hx("screen_is_pause_off", "byte: 0 = menu de titulo, !=0 = pausa")
    hx("screen_cursor_off", "byte: entrada sob o cursor")
    hx("screen_dialog_off", "ponteiro da caixa de dialogo aberta")
    hx("screen_callback_off", "callback ao fechar a caixa (0x822E2538)")
    hx("leaderboards_action", "acao de CLASSIFICACOES (e da entrada de PC)")
    dec("leaderboards_text_id", "texto de CLASSIFICACOES")
    dec("leaderboards_target_page", "pagina-alvo de CLASSIFICACOES")
    dec("leaderboards_icon", "icone de CLASSIFICACOES")
    hx("settings_icon", "icone de CONFIGURACOES (pagina AJUDA E OPCOES)")
    hx("exit_action", "acao de VOLTAR A BIBLIOTECA DE JOGOS")
    dec("exit_text_id", "texto de VOLTAR A BIBLIOTECA DE JOGOS")
    hx("exit_dialog_fn", "abre a confirmacao 'VOCE TEM CERTEZA?'")
    hx("exit_dialog_title_ret", "retorno do GetString(5) do titulo da confirmacao")
    dec("exit_dialog_question_id", "texto 'VOCE TEM CERTEZA?'")
    hx("exit_yes_callback", "SIM: no original vai ao painel (XLNI_DASH_ARCADE)")
    hx("exit_no_callback", "NAO: volta ao menu")
    hx("hook_title_skip_leaderboards", "titulo: registro de CLASSIFICACOES")
    hx("title_skip_to", "titulo: proxima entrada (JOGAR/CONQUISTAS...)")
    hx("hook_title_exit_entry", "titulo: depois de 'li r4,5'")
    hx("hook_pause_skip_leaderboards", "pausa: registro de CLASSIFICACOES")
    hx("pause_skip_to", "pausa: proxima entrada (CONQUISTAS)")
    hx("hook_help_page_built", "fim da pagina AJUDA E OPCOES (titulo e pausa)")
    dec("help_page_index", "pagina AJUDA E OPCOES")
    dec("kazooie_icon", "icone do Kazooie (o de CLASSIFICACOES)")
    hx("entry_icon_off", "byte do icone na entrada (0x822E5740)")
    hx("hook_get_string", "GetString(id)")
    dec("title_start_y", "y da 1a entrada no titulo (0x82005AAC)")
    dec("title_step_y", "passo no titulo (0x82005AB4)")
    dec("pause_start_y", "y da 1a entrada na pausa (0x82005AB8)")
    dec("pause_step_y", "passo na pausa (0x82005AC8)")
    w("")
    w("// ---- paginas de PC dentro do menu do jogo (MECHANICS.md secao 18) ----")
    w("namespace ingame {")
    notes = ingame.get("notes") or {}
    for key in sorted(ingame):
        v = ingame[key]
        if key in ("why", "notes") or isinstance(v, (bool, dict, list)):
            continue
        note = notes.get(key, "")
        tail = ("  // " + note) if note else ""
        if isinstance(v, str) and v.lower().startswith("0x"):
            w("constexpr uint32_t %-26s = %su;%s" % (_camel(key), v.lower(), tail))
        elif isinstance(v, int):
            w("constexpr uint32_t %-26s = %du;%s" % (_camel(key), v, tail))
        elif isinstance(v, float):
            w("constexpr float %-29s = %s;%s" % (_camel(key), _f32(v), tail))
    w("}  // namespace ingame")
    w("")
    w("}  // namespace mechanics")
    w("}  // namespace pc")
    w("")
    text = "\n".join(lines)

    wrote = False
    try:
        with open(HEADER_PATH, "r", encoding="utf-8") as f:
            wrote = f.read() == text
    except OSError:
        pass
    if not wrote:
        with open(HEADER_PATH, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)

    print("[mechanics] OK: %s %s (%d pares K-60, frame step=%d, hooks conferidos no pc_hooks.toml)" %
          (os.path.relpath(HEADER_PATH, APP),
           "inalterado" if wrote else "gerado",
           len(k60), int(frame["original_step"])))
    return 0


def _camel(key):
    return "k" + "".join(part.capitalize() for part in key.split("_"))


def _f32(v):
    """Print a float as a C++ f32 literal with the same 32-bit pattern."""
    import struct
    f = struct.unpack(">f", struct.pack(">f", v))[0]
    t = repr(f)
    if "e" in t or "E" in t:
        return t + "f"
    if "." not in t:
        t += ".0"
    return t + "f"


if __name__ == "__main__":
    sys.exit(main())