"""M04 - roda o jogo real (out/native/banjotooie.exe) numa pasta de usuario isolada.

Base comum da bateria (tools/m04/bateria.py) e uso avulso. Nunca usa a pasta de saves da pessoa:
sempre passa --user_data_root dentro de _audit/out/m04/.

Uso avulso:
  python -I tools/m04/rodar.py <pasta_saida> [--save <savegame.dat>] [--settings <toml>]
         [--timeout 300] -- <argumentos do jogo, ex.: --probe-dd-worlds {out}/dd.json>
'{out}' nos argumentos vira a pasta de saida.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

APP = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(APP, "out", "native", "banjotooie.exe")
GAME = os.path.join(os.path.dirname(APP), "Banjo Tooie BR")
CACHE = os.path.join(APP, "out", "craft-runtime", "cache")
SAVE_REL = os.path.join("B13EBABEBABEBABE", "58410955", "00000001", "BanjoSave.bin",
                        "savegame.dat")
HEADER_REL = os.path.join("B13EBABEBABEBABE", "58410955", "Headers", "00000001",
                          "BanjoSave.bin.header")


def preparar_usuario(user, save=None, settings=None, header=None):
    """Pasta de usuario de teste. save/header: caminhos de arquivos a copiar; settings: texto."""
    if os.path.isdir(user):
        shutil.rmtree(user)
    os.makedirs(user)
    if save:
        dst = os.path.join(user, SAVE_REL)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(save, dst)
    if header:
        dst = os.path.join(user, HEADER_REL)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(header, dst)
    if settings is not None:
        with open(os.path.join(user, "pc_settings.toml"), "w", encoding="utf-8",
                  newline="\n") as f:
            f.write(settings)


def rodar(out, args, timeout=300, save=None, settings=None, header=None, game=GAME,
          keep_user=False, mute=True):
    """Roda o jogo; devolve dict com codigo, segundos, caminhos e se estourou o prazo."""
    os.makedirs(out, exist_ok=True)
    user = os.path.join(out, "user")
    if not keep_user or not os.path.isdir(user):
        preparar_usuario(user, save=save, settings=settings, header=header)
    boot = os.path.join(out, "boot.json")
    full = [EXE, "--game_data_root", game, "--user_data_root", user, "--cache_path", CACHE,
            "--port_silent", "--boot_report", boot,
            # o app encerra execuções automáticas em 240 s (smoke_timeout_seconds); as sondas
            # longas precisam do prazo da própria rodada
            "--smoke_timeout_seconds", str(max(30, int(timeout) - 10)),
            "--log_file", os.path.join(out, "runtime.log"), "--log_max_file_size_mb", "1024"]
    if mute:
        full.append("--audio_mute")
    full += [a.replace("{out}", out) for a in args]
    t0 = time.time()
    timed_out = False
    with open(os.path.join(out, "stdout.txt"), "wb") as so, \
            open(os.path.join(out, "stderr.txt"), "wb") as se:
        p = subprocess.Popen(full, cwd=APP, stdout=so, stderr=se)
        try:
            code = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            p.kill()
            code = p.wait()
    secs = time.time() - t0
    rep = None
    if os.path.exists(boot):
        try:
            with open(boot, encoding="utf-8") as f:
                rep = json.load(f)
        except (OSError, ValueError):
            rep = None
    with open(os.path.join(out, "comando.txt"), "w", encoding="utf-8") as f:
        f.write(subprocess.list2cmdline(full) + "\n")
    return {"code": code, "secs": round(secs, 1), "timed_out": timed_out, "out": out,
            "user": user, "boot": rep}


def ler_json(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def main():
    if "--" not in sys.argv:
        print(__doc__)
        return 2
    i = sys.argv.index("--")
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--save")
    ap.add_argument("--settings")
    ap.add_argument("--timeout", type=float, default=300)
    a = ap.parse_args(sys.argv[1:i])
    settings = None
    if a.settings:
        with open(a.settings, encoding="utf-8") as f:
            settings = f.read()
    r = rodar(os.path.abspath(a.out), sys.argv[i + 1:], timeout=a.timeout, save=a.save,
              settings=settings)
    r.pop("boot", None)
    print(json.dumps(r, ensure_ascii=False))
    return 0 if r["code"] == 0 and not r["timed_out"] else 1


if __name__ == "__main__":
    sys.exit(main())
