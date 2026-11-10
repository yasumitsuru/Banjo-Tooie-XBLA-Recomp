"""MISSAO 02 - inspecao automatica da traducao PT-BR (somente leitura).

Confere os arquivos traduzidos fornecidos pela pessoa, sem altera-los:
  * X360_strings.dat  - textos da camada XBLA (menus, ajuda, tela de abertura), 6 idiomas;
  * db360.cmp         - banco de dados do jogo (dialogos), conferido como o jogo o tem na
                        memoria (dump do port, --port_dump_db), pois o arquivo e LZX/XCTD;
  * xarialuni.ttf     - fonte TrueType usada pela camada XBLA (tabelas e cobertura de caracteres);
  * ArcadeInfo.xml    - descricao do painel (pt-BR);
  * db360*.cmp        - cabecalho XCTD (0x0FF512ED) que o leitor do jogo exige.
Opcionalmente cruza com o que o jogo leu de fato (rastro de E/S do runtime) e com a copia do
texto que o jogo guardou na memoria (dump de faixas, --port_dump_ranges).

Cada verificacao vira PASS/FAIL (ou PENDENTE quando faltou a entrada). Os textos completos vao
para arquivos de texto na pasta de saida (que fica fora do Git: conteudo do jogo); o relatorio
JSON tem so contagens, hashes e poucos exemplos curtos.

Uso:
  python -I traducao.py --jogo <pasta do jogo> --saida <pasta nova ou vazia>
         [--db <dump do db360>] [--heap <dump de memoria>]... [--rastro <log trace do runtime>]
         [--linha-de-base <json>] [--gravar-linha-de-base <json>]
Codigo de saida: 0 = tudo PASS (PENDENTE nao reprova), 1 = alguma FAIL, 2 = erro de uso.
"""
import argparse
import collections
import hashlib
import json
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

# Ordem dos idiomas em X360_strings.dat e nos registros de dialogo do db360 (conferida nos dados):
# o espaco 0 e o do ingles, que o jogo usa com user_language=1; a traducao PT-BR ocupa esse espaco.
X360_LANGS = ["PT-BR (espaco do ingles)", "frances", "alemao", "espanhol", "italiano", "japones"]
DB_LANGS = ["PT-BR (espaco do ingles)", "japones", "frances", "alemao", "italiano", "espanhol"]
X360_LATIN = (1, 2, 3, 4)   # idiomas de referencia para comparar estrutura (sem o japones)
DB_LATIN = (2, 3, 4)        # frances, alemao, italiano (o espaco 5 as vezes termina no fim do asset)

TEXT_BYTES = set(range(0x20, 0x7F)) | set(range(0x80, 0x100)) | {0x0A}
# Icones de botao no texto (nao sao letras): 0x7F e 0x80-0x9F, usados igual em todos os idiomas
# (ex.: 0x7F = botao A em "PRESSIONE \x7f PARA JOGAR", X360_strings 1267).
BUTTON_CODES = set(range(0x7F, 0xA0))
TEXT_BYTES = TEXT_BYTES | {0x7F}


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class Report:
    def __init__(self):
        self.checks = []
        self.stats = {}

    def check(self, cid, desc, ok, detail, status=None):
        st = status or ("PASS" if ok else "FAIL")
        self.checks.append({"id": cid, "verificacao": desc, "status": st, "detalhe": detail})
        print(f"{st:8} {cid:4} {desc}: {detail}")

    def pending(self, cid, desc, why):
        self.check(cid, desc, False, why, status="PENDENTE")


# ----------------------------------------------------------------------------- X360_strings.dat
def parse_x360(data):
    n, langs = struct.unpack_from("<HH", data, 0)
    sizes = list(struct.unpack_from("<%dI" % langs, data, 4))
    len_tab = 4 + 4 * langs
    pos = len_tab + 4 * n * langs
    out = {"n": n, "langs": langs, "sizes": sizes, "blocks_start": pos, "blocks": [],
           "lengths": [], "strings": []}
    for l in range(langs):
        lens = list(struct.unpack_from("<%dI" % n, data, len_tab + 4 * n * l))
        block = data[pos:pos + sizes[l]]
        strs, p = [], 0
        for ln in lens:
            strs.append(block[p:p + ln])
            p += ln
        out["blocks"].append((pos, block))
        out["lengths"].append(lens)
        out["strings"].append(strs)
        pos += sizes[l]
    out["end_of_blocks"] = pos
    return out


def x360_kind(raw):
    """Tipo de uma string crua (com o 0 final): vazia, utf16 (UTF-16BE) ou 8 bits.
    As strings UTF-16BE (mensagens das caixas de dialogo do sistema) comecam com 0x00 e o
    comprimento declarado inclui o terminador de 2 bytes 00 00."""
    body = raw[:-1]
    if not body:
        return "vazia"
    if body[:1] == b"\x00":
        return "utf16"
    return "8bits"


def x360_body(raw):
    return raw[:-2] if x360_kind(raw) == "utf16" else raw[:-1]


def x360_decode(raw):
    if x360_kind(raw) == "utf16":
        return x360_body(raw).decode("utf-16-be", errors="replace")
    return x360_body(raw).decode("latin-1")


def markers(text_bytes):
    """Marcadores que precisam sobreviver a traducao: %x, ~ (numero), icones de botao."""
    s = text_bytes.replace(b"\x00", b"")
    fmt = re.findall(rb"%[-+0-9.]*[a-zA-Z]", s)
    return sorted(fmt) + [b"~"] * s.count(b"~") + sorted(bytes([c]) for c in s if c in BUTTON_CODES)


def majority(values):
    c = collections.Counter(values)
    return c.most_common(1)[0] if c else (None, 0)


# ----------------------------------------------------------------------------- TrueType
def ttf_tables(data):
    num = struct.unpack_from(">H", data, 4)[0]
    tabs = {}
    for i in range(num):
        tag, csum, off, ln = struct.unpack_from(">4sIII", data, 12 + 16 * i)
        tabs[tag.decode("latin-1")] = (csum, off, ln)
    return tabs


def ttf_checksum(b):
    b = b + b"\x00" * ((4 - len(b) % 4) % 4)
    return sum(struct.unpack(">%dI" % (len(b) // 4), b)) & 0xFFFFFFFF


def ttf_cmap(data, tabs):
    _, off, _ = tabs["cmap"]
    nsub = struct.unpack_from(">H", data, off + 2)[0]
    cps = set()
    used = []
    for i in range(nsub):
        plat, enc, sub = struct.unpack_from(">HHI", data, off + 4 + 8 * i)
        so = off + sub
        fmt = struct.unpack_from(">H", data, so)[0]
        if fmt == 4 and (plat, enc) in ((3, 1), (0, 3), (0, 4)):
            segx2 = struct.unpack_from(">H", data, so + 6)[0]
            seg = segx2 // 2
            ends = struct.unpack_from(">%dH" % seg, data, so + 14)
            starts = struct.unpack_from(">%dH" % seg, data, so + 16 + segx2)
            deltas = struct.unpack_from(">%dh" % seg, data, so + 16 + 2 * segx2)
            ro_pos = so + 16 + 3 * segx2
            ros = struct.unpack_from(">%dH" % seg, data, ro_pos)
            for k in range(seg):
                for c in range(starts[k], ends[k] + 1):
                    if c == 0xFFFF:
                        continue
                    if ros[k] == 0:
                        g = (c + deltas[k]) & 0xFFFF
                    else:
                        gp = ro_pos + 2 * k + ros[k] + 2 * (c - starts[k])
                        g = struct.unpack_from(">H", data, gp)[0]
                        g = (g + deltas[k]) & 0xFFFF if g else 0
                    if g:
                        cps.add(c)
            used.append(f"{plat}/{enc}/fmt4")
        elif fmt == 12:
            ngroups = struct.unpack_from(">I", data, so + 12)[0]
            for k in range(ngroups):
                a, b, g = struct.unpack_from(">III", data, so + 16 + 12 * k)
                for c in range(a, b + 1):
                    if g + (c - a):
                        cps.add(c)
            used.append(f"{plat}/{enc}/fmt12")
    return cps, used


# ----------------------------------------------------------------------------- db360 (memoria)
def db_table(dump):
    n = struct.unpack_from(">I", dump, 0)[0]
    base = 8 + 4 * n
    ents = struct.unpack_from(">%dI" % n, dump, 8)
    offs = [e >> 6 for e in ents]
    main = n
    for i in range(1, n):
        if offs[i] < offs[i - 1]:
            main = i
            break
    return n, base, ents, offs, main


def scan_strings(b):
    """Strings com prefixo de tamanho (1 byte, conta o 0 final) e terminadas em 0.
    Devolve (strings, esqueleto) - o esqueleto e o bloco com cada string trocada por FF 53,
    para comparar a estrutura (comandos, ordem, quantidade) entre idiomas."""
    out, sk, p = [], bytearray(), 0
    ln = len(b)

    def valid(q):
        L = b[q]
        return (L >= 2 and q + L < ln and b[q + L] == 0 and
                all(c in TEXT_BYTES for c in b[q + 1:q + L]))

    while p < ln:
        if valid(p):
            # ambiguidade: comando seguido do tamanho real (b[p+1] == L-1, mesmo 0 final)
            q = p
            while q + 1 < ln and b[q + 1] == b[q] - 1 and valid(q + 1):
                sk.append(b[q])
                q += 1
            L = b[q]
            out.append(b[q + 1:q + L])
            sk += b"\xffS"
            p = q + L + 1
        else:
            sk.append(b[p])
            p += 1
    return out, bytes(sk)


def fit_template(sk, b):
    """Le o bloco b seguindo o esqueleto sk de outro idioma: os bytes de comando tem de ser
    iguais e, em cada marcador FF 53, tem de haver uma string valida (tamanho + texto + 0).
    Resolve a ambiguidade do scan_strings (uma letra pode valer como tamanho). Devolve as
    strings, ou None se o bloco nao tem exatamente essa estrutura."""
    out, p, k, ln = [], 0, 0, len(b)
    while k < len(sk):
        if sk[k] == 0xFF and k + 1 < len(sk) and sk[k + 1] == 0x53:
            if p >= ln:
                return None
            L = b[p]  # L == 1 e uma string vazia valida (so o 0 final)
            if L < 1 or p + L >= ln or b[p + L] != 0 or not all(c in TEXT_BYTES for c in b[p + 1:p + L]):
                return None
            out.append(b[p + 1:p + L])
            p += L + 1
            k += 2
        else:
            if p >= ln or b[p] != sk[k]:
                return None
            p += 1
            k += 1
    return out if p == ln else None


def same_structure(rec, slot, refs):
    """Estrutura do espaco `slot` igual a de algum idioma de refs (nos dois sentidos).
    Devolve (strings do espaco lidas pela estrutura comum, idioma de referencia) ou (None, None)."""
    raw = rec["raw"][slot]
    for j in refs:
        got = fit_template(rec["slots"][j][1], raw)
        if got is not None:
            return got, j
    own = rec["slots"][slot][1]
    for j in refs:
        if fit_template(own, rec["raw"][j]) is not None:
            return rec["slots"][slot][0], j
    return None, None


def find_text_records(dump, base):
    """Registros de texto multi-idioma (6 idiomas) em qualquer ponto do banco:
    tipo A = u8 6 + 6 x u16 LE (primeiro deslocamento 13); tipo B = u16 LE 6 + 6 x u16 LE (14)."""
    recs = []
    i = base
    end = len(dump) - 14
    while i < end:
        if dump[i] == 6:
            for kind, first, start in (("A", 13, 1), ("B", 14, 2)):
                if kind == "B" and dump[i + 1] != 0:
                    continue
                o = struct.unpack_from("<6H", dump, i + start)
                if o[0] == first and all(o[k] < o[k + 1] for k in range(5)) and o[5] < 8000:
                    blocks = [dump[i + o[k]:i + o[k + 1]] for k in range(5)]
                    sc = [scan_strings(b) for b in blocks]
                    # texto de verdade: cada bloco tem ao menos uma string de 3+ caracteres de
                    # texto (descarta audio/imagem que por acaso parece um cabecalho de registro)
                    def covered(k):
                        ok = set(range(0x20, 0xA0)) | set(range(0xC0, 0x100)) | {0x0A, 0xAA, 0xBA}
                        return any(len(t) >= 3 and all(c in ok for c in t) for t in sc[k][0])
                    if all(sc[k][0] and covered(k) for k in (0,) + DB_LATIN):
                        recs.append({"pos": i, "kind": kind, "offs": o, "slots": sc, "raw": blocks})
                        i += o[5]
                        break
        i += 1
    return recs


EN_WORDS = re.compile(rb"\b(THE|YOU|YOUR|WHAT|THIS|THAT|WITH|ARE|HAVE|IT'S|YOU'RE|DON'T|GAME|WANT)\b")
PT_WORDS = re.compile(rb"\b(VOC\xca|N\xc3O|PARA|COM|ESSE|ESTA|UMA|QUE|PRESSIONE|JOGO|SEU|SUA|DE|DO|DA|EU)\b")


# ----------------------------------------------------------------------------- rastro de E/S
def parse_trace(log_path):
    # Pedido e resultado de cada chamada saem na mesma thread ([tNNNN]); varias threads leem ao
    # mesmo tempo, entao o pedido pendente e guardado por thread (senao uma leitura se perde).
    re_tid = re.compile(r"\] \[t(\d+)\] \[Nt")
    re_open = re.compile(r"\[NtCreateFile\] path=(\S+) ")
    re_open_res = re.compile(r"\[NtCreateFile\] -> (0x[0-9a-f]+) handle=(0x[0-9a-f]+)")
    re_fail = re.compile(r"\[NtCreateFile\] FAILED: path='([^']+)' -> (0x[0-9a-f]+)")
    re_read = re.compile(r"\[NtReadFile\] handle=(0x[0-9a-f]+) .* len=(0x[0-9a-f]+)")
    re_read_res = re.compile(r"\[NtReadFile\] -> (0x[0-9a-f]+) .*iosb_info=(\d+)")
    files, handles, fails = {}, {}, {}
    pending_path, pending_read = {}, {}
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if "[Nt" not in line:
                continue
            t = re_tid.search(line)
            tid = t.group(1) if t else None
            m = re_open.search(line)
            if m:
                pending_path[tid] = m.group(1)
                continue
            m = re_open_res.search(line)
            if m and pending_path.get(tid):
                key = pending_path.pop(tid).split(":", 1)[-1].lstrip("\\").lower()
                e = files.setdefault(key, {"aberturas": 0, "leituras": [], "bytes": 0})
                e["aberturas"] += 1
                handles[m.group(2)] = key
                continue
            m = re_fail.search(line)
            if m:
                fails[m.group(1)] = m.group(2)
                pending_path.pop(tid, None)
                continue
            m = re_read.search(line)
            if m:
                pending_read[tid] = (m.group(1), int(m.group(2), 16))
                continue
            m = re_read_res.search(line)
            if m and tid in pending_read:
                key = handles.get(pending_read.pop(tid)[0])
                if key:
                    files[key]["leituras"].append(int(m.group(2)))
                    files[key]["bytes"] += int(m.group(2))
    return files, fails


# ----------------------------------------------------------------------------- principal
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--jogo", required=True)
    ap.add_argument("--saida", required=True)
    ap.add_argument("--db")
    ap.add_argument("--heap", action="append", default=[])
    ap.add_argument("--rastro")
    ap.add_argument("--linha-de-base")
    ap.add_argument("--gravar-linha-de-base")
    a = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    game = a.jogo
    raw_dir = os.path.join(game, "RAWFiles")
    os.makedirs(a.saida, exist_ok=True)
    rep = Report()
    base_stats = {}  # o que entra na linha de base (sem texto do jogo)

    # --- inventario e hashes -------------------------------------------------
    inventory = {}
    for dp, _dn, fn in os.walk(game):
        for name in fn:
            p = os.path.join(dp, name)
            rel = os.path.relpath(p, game).replace("\\", "/")
            inventory[rel] = {"bytes": os.path.getsize(p), "sha256": file_sha256(p)}
    base_stats["arquivos"] = inventory
    with open(os.path.join(a.saida, "hashes.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(inventory, f, indent=1, sort_keys=True)
    rep.check("F1", "inventario da pasta do jogo", len(inventory) > 0,
              f"{len(inventory)} arquivos lidos (sha256 de cada um, em hashes.json)")

    # --- X360_strings.dat ------------------------------------------------------
    xpath = os.path.join(raw_dir, "X360_strings.dat")
    xdata = open(xpath, "rb").read()
    x = parse_x360(xdata)
    n, L = x["n"], x["langs"]
    rep.check("X1", "X360_strings: cabecalho", 0 < n < 20000 and L == 6,
              f"{n} strings x {L} idiomas; blocos {x['sizes']} bytes")
    sums_ok = all(sum(x["lengths"][l]) == x["sizes"][l] for l in range(L))
    rep.check("X2", "X360_strings: soma dos comprimentos = tamanho de cada bloco", sums_ok,
              ", ".join(f"{X360_LANGS[l]} {sum(x['lengths'][l])}/{x['sizes'][l]}" for l in range(L)))
    rep.check("X3", "X360_strings: blocos dentro do arquivo", x["end_of_blocks"] <= len(xdata),
              f"fim dos blocos 0x{x['end_of_blocks']:X} de 0x{len(xdata):X}; "
              f"{len(xdata) - x['end_of_blocks']} bytes depois (nao fazem parte dos 6 blocos)")
    bad_term = [(l, i) for l in range(L) for i, s in enumerate(x["strings"][l]) if not s or s[-1] != 0]
    rep.check("X4", "X360_strings: toda string termina em 0 no comprimento declarado", not bad_term,
              f"{len(bad_term)} sem terminador")
    pt = x["strings"][0]
    kinds = [x360_kind(s) for s in pt]
    inner = [i for i, s in enumerate(pt) if kinds[i] == "8bits" and b"\x00" in s[:-1]]
    rep.check("X5", "X360_strings PT-BR: strings de 8 bits sem 0 no meio (nada truncado)", not inner,
              f"{kinds.count('8bits')} strings de 8 bits; {len(inner)} com 0 interno")
    u16_bad = []
    for i, s in enumerate(pt):
        if kinds[i] != "utf16":
            continue
        body = x360_body(s)
        try:
            txt = body.decode("utf-16-be")
        except UnicodeDecodeError:
            txt = None
        if len(s) % 2 or s[-2:] != b"\x00\x00" or txt is None or "\x00" in txt:
            u16_bad.append(i)
    rep.check("X6", "X360_strings PT-BR: strings UTF-16BE bem formadas (pares completos + terminador 00 00)", not u16_bad,
              f"{kinds.count('utf16')} strings UTF-16BE (mensagens do sistema); {len(u16_bad)} com defeito"
              + (f" {u16_bad[:8]}" if u16_bad else ""))
    kind_mism = []
    for i in range(n):
        ref, cnt = majority([x360_kind(x["strings"][l][i]) for l in X360_LATIN])
        if cnt >= 3 and kinds[i] != ref and not (kinds[i] == "vazia" and ref == "vazia"):
            kind_mism.append(i)
    rep.check("X7", "X360_strings PT-BR: mesma codificacao por string que FR/DE/ES/IT", not kind_mism,
              f"{len(kind_mism)} strings com codificacao diferente da maioria"
              + (f" {kind_mism[:8]}" if kind_mism else ""))
    missing = [i for i in range(n) if kinds[i] == "vazia" and
               sum(1 for l in X360_LATIN if x360_kind(x["strings"][l][i]) != "vazia") >= 3]
    used_ids = [i for i in range(n) if sum(1 for l in X360_LATIN if x360_kind(x["strings"][l][i]) != "vazia") >= 3]
    rep.check("X8", "X360_strings PT-BR: sem string vazia onde FR/DE/ES/IT tem texto", not missing,
              f"{len(used_ids) - len(missing)}/{len(used_ids)} strings usadas pelos idiomas latinos tem "
              f"PT-BR; {kinds.count('vazia')} vazias tambem vazias nos outros (so japones)")
    # A traducao foi feita a partir do ingles, que nao esta nos arquivos; por isso um marcador
    # so e suspeito quando o PT-BR difere de TODOS os idiomas de referencia.
    mk_mism, mk_minor = [], []
    for i in used_ids:
        refs = [tuple(markers(x["strings"][l][i])) for l in X360_LATIN]
        mine = tuple(markers(pt[i]))
        if mine not in refs:
            mk_mism.append(i)
        elif mine != majority(refs)[0]:
            mk_minor.append(i)
    rep.check("X9", "X360_strings PT-BR: marcadores (%d/%s, ~, icones de botao) iguais aos de algum idioma",
              not mk_mism, f"{len(mk_mism)} strings diferentes de todos" + (f" {mk_mism[:10]}" if mk_mism else "")
              + f"; {len(mk_minor)} iguais a uma minoria (ex.: italiano) {mk_minor[:6]}")
    pt_text = [x360_decode(s) for s in pt]
    hist = collections.Counter(c for t in pt_text for c in t if ord(c) > 0x7E or ord(c) < 0x20)
    with open(os.path.join(a.saida, "textos_x360_ptbr.txt"), "w", encoding="utf-8", newline="\n") as f:
        for i, t in enumerate(pt_text):
            f.write(f"{i:04d}\t{kinds[i]}\t{t!r}\n")
    base_stats["x360"] = {
        "sha256": sha256(xdata), "strings_por_idioma": n, "idiomas": L, "tamanhos_blocos": x["sizes"],
        "sha256_blocos": [sha256(b) for _, b in x["blocks"]],
        "ptbr": {"8bits": kinds.count("8bits"), "utf16": kinds.count("utf16"), "vazias": kinds.count("vazia"),
                 "caracteres": sum(len(t) for t in pt_text),
                 "sha256_textos": sha256("\n".join(pt_text).encode("utf-8")),
                 "nao_ascii": {f"U+{ord(k):04X}": v for k, v in sorted(hist.items())}},
    }

    # --- fonte TrueType --------------------------------------------------------
    fpath = os.path.join(raw_dir, "xarialuni.ttf")
    fdata = open(fpath, "rb").read()
    tabs = ttf_tables(fdata)
    bad_ck = [t for t, (ck, off, ln) in tabs.items() if t != "head" and ttf_checksum(fdata[off:off + ln]) != ck]
    head_off = tabs["head"][1]
    head = bytearray(fdata[head_off:head_off + tabs["head"][2]])
    head[8:12] = b"\x00\x00\x00\x00"
    head_ok = ttf_checksum(bytes(head)) == tabs["head"][0] and \
        struct.unpack_from(">I", fdata, head_off + 12)[0] == 0x5F0F3CF5
    whole = (0xB1B0AFBA - ttf_checksum(fdata[:head_off + 8] + b"\x00\x00\x00\x00" + fdata[head_off + 12:])) & 0xFFFFFFFF
    adj = struct.unpack_from(">I", fdata, head_off + 8)[0]
    rep.check("T1", "xarialuni.ttf: somas de verificacao das tabelas e do arquivo",
              not bad_ck and head_ok and whole == adj,
              f"{len(tabs)} tabelas; com defeito: {bad_ck or 'nenhuma'}; head ok={head_ok}; "
              f"ajuste do arquivo {'confere' if whole == adj else 'NAO confere'}")
    cps, used = ttf_cmap(fdata, tabs)
    need = set()
    for t in pt_text:
        for c in t:
            o = ord(c)
            if o in (0x0A,) or 0x7F <= o < 0xA0:  # quebra de linha e icones de botao
                continue
            need.add(o)
    lacking = sorted(need - cps)
    rep.check("T2", "xarialuni.ttf cobre todos os caracteres do PT-BR da camada XBLA", not lacking,
              f"{len(need)} caracteres distintos usados; cmap {used}; faltando: "
              + (", ".join(f"U+{c:04X} {chr(c)!r}" for c in lacking) if lacking else "nenhum"))
    btn = sorted({ord(c) for t in pt_text for c in t if 0x7F <= ord(c) < 0xA0})
    base_stats["fonte"] = {"sha256": sha256(fdata), "tabelas": len(tabs), "codepoints": len(cps)}

    # --- ArcadeInfo.xml ---------------------------------------------------------
    apath = os.path.join(game, "ArcadeInfo.xml")
    if os.path.exists(apath):
        araw = open(apath, "rb").read()
        enc = "utf-16" if araw[:2] in (b"\xfe\xff", b"\xff\xfe") else "utf-8"
        try:
            atext = araw.decode(enc)
            root = ET.fromstring(atext.lstrip("﻿").encode("utf-8"))
            locs = sorted({el.attrib.get(k) for el in root.iter() for k in el.attrib
                           if k.lower() in ("locale", "lang", "language") and el.attrib.get(k)})
            pt_loc = [l for l in locs if l and l.lower().startswith("pt")]
            rep.check("A1", "ArcadeInfo.xml: XML valido com idioma pt", bool(pt_loc),
                      f"codificacao {enc}; idiomas declarados: {locs or 'nenhum atributo de idioma'}")
        except (UnicodeDecodeError, ET.ParseError) as e:
            rep.check("A1", "ArcadeInfo.xml: XML valido com idioma pt", False, f"erro: {e}")

    # --- cabecalhos XCTD ---------------------------------------------------------
    for cid, name in (("C1", "db360.cmp"), ("C2", "db360.textures.cmp")):
        with open(os.path.join(raw_dir, name), "rb") as f:
            h = f.read(16)
        magic, ver, crc, flags = struct.unpack(">IIII", h)
        win = 0x8000 << ((flags >> 4) & 3)
        rep.check(cid, f"{name}: cabecalho XCTD que o leitor do jogo exige (0x825B9218)",
                  magic == 0x0FF512ED and ver == 0x01000000,
                  f"magia 0x{magic:08X}, versao 0x{ver:08X}, campo+8 0x{crc:08X}, "
                  f"campo+12 0x{flags:08X} -> janela LZX {win // 1024} KiB")

    # --- db360 descompactado (memoria do jogo) ---------------------------------
    if a.db:
        dump = open(a.db, "rb").read()
        nent, base, ents, offs, main = db_table(dump)
        last = max(offs[:main])
        alias_ok = all(o <= last for o in offs[main:])
        rep.check("D1", "db360 na memoria: tabela de assets coerente",
                  base == 8 + 4 * nent and main > 0 and alias_ok and base + last <= len(dump),
                  f"{nent} entradas; {main} em ordem crescente + {nent - main} apelidos dentro do banco; "
                  f"dados a partir de +0x{base:X}; ultimo asset em +0x{last:X}; dump {len(dump)} bytes")
        recs = find_text_records(dump, base)
        kinds_db = collections.Counter(r["kind"] for r in recs)
        sk_eq = collections.Counter()
        sk_bad = []
        mk_bad = []
        cnt_pt = 0
        en_like = []
        pt_lines = []
        empty_pt = []  # string vazia no PT-BR onde os idiomas de referencia tem texto
        for r in recs:
            # Estrutura (comandos, ordem, quantidade de strings) do PT-BR lida com o molde de
            # FR/DE/IT; se encaixar, as strings PT-BR saem desse encaixe (sem ambiguidade).
            got, ref_j = same_structure(r, 0, DB_LATIN)
            if got is None:
                sk_bad.append(r)
                s0 = r["slots"][0][0]
            else:
                s0 = got
                k0 = r["slots"][0][1] if fit_template(r["slots"][0][1], r["raw"][0]) is not None else None
                sk_eq[sum(1 for j in DB_LATIN if fit_template(r["slots"][j][1], r["raw"][0]) is not None
                          or (k0 is not None and fit_template(k0, r["raw"][j]) is not None))] += 1
                # marcadores por string, comparados com os idiomas de mesma estrutura
                for si, s in enumerate(s0):
                    cand = []
                    for j in DB_LATIN:
                        sj = fit_template(r["slots"][ref_j][1], r["raw"][j])
                        if sj is not None and si < len(sj):
                            cand.append(tuple(markers(sj[si])))
                    if cand and tuple(markers(s)) not in cand:
                        mk_bad.append((r["pos"] - base, si))
            r["pt"] = s0
            cnt_pt += len(s0)
            if got is not None:
                for si, t0 in enumerate(s0):
                    refs_si = []
                    for j in DB_LATIN:
                        sj = fit_template(r["slots"][ref_j][1], r["raw"][j])
                        if sj is not None and si < len(sj):
                            refs_si.append(sj[si])
                    if not t0 and refs_si and all(rr for rr in refs_si):
                        empty_pt.append((r["pos"] - base, si, refs_si[0]))
            joined = b" ".join(s0)
            if len(EN_WORDS.findall(joined)) > len(PT_WORDS.findall(joined)) + 1:
                en_like.append(r["pos"] - base)
            for si, s in enumerate(s0):
                pt_lines.append(f"+0x{r['pos'] - base:07X}\t{r['kind']}\t{si}\t{s.decode('latin-1')!r}")
        rep.check("D2", "db360: registros de dialogo com 6 idiomas encontrados e PT-BR no espaco 0",
                  len(recs) > 0 and cnt_pt > 0,
                  f"{len(recs)} registros (tipo A {kinds_db['A']}, tipo B {kinds_db['B']}); "
                  f"{cnt_pt} strings PT-BR")
        total = len(recs)
        rep.check("D3", "db360 PT-BR: estrutura de cada dialogo igual a de FR/DE/IT (nada cortado ou a mais)",
                  not sk_bad,
                  f"{total - len(sk_bad)}/{total} registros com a mesma estrutura de pelo menos um idioma "
                  f"de referencia ({sk_eq[3]} iguais aos tres); divergentes: "
                  + (", ".join(f"+0x{r['pos'] - base:X}" for r in sk_bad[:8]) or "nenhum"))
        rep.check("D4", "db360 PT-BR: marcadores (~, icones de botao) iguais aos de algum idioma de mesma estrutura", not mk_bad,
                  f"{len(mk_bad)} strings divergentes" + (f" {mk_bad[:6]}" if mk_bad else ""))
        only_space = all(ref.strip() == b"" for _, _, ref in empty_pt)
        rep.check("D6", "db360 PT-BR: sem linha vazia onde FR/DE/IT tem texto (espacos nao contam)",
                  only_space,
                  f"{len(empty_pt)} linhas vazias no PT-BR; nos outros idiomas elas sao "
                  + (", ".join(f"+0x{p:X}#{si}={ref!r}" for p, si, ref in empty_pt[:6]) or "-"))
        rep.check("D5", "db360: espaco PT-BR sem dialogo em ingles (heuristica de palavras)", not en_like,
                  f"{len(en_like)} registros com mais palavras inglesas que portuguesas"
                  + (f" {[hex(p) for p in en_like[:6]]}" if en_like else ""))
        dhist = collections.Counter(b for r in recs for s in r["pt"] for b in s if b >= 0x80)
        with open(os.path.join(a.saida, "textos_db360_ptbr.txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(pt_lines) + "\n")
        base_stats["db360_memoria"] = {
            "entradas": nent, "ordenadas": main, "base_dados": base, "ultimo_asset": last,
            "sha256_tabela_e_dados": sha256(dump[:base + last]),
            "registros": len(recs), "registros_A": kinds_db["A"], "registros_B": kinds_db["B"],
            "strings_ptbr": cnt_pt,
            "sha256_textos_ptbr": sha256("\n".join(pt_lines).encode("utf-8")),
            "bytes_altos_ptbr": {f"0x{k:02X}": v for k, v in sorted(dhist.items())},
        }
        base_stats["db360_memoria"]["estrutura_igual"] = total - len(sk_bad)
    else:
        rep.pending("D1", "db360 na memoria", "sem --db (dump do port com --port_dump_db)")

    # --- texto do X360 na memoria do jogo --------------------------------------
    if a.heap:
        block0 = x["blocks"][0][1]
        found = []
        for hp in a.heap:
            hd = open(hp, "rb").read()
            pos = hd.find(block0)
            found.append((os.path.basename(hp), pos))
        hit = [f for f in found if f[1] >= 0]
        # conferencia string a string (o jogo pode copiar as strings separadas)
        per_string = None
        if not hit:
            hd_all = b"".join(open(hp, "rb").read() for hp in a.heap)
            per_string = sum(1 for s in pt if s[:-1] and s in hd_all)
        rep.check("M1", "bloco PT-BR do X360_strings na memoria do jogo identico ao arquivo", bool(hit),
                  (f"bloco de {len(block0)} bytes achado em {hit}" if hit else
                   f"bloco inteiro nao achado; {per_string}/{len([s for s in pt if s[:-1]])} strings achadas soltas"))
    else:
        rep.pending("M1", "bloco PT-BR na memoria do jogo", "sem --heap (dump do port com --port_dump_ranges)")

    # --- rastro: o que o jogo leu -------------------------------------------------
    if a.rastro:
        files, fails = parse_trace(a.rastro)
        xs = files.get("rawfiles\\x360_strings.dat")
        size0 = x["sizes"][0]
        # o leitor do jogo pede o bloco do seu idioma em setores inteiros + o resto: 0xB400 + 0x200
        sector_part = size0 // 512 * 512
        reads_lang0 = bool(xs) and sector_part in xs["leituras"]
        other = [l for l in range(1, L) if xs and (x["sizes"][l] // 512 * 512) in xs["leituras"]]
        rep.check("R1", "o jogo leu o bloco do espaco 0 (PT-BR) do X360_strings.dat",
                  reads_lang0 and not other,
                  f"leituras {xs['leituras'] if xs else 'nenhuma'}; bloco PT-BR {size0} B = {sector_part} + "
                  f"{size0 - sector_part}; outros idiomas lidos: {other or 'nenhum'}")
        trans = ["rawfiles\\x360_strings.dat", "rawfiles\\db360.cmp", "rawfiles\\db360.textures.cmp",
                 "rawfiles\\xarialuni.ttf"]
        opened = {k: files.get(k, {}).get("bytes", 0) for k in trans}
        rep.check("R2", "o jogo abriu e leu os arquivos traduzidos", all(v > 0 for v in opened.values()),
                  "; ".join(f"{k.split(chr(92))[-1]}: {v} bytes lidos" for k, v in opened.items()))
        unexpected = {k: v for k, v in fails.items() if "db360_texfilelist" not in k.lower()}
        rep.check("R3", "nenhuma abertura de arquivo falhou (fora a lista opcional de texturas)",
                  not unexpected, f"falhas: {fails or 'nenhuma'}")
        for cid, name in (("R4", "db360.cmp"), ("R5", "db360.textures.cmp")):
            got = files.get("rawfiles\\" + name.lower(), {})
            size = inventory["RAWFiles/" + name]["bytes"]
            rep.check(cid, f"{name} lido por inteiro (nada do fim do arquivo ficou sem ler)",
                      got.get("bytes", 0) >= size,
                      f"{got.get('bytes', 0)} de {size} bytes em {len(got.get('leituras', []))} leituras")
    else:
        rep.pending("R1", "o que o jogo leu", "sem --rastro (log do runtime com --log_level=trace)")

    # --- linha de base (regressao) ------------------------------------------------
    if a.gravar_linha_de_base:
        with open(a.gravar_linha_de_base, "w", encoding="utf-8", newline="\n") as f:
            json.dump(base_stats, f, indent=1, ensure_ascii=False, sort_keys=True)
        print("linha de base gravada em", a.gravar_linha_de_base)
    if a.linha_de_base:
        ref = json.load(open(a.linha_de_base, encoding="utf-8"))
        diffs = []

        def cmp(path, r, c):
            if isinstance(r, dict) and isinstance(c, dict):
                for k in sorted(set(r) | set(c)):
                    if k not in c:
                        diffs.append(f"{path}/{k}: ausente agora")
                    elif k not in r:
                        diffs.append(f"{path}/{k}: novo")
                    else:
                        cmp(f"{path}/{k}", r[k], c[k])
            elif r != c:
                diffs.append(f"{path}: era {str(r)[:60]} agora {str(c)[:60]}")

        skipped = []
        for part in ref:
            if part in base_stats:
                cmp(part, ref[part], base_stats[part])
            else:
                skipped.append(part)  # ex.: db360_memoria sem --db; conferido no passo com o dump
        rep.stats["diferencas_linha_de_base"] = diffs  # lista completa (o detalhe mostra 8)
        rep.check("B1", "regressao: tudo igual a linha de base (hashes, contagens, textos)", not diffs,
                  f"{len(diffs)} diferencas" + (": " + "; ".join(diffs[:8]) if diffs else "")
                  + f"; partes conferidas: {sorted(p for p in ref if p in base_stats)}"
                  + (f"; sem entrada nesta execucao: {skipped}" if skipped else ""))

    fails_n = sum(1 for c in rep.checks if c["status"] == "FAIL")
    result = {"versao": 1, "status": "PASS" if fails_n == 0 else "FAIL", "verificacoes": rep.checks,
              "estatisticas": {k: v for k, v in base_stats.items() if k != "arquivos"},
              "icones_de_botao_ptbr_x360": [f"0x{c:02X}" for c in btn]}
    if "diferencas_linha_de_base" in rep.stats:
        result["diferencas_linha_de_base"] = rep.stats["diferencas_linha_de_base"]
    with open(os.path.join(a.saida, "relatorio_traducao.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(result, f, indent=1, ensure_ascii=False)
    print("STATUS", result["status"], f"({fails_n} FAIL)")
    return 0 if fails_n == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
