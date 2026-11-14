"""M04 etapa D: 1080p a 2x (C-22), sem bordas no monitor inteiro (C-25), monitor escolhido e
resolucao automatica pelo monitor certo (C-29), monitor que nao e 16:9 (C-27).

Roda o jogo real com --probe-display (sem trocas de modo) numa pasta isolada por caso, com o
pc_settings.toml do caso, e confere no relatorio da sonda:
  - guest_framebuffer: imagem interna (1080p -> 2560x1440; 1440p -> 2560x1440; 4K -> 3840x2160);
  - window.monitor.device = monitor pedido; chosen_monitor = monitor pedido;
  - sem bordas: retangulo externo = retangulo do monitor (nao a area util);
  - janela: cliente = tamanho de saida quando cabe na area util;
  - resolucao automatica = classe do monitor (>=3840x2160 4K, >=2560x1440 1440p, senao 1080p).
Monitores: lidos do Windows (EnumDisplayMonitors via PowerShell nao e preciso: a sonda informa).

Uso: python -I tools/m04/test_monitores.py [--casos a,b]
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import datetime
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rodar  # noqa: E402

GUEST = {"1080p": (2560, 1440), "1440p": (2560, 1440), "4k": (3840, 2160)}
OUT = {"1080p": (1920, 1080), "1440p": (2560, 1440), "4k": (3840, 2160)}


def monitores():
    """[(device, w, h, primary)] pelo Windows (DPI fisico)."""
    user32 = ctypes.windll.user32
    try:
        user32.SetThreadDpiAwarenessContext(ctypes.c_void_p(-4))
    except AttributeError:
        pass

    class MONITORINFOEXW(ctypes.Structure):
        _fields_ = [("cbSize", wt.DWORD), ("rcMonitor", wt.RECT), ("rcWork", wt.RECT),
                    ("dwFlags", wt.DWORD), ("szDevice", wt.WCHAR * 32)]
    out = []
    PROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HMONITOR, wt.HDC, ctypes.POINTER(wt.RECT), wt.LPARAM)

    def cb(h, _dc, _r, _p):
        mi = MONITORINFOEXW()
        mi.cbSize = ctypes.sizeof(mi)
        user32.GetMonitorInfoW(h, ctypes.byref(mi))
        r = mi.rcMonitor
        out.append((mi.szDevice, r.right - r.left, r.bottom - r.top, bool(mi.dwFlags & 1)))
        return True
    user32.EnumDisplayMonitors(None, None, PROC(cb), 0)
    return out


def classe(w, h):
    return "4k" if w >= 3840 and h >= 2160 else "1440p" if w >= 2560 and h >= 1440 else "1080p"


def toml(resolution, auto, mode, monitor):
    return ("settings_version = 3\n[jogo]\nskip_intro_logos = true\npause_on_focus_loss = false\n"
            "[video]\nresolution = \"%s\"\nresolution_auto = %s\ndisplay_mode = \"%s\"\n"
            "monitor = \"%s\"\n" % (resolution, "true" if auto else "false", mode, monitor))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--casos")
    a = ap.parse_args()
    mons = monitores()
    prim = next(m for m in mons if m[3])
    others = [m for m in mons if not m[3]]
    casos = [
        # nome, monitor, resolucao (None = automatica), modo
        ("principal_1080p_sembordas", prim, "1080p", "SemBordas"),
        ("principal_1080p_janela", prim, "1080p", "Janela"),
        ("principal_auto_sembordas", prim, None, "SemBordas"),
    ]
    for m in others:
        tag = m[0].replace("\\", "").replace(".", "")
        casos.append(("%s_auto_janela" % tag, m, None, "Janela"))
        casos.append(("%s_auto_sembordas" % tag, m, None, "SemBordas"))
    if a.casos:
        casos = [c for c in casos if c[0] in a.casos.split(",")]
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    root = os.path.join(rodar.APP, "_audit", "out", "m04", "D", "monitores-" + stamp)
    os.makedirs(root)
    res = {"version": 1, "monitores": [{"device": m[0], "w": m[1], "h": m[2], "principal": m[3]}
                                       for m in mons], "casos": []}
    ok_all = True
    for nome, mon, resol, modo in casos:
        out = os.path.join(root, nome)
        r = rodar.rodar(out, ["--probe-display", "{out}/display.json", "--frames", "120"],
                        timeout=180,
                        settings=toml(resol or "4k", resol is None, modo, mon[0]))
        rep = rodar.ler_json(os.path.join(out, "display.json")) or {}
        esperado = resol or classe(mon[1], mon[2])
        g = rep.get("guest_framebuffer", {})
        w = rep.get("window", {})
        wm = w.get("monitor", {})
        ch = rep.get("chosen_monitor", {})
        outer = w.get("outer_rect", {})
        client = w.get("client_rect", {})
        mrect = wm.get("rect", {})
        work = wm.get("work_area", {})
        conf = []
        conf.append(("saiu com 0", r["code"] == 0 and not r["timed_out"]))
        conf.append(("monitor escolhido = pedido", ch.get("device") == mon[0]))
        conf.append(("janela no monitor pedido", wm.get("device") == mon[0]))
        conf.append(("resolucao aplicada = %s" % esperado,
                     rep.get("applied", {}).get("resolution") == esperado))
        conf.append(("imagem interna %dx%d" % GUEST[esperado],
                     (g.get("width"), g.get("height")) == GUEST[esperado]))
        conf.append(("imagem com conteudo", bool(g.get("non_blank"))))
        conf.append(("modo confere com a janela", bool(w.get("mode_match"))))
        if modo == "SemBordas":
            conf.append(("sem bordas = monitor inteiro",
                         (outer.get("width"), outer.get("height")) == (mrect.get("width"),
                                                                       mrect.get("height"))
                         and outer.get("left") == mrect.get("left")))
        if modo == "Janela":
            ow, oh = OUT[esperado]
            cabe = work.get("width", 0) >= ow and work.get("height", 0) >= oh
            if cabe:
                conf.append(("cliente = %dx%d" % (ow, oh),
                             (client.get("width"), client.get("height")) == (ow, oh)))
            else:
                conf.append(("cliente cabe na area util", client.get("width", 0) <= work.get(
                    "width", 0) and client.get("height", 0) <= work.get("height", 0)))
        ok = all(v for _, v in conf)
        ok_all &= ok
        res["casos"].append({"caso": nome, "monitor": mon[0], "monitor_wh": [mon[1], mon[2]],
                             "resolucao": resol or "auto", "modo": modo, "ok": ok,
                             "conferencias": [{"o_que": k, "ok": bool(v)} for k, v in conf],
                             "guest": [g.get("width"), g.get("height")],
                             "outer": outer, "client": client, "monitor_rect": mrect,
                             "work": work, "aplicada": rep.get("applied", {}),
                             "segundos": r["secs"]})
        print(("PASS " if ok else "FAIL ") + nome)
        for k, v in conf:
            if not v:
                print("   falhou:", k)
    res["ok"] = ok_all
    with open(os.path.join(root, "resultado.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    print("resultado:", os.path.join(root, "resultado.json"))
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
