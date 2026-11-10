import struct

d = open(r"W:\Decomp\Banjo 3\app\_audit\out\xex_image.bin", "rb").read()
va = 0x80400
n = 0x1DDF8 // 8
entries = []
for i in range(n):
    a, s = struct.unpack_from(">II", d, va + i * 8)
    if a:
        size = s & 0x3FFFFFFF
        entries.append((a, size))

entries.sort()


def enclosing(addr):
    for a, s in entries:
        if a <= addr < a + s:
            return a, s
    return None


targets = [
    0x825B6EAC,  # "banjo2_360.tbl"
    0x825B7079,  # German load-fail message
    0x82018890,  # "Options"
    0x820740DD,  # "Brightness"
    0x8204F099,  # "Audio"
    0x82073B6D,  # "XuiCanvas"
    0x8207FDC0,  # pdb path
    0x82003534,  # XLNI_DASH_ARCADE
]
for t in targets:
    r = enclosing(t)
    print("0x%X -> in .text fn 0x%X size 0x%X" % (t, r[0], r[1]) if r and r[0] >= 0x820A0000 else ("0x%X -> %s" % (t, r)))
