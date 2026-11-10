"""Dump printable ASCII strings of the flat XEX image with virtual addresses."""
import re

BASE = 0x82000000
d = open(r"W:\Decomp\Banjo 3\app\_audit\out\xex_image.bin", "rb").read()
out = open(r"W:\Decomp\Banjo 3\app\_audit\out\xex_strings_vaddr.txt", "w", encoding="utf-8", errors="replace")
pat = re.compile(rb"[ -~]{4,}")
for m in pat.finditer(d):
    s = m.group().decode("ascii", "replace")
    out.write("0x%08X | %s\n" % (BASE + m.start(), s))
out.close()
print("done")
