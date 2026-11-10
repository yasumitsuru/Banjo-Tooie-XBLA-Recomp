import re

BASE = 0x82000000
d = open(r"W:\Decomp\Banjo 3\app\_audit\out\xex_image.bin", "rb").read()


def dump(rng, label):
    lo, hi = rng
    print("===== %s (0x%X..0x%X) =====" % (label, lo, hi))
    chunk = d[lo - BASE: hi - BASE]
    for m in re.finditer(rb"[ -~]{3,}", chunk):
        s = m.group().decode("ascii", "replace")
        print("  0x%08X  %r" % (lo + m.start(), s))
    print()


dump((0x820187A0, 0x82018980), "Options block")
dump((0x82073F80, 0x82074280), "Brightness block")
dump((0x8204EE00, 0x8204F300), "Audio block")
dump((0x82073980, 0x82073E80), "XUI class block")
dump((0x8207FCE0, 0x8207FF00), "PDB/build-info block")
