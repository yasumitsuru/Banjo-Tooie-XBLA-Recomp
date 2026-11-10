"""MISSAO 02 - teste negativo do verificador da traducao (prova que ele acusa defeitos).

Cria COPIAS dos arquivos traduzidos numa pasta nova, estraga cada copia de um jeito conhecido
(string cortada, texto sumido, marcador perdido, acento trocado, caractere sem glifo, dialogo
cortado no db360) e roda o traducao.py em cada uma. Cada caso diz quais verificacoes TEM de
falhar; o teste passa so se o verificador acusar exatamente o defeito plantado (e o caso de
controle, sem defeito, passar). Os arquivos originais do jogo so sao lidos.
Rodada 2 (parte jogavel): com --referencias/--capturas, tambem planta defeitos em COPIAS das
capturas do menu de pausa e da mensagem do sistema (item apagado, fim de frase cortado, tela
trocada, fala ausente) e confere que a regressao visual (IoU da tela e de cada linha) acusa.

Uso: python -I teste_negativo.py --jogo <pasta do jogo> --saida <pasta nova>
        --linha-de-base <linha_de_base_traducao.json> [--db <db_mem.bin>]
        [--referencias <pasta> --capturas <pasta do teste>]
Codigo de saida: 0 = todos os casos como esperado, 1 = algum caso nao foi acusado.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import traducao as T  # noqa: E402  (mesma pasta; -I nao poe a pasta do script no sys.path)

TRADUCAO = os.path.join(HERE, "traducao.py")
COMPARAR = os.path.join(HERE, "comparar_capturas.py")
COPIAR = ["ArcadeInfo.xml", "RAWFiles/X360_strings.dat", "RAWFiles/xarialuni.ttf"]
CABECALHO = ["RAWFiles/db360.cmp", "RAWFiles/db360.textures.cmp"]  # so os 16 bytes lidos


def serialize_x360(x, strings):
    """Remonta o X360_strings.dat a partir de strings[idioma][i] (com o 0 final)."""
    n, L = x["n"], x["langs"]
    sizes = [sum(len(s) for s in strings[l]) for l in range(L)]
    out = bytearray(struct.pack("<HH", n, L))
    out += struct.pack("<%dI" % L, *sizes)
    for l in range(L):
        out += struct.pack("<%dI" % n, *[len(s) for s in strings[l]])
    for l in range(L):
        out += b"".join(strings[l])
    return out


def mini_jogo(src, dst, x360_bytes=None):
    for rel in COPIAR + CABECALHO:
        p = os.path.join(dst, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        if rel in CABECALHO:
            with open(os.path.join(src, rel), "rb") as f, open(p, "wb") as g:
                g.write(f.read(16))
        else:
            shutil.copyfile(os.path.join(src, rel), p)
    if x360_bytes is not None:
        with open(os.path.join(dst, "RAWFiles/X360_strings.dat"), "rb") as f:
            original = f.read()
        x = T.parse_x360(original)
        tail = original[x["end_of_blocks"]:]  # bytes depois dos 6 blocos ficam como estao
        with open(os.path.join(dst, "RAWFiles/X360_strings.dat"), "wb") as f:
            f.write(bytes(x360_bytes) + tail)


def roda(game, saida, extra):
    r = subprocess.run([sys.executable, "-I", TRADUCAO, "--jogo", game, "--saida", saida] + extra,
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    with open(saida + ".log", "w", encoding="utf-8", newline="\n") as f:
        f.write(r.stdout + r.stderr)
    rep = json.load(open(os.path.join(saida, "relatorio_traducao.json"), encoding="utf-8"))
    return {c["id"]: c["status"] for c in rep["verificacoes"]}, r.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jogo", required=True)
    ap.add_argument("--saida", required=True)
    ap.add_argument("--linha-de-base", required=True)
    ap.add_argument("--db")
    ap.add_argument("--referencias")
    ap.add_argument("--capturas")
    a = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if os.path.exists(a.saida) and os.listdir(a.saida):
        print("a pasta de saida tem de ser nova ou vazia:", a.saida)
        return 2
    os.makedirs(a.saida, exist_ok=True)

    xdata = open(os.path.join(a.jogo, "RAWFiles/X360_strings.dat"), "rb").read()
    x = T.parse_x360(xdata)
    pt = x["strings"][0]
    kinds = [T.x360_kind(s) for s in pt]
    used = [i for i in range(x["n"]) if kinds[i] == "8bits" and len(pt[i]) > 12]

    def copia():
        return [list(x["strings"][l]) for l in range(x["langs"])]

    casos = []

    # 0. controle: copia sem defeito -> X*, T*, A1, C* e B1 (partes x360/fonte) passam
    casos.append(("controle_sem_defeito", None, [], "nenhuma verificacao falha"))

    # 1. 0 no meio de uma string PT-BR (o jogo le ate o 0: o resto da frase sumiria)
    s = copia()
    i = used[0]
    b = bytearray(s[0][i]); b[len(b) // 2] = 0; s[0][i] = bytes(b)
    casos.append(("x360_string_cortada_por_zero", serialize_x360(x, s), ["X5", "B1"],
                  f"string {i}: byte do meio trocado por 0"))

    # 2. string PT-BR esvaziada (texto perdido) onde FR/DE/ES/IT tem texto
    s = copia()
    i = used[1]
    s[0][i] = b"\x00"
    casos.append(("x360_texto_perdido", serialize_x360(x, s), ["X8", "B1"], f"string {i} vazia"))

    # 3. marcador perdido (%d/%s ou ~): o jogo deixaria de mostrar o numero/valor
    s = copia()
    def marcador_conferido(i):  # o X9 so confere strings com texto em 3+ idiomas latinos
        refs = [tuple(T.markers(x["strings"][l][i])) for l in T.X360_LATIN]
        return (sum(1 for l in T.X360_LATIN if T.x360_kind(x["strings"][l][i]) != "vazia") >= 3
                and tuple(T.markers(pt[i])) in refs)
    alvo = next(i for i in range(x["n"]) if kinds[i] == "8bits" and
                (b"%" in pt[i] or b"~" in pt[i]) and T.markers(pt[i]) and marcador_conferido(i))
    body = pt[alvo][:-1]
    if b"~" in body:
        body = body.replace(b"~", b"", 1)
    else:
        k = body.index(b"%")
        body = body[:k] + body[k + 2:]
    s[0][alvo] = body + b"\x00"
    casos.append(("x360_marcador_perdido", serialize_x360(x, s), ["X9", "B1"],
                  f"string {alvo}: um marcador removido"))

    # 4. tamanho do bloco PT-BR no cabecalho diferente da soma das strings
    bad = bytearray(serialize_x360(x, copia()))
    struct.pack_into("<I", bad, 4, x["sizes"][0] + 1)
    casos.append(("x360_tamanho_inconsistente", bad, ["X2"], "tamanho do bloco 0 + 1 no cabecalho"))

    # 5. acento trocado (mesmo tamanho): so a linha de base percebe -> prova o valor da regressao
    s = copia()
    alvo = next(i for i in range(x["n"]) if kinds[i] == "8bits" and any(c >= 0xC0 for c in pt[i]))
    b = bytearray(s[0][alvo])
    k = next(j for j, c in enumerate(b) if c >= 0xC0)
    troca = {0xC1: 0x41, 0xC3: 0x41, 0xC9: 0x45, 0xCA: 0x45, 0xCD: 0x49, 0xD3: 0x4F, 0xD5: 0x4F,
             0xDA: 0x55, 0xC7: 0x43, 0xC2: 0x41, 0xD4: 0x4F, 0xC0: 0x41}
    b[k] = troca.get(b[k], 0x41)
    s[0][alvo] = bytes(b)
    casos.append(("x360_acento_trocado", serialize_x360(x, s), ["B1"],
                  f"string {alvo}: 0x{pt[alvo][k]:02X} -> 0x{b[k]:02X} (sem acento)"))

    # 6. caractere que a fonte nao tem (o jogo mostraria um quadrado)
    s = copia()
    alvo = next(i for i in range(x["n"]) if kinds[i] == "utf16" and len(pt[i]) > 8)
    # 2o caractere -> U+E000 (uso privado); o 1o fica, pois o 0x00 inicial e o que marca UTF-16
    b = bytearray(s[0][alvo]); b[2:4] = b"\xe0\x00"
    s[0][alvo] = bytes(b)
    casos.append(("x360_caractere_sem_glifo", serialize_x360(x, s), ["T2", "B1"],
                  f"string UTF-16 {alvo}: 2o caractere -> U+E000"))

    def b1_nas_partes(saida_rel, partes):
        # na mini pasta faltam arquivos e o db360 esta cortado: o B1 completo sempre acusaria o
        # inventario. Por isso o B1 e conferido so nas partes de texto/fonte/banco.
        rep = json.load(open(os.path.join(saida_rel, "relatorio_traducao.json"), encoding="utf-8"))
        diffs = rep.get("diferencas_linha_de_base", [])
        return [d for d in diffs if d.startswith(partes)]

    def julga(nome, desc, st, devem_falhar):
        falhas = sorted(k for k, v in st.items() if v == "FAIL")
        if devem_falhar:
            ok = set(devem_falhar) <= set(falhas)  # o defeito plantado tem de ser acusado
        else:
            ok = not falhas  # controle: nada pode falhar
        extras = sorted(set(falhas) - set(devem_falhar))
        resultados.append({"caso": nome, "defeito": desc, "deviam_falhar": sorted(devem_falhar),
                           "falharam": falhas, "falhas_em_cascata": extras, "status": "PASS" if ok else "FAIL"})
        print(f"{'PASS' if ok else 'FAIL':5} {nome}: deviam falhar {sorted(devem_falhar)}, falharam {falhas}")

    resultados = []
    for nome, dados, devem_falhar, desc in casos:
        g = os.path.join(a.saida, nome, "jogo")
        mini_jogo(a.jogo, g, dados)
        rel = os.path.join(a.saida, nome, "relatorio")
        st, rc = roda(g, rel, ["--linha-de-base", a.linha_de_base])
        st["B1"] = "FAIL" if b1_nas_partes(rel, ("x360/", "fonte/")) else "PASS"
        julga(nome, desc, st, devem_falhar)

    # 7. db360: um dialogo PT-BR cortado no meio (0 no lugar de uma letra)
    if a.db:
        dump = bytearray(open(a.db, "rb").read())
        n, base, _e, _o, _m = T.db_table(bytes(dump))
        recs = T.find_text_records(bytes(dump), base)
        # registro com 3+ falas no PT-BR (cortar uma nao some com o registro inteiro)
        r = next(r for r in recs if r["kind"] == "A" and len(r["slots"][0][0]) >= 3
                 and len(r["slots"][0][0][0]) > 10)
        pos = r["pos"] + r["offs"][0]
        blk = bytes(dump[pos:pos + r["offs"][1] - r["offs"][0]])
        k = blk.index(r["slots"][0][0][0]) + 5
        dump[pos + k] = 0
        dpath = os.path.join(a.saida, "db_dialogo_cortado", "db_mem.bin")
        os.makedirs(os.path.dirname(dpath), exist_ok=True)
        open(dpath, "wb").write(dump)
        del dump
        g = os.path.join(a.saida, "db_dialogo_cortado", "jogo")
        mini_jogo(a.jogo, g)
        rel = os.path.join(a.saida, "db_dialogo_cortado", "relatorio")
        st, rc = roda(g, rel, ["--db", dpath, "--linha-de-base", a.linha_de_base])
        st["B1"] = "FAIL" if b1_nas_partes(rel, ("x360/", "fonte/", "db360_memoria/")) else "PASS"
        julga("db_dialogo_cortado", f"registro +0x{r['pos'] - base:X}: 0 no meio da 1a fala", st, ["D3", "B1"])
        os.remove(dpath)  # 50 MB de conteudo do jogo: nao precisa ficar no disco

    # 8. regressao visual: uma tela errada no lugar da certa tem de dar IoU baixo
    if a.referencias and a.capturas:
        pares = [("configuracoes", "3_menus/ajuda_e_opcoes.png", "165,180,700,470", "claro"),
                 ("dialogo_intro_1", "4_dialogo/dialogo_intro_2b.png", "230,130,1030,500", "laranja"),
                 ("titulo", "3_menus/menu_principal.png", "420,460,860,492", "branco")]
        for ref, nova, reg, cor in pares:
            rp = os.path.join(a.referencias, ref + ".png")
            np_ = os.path.join(a.capturas, nova)
            if not (os.path.exists(rp) and os.path.exists(np_)):
                resultados.append({"caso": f"visual_{ref}_trocada", "status": "PENDENTE", "defeito": "faltou imagem"})
                continue
            j = json.loads(subprocess.run([sys.executable, "-I", COMPARAR, rp, np_, reg, cor],
                                          capture_output=True, text=True).stdout)
            ok = j["iou"] < 0.75 or j["pixels_novo"] < 300
            resultados.append({"caso": f"visual_{ref}_trocada", "defeito": f"tela {nova} no lugar de {ref}",
                               "deviam_falhar": ["regressao visual"], "falharam": [f"IoU {j['iou']}, pixels {j['pixels_novo']}"],
                               "status": "PASS" if ok else "FAIL"})
            print(f"{'PASS' if ok else 'FAIL':5} visual_{ref}_trocada: IoU {j['iou']} (tem de ser < 0.75)")

    # 9. rodada 2 (parte jogavel): mesma regra do test_traducao.ps1 -> FAIL se IoU < 0.75, se a pior
    # linha < 0.75 (telas paradas), ou se um dos lados tem < 300 pixels da cor do texto.
    if a.referencias and a.capturas:
        import comparar_capturas as CMP  # noqa: E402  (mesma pasta)
        from PIL import Image  # noqa: E402

        def compara(rp, np_, reg, cor):
            return json.loads(subprocess.run([sys.executable, "-I", COMPARAR, rp, np_, reg, cor],
                                             capture_output=True, text=True).stdout)

        def reprova(j, por_linha):
            linha_ruim = por_linha and (j["iou_linha_min"] is None or j["iou_linha_min"] < 0.75)
            return j["iou"] < 0.75 or linha_ruim or j["pixels_ref"] < 300 or j["pixels_novo"] < 300

        def apaga(src, dst, x0, y0, x1, y1):
            """Copia da captura com um retangulo pintado de preto (texto apagado)."""
            im = Image.open(src).convert("RGB")
            im.paste((0, 0, 0), (x0, y0, x1 + 1, y1 + 1))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            im.save(dst)

        def linhas_da_ref(rp, reg, cor):
            px = CMP.mask(CMP.read_png(rp), [int(v) for v in reg.split(",")], cor)
            return px, CMP.text_lines(px)

        casos9 = []
        cap = lambda rel: os.path.join(a.capturas, rel)  # noqa: E731
        ref = lambda n: os.path.join(a.referencias, n + ".png")  # noqa: E731

        # 9a. um item do menu de pausa some (4a linha, CLASSIFICACOES): a tela inteira muda pouco,
        # a linha dele zera -> so a verificacao por linha acusa
        reg = "290,165,600,545"
        if os.path.exists(ref("pausa")) and os.path.exists(cap("4b_jogavel/pausa_b.png")):
            _px, ls = linhas_da_ref(ref("pausa"), reg, "cinza")
            if len(ls) >= 4:
                y0, y1 = ls[3]
                x0, _y, x1, _y2 = [int(v) for v in reg.split(",")]
                dst = os.path.join(a.saida, "visual_r2", "pausa_sem_item.png")
                apaga(cap("4b_jogavel/pausa_b.png"), dst, x0, y0 - 2, x1, y1 + 2)
                casos9.append(("visual_pausa_item_apagado", "pausa", dst, reg, "cinza", True,
                               f"4a linha do menu de pausa (y {y0}-{y1}) apagada na captura"))

        # 9b. um trecho de uma linha some (2a linha da mensagem do sistema apagada de 60% da largura
        # do texto ate o fim da regiao x<=680: "...CTADO A") -> a linha cai, a tela inteira pouco
        # 9b2. o fim da frase some: na faixa a direita do Bottles (x 820-890, mensagem_xbox_live_fim),
        # a 2a linha apagada a partir de 40% da largura ("...IVE" de XBOX LIVE cortado)
        for nome, reg, frac, arq, desc in [
                ("visual_mensagem_trecho_da_linha_apagado", "190,230,680,305", 0.6,
                 "xbox_live_trecho_apagado.png", "trecho do meio da 2a linha apagado"),
                ("visual_mensagem_fim_de_frase_cortado", "820,230,890,305", 0.4,
                 "xbox_live_fim_cortado.png", "fim da 2a linha (ultimas letras da frase) apagado")]:
            if not (os.path.exists(ref("mensagem_xbox_live")) and os.path.exists(cap("4b_jogavel/xbox_live_b.png"))):
                resultados.append({"caso": nome, "status": "PENDENTE", "defeito": "faltou imagem"})
                continue
            px, ls = linhas_da_ref(ref("mensagem_xbox_live"), reg, "cinza")
            if len(ls) < 2:
                resultados.append({"caso": nome, "status": "FAIL", "defeito": f"referencia sem 2 linhas: {ls}"})
                continue
            y0, y1 = ls[1]
            xs = [x for x, y in px if y0 <= y <= y1]
            corte = min(xs) + int(frac * (max(xs) - min(xs)))
            x1 = int(reg.split(",")[2])
            dst = os.path.join(a.saida, "visual_r2", arq)
            apaga(cap("4b_jogavel/xbox_live_b.png"), dst, corte, y0 - 2, x1, y1 + 2)
            casos9.append((nome, "mensagem_xbox_live", dst, reg, "cinza", True,
                           f"{desc}: x {corte}-{x1}, y {y0}-{y1}"))

        # 9c-9e. tela errada no lugar da certa / fala ausente
        trocas = [("visual_pausa_ajuda_e_opcoes_trocada", "pausa_ajuda_e_opcoes", "4b_jogavel/pausa_b.png",
                   "290,165,560,345", "cinza", True, "menu de pausa no lugar do submenu AJUDA E OPCOES"),
                  ("visual_mensagem_xbox_live_trocada", "mensagem_xbox_live", "4b_jogavel/sair_do_jogo_b.png",
                   "190,230,680,305", "cinza", True, "confirmacao SAIR DO JOGO no lugar da mensagem do sistema"),
                  ("visual_dialogo_bottles_ausente", "dialogo_bottles", "4b_jogavel/pausa_b.png",
                   "280,555,1040,645", "branco", False, "tela sem a fala do Bottles (menu de pausa aberto)")]
        for nome, r_, rel, reg, cor, por_linha, desc in trocas:
            if os.path.exists(ref(r_)) and os.path.exists(cap(rel)):
                casos9.append((nome, r_, cap(rel), reg, cor, por_linha, desc))
            else:
                resultados.append({"caso": nome, "status": "PENDENTE", "defeito": "faltou imagem"})

        # controle: a captura certa, sem defeito, tem de passar (senao o caso negativo nao prova nada)
        controles = [("visual_controle_pausa", "pausa", cap("4b_jogavel/pausa_b.png"), "290,165,600,545", "cinza", True),
                     ("visual_controle_xbox_live", "mensagem_xbox_live", cap("4b_jogavel/xbox_live_b.png"),
                      "190,230,680,305", "cinza", True),
                     ("visual_controle_xbox_live_fim", "mensagem_xbox_live", cap("4b_jogavel/xbox_live_b.png"),
                      "820,230,890,305", "cinza", True)]
        for nome, r_, np_, reg, cor, por_linha in controles:
            if not (os.path.exists(ref(r_)) and os.path.exists(np_)):
                resultados.append({"caso": nome, "status": "PENDENTE", "defeito": "faltou imagem"})
                continue
            j = compara(ref(r_), np_, reg, cor)
            ok = not reprova(j, por_linha)
            resultados.append({"caso": nome, "defeito": "nenhum (captura certa)", "deviam_falhar": [],
                               "falharam": [] if ok else [f"IoU {j['iou']}, pior linha {j['iou_linha_min']}"],
                               "status": "PASS" if ok else "FAIL"})
            print(f"{'PASS' if ok else 'FAIL':5} {nome}: IoU {j['iou']}, pior linha {j['iou_linha_min']} (tem de passar)")

        for nome, r_, np_, reg, cor, por_linha, desc in casos9:
            j = compara(ref(r_), np_, reg, cor)
            ok = reprova(j, por_linha)
            so_linha = ok and not (j["iou"] < 0.75 or j["pixels_ref"] < 300 or j["pixels_novo"] < 300)
            det = f"IoU {j['iou']}, pior linha {j['iou_linha_min']}, pixels ref/novo {j['pixels_ref']}/{j['pixels_novo']}"
            resultados.append({"caso": nome, "defeito": desc, "deviam_falhar": ["regressao visual"],
                               "falharam": [det], "acusado_so_pela_verificacao_por_linha": so_linha,
                               "status": "PASS" if ok else "FAIL"})
            print(f"{'PASS' if ok else 'FAIL':5} {nome}: {det}{' (so a verificacao por linha acusou)' if so_linha else ''}")

    falhou =[r for r in resultados if r["status"] == "FAIL"]
    out = {"versao": 1, "status": "FAIL" if falhou else "PASS", "casos": resultados}
    with open(os.path.join(a.saida, "resultado_negativo.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1, ensure_ascii=False)
    print("STATUS", out["status"])
    return 1 if falhou else 0


if __name__ == "__main__":
    sys.exit(main())
