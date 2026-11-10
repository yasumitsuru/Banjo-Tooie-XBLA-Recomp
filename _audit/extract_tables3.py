import struct

BASE = 0x82000000
d = open(r"W:\Decomp\Banjo 3\app\_audit\out\xex_image.bin", "rb").read()


def u16be(addr):
    return struct.unpack_from(">H", d, addr - BASE)[0]


def u16str_be(addr, maxlen=0x70):
    out = []
    for i in range(0, maxlen, 2):
        w = u16be(addr + i)
        if w == 0:
            break
        out.append(chr(w) if 0x20 <= w < 0xD800 else "\\u%04X" % w)
    return "".join(out)


print("=== fatal dialog: inline UTF-16BE strings per language ===")
tables = {
    "lang2 (JA, .data)": (0x8263E7A0, 0x8263E7B0, 0x8263E7FC),
    "lang3": (0x82003628, 0x82003580, 0x82003548),
    "lang4": (0x82003708, 0x82003688, 0x82003640),
    "lang5": (0x820037E8, 0x82003780, 0x82003738),
    "lang6": (0x820038C0, 0x82003840, 0x82003808),
    "default (EN)": (0x82003990, 0x82003920, 0x820038EC),
}
for name, addrs in tables.items():
    print("%s:" % name)
    print("    [title?] 0x%08X: %r" % (addrs[0], u16str_be(addrs[0])))
    print("    [text? ] 0x%08X: %r" % (addrs[1], u16str_be(addrs[1])))
    print("    [button?] 0x%08X: %r" % (addrs[2], u16str_be(addrs[2])))

print()
print("=== video settings keys (both) ===")
for a in (0x820740AC, 0x820740C4, 0x820740DC):
    print("  0x%08X: %r" % (a, u16str_be(a, 32)))

print()
print("=== XUI class names (UTF-16LE) ===")


def u16str_le(addr, maxlen=64):
    out = []
    for i in range(0, maxlen, 2):
        w = struct.unpack_from("<H", d, addr - BASE + i)[0]
        if w == 0:
            break
        out.append(chr(w) if 0x20 <= w < 0xD800 else "\\u%04X" % w)
    return "".join(out)


for a in (0x82073B6D, 0x82073B81, 0x82073B99, 0x82073C19, 0x82073D19):
    print("  0x%08X: %r" % (a, u16str_le(a)))

print()
print("=== audio device names (UTF-16LE) ===")
print("  0x8204F06D: %r" % u16str_le(0x8204F06D))
print("  0x8204F099: %r" % u16str_le(0x8204F099))
