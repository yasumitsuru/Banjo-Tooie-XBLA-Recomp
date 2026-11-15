"""M04: analisa as fotos de --probe-fisica --fisica-descoberta (0x82600000..0x82700000).

Procura, comparando as fotos tiradas parado / andando / no ar:
  - candidatos a ESTADO DO JOGADOR: byte, u16 ou u32 igual em todas as fotos "parado" e
    diferente em "andando" e "no_ar" (e diferente entre os dois);
  - CONTADORES DE TEMPO: u32 que cresce sempre entre fotos, com taxa por segundo de relógio
    perto de 1, 30 ou 60, e f32 que cresce ~1 por segundo (relógio em segundos);
  - ponteiros fixos para um endereço dado (--alvo 0x4030E3F0).
Com duas pastas (60 e 30 FPS) compara a taxa de cada contador nas duas configurações.

Uso: python -I tools/m04/descoberta.py <pasta60> [<pasta30>]
"""
import json
import os
import struct
import sys

BASE = 0x82600000


def carregar(d):
    meta = json.load(open(os.path.join(d, "fisica.json"), encoding="utf-8"))["camera_dumps"]
    dumps = [open(os.path.join(d, "fisica_desc%d.bin" % m["i"]), "rb").read() for m in meta]
    return meta, dumps


def estados(meta, dumps):
    idle = [i for i, m in enumerate(meta) if m["when"] == "parado"]
    walk = [i for i, m in enumerate(meta) if m["when"] == "andando"]
    air = [i for i, m in enumerate(meta) if m["when"] == "no_ar"]
    out = []
    for size, fmt in ((1, "B"), (2, ">H"), (4, ">I")):
        n = len(dumps[0]) // size
        for off in range(0, len(dumps[0]) - size + 1, size):
            vals = [struct.unpack_from(fmt, d, off)[0] for d in dumps]
            vi = {vals[i] for i in idle}
            if len(vi) != 1:
                continue
            v0 = vals[idle[0]]
            vw = {vals[i] for i in walk}
            va = {vals[i] for i in air}
            if v0 in vw or v0 in va or vw & va:
                continue
            if size == 4 and (v0 > 0xFFFF and v0 < 0x3F000000):
                continue  # floats/ponteiros mudando à toa
            out.append({"addr": "0x%08X" % (BASE + off), "tam": size, "parado": v0,
                        "andando": sorted(vw), "no_ar": sorted(va)})
    return out


def contadores(meta, dumps):
    t = [m["t"] for m in meta]
    out = []
    n = len(dumps[0]) // 4
    U = [struct.unpack(">%dI" % n, d) for d in dumps]
    F = [struct.unpack(">%df" % n, d) for d in dumps]
    for i in range(n):
        u = [x[i] for x in U]
        if all(b > a for a, b in zip(u, u[1:])) and u[-1] - u[0] < 10 ** 7:
            rate = (u[-1] - u[0]) / (t[-1] - t[0])
            if 0.5 < rate < 1.5 or 25 < rate < 65:
                out.append({"addr": "0x%08X" % (BASE + 4 * i), "tipo": "u32", "taxa_s": round(rate, 2),
                            "inicio": u[0], "fim": u[-1]})
        f = [x[i] for x in F]
        if all(v == v and abs(v) < 1e7 for v in f) and all(b > a for a, b in zip(f, f[1:])):
            rate = (f[-1] - f[0]) / (t[-1] - t[0])
            if 0.5 < rate < 1.5 or 25 < rate < 65:
                out.append({"addr": "0x%08X" % (BASE + 4 * i), "tipo": "f32", "taxa_s": round(rate, 3),
                            "inicio": f[0], "fim": f[-1]})
    return out


def main():
    d60 = sys.argv[1]
    m60, D60 = carregar(d60)
    st = estados(m60, D60)
    print("candidatos a estado:", len(st))
    for c in st[:60]:
        print(" ", c)
    c60 = contadores(m60, D60)
    res = {"estado": st, "contadores60": c60}
    if len(sys.argv) > 2:
        m30, D30 = carregar(sys.argv[2])
        st30 = {c["addr"]: c for c in estados(m30, D30)}
        res["estado_confirmado_30"] = [c for c in st if c["addr"] in st30]
        c30 = {c["addr"]: c for c in contadores(m30, D30)}
        res["contadores"] = [{"addr": c["addr"], "tipo": c["tipo"], "taxa60": c["taxa_s"],
                              "taxa30": c30[c["addr"]]["taxa_s"] if c["addr"] in c30 else None}
                             for c in c60]
        print("estado confirmado a 30 FPS:", len(res["estado_confirmado_30"]))
        for c in res["estado_confirmado_30"][:40]:
            print("  ", c, "| 30:", st30[c["addr"]]["parado"], st30[c["addr"]]["andando"],
                  st30[c["addr"]]["no_ar"])
        print("contadores (taxa por segundo a 60 / 30):")
        for c in res["contadores"]:
            print("  ", c)
    json.dump(res, open(os.path.join(d60, "descoberta.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
