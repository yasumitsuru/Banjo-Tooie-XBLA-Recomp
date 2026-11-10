import struct

d = open(r"W:\Decomp\Banjo 3\app\_audit\out\xex_image.bin", "rb").read()
va = 0x80400
n = 0x1DDF8 // 8
entries = []
for i in range(n):
    a, s = struct.unpack_from(">II", d, va + i * 8)
    if a:
        entries.append((a, s))
print("total nonzero:", len(entries))
print("entry@14736:", hex(entries[14736][0]), hex(entries[14736][1]))
for tgt in (0x8259ADA8, 0x825B3AF8, 0x825505B4):
    for a, s in entries:
        if a <= tgt < a + s:
            print("0x%X inside pdata fn 0x%X size 0x%X" % (tgt, a, s))
            break
