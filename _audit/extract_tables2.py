import struct

BASE = 0x82000000
d = open(r"W:\Decomp\Banjo 3\app\_audit\out\xex_image.bin", "rb").read()


def u32(addr):
    return struct.unpack_from(">I", d, addr - BASE)[0]


def u16be(addr):
    return struct.unpack_from(">H", d, addr - BASE)[0]


def u16str_be(addr, maxlen=128):
    out = []
    for i in range(0, maxlen, 2):
        w = u16be(addr + i)
        if w == 0:
            break
        out.append(chr(w) if 0x20 <= w < 0xD800 else "\\u%04X" % w)
    return "".join(out)


print("=== settings UTF-16BE keys ===")
for a in (0x820740AC, 0x820740DC, 0x820740F4, 0x82074100, 0x8207410C, 0x82074114, 0x8207411C):
    print("  0x%08X  %r" % (a, u16str_be(a)))

print()
print("=== audio device strings ===")
print("  0x8204F06D  %r" % u16str_be(0x8204F06D))
print("  0x8204F099  %r" % u16str_be(0x8204F099))

print()
print("=== XUI class names (UTF-16BE) ===")
for a in (0x82073B6D, 0x82073B81, 0x82073B99, 0x82073C19, 0x82073D19):
    print("  0x%08X  %r" % (a, u16str_be(a)))

print()
print("=== fatal-dialog pointer cells ===")
cells = [0x8263E7A0, 0x8263E7B0, 0x8263E7FC,
         0x82003628, 0x82003580, 0x82003548,
         0x82003708, 0x82003688, 0x82003640,
         0x820037E8, 0x82003780, 0x82003738,
         0x820038C0, 0x82003840, 0x82003808,
         0x82003990, 0x82003920, 0x820038EC]
for a in cells:
    p = u32(a)
    print("  0x%08X -> 0x%08X" % (a, p))

print()
print("=== follow pointers to strings ===")
seen = set()
for a in cells:
    p = u32(a)
    if p == 0 or p in seen:
        continue
    seen.add(p)
    # try utf16be
    try:
        s = u16str_be(p, 160)
        if len(s) >= 2:
            print("  0x%08X (from 0x%08X): %r" % (p, a, s))
    except Exception as e:  # noqa: BLE001
        print("  0x%08X: <err %s>" % (p, e))
