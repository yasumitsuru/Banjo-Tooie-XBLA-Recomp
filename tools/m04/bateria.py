"""M04 (C-97, etapa F): bateria de regressao num comando so, no jogo real e em pastas isoladas.

Roda em sequencia e grava _audit/out/m04/F/bateria-<data>/resultado.json:
  integridade dos 36 arquivos PT-BR e foto da pasta de saves da pessoa (inicio e fim);
  auto-teste do jogo (o mesmo do Craft); registro de sessao desse auto-teste (cabecalho, eventos,
  bloco de desempenho); saves (test_saves.py --rapido); CONQUISTAS (--probe-conquistas);
  monitores (test_monitores.py); viagem aos 18 mapas com os 3 perfis de distancia de desenho
  (--probe-dd-worlds); com --longos tambem traducao PT-BR (test_traducao.ps1), fisica 30x60
  (fisica_30x60.py) e matriz de video 3x3 (test_display_modes.py).
Sai com 0 quando tudo passa. A pasta de saves da pessoa so e lida (foto), nunca escrita.

Uso: <python com Pillow> -I tools/m04/bateria.py [--longos] [--so a,b]
"""
import argparse
import datetime
import glob
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rodar  # noqa: E402

APP = rodar.APP
PY = sys.executable
PY314 = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "Python", "Python314",
                     "python.exe")


def sh(args, timeout, log):
    t0 = time.time()
    with open(log, "wb") as f:
        try:
            p = subprocess.run(args, cwd=APP, stdout=f, stderr=subprocess.STDOUT, timeout=timeout)
            code = p.returncode
        except subprocess.TimeoutExpired:
            code = -1
    return code, round(time.time() - t0, 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--longos", action="store_true")
    ap.add_argument("--so")
    a = ap.parse_args()
    so = set(a.so.split(",")) if a.so else None
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    root = os.path.join(APP, "_audit", "out", "m04", "F", "bateria-" + stamp)
    os.makedirs(root)
    res = {"version": 1, "inicio": stamp, "itens": []}

    def item(nome, ok, detalhe, segundos=None):
        res["itens"].append({"item": nome, "ok": bool(ok), "detalhe": detalhe,
                             "segundos": segundos})
        print(("PASS " if ok else "FAIL ") + nome + " - " + str(detalhe)[:160], flush=True)
        with open(os.path.join(root, "resultado.json"), "w", encoding="utf-8") as f:
            json.dump(res, f, ensure_ascii=False, indent=1)

    def quer(n):
        return so is None or n in so

    inv = os.path.join(APP, "_audit", "out", "inventory.json")
    game = rodar.GAME
    code, s = sh([PY, "-I", "_audit/verify_baseline.py", game, inv,
                  os.path.join(root, "integridade_inicio.json")], 300,
                 os.path.join(root, "integridade_inicio.log"))
    item("integridade_inicio", code == 0, "36 arquivos PT-BR", s)
    foto0 = os.path.join(root, "pessoa_inicio.txt")
    code, s = sh([PY, "-I", "tools/m04/foto_pessoa.py", "--out", foto0], 120,
                 os.path.join(root, "pessoa_inicio.log"))
    item("foto_pessoa_inicio", code == 0, "pasta de saves da pessoa (somente leitura)", s)

    if quer("autoteste"):
        out = os.path.join(root, "autoteste")
        r = rodar.rodar(out, ["--self-test", "{out}/selftest.json"], timeout=120)
        st = rodar.ler_json(os.path.join(out, "selftest.json")) or {"steps": []}
        steps = st.get("steps", [])
        ok = r["code"] == 0 and steps and all(x["passed"] for x in steps)
        item("autoteste", ok, "%d/%d passos" % (sum(x["passed"] for x in steps), len(steps)),
             r["secs"])
        logs = glob.glob(os.path.join(out, "user", "logs", "sessao-*.txt"))
        txt = open(logs[0], encoding="utf-8", errors="replace").read() if logs else ""
        conf = {"cabecalho": "=== Banjo-Tooie" in txt and "gpu:" in txt and "build:" in txt,
                "eventos": "[sessao] mapa:" in txt and "[sessao] etapa:" in txt,
                "desempenho": "[sessao] desempenho:" in txt,
                "fim": "[sessao] sessao: fim:" in txt}
        item("registro_de_sessao", all(conf.values()), conf)
    if quer("saves"):
        code, s = sh([PY, "-I", "tools/test_saves.py", "--rapido"], 1800,
                     os.path.join(root, "saves.log"))
        item("saves", code == 0, "tools/test_saves.py --rapido", s)
    if quer("conquistas"):
        out = os.path.join(root, "conquistas")
        r = rodar.rodar(out, ["--probe-conquistas", "{out}/conquistas.json"], timeout=400)
        d = rodar.ler_json(os.path.join(out, "conquistas.json")) or {}
        stub = open(os.path.join(out, "runtime.log"), encoding="utf-8",
                    errors="replace").read().count("XamShowAchievementsUI")
        item("conquistas", d.get("ok") and stub == 0,
             "menu principal e pausa; chamadas ao painel do Xbox: %d" % stub, r["secs"])
    if quer("monitores"):
        code, s = sh([PY, "-I", "tools/m04/test_monitores.py"], 2400,
                     os.path.join(root, "monitores.log"))
        item("monitores", code == 0, "tools/m04/test_monitores.py", s)
    if quer("mundos"):
        out = os.path.join(root, "mundos")
        r = rodar.rodar(out, ["--probe-dd-worlds", "{out}/dd.json"], timeout=1500)
        d = rodar.ler_json(os.path.join(out, "dd.json")) or {"worlds": []}
        w = d.get("worlds", [])
        ok = d.get("finished") == 1 and len(w) == 18 and all(x["verdict"] == "PASS" for x in w)
        item("mundos_distancia", ok, "%d/18 mapas PASS, terminado=%s" % (
            sum(x["verdict"] == "PASS" for x in w), d.get("finished")), r["secs"])
    if a.longos and quer("traducao"):
        code, s = sh(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                      os.path.join(APP, "tools", "test_traducao.ps1")], 2400,
                     os.path.join(root, "traducao.log"))
        item("traducao_ptbr", code == 0, "tools/test_traducao.ps1", s)
    if a.longos and quer("fisica"):
        code, s = sh([PY314 if os.path.exists(PY314) else PY, "-I", "tools/m04/fisica_30x60.py"],
                     3600, os.path.join(root, "fisica.log"))
        item("fisica_30x60", code == 0, "tools/m04/fisica_30x60.py (ver resultado.json)", s)
    if a.longos and quer("matriz"):
        code, s = sh([PY314 if os.path.exists(PY314) else PY, "-I", "tools/test_display_modes.py",
                      "--out", os.path.join(root, "matriz")], 5400,
                     os.path.join(root, "matriz.log"))
        item("matriz_video", code in (0, 2), "codigo %d (2 = so limite fisico)" % code, s)

    code, s = sh([PY, "-I", "_audit/verify_baseline.py", game, inv,
                  os.path.join(root, "integridade_fim.json")], 300,
                 os.path.join(root, "integridade_fim.log"))
    item("integridade_fim", code == 0, "36 arquivos PT-BR", s)
    code, s = sh([PY, "-I", "tools/m04/foto_pessoa.py", "--out",
                  os.path.join(root, "pessoa_fim.txt"), "--compare", foto0], 120,
                 os.path.join(root, "pessoa_fim.log"))
    item("pasta_da_pessoa_igual", code == 0, "foto do fim = foto do inicio", s)
    res["ok"] = all(i["ok"] for i in res["itens"])
    with open(os.path.join(root, "resultado.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    print("TOTAL: %d/%d PASS (%s)" % (sum(i["ok"] for i in res["itens"]), len(res["itens"]), root))
    return 0 if res["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
