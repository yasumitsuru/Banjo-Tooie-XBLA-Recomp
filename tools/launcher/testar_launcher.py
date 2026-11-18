"""Testes de ponta a ponta do launcher portátil (missão do launcher, 2026-11-16).

Usa o ZIP de distribuição como a pessoa usaria: extrai numa pasta de teste, confere pastas
erradas, prepara o jogo (cópia conferida por SHA-256), grava opções, abre o jogo pelo launcher
(auto-teste do port), move a pasta portátil para outro lugar e abre de novo, exporta/importa os
dados e confere que a pasta original do jogo e a pasta de saves da pessoa (Documentos\\My Games)
não mudaram. Tudo acontece em app/_audit/out/launcher/ (ignorada pelo Git).

Uso:  python -I tools/launcher/testar_launcher.py --jogo "<pasta do jogo>" [--sem-jogo]
Saída: app/_audit/out/launcher/resultado.json (+ capturas da interface em ui/).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

APP = Path(__file__).resolve().parents[2]
PORTABLE = APP / "out" / "portable"
PKG_NAME = "Banjo-Tooie-PT-BR-Recompiled"
EXE = "Banjo-Tooie PT-BR Recompiled.exe"
WORK = APP / "_audit" / "out" / "launcher"
FORBIDDEN = {"default.xex", "db360.cmp", "db360.textures.cmp", "x360_strings.dat", "banjo2_360.tbl",
             "banjo2_360.ctl", "xarialuni.ttf", "arcadenormal.ptc", "arcadewide.ptc", "savegame.dat",
             "banjotooie.toml", "pc_settings.toml"}

results = []


def check(name, ok, detail=""):
    results.append({"teste": name, "ok": bool(ok), "detalhe": detail})
    print(("PASS " if ok else "FAIL ") + name + (" - " + detail if detail else ""), flush=True)
    return ok


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def tree_hashes(root):
    root = Path(root)
    return {str(p.relative_to(root)): sha(p) for p in sorted(root.rglob("*")) if p.is_file()}


def teste(root, name, *args, timeout=900, idioma="pt"):
    out = WORK / "json" / (name + ".json")
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    t0 = time.time()
    extra = ["--idioma", idioma] if idioma else []  # sem idioma: o gravado no launcher.ini
    p = subprocess.run([str(root / EXE), "--teste", str(out), *args, *extra], timeout=timeout)
    data = json.loads(out.read_text(encoding="utf-8")) if out.exists() else {}
    data["_codigo_saida"] = p.returncode
    data["_segundos"] = round(time.time() - t0, 1)
    return data


def capture(root, page, png, idioma="pt"):
    png.parent.mkdir(parents=True, exist_ok=True)
    if png.exists():
        png.unlink()
    p = subprocess.run([str(root / EXE), "--captura-ui", str(png), "--pagina", page, "--idioma", idioma], timeout=120)
    return p.returncode == 0 and png.exists() and png.stat().st_size > 10000


# Textos que denunciam português na interface em inglês. "jogo\" e "dados\" são nomes de pastas.
PT_WORDS = re.compile(r"[ãõçáéíóúâêôàÃÕÇÁÉÍÓÚÂÊÔ]|\b(de|da|dos|das|para|não|com|pasta|arquivo|opções|Abrir|Salvar)\b|\bjogo\b(?!\\)")
PT_ALLOW = ("Português (Brasil)", "IDIOMA · LANGUAGE", "Idioma / Language", "(ABNT2)")


def textos_ui(root, name, *args):
    out = WORK / "json" / (name + ".json")
    if out.exists():
        out.unlink()
    p = subprocess.run([str(root / EXE), "--textos-ui", str(out), *args], timeout=120)
    data = json.loads(out.read_text(encoding="utf-8")) if out.exists() else {"idioma": None, "textos": []}
    data["_codigo_saida"] = p.returncode
    data["_restos_pt"] = [t for t in data["textos"] if PT_WORDS.search(t) and not any(a in t for a in PT_ALLOW)]
    return data


def foto_pessoa(out):
    return subprocess.run([sys.executable, "-I", str(APP / "tools" / "m04" / "foto_pessoa.py"),
                           "--out", str(out)], capture_output=True, text=True).returncode == 0


def compara_pessoa(out, before):
    p = subprocess.run([sys.executable, "-I", str(APP / "tools" / "m04" / "foto_pessoa.py"),
                        "--out", str(out), "--compare", str(before)], capture_output=True, text=True)
    return p.returncode == 0, (p.stdout + p.stderr).strip()[-400:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jogo", required=True)
    args = ap.parse_args()
    game = Path(args.jogo)
    WORK.mkdir(parents=True, exist_ok=True)
    zips = sorted(PORTABLE.glob(PKG_NAME + "-*-win64.zip"))
    if not check("pacote: ZIP de distribuição existe", zips, str(zips[-1] if zips else PORTABLE)):
        return finish()
    zf = zips[-1]

    # 1. Conteúdo do pacote: íntegro, sem arquivos do jogo, com o runtime e as licenças.
    with zipfile.ZipFile(zf) as z:
        bad = z.testzip()
        names = [n for n in z.namelist() if not n.endswith("/")]
    check("pacote: ZIP íntegro (CRC de todas as entradas)", bad is None, f"{len(names)} arquivos")
    lower = {Path(n).name.lower() for n in names}
    check("pacote: nenhum arquivo do jogo, save ou opção de desenvolvimento",
          not (lower & FORBIDDEN) and not any(n.lower().startswith("58410955") for n in lower),
          ", ".join(sorted(lower & FORBIDDEN)) or "nenhum")
    need = [f"{PKG_NAME}/{EXE}", f"{PKG_NAME}/bin/banjotooie.exe", f"{PKG_NAME}/bin/rexruntime.dll",
            f"{PKG_NAME}/bin/vcruntime140.dll", f"{PKG_NAME}/bin/vcruntime140_1.dll",
            f"{PKG_NAME}/bin/msvcp140.dll", f"{PKG_NAME}/bin/msvcp140_atomic_wait.dll",
            f"{PKG_NAME}/LEIA-ME.txt", f"{PKG_NAME}/CREDITOS.txt", f"{PKG_NAME}/README-EN.txt", f"{PKG_NAME}/CREDITS-EN.txt",
            f"{PKG_NAME}/LICENCAS/launcher-GPL-3.0.txt", f"{PKG_NAME}/LICENCAS/ReXGlue-SDK-BSD-3-Clause.txt"]
    missing = [n for n in need if n not in names]
    check("pacote: launcher, port, runtime VC++, LEIA-ME/README-EN, créditos PT/EN e licenças", not missing,
          ", ".join(missing) or f"{len(need)} itens")

    # 2. Extrai como a pessoa faria (pasta com espaços no nome).
    base = WORK / "extraido com espaco"
    if base.exists():
        shutil.rmtree(base)
    base.mkdir(parents=True)
    with zipfile.ZipFile(zf) as z:
        z.extractall(base)
    root = base / PKG_NAME
    manifest = (root / "MANIFESTO-SHA256.txt").read_text(encoding="utf-8").splitlines()
    wrong = [l for l in manifest if sha(root / l.split("  ", 1)[1]) != l.split("  ", 1)[0]]
    check("pacote: SHA-256 de cada arquivo extraído confere com o manifesto", not wrong,
          f"{len(manifest)} arquivos" if not wrong else "; ".join(wrong[:3]))

    # Fotos do que não pode mudar: a pasta do jogo da pessoa e os saves em Documentos.
    game_before = tree_hashes(game)
    foto_antes = WORK / "foto_pessoa_antes.txt"
    check("foto (somente leitura) dos saves da pessoa em Documentos", foto_pessoa(foto_antes))

    # 3. Interface antes de preparar.
    check("interface: abre e desenha a página Jogar (antes de preparar)",
          capture(root, "jogar", WORK / "ui" / "1-jogar-antes.png"))

    # 4. Conferência de pastas erradas.
    empty = WORK / "pasta vazia"
    shutil.rmtree(empty, ignore_errors=True)
    empty.mkdir()
    d = teste(root, "a_vazia", "--jogo", str(empty))
    c = d.get("conferencia", {})
    check("conferência: pasta vazia é recusada com a lista do que falta",
          c.get("ok") is False and "default.xex" in " ".join(c.get("faltando_obrigatorios", [])),
          c.get("mensagem", ""))
    d = teste(root, "b_inexistente", "--jogo", str(WORK / "nao existe"))
    c = d.get("conferencia", {})
    check("conferência: pasta inexistente é recusada", c.get("ok") is False, c.get("mensagem", ""))
    # Pasta com os nomes certos mas conteúdo falso: a pré-checagem do jogo aceita a estrutura e a
    # versão é recusada pelo SHA-256 do default.xex.
    fake = WORK / "versao falsa"
    shutil.rmtree(fake, ignore_errors=True)
    (fake / "RAWFiles").mkdir(parents=True)
    (fake / "default.xex").write_bytes(b"XEX2" + b"\0" * 4092)
    for n in ["db360.cmp", "db360.textures.cmp", "X360_strings.dat", "banjo2_360.tbl", "banjo2_360.ctl",
              "xarialuni.ttf", "ArcadeNormal.ptc", "ArcadeWide.ptc"]:
        (fake / "RAWFiles" / n).write_bytes(b"\0" * 64)
    d = teste(root, "c_versao_falsa", "--jogo", str(fake))
    c = d.get("conferencia", {})
    check("conferência: versão incompatível (default.xex diferente) é recusada",
          c.get("ok") is False and c.get("compativel") is False and "outra versão" in c.get("mensagem", ""),
          c.get("mensagem", ""))

    # 5. Pasta certa: conferência + preparação com cópia.
    d = teste(root, "d_preparar", "--jogo", str(game), "--preparar", "--copiar")
    c, prep = d.get("conferencia", {}), d.get("preparo", {})
    check("conferência: pasta do jogo reconhecida, versão identificada", c.get("ok") is True,
          c.get("versao", "") + " - " + c.get("mensagem", ""))
    check("preparação: concluída e marcada como pronta", prep.get("ok") and d.get("preparado"),
          prep.get("erro", ""))
    ini = d.get("launcher_ini") or ""
    check("preparação: caminho do jogo guardado relativo à pasta portátil", 'jogo = "jogo"' in ini, ini.strip().replace("\n", " | "))
    copied = tree_hashes(root / "jogo")
    check("preparação: cópia em jogo\\ idêntica ao original (SHA-256 de todos os arquivos)",
          copied == game_before, f"{len(copied)} de {len(game_before)} arquivos")

    # 6. Opções: grava, relê e confere nos arquivos que o jogo lê.
    opts = ("resolution=1080p;internal_scale=1;display_mode=Janela;fps60=true;lock60=true;"
            "host_vsync=true;swap_post_effect=fxaa;native_2x_msaa=false;anisotropic_override=4;"
            "draw_distance=Longe;volume_master=80;dialog_speed=1;show_fps=true;keybind_a=Space;"
            "keybind_x=G;cam_key_up=I;pad_deadzone=0.18;log_detailed=true")
    d = teste(root, "e_opcoes", "--opcoes", opts)
    o = d.get("opcoes", {})
    check("opções: gravadas e relidas iguais", d.get("resultado") == "PASS" and not o.get("diferencas"),
          "; ".join(o.get("diferencas", [])) or f"{len(o.get('gravadas', {}))} opções")
    pcs, rt = o.get("pc_settings", ""), o.get("runtime_toml") or ""
    check("opções: resolução e escala só em dados\\pc_settings.toml (versão 4)",
          'resolution = "1080p"' in pcs and "resolution_auto = false" in pcs and "internal_scale = 1" in pcs
          and "settings_version = 4" in pcs and "resolution" not in rt and "internal_scale" not in rt)
    check("opções: cvars do runtime em bin\\banjotooie.toml",
          'swap_post_effect = "fxaa"' in rt and "anisotropic_override = 4" in rt and 'keybind_x = "G"' in rt
          and "host_vsync = true" in rt)

    # 7. Abre o jogo pelo launcher (auto-teste do port) e confere pastas e opções aplicadas.
    boot = root / "logs" / "boot.json"
    selftest = root / "logs" / "selftest.json"
    for f in (boot, selftest):
        if f.exists():
            f.unlink()
    d = teste(root, "f_jogo", "--abrir-jogo",
              f'--self-test "{selftest}" --boot_report "{boot}"', "--espera", "200")
    j = d.get("jogo", {})
    check("jogo: aberto pelo launcher e fechado com código 0", j.get("codigo") == 0, json.dumps(j, ensure_ascii=False))
    st = json.loads(selftest.read_text(encoding="utf-8")) if selftest.exists() else {"steps": []}
    passed = sum(1 for s in st["steps"] if s.get("passed"))
    check("jogo: auto-teste do port passou", st["steps"] and passed == len(st["steps"]),
          f"{passed}/{len(st['steps'])}: " + ", ".join(s["step"] for s in st["steps"] if not s.get("passed")))
    info = json.loads(boot.read_text(encoding="utf-8")).get("info", {}) if boot.exists() else {}
    check("jogo: dados, cache e jogo dentro da pasta portátil",
          info.get("user_data_root", "").lower() == str(root / "dados").lower()
          and info.get("cache_root", "").lower() == str(root / "cache").lower()
          and info.get("game_data_root", "").lower().rstrip("\\") == str(root / "jogo").lower(),
          f"dados={info.get('user_data_root')} cache={info.get('cache_root')} jogo={info.get('game_data_root')}")
    check("jogo: resolução e escala do launcher aplicadas", info.get("pc_video", "").startswith("1080p escala 1x"),
          info.get("pc_video", ""))
    logs = sorted((root / "dados" / "logs").glob("sessao-*.txt"))
    text = logs[-1].read_text(encoding="utf-8", errors="replace") if logs else ""
    check("jogo: FXAA e anisotropia do launcher aplicados (registro de sessão)",
          "FXAA fxaa" in text and "anisotropia 4" in text, logs[-1].name if logs else "sem registro")
    pcs_after = (root / "dados" / "pc_settings.toml").read_text(encoding="utf-8")
    check("opções: preservadas depois de o jogo regravar o arquivo",
          'resolution = "1080p"' in pcs_after and "volume_master = 80" in pcs_after and "dialog_speed = 1" in pcs_after
          and 'cam_key_up = "I"' in pcs_after and "pad_deadzone = 0.18" in pcs_after)

    # 8. Portabilidade: move a pasta inteira para outro lugar e abre de novo.
    moved_base = WORK / "movido" / "Outra Pasta"
    shutil.rmtree(WORK / "movido", ignore_errors=True)
    moved_base.parent.mkdir(parents=True)
    shutil.move(str(base), str(moved_base))
    root2 = moved_base / PKG_NAME
    boot2 = root2 / "logs" / "boot2.json"
    d = teste(root2, "g_movido", "--abrir-jogo", f'--smoke_seconds 20 --boot_report "{boot2}"', "--espera", "150")
    check("portabilidade: depois de mover, continua preparado e acha o jogo",
          d.get("preparado") and str(d.get("jogo_guardado", "")).lower() == str(root2 / "jogo").lower(),
          str(d.get("jogo_guardado")))
    info2 = json.loads(boot2.read_text(encoding="utf-8")).get("info", {}) if boot2.exists() else {}
    check("portabilidade: o jogo abre da pasta nova (código 0) com dados e cache nela",
          d.get("jogo", {}).get("codigo") == 0 and info2.get("user_data_root", "").lower() == str(root2 / "dados").lower()
          and info2.get("cache_root", "").lower() == str(root2 / "cache").lower(),
          f"código {d.get('jogo', {}).get('codigo')}, dados={info2.get('user_data_root')}")
    check("portabilidade: opções seguem com a pasta", d.get("opcoes_atuais", {}).get("swap_post_effect") == "fxaa"
          and d.get("opcoes_atuais", {}).get("resolution") == "1080p")
    abs_hits = [str(p.relative_to(root2)) for p in [root2 / "config" / "launcher.ini", root2 / "dados" / "pc_settings.toml",
                                                    root2 / "bin" / "banjotooie.toml"]
                if p.exists() and str(base).lower() in p.read_text(encoding="utf-8", errors="replace").lower()]
    check("portabilidade: nenhum caminho absoluto antigo gravado nas configurações", not abs_hits, ", ".join(abs_hits))

    # 9. Saves: exportar, importar e copiar de Documentos (sem tocar no original).
    zip_out = WORK / "backup-dados.zip"
    if zip_out.exists():
        zip_out.unlink()
    d = teste(root2, "h_exportar", "--exportar", str(zip_out))
    with zipfile.ZipFile(zip_out) as z:
        zn = z.namelist()
    check("saves: exportação gera .zip com opções e registros", "pc_settings.toml" in zn and any(n.startswith("logs/") for n in zn),
          f"{len(zn)} arquivos")
    d = teste(root2, "i_importar_zip", "--importar-zip", str(zip_out))
    check("saves: importação do .zip, com os dados anteriores guardados antes",
          "importados" in str(d.get("importar_zip")) and any((root2 / "backups-launcher").glob("dados-antes-*.zip")),
          str(d.get("importar_zip")))
    d = teste(root2, "j_importar_documentos", "--importar-documentos")
    msg = str(d.get("importar_documentos"))
    check("saves: cópia do save de Documentos (ou aviso claro quando não há)",
          msg.startswith("Save copiado") or "Não há save" in msg, msg)

    # 9b. Shaders guardados no cache da pasta portátil (o jogo os cria de novo e sai).
    d = teste(root2, "l_shaders", "--preparar-shaders")
    msg = str(d.get("shaders"))
    check("shaders: preparação pelo launcher (--prepare-shaders do port) com relatório",
          msg.startswith("Shaders preparados") and (root2 / "logs" / "shaders.json").exists(), msg[:200])

    # 10. Diagnóstico.
    d = teste(root2, "k_diagnostico", "--diagnostico")
    diag = Path(d.get("diagnostico") or "nada")
    dtext = diag.read_text(encoding="utf-8-sig") if diag.exists() else ""
    check("diagnóstico: arquivo gerado com programa, jogo, monitores e opções",
          "SHA-256" in dtext and "[Monitores]" in dtext and "pc_settings" in dtext, diag.name)

    # 11. Originais intactos.
    check("originais: pasta do jogo da pessoa idêntica (SHA-256 de todos os arquivos)",
          tree_hashes(game) == game_before, f"{len(game_before)} arquivos")
    ok, detail = compara_pessoa(WORK / "foto_pessoa_depois.txt", foto_antes)
    check("originais: saves da pessoa em Documentos sem mudança", ok, detail)

    # 12. Sem rede e sem Registro no código do launcher.
    src = "\n".join((APP / "launcher" / n).read_text(encoding="utf-8") for n in ("Core.cs", "Lang.cs", "Launcher.cs"))
    net = [w for w in ("WebClient", "HttpClient", "WebRequest", "System.Net", "Socket", "Microsoft.Win32.Registry",
                       "RegistryKey") if w in src]
    check("launcher: nenhum código de rede nem de Registro do Windows", not net, ", ".join(net))

    # 13. Interface depois de preparar, todas as páginas (pasta movida).
    for i, page in enumerate(["jogar", "graficos", "controles", "jogo", "saves", "registro", "creditos"], 2):
        check(f"interface: página {page}", capture(root2, page, WORK / "ui" / f"{i}-{page}.png"))

    # 14. Inglês: mensagens, textos de todas as páginas, troca pelo seletor (gravada) e capturas.
    d = teste(root2, "m_vazia_en", "--jogo", str(empty), idioma="en")
    msg = d.get("conferencia", {}).get("mensagem", "")
    check("inglês: conferência em inglês (pasta vazia)", d.get("idioma") == "en" and msg.startswith("Missing required files"), msg)
    t = textos_ui(root2, "n_textos_en", "--idioma", "en")
    check("inglês: nenhum texto em português nas 7 páginas", t["idioma"] == "en" and len(t["textos"]) > 250 and not t["_restos_pt"],
          f"{len(t['textos'])} textos; restos: {t['_restos_pt'][:3]}")
    t = textos_ui(root2, "o_textos_pt", "--idioma", "pt")
    check("português: interface continua em português", t["idioma"] == "pt" and "Jogar" in t["textos"] and "SALVAR CONFIGURAÇÕES" in t["textos"],
          f"{len(t['textos'])} textos")
    t = textos_ui(root2, "p_troca_en", "--idioma", "pt", "--trocar-para", "en")
    ini = (root2 / "config" / "launcher.ini").read_text(encoding="utf-8")
    check("inglês: seletor troca a janela para inglês e grava o idioma", t["idioma"] == "en" and "Play" in t["textos"]
          and not t["_restos_pt"] and 'idioma = "en"' in ini, ini.strip().replace("\n", " | "))
    d = teste(root2, "q_idioma_gravado", idioma=None)
    check("inglês: próxima abertura usa o idioma gravado", d.get("idioma") == "en", str(d.get("idioma")))
    for i, page in enumerate(["jogar", "graficos", "controles", "jogo", "saves", "registro", "creditos"], 2):
        check(f"inglês: página {page}", capture(root2, page, WORK / "ui" / f"en-{i}-{page}.png", idioma="en"))
    t = textos_ui(root2, "r_troca_pt", "--trocar-para", "pt")
    ini = (root2 / "config" / "launcher.ini").read_text(encoding="utf-8")
    check("português: seletor volta para português e grava", t["idioma"] == "pt" and "Jogar" in t["textos"] and 'idioma = "pt"' in ini)
    check("idioma: preparação e opções intactas depois das trocas", 'jogo = "jogo"' in ini and 'estado = "pronto"' in ini
          and teste(root2, "s_depois_troca").get("opcoes_atuais", {}).get("swap_post_effect") == "fxaa")

    # 15. Pedido de 2026-11-17: página Jogar sem rolagem em qualquer tamanho, créditos sem a linha
    # da tradução PT-BR e velocidades Lenta/Muito lenta (legendas mais tempo na tela).
    for size in ("800x600", "1220x900", "2400x1300"):
        png = WORK / "ui" / f"resp-jogar-{size}.png"
        if png.exists():
            png.unlink()
        subprocess.run([str(root2 / EXE), "--captura-ui", str(png), "--pagina", "jogar", "--tamanho", size, "--idioma", "pt"], timeout=120)
        rep = json.loads(png.with_suffix(".json").read_text(encoding="utf-8")) if png.with_suffix(".json").exists() else {}
        check(f"responsivo: página Jogar cabe sem rolagem em {size}", rep.get("jogar_cabe") is True and rep.get("botao_jogar_visivel") is True,
              f"escala {rep.get('escala')}, cliente {rep.get('cliente')}, página {rep.get('pagina_jogar')}, fim {rep.get('jogar_fim_conteudo')}")
    png = WORK / "ui" / "resp-graficos-1600x1000.png"
    subprocess.run([str(root2 / EXE), "--captura-ui", str(png), "--pagina", "graficos", "--tamanho", "1600x1000", "--idioma", "pt"], timeout=120)
    rep = json.loads(png.with_suffix(".json").read_text(encoding="utf-8")) if png.with_suffix(".json").exists() else {}
    check("responsivo: opções em duas colunas em janela larga", rep.get("colunas_GraphicsGroups") == 2, str(rep))
    for lang in ("pt", "en"):
        t = textos_ui(root2, f"t_creditos_{lang}", "--idioma", lang)
        check(f"créditos ({lang}): sem a linha da tradução PT-BR",
              not any(x.startswith("Tradução PT-BR:") or x.startswith("PT-BR translation:") for x in t["textos"]))
    t = textos_ui(root2, "u_lentas", "--idioma", "pt")
    check("diálogos: opções 0,5×, 1× e 2× mais lenta no launcher",
          any(x.startswith("0,5× mais lenta (legendas 1,5×") for x in t["textos"])
          and any(x.startswith("1× mais lenta (legendas 2×") for x in t["textos"])
          and any(x.startswith("2× mais lenta (legendas 3×") for x in t["textos"]))
    d = teste(root2, "v_dialogo_lento", "--opcoes", "dialog_speed=6")
    pcs = d.get("opcoes", {}).get("pc_settings", "")
    check("diálogos: 0,5× mais lenta grava dialog_speed = 6 no pc_settings.toml", "dialog_speed = 6" in pcs and not d.get("opcoes", {}).get("diferencas"))
    teste(root2, "w_dialogo_volta", "--opcoes", "dialog_speed=1")
    return finish()


def finish():
    ok = all(r["ok"] for r in results)
    out = {"resultado": "PASS" if ok else "FAIL", "passaram": sum(r["ok"] for r in results),
           "total": len(results), "testes": results, "quando": time.strftime("%Y-%m-%d %H:%M:%S")}
    (WORK / "resultado.json").write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"\n{out['resultado']}: {out['passaram']}/{out['total']}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
