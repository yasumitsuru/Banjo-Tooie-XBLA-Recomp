"""MISSAO 02 - compara o texto de duas capturas PNG do jogo (regressao visual, somente leitura).

O fundo do jogo se mexe (camera da abertura, quadros animados), entao a comparacao nao e de
pixels: em uma regiao da tela, marca os pixels com a cor do texto (branco dos menus XBLA ou
laranja/amarelo dos dialogos do jogo) e mede a sobreposicao (IoU) entre a referencia e a nova
captura. Texto igual no mesmo lugar => IoU alto; texto faltando, cortado ou trocado => IoU baixo.

Uso: python -I comparar_capturas.py <referencia.png> <nova.png> <x0,y0,x1,y1> <branco|claro|laranja>
Imprime um JSON: {"iou": ..., "pixels_ref": ..., "pixels_novo": ..., "regiao": [...]}.
"""
import json
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: nao e PNG")
    pos, idat, w = 8, b"", None
    while pos < len(data):
        ln, typ = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f"{path}: formato PNG nao suportado ({depth} bits, tipo {ctype})")
            bpp = 3 if ctype == 2 else 4
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        rows.append(bytes(line))
        prev = line
    return w, h, bpp, rows


def mask(png, region, kind):
    w, h, bpp, rows = png
    x0, y0, x1, y1 = region
    out = set()
    for y in range(max(0, y0), min(h, y1)):
        row = rows[y]
        for x in range(max(0, x0), min(w, x1)):
            r, g, b = row[x * bpp], row[x * bpp + 1], row[x * bpp + 2]
            if kind == "branco":
                hit = r > 205 and g > 205 and b > 205 and max(r, g, b) - min(r, g, b) < 40
            elif kind == "claro":  # itens de menu nao selecionados (cinza claro, sem cor)
                hit = min(r, g, b) > 140 and max(r, g, b) - min(r, g, b) < 35
            else:  # laranja/amarelo do texto dos dialogos
                hit = r > 200 and 110 < g < 235 and b < 100
            if hit:
                out.add((x, y))
    return out


def main():
    ref, new, reg, kind = sys.argv[1:5]
    region = [int(v) for v in reg.split(",")]
    a = mask(read_png(ref), region, kind)
    b = mask(read_png(new), region, kind)
    union = len(a | b)
    iou = len(a & b) / union if union else 1.0
    print(json.dumps({"iou": round(iou, 4), "pixels_ref": len(a), "pixels_novo": len(b),
                      "regiao": region, "cor": kind}))


if __name__ == "__main__":
    main()
