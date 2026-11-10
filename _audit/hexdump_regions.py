BASE = 0x82000000
d = open(r"W:\Decomp\Banjo 3\app\_audit\out\xex_image.bin", "rb").read()


def hexdump(addr, length, label):
    off = addr - BASE
    print("===== %s @0x%X (file 0x%X) =====" % (label, addr, off))
    for i in range(0, length, 16):
        chunk = d[off + i: off + i + 16]
        hx = " ".join("%02X" % b for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print("  0x%08X  %-47s  %s" % (addr + i, hx, asc))
    print()


hexdump(0x82073B40, 0x60, "XUI class names area")
hexdump(0x820740C0, 0x60, "Brightness record area")
hexdump(0x8204F060, 0x50, "Audio device area")
hexdump(0x8204F099 - 0x30, 0x80, "Audio strings area")
