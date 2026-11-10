"""MISSAO 00 (rodada de verificacao) - os enderecos que a base corrige existem
neste default.xex com o mesmo codigo?

Somente leitura. Compara cada endereco do overrides.toml da base
(iron-jay/banjo_tooie_xbla_recomp) com a imagem do XEX do usuario e testa as
previsoes que os proprios comentarios da base fazem sobre cada um:
  - setjmp/longjmp: inicio de funcao; o setjmp le o gancho em 0x826CCC58;
  - 4 alvos de "tail-call": existe pelo menos um `b` (sem link) apontando para eles;
  - lote achado por varredura de ponteiros: o endereco aparece como ponteiro
    de 32 bits em .rdata/.data;
  - 0x82280AF8: blr em 0x82280B98, `b 0x82280AC4` em 0x82280AF4 e NENHUM
    ponteiro para ele em .rdata/.data.
Um endereco "confere" quando todas as previsoes dele valem.

Uso: python -I check_overrides.py <default.xex> <overrides.toml> <functions.txt> <saida.json>
"""
import json
import struct
import sys
import tomllib

BASE = 0x82000000


def load_image(xex_path):
    d = open(xex_path, "rb").read()
    off_pe = struct.unpack_from(">I", d, 8)[0]
    off_sec = struct.unpack_from(">I", d, 0x10)[0]
    image_size = struct.unpack_from(">I", d, off_sec + 4)[0]
    return d[off_pe: off_pe + image_size]


def pe_sections(img):
    e_lfanew = struct.unpack_from("<I", img, 0x3C)[0]
    n = struct.unpack_from("<H", img, e_lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", img, e_lfanew + 20)[0]
    p = e_lfanew + 24 + opt_size
    out = {}
    for i in range(n):
        name = img[p: p + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va = struct.unpack_from("<II", img, p + 8)
        out[name] = (BASE + va, BASE + va + vsize)
        p += 40
    return out


def word(img, va):
    o = va - BASE
    if o < 0 or o + 4 > len(img):
        return None
    return struct.unpack_from(">I", img, o)[0]


def branch_target(w, va):
    """Destino de b/bl (opcode 18); None se nao for."""
    if w is None or (w >> 26) != 18:
        return None
    li = w & 0x03FFFFFC
    if li & 0x02000000:
        li -= 0x04000000
    tgt = li if (w & 2) else va + li
    return tgt & 0xFFFFFFFF


def classify(w):
    if w is None:
        return "fora da imagem"
    if w == 0x4E800020:
        return "blr"
    if w == 0x4E800420:
        return "bctr"
    if w == 0x7D8802A6:
        return "mflr r12 (prologo)"
    if w == 0x7C0802A6:
        return "mflr r0 (prologo)"
    if (w >> 16) == 0x9421:
        return "stwu r1 (prologo)"
    if (w >> 26) == 62 and ((w >> 16) & 0x3FF) == 0x021 and (w & 3) == 1:
        return "stdu r1 (prologo)"
    op = w >> 26
    if op == 18:
        return "bl" if w & 1 else "b"
    if w == 0:
        return "zero (sem codigo)"
    return f"op{op}"


def is_boundary(w):
    return w in (0x4E800020, 0x4E800420) or (w is not None and (w >> 26) == 18 and not (w & 1)) or w == 0


def main():
    xex, ovr, funcs_txt, out = sys.argv[1:5]
    img = load_image(xex)
    secs = pe_sections(img)
    text_lo, text_hi = secs[".text"]
    ov = tomllib.load(open(ovr, "rb"))

    ghidra_starts = set()
    for line in open(funcs_txt, encoding="utf-8", errors="replace"):
        a = line.split("|", 1)[0]
        if a.startswith("0x"):
            ghidra_starts.add(int(a, 16))

    pd_lo, pd_hi = secs[".pdata"]
    pdata_starts = set()
    for va in range(pd_lo, pd_hi - 7, 8):
        b = word(img, va)
        if b and text_lo <= b < text_hi:
            pdata_starts.add(b)

    # ponteiros de 32 bits alinhados em .rdata/.data
    ptr_count = {}
    for sec in (".rdata", ".data"):
        lo, hi = secs[sec]
        for va in range(lo, hi - 3, 4):
            v = word(img, va)
            if v is not None and text_lo <= v < text_hi:
                ptr_count[v] = ptr_count.get(v, 0) + 1

    # destinos de b/bl em .text
    b_targets, bl_targets = {}, {}
    for va in range(text_lo, text_hi - 3, 4):
        w = word(img, va)
        t = branch_target(w, va)
        if t is not None:
            dst = bl_targets if (w & 1) else b_targets
            dst[t] = dst.get(t, 0) + 1

    addrs = [("setjmp", ov["setjmp_address"]), ("longjmp", ov["longjmp_address"])]
    for k in ov.get("functions", {}):
        addrs.append(("function", int(k, 0) if isinstance(k, str) else k))

    tail_calls = {0x8223F200, 0x8223AC28, 0x82523A78, 0x822E8C00}
    pointer_batch_start = 0x820B0C04
    rows = []
    for kind, a in addrs:
        w = word(img, a)
        prev = word(img, a - 4)
        row = {
            "address": f"0x{a:08X}", "kind": kind,
            "in_text": text_lo <= a < text_hi,
            "word": None if w is None else f"0x{w:08X}", "instr": classify(w),
            "prev_instr": classify(prev), "prev_is_boundary": is_boundary(prev),
            "in_pdata": a in pdata_starts, "in_ghidra_functions": a in ghidra_starts,
            "data_pointers": ptr_count.get(a, 0),
            "b_refs": b_targets.get(a, 0), "bl_refs": bl_targets.get(a, 0),
        }
        checks = {"in_text": row["in_text"], "codigo_nao_zero": w not in (None, 0)}
        if kind == "setjmp":
            first5 = [word(img, a + 4 * i) for i in range(5)]
            row["first5"] = [f"0x{x:08X}" for x in first5]
            checks["le_gancho_0x826CCC58"] = any((x & 0xFFFF) == 0xCC58 for x in first5) and \
                any((x >> 26) == 15 and (x & 0xFFFF) == 0x826D for x in first5)
            checks["chamado_por_bl"] = row["bl_refs"] > 0
        elif kind == "longjmp":
            checks["inicio_de_funcao"] = row["in_pdata"] or row["in_ghidra_functions"] or row["prev_is_boundary"]
        elif a in tail_calls:
            checks["alvo_de_b_tail_call"] = row["b_refs"] > 0
        elif a == 0x82415170:
            checks["anterior_e_fronteira"] = row["prev_is_boundary"]
        elif a == 0x82280AF8:
            w_b = word(img, 0x82280AF4)
            checks["b_0x82280AC4_em_0x82280AF4"] = branch_target(w_b, 0x82280AF4) == 0x82280AC4 and not (w_b & 1)
            checks["blr_em_0x82280B98"] = word(img, 0x82280B98) == 0x4E800020
            checks["sem_ponteiro_em_dados"] = row["data_pointers"] == 0
        else:
            checks["ponteiro_em_rdata_data"] = row["data_pointers"] > 0
        row["checks"] = checks
        row["confere"] = all(checks.values())
        rows.append(row)

    summary = {
        "overrides_total": len(rows),
        "conferem": sum(r["confere"] for r in rows),
        "nao_conferem": [r["address"] for r in rows if not r["confere"]],
        "text_range": [f"0x{text_lo:08X}", f"0x{text_hi:08X}"],
        "pdata_function_starts": len(pdata_starts),
        "ghidra_function_starts": len(ghidra_starts),
    }
    json.dump({"summary": summary, "rows": rows}, open(out, "w", encoding="utf-8"), indent=2)
    print(json.dumps(summary, indent=2))
    for r in rows:
        bad = [k for k, v in r["checks"].items() if not v]
        print(f'{r["address"]} {r["kind"]:8s} {r["instr"]:22s} prev={r["prev_instr"]:22s} '
              f'ptr={r["data_pointers"]:3d} b={r["b_refs"]:3d} bl={r["bl_refs"]:3d} '
              f'pdata={int(r["in_pdata"])} ghidra={int(r["in_ghidra_functions"])} '
              f'{"OK" if r["confere"] else "FALHA " + ",".join(bad)}')


if __name__ == "__main__":
    main()
