"""Teste automático da segurança dos saves (M04, etapa B).

Roda o jogo de verdade (out/native/banjotooie.exe) com a sonda --probe-saves, cada cenário numa
pasta de usuário própria dentro de _audit/out/m04/B/. Nunca lê nem escreve a pasta de saves da
pessoa: o save real usado é a CÓPIA guardada em _audit/out/save-protecao-20261113/; os saves
estragados são cópias editadas (autorizado na M04 só para testes).

Uso:  python -I tools/test_saves.py [--corpus <pasta com saves reais para conferir>]
Sai com 0 quando todos os cenários passam. Resultado: <pasta da rodada>/resultado.json.
"""
import argparse
import ctypes
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from ctypes import wintypes

APP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(APP, "out", "native", "banjotooie.exe")
GAME = os.path.join(os.path.dirname(APP), "Banjo Tooie BR")
CACHE = os.path.join(APP, "out", "craft-runtime", "cache")
REAL = os.path.join(APP, "_audit", "out", "save-protecao-20261113", "savegame_seu_0921.dat")
SAVE_REL = os.path.join("B13EBABEBABEBABE", "58410955", "00000001", "BanjoSave.bin", "savegame.dat")
SLOT = [0x200, 0x400, 0x600, 0x800]


def sha(b):
    return hashlib.sha256(b).hexdigest()


def rd(p):
    with open(p, "rb") as f:
        return f.read()


def wr(p, b):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(b)


def edit(b, off, new):
    b = bytearray(b)
    b[off:off + len(new)] = new
    return bytes(b)


def slot_with_game(real):
    """Bloco de jogo válido do save real (o primeiro com KHJC)."""
    for o in SLOT:
        if real[o:o + 4] == b"KHJC":
            return o
    raise SystemExit("o save real de referência não tem jogo gravado")


class Run:
    def __init__(self, root):
        self.root = root
        self.results = []

    def user(self, name, save=None, backups=(), settings=None):
        d = os.path.join(self.root, name, "user")
        if save is not None:
            wr(os.path.join(d, SAVE_REL), save)
        for fname, data in backups:
            wr(os.path.join(d, "backups", fname), data)
        if settings:
            wr(os.path.join(d, "pc_settings.toml"), settings.encode("utf-8"))
        os.makedirs(d, exist_ok=True)
        return d

    def probe(self, name, user, extra=(), timeout=150):
        out = os.path.join(self.root, name, "probe.json")
        args = [EXE, "--game_data_root", GAME, "--user_data_root", user, "--cache_path", CACHE,
                "--audio_mute", "--port_silent", "--boot_report",
                os.path.join(self.root, name, "boot.json"), "--probe-saves", out, *extra]
        t0 = time.time()
        p = subprocess.run(args, cwd=APP, timeout=timeout, capture_output=True)
        secs = time.time() - t0
        data = None
        if os.path.exists(out):
            with open(out, encoding="utf-8") as f:
                data = json.load(f)
        return p.returncode, data, secs

    def check(self, name, conds, info):
        ok = all(v for _, v in conds)
        self.results.append({"cenario": name, "ok": ok,
                             "conferencias": [{"o_que": k, "ok": bool(v)} for k, v in conds],
                             "info": info})
        print(("PASS " if ok else "FAIL ") + name)
        for k, v in conds:
            if not v:
                print("   falhou:", k)
        return ok


def lock_exclusive(path):
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateFileW.restype = wintypes.HANDLE
    h = k32.CreateFileW(path, 0x80000000 | 0x40000000, 0, None, 3, 0x80, None)  # sem compartilhar
    if h in (None, wintypes.HANDLE(-1).value):
        raise SystemExit("não consegui prender o arquivo de teste")
    return k32, h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", help="pasta com saves reais (.dat) para conferir; copiados para a rodada")
    ap.add_argument("--rapido", action="store_true", help="pula a gravação real do jogo (S18, ~1-2 min)")
    a = ap.parse_args()
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    root = os.path.join(APP, "_audit", "out", "m04", "B", "run-" + stamp)
    os.makedirs(root)
    r = Run(root)
    real = rd(REAL)
    g = slot_with_game(real)
    # Saves de teste (cópias editadas)
    two = edit(real, [o for o in SLOT if o != g][0], real[g:g + 512])          # 2 jogos
    newgame = edit(bytes(3072), 0x100, real[0:256] if real[0] == 9 else real[0x100:0x200])  # sem jogos
    bad_type = edit(real, g + 4, b"\x09")                                        # seção tipo 9
    bad_sum = edit(real, g + 512 - 8, b"\x00\x00\x00\x01")                       # soma != 0
    bad_glob = edit(edit(real, 0, b"\x07"), 0x100, b"\x07")                      # nenhuma global
    deleted = edit(real, g, bytes(8))                                            # jogo apagado
    truncated = real[:1000]
    old_name = "save-20261101-100000-aaaaaaaaaaaaaaaa.dat"
    older_name = "save-20261031-100000-bbbbbbbbbbbbbbbb.dat"
    save_path = lambda u: os.path.join(u, SAVE_REL)

    # S1: save válido, sem backups: nada muda, backup inicial criado
    u = r.user("s01_valido", real)
    code, j, s = r.probe("s01_valido", u)
    r.check("s01_valido", [
        ("saiu com 0", code == 0), ("relatório", j is not None),
        ("abertura = ok", j and j["startup"]["action"] == "ok"),
        ("save igual", sha(rd(save_path(u))) == sha(real)),
        ("backup inicial", j and len(j["restorable"]) == 1),
    ], {"segundos": s, "probe": j})

    # S2: save todo zerado e nenhum backup: é um save novo, nada muda
    u = r.user("s02_em_branco", bytes(3072))
    code, j, s = r.probe("s02_em_branco", u)
    r.check("s02_em_branco", [
        ("abertura = em_branco", j and j["startup"]["action"] == "em_branco"),
        ("save igual", rd(save_path(u)) == bytes(3072)),
        ("sem cópia corrupt", j and not any(n.startswith("corrupt-") for n in j["backup_files"])),
    ], {"segundos": s, "probe": j})

    # S3..S6: save recusado pelas regras do jogo + backup válido: cópia exata + restauração
    for name, data in (("s03_truncado", truncated), ("s04_secao_tipo_9", bad_type),
                       ("s05_soma_diferente", bad_sum), ("s06_sem_global_valida", bad_glob)):
        u = r.user(name, data, backups=[(old_name, real)])
        code, j, s = r.probe(name, u)
        corrupt = j and j["startup"]["corrupt_copy"]
        cpath = os.path.join(u, "backups", corrupt) if corrupt else ""
        r.check(name, [
            ("abertura = restaurado", j and j["startup"]["action"] == "restaurado"),
            ("restaurado do backup", j and j["startup"]["restored_from"] == old_name),
            ("save = backup", sha(rd(save_path(u))) == sha(real)),
            ("cópia corrupt existe", bool(corrupt) and os.path.exists(cpath)),
            ("cópia corrupt = arquivo recusado, byte a byte", bool(corrupt) and os.path.exists(cpath)
             and sha(rd(cpath)) == sha(data)),
        ], {"segundos": s, "probe": j})

    # S7: jogo apagado no próprio jogo (8 bytes zerados) é válido: nada é restaurado
    u = r.user("s07_jogo_apagado", deleted, backups=[(old_name, real)])
    code, j, s = r.probe("s07_jogo_apagado", u)
    r.check("s07_jogo_apagado", [
        ("abertura = ok", j and j["startup"]["action"] == "ok"),
        ("save igual", sha(rd(save_path(u))) == sha(deleted)),
    ], {"segundos": s, "probe": j})

    # S8: save recusado e nenhum backup: guarda a cópia e não mexe no save
    u = r.user("s08_sem_backup", bad_type)
    code, j, s = r.probe("s08_sem_backup", u)
    corrupt = j and j["startup"]["corrupt_copy"]
    r.check("s08_sem_backup", [
        ("abertura = invalido_sem_backup", j and j["startup"]["action"] == "invalido_sem_backup"),
        ("save não mudou", sha(rd(save_path(u))) == sha(bad_type)),
        ("cópia corrupt = arquivo", bool(corrupt) and sha(rd(os.path.join(u, "backups", corrupt))) == sha(bad_type)),
    ], {"segundos": s, "probe": j})

    # S9: save preso por outro programa (como o OneDrive sincronizando): nada é mexido
    u = r.user("s09_save_preso", bad_type, backups=[(old_name, real)])
    k32, h = lock_exclusive(save_path(u))
    try:
        code, j, s = r.probe("s09_save_preso", u)
    finally:
        k32.CloseHandle(h)
    r.check("s09_save_preso", [
        ("abertura = bloqueado", j and j["startup"]["action"] == "bloqueado"),
        ("save não mudou", sha(rd(save_path(u))) == sha(bad_type)),
        ("nada restaurado", j and not j["startup"]["restored_from"]),
    ], {"segundos": s, "probe": j})

    # S10: save zerado mas backups com jogo gravado: é gravação perdida, restaura
    u = r.user("s10_zerado_com_backup", bytes(3072), backups=[(old_name, real)])
    code, j, s = r.probe("s10_zerado_com_backup", u)
    r.check("s10_zerado_com_backup", [
        ("abertura = restaurado", j and j["startup"]["action"] == "restaurado"),
        ("save = backup", sha(rd(save_path(u))) == sha(real)),
    ], {"segundos": s, "probe": j})

    # S11: backups inválidos mais novos (de versões antigas) não empurram os válidos para fora
    junk = [("save-202611%02d-120000-%016x.dat" % (5 + i, i), b"\xde" * 3072) for i in range(8)]
    feed = os.path.join(root, "s11_invalidos_nao_empurram", "feed")
    wr(os.path.join(feed, "1.dat"), two)
    u = r.user("s11_invalidos_nao_empurram", real,
               backups=[(older_name, real), (old_name, deleted)] + junk,
               settings="settings_version = 3\n[saves]\nsave_backup = true\nbackup_count = 3\n")
    code, j, s = r.probe("s11_invalidos_nao_empurram", u, ["--probe-saves-feed", feed])
    files = j["backup_files"] if j else []
    r.check("s11_invalidos_nao_empurram", [
        ("os 2 válidos antigos continuam", older_name in files and old_name in files),
        ("backup novo da gravação", j and any(x["games"] == 2 and x["kind"] == "save" for x in j["restorable"])),
        ("3 válidos para restaurar", j and len([x for x in j["restorable"] if x["kind"] == "save"]) == 3),
    ], {"segundos": s, "probe": j})

    # S12: rodízio: com 3 mantidos, ficam os 3 válidos mais novos
    feed = os.path.join(root, "s12_rodizio", "feed")
    wr(os.path.join(feed, "1.dat"), two)
    wr(os.path.join(feed, "2.dat"), deleted)
    olds = [("save-202610%02d-100000-%016x.dat" % (20 + i, i), real) for i in range(5)]
    u = r.user("s12_rodizio", real, backups=olds,
               settings="settings_version = 3\n[saves]\nsave_backup = true\nbackup_count = 3\n")
    code, j, s = r.probe("s12_rodizio", u, ["--probe-saves-feed", feed])
    saves = sorted(n for n in (j["backup_files"] if j else []) if n.startswith("save-"))
    r.check("s12_rodizio", [
        ("3 save-* sobraram", len(saves) == 3),
        ("os mais antigos saíram", olds[0][0] not in saves and olds[3][0] not in saves),
        ("o mais novo dos antigos ficou", olds[4][0] in saves),
    ], {"segundos": s, "probe": j, "save_files": saves})

    # S13: gravação com menos jogos guarda a anterior fora do rodízio (keep-*)
    feed = os.path.join(root, "s13_jogo_sumiu", "feed")
    wr(os.path.join(feed, "1.dat"), real)
    u = r.user("s13_jogo_sumiu", two, backups=[(old_name, two)])
    code, j, s = r.probe("s13_jogo_sumiu", u, ["--probe-saves-feed", feed])
    keeps = [n for n in (j["backup_files"] if j else []) if n.startswith("keep-")]
    r.check("s13_jogo_sumiu", [
        ("keep-* criado", len(keeps) == 1),
        ("keep-* = save com 2 jogos", len(keeps) == 1 and sha(rd(os.path.join(u, "backups", keeps[0]))) == sha(two)),
    ], {"segundos": s, "probe": j})

    # S14: escolher um backup anterior pela linha RESTAURAR do menu do jogo
    u = r.user("s14_escolher_backup", deleted, backups=[(older_name, real), (old_name, two)])
    code, j, s = r.probe("s14_escolher_backup", u, ["--probe-saves-choose", "1"])
    ch = j["choose"] if j else {}
    before_restore = [n for n in (j["backup_files"] if j else []) if n.startswith("save-")
                      and n not in (older_name, old_name)]
    r.check("s14_escolher_backup", [
        ("linha mostra a data do mais novo", "01.11 10:00" in ch.get("before", "")),
        ("direita vai para o anterior", "31.10 10:00" in ch.get("after_moves", "")),
        ("A pede confirmação", "CONFIRMA" in ch.get("after_confirm", "") or "SURE" in ch.get("after_confirm", "")),
        ("A de novo restaura", "RESTAURADO" in ch.get("after", "") or "RESTORED" in ch.get("after", "")),
        ("save = backup escolhido", sha(rd(save_path(u))) == sha(real)),
        ("save anterior guardado antes", any(sha(rd(os.path.join(u, "backups", n))) == sha(deleted)
                                             for n in before_restore)),
    ], {"segundos": s, "probe": j})

    # S16: gravação interrompida (sobrou savegame.dat.port-prev do runtime) e save pela metade:
    # volta o estado de antes da gravação, que é mais novo que o último backup
    u = r.user("s16_gravacao_interrompida", bad_type, backups=[(old_name, real)])
    wr(save_path(u) + ".port-prev", two)
    code, j, s = r.probe("s16_gravacao_interrompida", u)
    r.check("s16_gravacao_interrompida", [
        ("abertura viu a interrupção", j and j["startup"]["interrupted"] is True),
        ("abertura = restaurado", j and j["startup"]["action"] == "restaurado"),
        ("save = estado de antes da gravação", sha(rd(save_path(u))) == sha(two)),
        ("cópia corrupt = save pela metade", j and bool(j["startup"]["corrupt_copy"]) and
         sha(rd(os.path.join(u, "backups", j["startup"]["corrupt_copy"]))) == sha(bad_type)),
        (".port-prev removido", not os.path.exists(save_path(u) + ".port-prev")),
    ], {"segundos": s, "probe": j})

    # S17: gravação interrompida, mas o save atual passa: fica, e o anterior vai para keep-*
    u = r.user("s17_interrompida_save_bom", real, backups=[(old_name, real)])
    wr(save_path(u) + ".port-prev", two)
    code, j, s = r.probe("s17_interrompida_save_bom", u)
    keeps = [n for n in (j["backup_files"] if j else []) if n.startswith("keep-")]
    r.check("s17_interrompida_save_bom", [
        ("abertura = ok", j and j["startup"]["action"] == "ok" and j["startup"]["interrupted"] is True),
        ("save não mudou", sha(rd(save_path(u))) == sha(real)),
        ("anterior guardado em keep-*", len(keeps) == 1 and sha(rd(os.path.join(u, "backups", keeps[0]))) == sha(two)),
        (".port-prev removido", not os.path.exists(save_path(u) + ".port-prev")),
    ], {"segundos": s, "probe": j})

    # S15: conferência de saves reais (todos aceitos) e estragados (todos recusados)
    corpus = os.path.join(root, "s15_conferencia", "corpus")
    good = os.path.join(corpus, "reais")
    bad = os.path.join(corpus, "estragados")
    seen = set()
    sources = [a.corpus] if a.corpus else []
    for src in sources:
        for dp, _, fn in os.walk(src):
            for f in fn:
                if f.endswith(".dat"):
                    b = rd(os.path.join(dp, f))
                    if sha(b) not in seen:
                        seen.add(sha(b))
                        wr(os.path.join(good, "%04d.dat" % len(seen)), b)
    if not seen:
        wr(os.path.join(good, "0001.dat"), real)
        wr(os.path.join(good, "0002.dat"), two)
        wr(os.path.join(good, "0003.dat"), deleted)
        wr(os.path.join(good, "0004.dat"), newgame)
        seen = {1, 2, 3, 4}
    negs = {
        "tamanho_0": b"", "tamanho_1": b"\x09", "tamanho_3071": real[:3071], "tamanho_3073": real + b"\x00",
        "tudo_DE": b"\xde" * 3072, "tudo_FF": b"\xff" * 3072, "aleatorio": os.urandom(3072),
        "assinatura": edit(real, g, b"KHJD"), "tipo_7": edit(real, g + 4, b"\x07"),
        "versao_1E": edit(real, g + 6, b"\x1e"), "soma_jogo": bad_sum,
        "soma_globais": edit(edit(real, 0xF8, b"\x01"), 0x1F8, b"\x01"),
        "sem_global": bad_glob,
        "secao_passa_do_fim": edit(real, g + 4, b"\x02\xff"),
    }
    for k, v in negs.items():
        wr(os.path.join(bad, k + ".dat"), v)
    u = r.user("s15_conferencia", real)
    code, j, s = r.probe("s15_conferencia", u, ["--probe-saves-inspect", corpus])
    ins = j["inspect"] if j else []
    good_res = [x for x in ins if x["file"].startswith("reais")]
    bad_res = [x for x in ins if x["file"].startswith("estragados")]
    r.check("s15_conferencia", [
        ("todos os reais conferidos", len(good_res) == len(seen)),
        ("todos os reais aceitos", good_res and all(x["result"]["usable"] for x in good_res)),
        ("todos os estragados conferidos", len(bad_res) == len(negs)),
        ("todos os estragados recusados", bad_res and not any(x["result"]["usable"] for x in bad_res)),
    ], {"segundos": s, "reais": len(good_res), "estragados": len(bad_res),
        "aceitos_errados": [x["file"] for x in bad_res if x["result"]["usable"]],
        "recusados_errados": [x for x in good_res if not x["result"]["usable"]]})

    # S18: gravação REAL do jogo (jogo novo + todos os colecionáveis + SALVAR E SAIR, sonda
    # --make-advanced-save): o runtime guarda o estado anterior enquanto grava e apaga ao
    # fechar; o save gravado passa nas regras do jogo. Também é o save avançado de teste (C-95).
    if not a.rapido:
        name = "s18_gravacao_real"
        u = r.user(name)
        out = os.path.join(root, name, "advanced.json")
        log = os.path.join(root, name, "runtime.log")
        t0 = time.time()
        p = subprocess.run([EXE, "--game_data_root", GAME, "--user_data_root", u, "--cache_path", CACHE,
                            "--audio_mute", "--port_silent", "--log_file", log,
                            "--make-advanced-save", out], cwd=APP, timeout=240, capture_output=True)
        secs = time.time() - t0
        j = json.load(open(out, encoding="utf-8")) if os.path.exists(out) else None
        sp = save_path(u)
        logtxt = open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else ""
        ins = None
        if os.path.exists(sp):
            # mesma regra do jogo, pela sonda (C++), num diretório de conferência
            chk = os.path.join(root, name, "conferir")
            wr(os.path.join(chk, "gravado.dat"), rd(sp))
            _, jj, _ = r.probe(name + "_conferir", r.user(name + "_conferir", real), ["--probe-saves-inspect", chk])
            ins = jj["inspect"][0]["result"] if jj and jj.get("inspect") else None
        r.check(name, [
            ("sonda do jogo ok", p.returncode == 0 and j is not None and j.get("ok") is True),
            ("o jogo gravou o save", os.path.exists(sp)),
            ("save gravado passa nas regras do jogo", ins is not None and ins["usable"]),
            ("save gravado tem jogo", ins is not None and ins["games"] >= 1),
            ("runtime guardou o estado anterior durante a gravação", "write journal" in logtxt),
            ("nada sobrou depois de fechar (.port-prev)", not os.path.exists(sp + ".port-prev")),
        ], {"segundos": secs, "advanced": j, "inspect": ins})

    ok = all(x["ok"] for x in r.results)
    with open(os.path.join(root, "resultado.json"), "w", encoding="utf-8") as f:
        json.dump({"ok": ok, "pasta": root, "cenarios": r.results}, f, ensure_ascii=False, indent=1)
    print("TOTAL: %d/%d PASS  (%s)" % (sum(x["ok"] for x in r.results), len(r.results), root))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
