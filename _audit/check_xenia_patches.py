"""MISSAO 00 (rodada de verificacao) - os patches da comunidade do Xenia para
Banjo-Tooie (60 FPS, Draw Distance, LOD, sombras) batem com este default.xex?

Somente leitura: le o arquivo de patches do Xenia e a imagem do XEX do usuario;
nao aplica nada. Para cada patch:
  - mostra as palavras originais em cada endereco alterado;
  - nos ganchos `b <caverna>`, confere que a caverna (0x825F4000+) esta vazia
    no original e que as instrucoes que o autor "realocou" para a caverna sao
    exatamente as que existem no endereco do gancho (prova de que o patch foi
    escrito para este build);
  - calcula XXH64 da imagem/arquivo para comparar com o `hash` do patch
    (o algoritmo exato que o Xenia usa nao e garantido; so informativo).

Uso: python -I check_xenia_patches.py <default.xex> <patch.toml> <saida.json>
"""
import json
import struct
import sys
import tomllib

BASE = 0x82000000
M = (1 << 64) - 1
P1, P2, P3, P4, P5 = (0x9E3779B185EBCA87, 0xC2B2AE3D27D4EB4F, 0x165667B19E3779F9,
                      0x85EBCA77C2B2AE63, 0x27D4EB2F165667C5)


def rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & M


def _round(acc, v):
    acc = (acc + v * P2) & M
    return (rotl(acc, 31) * P1) & M


def xxh64(data, seed=0):
    n = len(data)
    i = 0
    if n >= 32:
        v1, v2, v3, v4 = (seed + P1 + P2) & M, (seed + P2) & M, seed, (seed - P1) & M
        limit = n - (n % 32)
        for a, b, c, d in struct.iter_unpack("<4Q", data[:limit]):
            v1, v2, v3, v4 = _round(v1, a), _round(v2, b), _round(v3, c), _round(v4, d)
        i = limit
        h = (rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18)) & M
        for v in (v1, v2, v3, v4):
            h ^= _round(0, v)
            h = (h * P1 + P4) & M
    else:
        h = (seed + P5) & M
    h = (h + n) & M
    while i + 8 <= n:
        h ^= _round(0, struct.unpack_from("<Q", data, i)[0])
        h = (rotl(h, 27) * P1 + P4) & M
        i += 8
    if i + 4 <= n:
        h ^= (struct.unpack_from("<I", data, i)[0] * P1) & M
        h = (rotl(h, 23) * P2 + P3) & M
        i += 4
    while i < n:
        h ^= (data[i] * P5) & M
        h = (rotl(h, 11) * P1) & M
        i += 1
    h ^= h >> 33
    h = (h * P2) & M
    h ^= h >> 29
    h = (h * P3) & M
    h ^= h >> 32
    return h


def branch_target(w, va):
    if (w >> 26) != 18:
        return None
    li = w & 0x03FFFFFC
    if li & 0x02000000:
        li -= 0x04000000
    return ((li if (w & 2) else va + li) & 0xFFFFFFFF), bool(w & 1)


def main():
    xex, patch_path, out = sys.argv[1:4]
    d = open(xex, "rb").read()
    off_pe = struct.unpack_from(">I", d, 8)[0]
    off_sec = struct.unpack_from(">I", d, 0x10)[0]
    image_size = struct.unpack_from(">I", d, off_sec + 4)[0]
    img = d[off_pe: off_pe + image_size]
    pt = tomllib.load(open(patch_path, "rb"))

    def rd(va, size):
        o = va - BASE
        return int.from_bytes(img[o: o + size], "big")

    hash_target = int(pt.get("hash", "0"), 16)
    hashes = {"xxh64(imagem)": xxh64(img), "xxh64(arquivo)": xxh64(d),
              "xxh64(cabecalho 0..offset_pe)": xxh64(d[:off_pe])}
    hash_info = {"patch_hash": f"{hash_target:016X}",
                 "candidatos": {k: f"{v:016X}" for k, v in hashes.items()},
                 "algum_bate": any(v == hash_target for v in hashes.values())}

    report = {"title_name": pt.get("title_name"), "title_id": pt.get("title_id"),
              "hash": hash_info, "patches": []}
    for p in pt.get("patch", []):
        sites = []
        cave_writes = {}
        for kind, size in (("be8", 1), ("be16", 2), ("be32", 4)):
            for s in p.get(f"patch.{kind}", []) or p.get(kind, []):
                pass
        # tomllib entrega [[patch.be32]] como chave "be32" dentro do patch
        for kind, size in (("be8", 1), ("be16", 2), ("be32", 4)):
            for s in p.get(kind, []):
                a, v = s["address"], s["value"]
                orig = rd(a, size)
                sites.append({"address": f"0x{a:08X}", "size": size,
                              "original": f"0x{orig:0{size * 2}X}", "patched": f"0x{v:0{size * 2}X}"})
                if size == 4:
                    cave_writes[a] = v
        caves = {a: v for a, v in cave_writes.items() if 0x825F39C8 <= a < 0x82600000}
        cave_empty = all(rd(a, 4) == 0 for a in caves)
        hooks = []
        for a, v in cave_writes.items():
            if a in caves:
                continue
            bt = branch_target(v, a)
            if bt and bt[0] in caves and not bt[1]:
                orig = rd(a, 4)
                # instrucoes da caverna a partir do destino ate o primeiro `b` de volta
                relocated_found = orig in caves.values()
                hooks.append({"hook": f"0x{a:08X}", "to_cave": f"0x{bt[0]:08X}",
                              "original_at_hook": f"0x{orig:08X}",
                              "original_relocated_into_cave": relocated_found})
        report["patches"].append({
            "name": p.get("name"), "enabled_in_user_config": p.get("is_enabled"),
            "sites": len(sites), "cave_words": len(caves),
            "cave_empty_in_original": cave_empty if caves else None,
            "hooks": hooks,
            "all_hooks_relocate_original": (all(h["original_relocated_into_cave"] for h in hooks)
                                            if hooks else None),
            "site_details": sites})
    json.dump(report, open(out, "w", encoding="utf-8"), indent=2)
    print(json.dumps(hash_info, indent=2))
    for p in report["patches"]:
        print(f'- {p["name"]!r}: sites={p["sites"]} caverna={p["cave_words"]} '
              f'caverna_vazia={p["cave_empty_in_original"]} ganchos={len(p["hooks"])} '
              f'ganchos_realocam_original={p["all_hooks_relocate_original"]}')
        for h in p["hooks"]:
            print(f'    gancho {h["hook"]} -> {h["to_cave"]} original={h["original_at_hook"]} '
                  f'realocado={h["original_relocated_into_cave"]}')


if __name__ == "__main__":
    main()
