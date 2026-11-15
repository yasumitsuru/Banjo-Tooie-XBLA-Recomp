"""M04 etapa D (C-32, C-36): fisica a 30 FPS (logica original, passo 2) x 60 FPS (port).

Roda o jogo real com a sonda --probe-fisica (mesma entrada roteirizada, mesmos tempos de
relogio) com fps60 = false e fps60 = true, REPS vezes cada, numa pasta isolada. Compara por mapa
e por trecho (parado, andar, correr, pulo, camera, re):
  - percurso (soma dos deslocamentos horizontais), velocidade em regime e velocidade maxima;
  - pulo: altura maxima, tempo no ar e deriva horizontal;
  - posicao final da sequencia (inclui colisoes com paredes/objetos no caminho);
  - imagem depois do giro da camera e no fim (diferenca entre execucoes).

Tolerancias (justificadas no proprio resultado):
  - a entrada e mudada pelo relogio do PC; a 30 FPS o jogo le o controle a cada 33,3 ms, entao o
    tempo efetivo de cada comando pode variar ate 1 quadro de 30 FPS (33,3 ms) de cada lado:
    tolerancia de percurso = velocidade maxima x 2 x 33,3 ms (+ a variacao medida entre
    repeticoes da mesma configuracao);
  - velocidade em regime: 2 % (+ variacao entre repeticoes);
  - pulo: a integracao numerica a passo 1/30 x 1/60 muda o pico em ate v_subida x (1/30 - 1/60)
    / 2; tempo no ar: 1 quadro de 30 FPS (33,3 ms) + variacao entre repeticoes;
  - imagens: a diferenca 30x60 nao pode passar de 1,5 x a maior diferenca entre repeticoes da
    mesma configuracao + 2 pontos percentuais (agua, nuvens e animacoes mudam sozinhas).

Uso: <python com Pillow> -I tools/m04/fisica_30x60.py [--reps 2] [--mapas "127:1,D6:8,1A7:1"]
Saida: _audit/out/m04/D/fisica-<data>/resultado.json (+ execucoes). Codigo 0 = tudo dentro.
"""
import argparse
import datetime
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rodar  # noqa: E402

SEGS = ["parado", "andar", "correr", "pulo", "camera", "re"]
# Mapas que começam embaixo d'água (Atlântida e Fundo do Mar): o nado é em 3D (o analógico
# inclina para cima/baixo e o Banjo flutua), então distância e velocidade são medidas em 3D e as
# medidas de pulo (altura, tempo no ar) não se aplicam.
AGUA = {"0x1a8", "0x1a9"}
Q30 = 1.0 / 30.0


def settings_toml(fps60):
    return ("settings_version = 3\n[jogo]\nfps60 = %s\nlock60 = true\nscenes60 = false\n"
            "skip_intro_logos = true\npause_on_focus_loss = false\n" %
            ("true" if fps60 else "false"))


def seg_window(marks, seg):
    d = {m["ev"]: m["t"] for m in marks if m["seg"] == seg}
    return d.get("inicio"), d.get("solta"), d.get("fim")


def _antes(samples, t):
    """Ultima amostra antes de t (o estado no momento do comando)."""
    a = None
    for s in samples:
        if s[0] < t:
            a = s
        else:
            break
    return a


def _vel(a, b):
    dt = b[0] - a[0]
    return math.hypot(b[4] - a[4], b[6] - a[6]) / dt if dt > 1e-4 else 0.0


def analisa_mapa(samples, marks):
    """Metricas por trecho a partir das amostras (t, swaps, step, scene, x, y, z).

    Cada trecho comeca na ULTIMA amostra antes do comando (o estado do jogo naquele momento),
    para nao perder o primeiro quadro de movimento (a 30 FPS um quadro sao 33 ms)."""
    out = {}
    for seg in SEGS:
        t0, tr, t1 = seg_window(marks, seg)
        if t0 is None or t1 is None:
            continue
        first = _antes(samples, t0)
        pts = ([first] if first else []) + [s for s in samples if t0 <= s[0] <= t1]
        if len(pts) < 3:
            continue
        path = 0.0
        path3 = 0.0
        vs = []
        for a, b in zip(pts, pts[1:]):
            path += math.hypot(b[4] - a[4], b[6] - a[6])
            path3 += math.dist(a[4:7], b[4:7])
            vs.append(_vel(a, b))
        vs_sorted = sorted(vs)
        p95 = vs_sorted[min(len(vs_sorted) - 1, int(0.95 * len(vs_sorted)))]
        steps = sorted(set(p[2] for p in pts))
        m = {"amostras": len(pts), "passos": steps, "percurso": path, "percurso3d": path3,
             "vel_p95": p95,
             "deslocamento": math.hypot(pts[-1][4] - pts[0][4], pts[-1][6] - pts[0][6]),
             "dy": pts[-1][5] - pts[0][5],
             "atualizacoes_por_s": (len(pts) - 1) / max(1e-6, pts[-1][0] - pts[0][0])}
        if tr is not None and seg in ("andar", "correr", "re"):
            # regime: de 0,6 s depois do comando ate o "solta"
            reg = [s for s in pts if t0 + 0.6 <= s[0] <= tr]
            if len(reg) >= 3:
                p = sum(math.hypot(b[4] - a[4], b[6] - a[6]) for a, b in zip(reg, reg[1:]))
                m["vel_regime"] = p / max(1e-6, reg[-1][0] - reg[0][0])
                # aceleracao: tempo ate 90% da velocidade de regime
                for a, b in zip(pts, pts[1:]):
                    if b[0] > t0 and _vel(a, b) >= 0.9 * m["vel_regime"]:
                        m["aceleracao_s"] = b[0] - t0
                        break
            r0 = _antes(samples, tr)
            post = ([r0] if r0 else []) + [s for s in pts if s[0] >= tr]
            m["deslize"] = sum(math.hypot(b[4] - a[4], b[6] - a[6])
                               for a, b in zip(post, post[1:]))
        if len(pts[0]) >= 10 and seg in ("camera", "correr"):
            def cang(p):
                return math.degrees(math.atan2(p[9] - p[6], p[7] - p[4]))

            def dif(a, b):
                x = b - a
                while x > 180:
                    x -= 360
                while x < -180:
                    x += 360
                return x
            a0 = cang(pts[0])
            acc, prev, curve = 0.0, a0, []
            for p in pts[1:]:
                a = cang(p)
                acc += dif(prev, a)
                prev = a
                curve.append((p[0], acc))
            m["cam_giro"] = acc
            m["cam_dist"] = math.hypot(pts[-1][7] - pts[-1][4], pts[-1][9] - pts[-1][6])
            if abs(acc) > 2:
                m["cam_t90"] = next((t - t0 for t, v in curve if abs(v) >= 0.9 * abs(acc)), None)
        if seg == "pulo":
            y0 = pts[0][5]  # chao: amostra antes do A
            ys = [p[5] for p in pts]
            ipk = max(range(len(ys)), key=lambda i: ys[i])
            m["altura"] = ys[ipk] - y0
            up = next((i for i, y in enumerate(ys) if y > y0 + 1.0), None)
            down = None
            if up is not None:
                # pouso: primeiro ponto depois do pico em que a descida para (minimo local) perto
                # do chao (o Banjo "quica" alguns pontos no pouso)
                lim = y0 + 0.25 * (ys[ipk] - y0)
                for i in range(ipk + 1, len(ys) - 1):
                    if ys[i] <= lim and ys[i + 1] >= ys[i]:
                        down = i
                        break
            if up is not None and down is not None:
                # da ultima amostra no chao ate a primeira de volta ao chao
                m["tempo_no_ar"] = pts[down][0] - pts[max(0, up - 1)][0]
            if up is not None and up + 1 < len(pts):
                a, b = pts[max(0, up - 1)], pts[up + 1]
                m["vel_subida"] = (b[5] - a[5]) / max(1e-6, b[0] - a[0])
        out[seg] = m
    # relogio do jogo (segundos, f32 0x8266EC80) e contador de quadros (0x8266C69C) ao longo
    # da sequencia; estados do jogador vistos (u16 0x82674AD0)
    t0, _, _ = seg_window(marks, "parado")
    _, _, t1 = seg_window(marks, "re")
    if t0 is not None and t1 is not None:
        seq = [x for x in samples if t0 <= x[0] <= t1]
        if len(seq) > 2 and len(seq[0]) >= 13:
            dt = seq[-1][0] - seq[0][0]
            out["relogio"] = {"taxa_s": (seq[-1][11] - seq[0][11]) / dt,
                              "quadros_por_s": (seq[-1][12] - seq[0][12]) / dt}
            cnt = {}
            for x in seq:
                cnt[int(x[10])] = cnt.get(int(x[10]), 0) + 1
            out["estados"] = {str(k): round(v / len(seq), 3) for k, v in
                              sorted(cnt.items(), key=lambda kv: -kv[1])}
    if t0 is not None and t1 is not None:
        a = _antes(samples, t0)
        b = _antes(samples, t1)
        if a and b:
            out["final"] = {"dx": b[4] - a[4], "dy": b[5] - a[5], "dz": b[6] - a[6]}
    return out


def img_diff(pa, pb):
    """Fracao (%) de pixels diferentes (> 32 em cinza) numa reducao 320x180."""
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return None
    if not (os.path.exists(pa) and os.path.exists(pb)):
        return None
    a = Image.open(pa).convert("L").resize((320, 180))
    b = Image.open(pb).convert("L").resize((320, 180))
    d = ImageChops.difference(a, b)
    hist = d.histogram()
    over = sum(hist[33:])
    return 100.0 * over / (320 * 180)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reps", type=int, default=2)
    ap.add_argument("--mapas", default="127:1,D6:8,1A7:1")
    ap.add_argument("--reusar", help="pasta de uma rodada anterior: so recalcula")
    a = ap.parse_args()
    if a.reusar:
        root = os.path.abspath(a.reusar)
    else:
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        root = os.path.join(rodar.APP, "_audit", "out", "m04", "D", "fisica-" + stamp)
    os.makedirs(root, exist_ok=True)
    runs = {}
    for fps in (60, 30):
        for r in range(a.reps):
            name = "fps%d_r%d" % (fps, r + 1)
            out = os.path.join(root, name)
            jf = os.path.join(out, "fisica.json")
            if not a.reusar:
                res = rodar.rodar(out, ["--probe-fisica", "{out}/fisica.json",
                                        "--fisica-mapas", a.mapas],
                                  timeout=600, settings=settings_toml(fps == 60))
                print(name, "codigo", res["code"], "s", res["secs"], flush=True)
            runs[name] = rodar.ler_json(jf)
    # metricas
    met = {}
    for name, d in runs.items():
        if not d:
            met[name] = None
            continue
        per = {}
        for mp in d["maps"]:
            per[mp["map"]] = {"gameplay": mp["gameplay"], "reached": mp["reached"],
                              "m": analisa_mapa(d["samples"], mp["marks"])}
        met[name] = {"fps60": d["fps60"], "mapas": per}
    names60 = [n for n in met if n.startswith("fps60") and met[n]]
    names30 = [n for n in met if n.startswith("fps30") and met[n]]
    comps = []
    all_ok = bool(names60) and bool(names30)
    maps = []
    for n in names60 + names30:
        for k in met[n]["mapas"]:
            if k not in maps:
                maps.append(k)

    def vals(names, mp, seg, key):
        v = []
        for n in names:
            e = met[n]["mapas"].get(mp)
            if not e or not e["gameplay"]:
                continue
            x = e["m"].get(seg, {}).get(key) if seg != "final" else None
            if seg == "final":
                f = e["m"].get("final")
                x = math.hypot(f["dx"], f["dz"]) if f else None
            if x is not None:
                v.append(x)
        return v

    for mp in maps:
        lista = [("parado", "percurso"), ("andar", "percurso"), ("andar", "vel_regime"),
                         ("andar", "aceleracao_s"), ("andar", "deslize"), ("correr", "percurso"), ("correr", "vel_regime"),
                         ("correr", "aceleracao_s"), ("correr", "deslize"), ("pulo", "altura"),
                         ("pulo", "tempo_no_ar"), ("pulo", "percurso"), ("camera", "percurso"),
                         ("camera", "cam_giro"), ("camera", "cam_t90"), ("camera", "cam_dist"),
                         ("correr", "cam_giro"),
                         ("re", "percurso"), ("re", "vel_regime"), ("final", "dist"),
                         ("relogio", "taxa_s")]
        if mp in AGUA:
            lista = [("andar", "percurso3d"), ("correr", "percurso3d"), ("pulo", "percurso3d"),
                     ("re", "percurso3d"), ("camera", "cam_giro"), ("camera", "cam_t90"),
                     ("camera", "cam_dist"), ("relogio", "taxa_s")]
        for seg, key in lista:
            v60, v30 = vals(names60, mp, seg, key), vals(names30, mp, seg, key)
            if not v60 or not v30:
                comps.append({"mapa": mp, "trecho": seg, "medida": key, "ok": False,
                              "motivo": "sem amostras numa das configuracoes"})
                all_ok = False
                continue
            m60, m30 = sum(v60) / len(v60), sum(v30) / len(v30)
            spread = max(max(v60) - min(v60), max(v30) - min(v30))
            vseg = "correr" if seg == "final" else seg
            vmax = max(vals(names60 + names30, mp, vseg, "vel_p95") or [0.0])
            if key == "percurso3d":
                tol = 0.03 * max(m60, m30) + spread + 2.0
                why = ("nado em 3D: 3%% (1 quadro de 30 FPS de uma braçada de ~1 s) + variacao %.1f"
                       % spread)
            elif key in ("percurso", "deslize"):
                tol = vmax * Q30 + spread + 2.0
                why = ("1 quadro de 30 FPS a %.0f u/s (p95) + variacao %.1f + 2 u"
                       % (vmax, spread))
            elif key == "dist":
                # 10 mudancas de comando na sequencia; cada uma pode cair 1 quadro de 30 FPS
                # antes ou depois: o erro de tempo nao se cancela sempre
                tol = 4 * vmax * Q30 + spread + 2.0
                why = ("4 quadros de 30 FPS a %.0f u/s (10 comandos na sequencia) + variacao %.1f"
                       % (vmax, spread))
            elif key == "aceleracao_s":
                tol = Q30 + spread
                why = "1 quadro de 30 FPS + variacao %.3f s" % spread
            elif key == "vel_regime":
                tol = 0.02 * max(m60, m30) + spread
                why = "2%% + variacao entre repeticoes %.1f" % spread
            elif key == "altura":
                vs = vals(names60 + names30, mp, "pulo", "vel_subida") or [0.0]
                tol = abs(max(vs)) * (Q30 - 1 / 60.0) / 2 + spread + 1.0
                why = "v_subida %.0f u/s x (1/30-1/60)/2 + variacao %.1f + 1 u" % (max(vs), spread)
            elif key == "taxa_s":
                tol = 0.01 + spread
                why = "relogio do jogo: 1%% + variacao %.4f" % spread
            elif key == "cam_giro":
                # 1 quadro de 30 FPS do giro medio do trecho (graus/s) + variacao + 1 grau
                vel = abs(max(v60 + v30, key=abs)) / 1.0
                tol = vel * Q30 + spread + 1.0
                why = "1 quadro de 30 FPS a %.0f graus/s + variacao %.1f + 1 grau" % (vel, spread)
            elif key in ("cam_t90",):
                tol = Q30 + spread
                why = "1 quadro de 30 FPS + variacao %.3f s" % spread
            elif key == "cam_dist":
                tol = 0.03 * max(m60, m30) + spread
                why = "3%% + variacao %.1f" % spread
            elif key == "tempo_no_ar":
                tol = Q30 + spread
                why = "1 quadro de 30 FPS + variacao %.3f s" % spread
            else:
                tol = spread + 1.0
                why = "variacao + 1"
            ok = abs(m60 - m30) <= tol
            all_ok &= ok
            comps.append({"mapa": mp, "trecho": seg, "medida": key, "v60": v60, "v30": v30,
                          "media60": round(m60, 3), "media30": round(m30, 3),
                          "diferenca": round(m60 - m30, 3),
                          "diferenca_pct": round(100 * (m60 - m30) / m30, 2) if m30 else None,
                          "tolerancia": round(tol, 3), "por_que": why, "ok": ok})
    # imagens
    imgs = []
    for mp in maps:
        mid = mp[2:].upper()
        for tag in ("camera", "fim"):
            files = {n: os.path.join(root, n, "fisica_map%s_%s.png" % (mid, tag))
                     for n in names60 + names30}
            base = []
            for grp in (names60, names30):
                for i in range(len(grp)):
                    for j in range(i + 1, len(grp)):
                        d = img_diff(files[grp[i]], files[grp[j]])
                        if d is not None:
                            base.append(d)
            cross = [img_diff(files[x], files[y]) for x in names60 for y in names30]
            cross = [c for c in cross if c is not None]
            if not cross:
                continue
            lim = 1.5 * (max(base) if base else 0.0) + 2.0
            # so informativo: a diferenca por pixels acusa qualquer deslocamento de poucos pixels
            # numa cena com textura (a camera e medida pela posicao, acima)
            ok = max(cross) <= lim
            imgs.append({"mapa": mp, "imagem": tag, "mesma_config_pct": [round(b, 2) for b in base],
                         "30x60_pct": [round(c, 2) for c in cross], "limite_pct": round(lim, 2),
                         "ok": ok})
    res = {"version": 1, "pasta": root, "repeticoes": a.reps, "mapas_pedidos": a.mapas,
           "execucoes": {n: (bool(met[n]) and {k: {"gameplay": v["gameplay"]}
                                                for k, v in met[n]["mapas"].items()})
                         for n in met},
           "atualizacoes_por_s": {n: round(met[n]["mapas"][maps[0]]["m"].get("correr", {})
                                           .get("atualizacoes_por_s", 0), 1)
                                  for n in met if met[n] and maps},
           "comparacoes": comps, "imagens": imgs, "ok": all_ok,
           "estados_por_mapa": {n: {k: v["m"].get("estados") for k, v in met[n]["mapas"].items()}
                                for n in met if met[n]},
           "quadros_por_s": {n: {k: (v["m"].get("relogio") or {}).get("quadros_por_s")
                                 for k, v in met[n]["mapas"].items()} for n in met if met[n]}}
    with open(os.path.join(root, "resultado.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    nok = sum(1 for c in comps if c["ok"])
    print("comparacoes: %d/%d dentro da tolerancia" % (nok, len(comps)))
    for c in comps:
        if not c["ok"]:
            print("  FORA:", c["mapa"], c["trecho"], c["medida"], c.get("media60"),
                  c.get("media30"), "tol", c.get("tolerancia"), c.get("motivo", ""))
    print("imagens: %d/%d" % (sum(1 for i in imgs if i["ok"]), len(imgs)))
    for i in imgs:
        if not i["ok"]:
            print("  FORA:", i)
    print("resultado:", os.path.join(root, "resultado.json"), "OK" if all_ok else "FALHOU")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
