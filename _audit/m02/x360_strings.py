"""MISSAO 02 - le X360_strings.dat (textos da camada XBLA) e confere a estrutura. Somente leitura.

Formato (deduzido do arquivo e conferido aqui, nao presumido):
  0x00 u16 LE  numero de strings por idioma (N)
  0x02 u16 LE  numero de idiomas (L)
  0x04 L x u32 LE  tamanho em bytes do bloco de texto de cada idioma
  depois       L x N x u32 LE  comprimento de cada string (bytes, incluindo o 0 final), por idioma
  depois       L blocos de texto, strings terminadas em 0, na ordem dos idiomas
Cada verificacao vira um item PASS/FAIL no JSON. As strings vao para um relatorio por idioma.

Uso: python -I x360_strings.py <X360_strings.dat> <saida.json> [<saida_strings_idioma0.txt>]
"""
import collections
import hashlib
import json
import struct
import sys


def classify(raw_strings):
    """Codificacao provavel de um bloco: utf-16 (muitos 0 intercalados), utf-8 valido com
    multibyte, ou 1 byte por caractere (cp1252)."""
    blob = b"".join(raw_strings)
    if blob.count(b"\x00") > len(blob) // 4:
        return "utf-16?"
    try:
        blob.decode("utf-8")
        if any(b >= 0x80 for b in blob):
            return "utf-8"
        return "ascii"
    except UnicodeDecodeError:
        return "cp1252"


def main():
    path, out = sys.argv[1], sys.argv[2]
    txt_out = sys.argv[3] if len(sys.argv) > 3 else None
    d = open(path, "rb").read()
    checks = []

    def check(name, ok, detail):
        checks.append({"verificacao": name, "status": "PASS" if ok else "FAIL", "detalhe": detail})

    n, langs = struct.unpack_from("<HH", d, 0)
    sizes = list(struct.unpack_from("<%dI" % langs, d, 4))
    len_tab = 4 + 4 * langs
    blocks_start = len_tab + 4 * n * langs
    check("cabecalho", 0 < n < 20000 and 0 < langs <= 16,
          f"{n} strings x {langs} idiomas; blocos de {sizes} bytes")
    lengths = [list(struct.unpack_from("<%dI" % n, d, len_tab + 4 * n * l)) for l in range(langs)]
    pos = blocks_start
    per_lang = []
    for l in range(langs):
        ok_sum = sum(lengths[l]) == sizes[l]
        check(f"idioma{l}: soma dos comprimentos = tamanho do bloco", ok_sum,
              f"soma {sum(lengths[l])} vs bloco {sizes[l]}")
        block = d[pos:pos + sizes[l]]
        check(f"idioma{l}: bloco dentro do arquivo", pos + sizes[l] <= len(d),
              f"0x{pos:X}..0x{pos + sizes[l]:X} de 0x{len(d):X}")
        raws, bad_term, inner_nul = [], [], 0
        p = 0
        for i, ln in enumerate(lengths[l]):
            s = block[p:p + ln]
            if ln == 0 or s[-1:] != b"\x00":
                bad_term.append(i)
            body = s[:-1]
            inner_nul += body.count(b"\x00")
            raws.append(body)
            p += ln
        enc = classify(raws)
        check(f"idioma{l}: toda string termina em 0 no comprimento declarado", not bad_term,
              f"{len(bad_term)} sem terminador" + (f" (ex.: {bad_term[:5]})" if bad_term else ""))
        if enc != "utf-16?":
            check(f"idioma{l}: sem 0 no meio das strings (nada truncado)", inner_nul == 0,
                  f"{inner_nul} bytes 0 internos")
        dec = "utf-16-le" if enc == "utf-16?" else ("utf-8" if enc in ("utf-8", "ascii") else "cp1252")
        texts = [r.decode(dec, errors="replace") for r in raws]
        chars = collections.Counter(c for t in texts for c in t if ord(c) > 0x7E)
        per_lang.append({
            "idioma": l, "inicio": pos, "bytes": sizes[l], "codificacao": enc,
            "strings_vazias": sum(1 for t in texts if not t),
            "maior_string": max((len(t) for t in texts), default=0),
            "sha256_bloco": hashlib.sha256(block).hexdigest(),
            "caracteres_nao_ascii": dict(sorted(chars.items())),
            "amostra": texts[:12],
        })
        if l == 0 and txt_out:
            with open(txt_out, "w", encoding="utf-8", newline="\n") as f:
                for i, t in enumerate(texts):
                    f.write(f"{i:04d}\t{t!r}\n")
        pos += sizes[l]
    trailing = len(d) - pos
    check("fim dos blocos", trailing >= 0, f"{trailing} bytes depois do ultimo bloco")
    result = {
        "arquivo": path, "tamanho": len(d), "sha256": hashlib.sha256(d).hexdigest(),
        "strings_por_idioma": n, "idiomas": langs, "inicio_blocos": blocks_start,
        "bytes_apos_blocos": trailing, "verificacoes": checks, "idiomas_detalhe": per_lang,
        "status": "PASS" if all(c["status"] == "PASS" for c in checks) else "FAIL",
    }
    json.dump(result, open(out, "w", encoding="utf-8"), indent=2, ensure_ascii=False)
    for c in checks:
        print(f"{c['status']:4} {c['verificacao']}: {c['detalhe']}")
    for pl in per_lang:
        print(f"idioma{pl['idioma']}: {pl['codificacao']:7} {pl['bytes']:6} bytes; amostra: {pl['amostra'][:4]}")
    print("STATUS", result["status"])


if __name__ == "__main__":
    main()
