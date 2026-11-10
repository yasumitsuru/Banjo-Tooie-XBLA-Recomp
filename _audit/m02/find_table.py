"""MISSAO 02 - procura no db360.cmp sequencias longas de u32 BE com (v>>6) crescente (tabela de assets)."""
import struct, sys
data = open(sys.argv[1], "rb").read()
n = len(data)
best = []
for align in range(4):
    vals = [struct.unpack_from(">I", data, o)[0] for o in range(align, n - 3, 4)]
    run_start = 0
    for i in range(1, len(vals)):
        ok = (vals[i] >> 6) >= (vals[i - 1] >> 6) and (vals[i] >> 6) <= n
        if not ok:
            if i - run_start >= 64:
                best.append((i - run_start, align + run_start * 4, vals[run_start], vals[i - 1]))
            run_start = i
    if len(vals) - run_start >= 64:
        best.append((len(vals) - run_start, align + run_start * 4, vals[run_start], vals[-1]))
best.sort(reverse=True)
for length, off, first, last in best[:10]:
    print(f"run {length} entradas em 0x{off:X}: primeiro=0x{first:08X} (off 0x{first>>6:X}) ultimo=0x{last:08X} (off 0x{last>>6:X})")
