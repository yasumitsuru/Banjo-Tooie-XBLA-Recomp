"""MISSÃO 00 — non-destructive inspection of the provided game files.

Reads only. Writes its findings as JSON/TXT into app/_audit/.
PIRS listing logic mirrors the validated offsets of the base project's
launcher/stfs_extract.py (iron-jay/banjo_tooie_xbla_recomp).
"""
import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
os.makedirs(OUT, exist_ok=True)

GAME = r"W:\Decomp\Banjo 3\Banjo Tooie BR"

BLOCK_SIZE = 0x1000
BLOCKS_PER_HASH_TABLE = 0xAA


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def u24le(b, off):
    return b[off] | (b[off + 1] << 8) | (b[off + 2] << 16)


# ---------------------------------------------------------------- inventory
def inventory():
    files = []
    for root, _dirs, names in os.walk(GAME):
        for n in names:
            p = os.path.join(root, n)
            rel = os.path.relpath(p, GAME)
            files.append({
                "rel": rel.replace("\\", "/"),
                "size": os.path.getsize(p),
                "sha256": sha256(p),
            })
    files.sort(key=lambda f: f["rel"])
    with open(os.path.join(OUT, "inventory.json"), "w", encoding="utf-8") as f:
        json.dump({"root": GAME, "files": files}, f, indent=2)
    print(f"inventory: {len(files)} files")


# ---------------------------------------------------------------- PIRS list
def pirs_list(path):
    with open(path, "rb") as f:
        data = f.read()
    magic = data[0:4]
    header_size = struct.unpack_from(">I", data, 0x340)[0]
    vd = data[0x379:0x379 + 0x24]
    ft_block_count = struct.unpack_from("<H", vd, 0x2)[0]
    ft_block_num = u24le(vd, 0x4)
    base_offset = (header_size + 0xFFF) & ~0xFFF

    def hash_blocks_before(block_num):
        count = block_num // BLOCKS_PER_HASH_TABLE + 1
        if block_num >= BLOCKS_PER_HASH_TABLE:
            count += block_num // (BLOCKS_PER_HASH_TABLE * BLOCKS_PER_HASH_TABLE) + 1
            if block_num >= BLOCKS_PER_HASH_TABLE * BLOCKS_PER_HASH_TABLE:
                count += 1
        return count

    def block_offset(block_num):
        return base_offset + (block_num + hash_blocks_before(block_num)) * BLOCK_SIZE

    table = bytearray()
    for i in range(ft_block_count):
        off = block_offset(ft_block_num + i)
        table += data[off:off + BLOCK_SIZE]

    entries = []
    for i in range(len(table) // 0x40):
        rec = table[i * 0x40:(i + 1) * 0x40]
        if rec[0] == 0:
            continue
        name_len = rec[0x28] & 0x3F
        if name_len == 0 or name_len > 0x28:
            continue
        try:
            name = rec[0:name_len].decode("ascii")
        except UnicodeDecodeError:
            continue
        start_block = u24le(rec, 0x2F)
        path_indicator = struct.unpack_from(">h", rec, 0x32)[0]
        file_size = struct.unpack_from(">I", rec, 0x34)[0]
        if file_size > len(data):
            continue
        if start_block * BLOCK_SIZE > len(data):
            continue
        entries.append({
            "name": name,
            "start_block": start_block,
            "file_size": file_size,
            "path_indicator": path_indicator,
        })
    out = {
        "path": path,
        "magic": magic.decode("latin1"),
        "header_size": header_size,
        "file_table_block_num": ft_block_num,
        "file_table_block_count": ft_block_count,
        "entries": entries,
    }
    with open(os.path.join(OUT, "pirs_list.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
    with open(os.path.join(OUT, "pirs_list.txt"), "w", encoding="utf-8") as f:
        for e in entries:
            f.write(f"{e['file_size']:>10}  block {e['start_block']:>5}  {e['name']}\n")
    print(f"pirs: {len(entries)} entries (magic={magic!r})")


# ------------------------------------------------------- language probes
def probes():
    # utf16le encodings of menu strings per language
    langs = {
        "EN": ["New Game", "Continue", "Options", "Save and Quit", "Jiggy",
               "Controller", "Brightness", "Sound"],
        "PT-BR": ["Novo Jogo", "Continuar", "Opções", "Salvar e Sair",
                  "Brilho", "Controles", "Sair"],
        "FR": ["Nouvelle Partie", "Continuer", "Options"],
        "ES": ["Nueva Partida", "Continuar", "Opciones"],
        "DE": ["Neues Spiel", "Fortsetzen", "Optionen"],
        "IT": ["Nuova Partita", "Continua", "Opzioni"],
    }
    targets = [
        os.path.join(GAME, "RAWFiles", "X360_strings.dat"),
        os.path.join(GAME, "default.xex"),
        os.path.join(GAME, "RAWFiles", "58410955033F0001"),
    ]
    result = {}
    for t in targets:
        with open(t, "rb") as f:
            data = f.read()
        name = os.path.basename(t)
        result[name] = {"size": len(data)}
        for lang, words in langs.items():
            hits = []
            for w in words:
                for enc_name, enc in (("utf16le", "utf-16-le"), ("ascii", "ascii")):
                    try:
                        needle = w.encode(enc)
                    except UnicodeEncodeError:
                        continue
                    n = data.count(needle)
                    if n:
                        hits.append({"word": w, "enc": enc_name, "count": n})
            result[name][lang] = hits
    with open(os.path.join(OUT, "language_probes.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    for tgt, r in result.items():
        print(f"probes {tgt}:")
        for lang in langs:
            print(f"  {lang}: {r[lang]}")


# ---------------------------------------------------------------- PNG dims
def png_dims():
    dims = {}
    for n in sorted(os.listdir(GAME)):
        if not n.lower().endswith(".png"):
            continue
        p = os.path.join(GAME, n)
        with open(p, "rb") as f:
            sig = f.read(24)
        if sig[:8] != b"\x89PNG\r\n\x1a\n":
            dims[n] = "not png"
            continue
        w, h = struct.unpack(">II", sig[16:24])
        dims[n] = {"w": w, "h": h}
    with open(os.path.join(OUT, "png_dims.json"), "w", encoding="utf-8") as f:
        json.dump(dims, f, indent=2)
    print("png dims:", dims)


# ---------------------------------------------------------------- XEX header
def xex_header():
    p = os.path.join(GAME, "default.xex")
    with open(p, "rb") as f:
        head = f.read(0x2000)
    magic = head[0:4]
    module_flags = struct.unpack_from(">I", head, 0x4)[0]
    header_size = struct.unpack_from(">I", head, 0x8)[0]
    u10 = struct.unpack_from(">I", head, 0x10)[0]
    u14 = struct.unpack_from(">I", head, 0x14)[0]
    u18 = struct.unpack_from(">I", head, 0x18)[0]
    u1c = struct.unpack_from(">I", head, 0x1C)[0]
    # scan header for known optional-header keys
    found = {}
    for off in range(0, len(head) - 8, 4):
        key, val = struct.unpack_from(">II", head, off)
        if key in (0x00040001, 0x00010100, 0x00010200, 0x00020000,
                   0x00040401, 0x00040402):
            found.setdefault(hex(key), hex(val))
    out = {
        "magic": magic.decode("latin1"),
        "module_flags": hex(module_flags),
        "header_size": hex(header_size),
        "u_0x10": hex(u10),
        "u_0x14": hex(u14),
        "opt_header_count_hint_0x18": u18,
        "u_0x1C": hex(u1c),
        "known_keys_found": found,
    }
    with open(os.path.join(OUT, "xex_header.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
    print("xex header:", out)


if __name__ == "__main__":
    which = sys.argv[1:] or ["all"]
    if "all" in which or "inventory" in which:
        inventory()
    if "all" in which or "pirs" in which:
        pirs_list(os.path.join(GAME, "RAWFiles", "58410955033F0001"))
    if "all" in which or "probes" in which:
        probes()
    if "all" in which or "png" in which:
        png_dims()
    if "all" in which or "xex" in which:
        xex_header()
