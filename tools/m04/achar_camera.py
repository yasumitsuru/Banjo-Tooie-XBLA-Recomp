"""M04 etapa D: acha a posicao da camera na memoria de dados do jogo (0x82600000..0x82700000).

Le as 5 fotos da memoria gravadas por --probe-fisica --fisica-camera-dump (camera parada; depois de
girar para a direita 2 vezes; depois de girar para a esquerda; depois de andar) e procura trios
de floats (x, y, z, big-endian) que se comportam como a camera em orbita do jogador: distancia
horizontal ao jogador quase constante enquanto o angulo em volta dele muda no sentido do giro e
volta no giro contrario.

Uso: python -I tools/m04/achar_camera.py <pasta da execucao>
"""
import json
import math
import os
import struct
import sys

BASE = 0x82600000


def main():
    d = sys.argv[1]
    meta = json.load(open(os.path.join(d, "fisica.json"), encoding="utf-8"))
    players = [c["player"] for c in meta["camera_dumps"]]
    dumps = [open(os.path.join(d, "fisica_cam%d.bin" % i), "rb").read() for i in range(5)]
    n = len(dumps[0]) // 4
    fl = [struct.unpack(">%df" % n, b) for b in dumps]
    out = []
    for i in range(n - 2):
        pts = [(f[i], f[i + 1], f[i + 2]) for f in fl]
        ok = True
        for p in pts:
            for v in p:
                if not (v == v) or abs(v) > 1e5:
                    ok = False
        if not ok:
            continue
        dist, ang = [], []
        for p, pl in zip(pts, players):
            dx, dz = p[0] - pl[0], p[2] - pl[2]
            dist.append(math.hypot(dx, dz))
            ang.append(math.degrees(math.atan2(dz, dx)))
        if not (50 < dist[0] < 5000):
            continue
        if any(abs(dist[k] - dist[0]) > 0.3 * dist[0] for k in (1, 2, 3)):
            continue

        def dif(a, b):
            x = b - a
            while x > 180:
                x -= 360
            while x < -180:
                x += 360
            return x
        a1, a2, a3 = dif(ang[0], ang[1]), dif(ang[1], ang[2]), dif(ang[2], ang[3])
        if abs(a1) < 5 or abs(a2) < 5 or abs(a3) < 5:
            continue
        if (a1 > 0) != (a2 > 0) or (a3 > 0) == (a1 > 0):
            continue
        moved = math.dist(pts[4], pts[3])
        out.append({"addr": "0x%08X" % (BASE + 4 * i), "dist": [round(x, 1) for x in dist],
                    "giros_graus": [round(a1, 1), round(a2, 1), round(a3, 1)],
                    "y": [round(p[1], 1) for p in pts], "andou": round(moved, 1)})
    json.dump(out, open(os.path.join(d, "camera_candidatos.json"), "w"), indent=1)
    print(len(out), "candidatos")
    for c in out[:30]:
        print(c)


if __name__ == "__main__":
    main()
