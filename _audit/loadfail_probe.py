import re

refs = open(r"W:\Decomp\Banjo 3\app\_audit\out\ghidra_xex_out\refs_to_data.txt").read().splitlines()
targets = [0x820036D1, 0x82003921, 0x82003789, 0x82003859, 0x82003600]
for t in targets:
    near = []
    for line in refs:
        m = re.search(r"\|(0x[0-9a-fA-F]+)\|", line)
        if not m:
            continue
        to = int(m.group(1), 16)
        if t - 0x80 <= to <= t + 0x80:
            near.append(line)
    print("=== 0x%08X: %d refs ===" % (t, len(near)))
    for l in near[:10]:
        print("   ", l)

BASE = 0x82000000
d = open(r"W:\Decomp\Banjo 3\app\_audit\out\xex_image.bin", "rb").read()
chunk = d[0x3500:0x3C00]
print("=== strings 0x82003500..0x82003C00 ===")
for m in re.finditer(rb"[\x20-\x7e\xc0-\xff]{3,}", chunk):
    s = m.group().decode("latin1")
    print("  0x%08X  %r" % (BASE + 0x3500 + m.start(), s))
