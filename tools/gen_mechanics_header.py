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