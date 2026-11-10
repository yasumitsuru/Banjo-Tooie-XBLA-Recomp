import re

refs = open(r"W:\Decomp\Banjo 3\app\_audit\out\ghidra_xex_out\refs_to_data.txt").read().splitlines()
targets = [0x82018890, 0x820740DD, 0x8204F099, 0x82073B6D, 0x8207FDC0, 0x825B6EAC]
for t in targets:
    near = []
    for line in refs:
        m = re.search(r"\|(0x[0-9a-fA-F]+)\|", line)
        if not m:
            continue
        to = int(m.group(1), 16)
        if t - 0x100 <= to <= t + 0x100:
            near.append(line)
    print("=== 0x%08X: %d refs within +-0x100 ===" % (t, len(near)))
    for l in near[:8]:
        print("   ", l)
