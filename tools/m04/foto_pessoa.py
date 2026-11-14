"""M04 - "foto" da pasta de saves e opcoes da pessoa (somente leitura).

Lista cada arquivo de Documentos\\My Games\\Banjo-Tooie XBLA com tamanho, data (ticks .NET) e
SHA-256, no mesmo formato de _audit/out/m04/A/saves_pessoa.txt. Com --compare <arquivo>, compara
com uma foto anterior e sai com codigo 1 se algo mudou.

Uso:
  python -I tools/m04/foto_pessoa.py --out <foto.txt> [--compare <foto_anterior.txt>]
"""
import argparse
import ctypes
import ctypes.wintypes
import hashlib
import os
import sys
import uuid

EPOCH_TICKS = 621355968000000000  # 0001-01-01 -> 1970-01-01 em ticks de 100 ns


def documents_dir():
    # FOLDERID_Documents, a mesma pasta que o jogo usa (pc_userdir.cpp)
    fid = uuid.UUID("{FDD39AD0-238F-46AF-ADB4-6C85480369C7}")

    class GUID(ctypes.Structure):
        _fields_ = [("d1", ctypes.c_ulong), ("d2", ctypes.c_ushort),
                    ("d3", ctypes.c_ushort), ("d4", ctypes.c_ubyte * 8)]

    g = GUID(fid.fields[0], fid.fields[1], fid.fields[2],
             (ctypes.c_ubyte * 8)(*fid.bytes[8:]))
    p = ctypes.c_wchar_p()
    hr = ctypes.windll.shell32.SHGetKnownFolderPath(ctypes.byref(g), 0, None, ctypes.byref(p))
    if hr != 0:
        raise OSError("SHGetKnownFolderPath falhou: 0x%08X" % (hr & 0xFFFFFFFF))
    path = p.value
    ctypes.windll.ole32.CoTaskMemFree(p)
    return path


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def snapshot(root):
    rows = []
    for dp, _dn, fn in os.walk(root):
        for n in fn:
            p = os.path.join(dp, n)
            st = os.stat(p)
            ticks = EPOCH_TICKS + st.st_mtime_ns // 100
            rel = p[len(root):]
            rows.append("%s|%d|%d|%s" % (rel, st.st_size, ticks, sha256(p)))
    rows.sort()
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--compare")
    ap.add_argument("--root", help="pasta a fotografar (padrao: My Games da conta atual)")
    a = ap.parse_args()
    root = a.root or os.path.join(documents_dir(), "My Games", "Banjo-Tooie XBLA")
    rows = snapshot(root)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(rows) + "\n")
    print("pasta:", root)
    print("arquivos:", len(rows))
    if a.compare:
        with open(a.compare, encoding="utf-8") as f:
            old = sorted(l.strip() for l in f if l.strip())
        if old == rows:
            print("IGUAL a", a.compare)
            return 0
        so, sn = set(old), set(rows)
        for l in sorted(so - sn):
            print("  antes:", l)
        for l in sorted(sn - so):
            print("  agora:", l)
        print("DIFERENTE de", a.compare)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
