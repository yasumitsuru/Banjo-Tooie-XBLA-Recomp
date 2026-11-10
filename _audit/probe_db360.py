"""Probe db360.cmp / db360.textures.cmp for a known compression scheme.

Read-only. Tries common zlib variants on a few offsets, reports the first
that produces a sane decompressed stream, and greps it for text.
"""
import re
import zlib

for name in ("db360.cmp", "db360.textures.cmp"):
    p = rf"W:\Decomp\Banjo 3\Banjo Tooie BR\RAWFiles\{name}"
    data = open(p, "rb").read()
    print(f"{name}: {len(data)} bytes, header {data[:16].hex()}")
    found = False
    for wbits in (15, -15, 31, 47):
        for off in (4, 8, 12, 16, 32):
            try:
                d = zlib.decompressobj(wbits)
                out = d.decompress(data[off:])
                if len(out) > 1024 and not d.unconsumed_tail:
                    print(f"  zlib wbits={wbits} off={off} -> {len(out)} bytes decompressed")
                    text_hits = [p for p in (b"Mayahem", b"Mumbo", b"Spiral", b"New Game", b"Jiggy", b"Banjo") if p in out]
                    print(f"    text hits: {text_hits}")
                    found = True
                    break
            except Exception:
                pass
        if found:
            break
    if not found:
        print("  no zlib variant matched")
