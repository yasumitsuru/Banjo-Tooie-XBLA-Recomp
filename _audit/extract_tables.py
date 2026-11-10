import struct
import re

BASE = 0x82000000
d = open(r"W:\Decomp\Banjo 3\app\_audit\out\xex_image.bin", "rb").read()


def u32(addr):
    return struct.unpack_from(">I", d, addr - BASE)[0]


def u16(addr):
    return struct.unpack_from(">H", d, addr - BASE)[0]


def cstr(addr, maxlen=256):
    off = addr - BASE
    s = d[off:off + maxlen]
    z = s.find(b"\x00")
    if z >= 0:
        s = s[:z]
    return s.decode("ascii", "replace")


# 1) language message tables used by the fatal dialog
print("=== fatal dialog message tables (3 strings each) ===")
tables = {
    "lang2 (.data)": (0x8263E7A0, 0x8263E7B0, 0x8263E7FC),
    "lang3": (0x82003628, 0x82003580, 0x82003548),
    "lang4": (0x82003708, 0x82003688, 0x82003640),
    "lang5": (0x820037E8, 0x82003780, 0x82003738),
    "lang6": (0x820038C0, 0x82003840, 0x82003808),
    "default (EN)": (0x82003990, 0x82003920, 0x820038EC),
}
for name, addrs in tables.items():
    vals = []
    for a in addrs:
        try:
            vals.append(cstr(a, 200))
        except Exception as e:  # noqa: BLE001
            vals.append("<%s>" % e)
    print("%-16s 0x%08X 0x%08X 0x%08X" % (name, *addrs))
    for v in vals:
        print("      %r" % v)

# 2) video settings key table (UTF-16) around 0x820740A0..0x82074180
print()
print("=== video settings UTF-16 keys ===")


def u16str(addr, maxlen=64):
    off = addr - BASE
    out = []
    for i in range(0, maxlen, 2):
        w = struct.unpack_from("<H", d, off + i)[0]
        if w == 0:
            break
        out.append(chr(w) if w < 0x300 else "?")
    return "".join(out)


for a in range(0x820740A0, 0x82074180, 2):
    w = u16(a)
    if 0x41 <= w <= 0x7A:
        s = u16str(a)
        if len(s) >= 2:
            print("  0x%08X  %r" % (a, s))

# 3) audio device function pointer table at 0x8204F0A0
print()
print("=== audio fn pointer table @0x8204F0A0 ===")
for i in range(0, 0x60, 4):
    p = u32(0x8204F0A0 + i)
    print("  [%02d] 0x%08X" % (i // 4, p))

# 4) video settings singleton struct @0x826B65AC
print()
print("=== settings singleton @0x826B65AC (11 words) ===")
for i in range(11):
    p = u32(0x826B65AC + i * 4)
    print("  +0x%02X = 0x%08X  (%d)" % (i * 4, p, p))

# 5) floats referenced by frame timing / audio DSP
print()
print("=== .rdata float constants ===")
for addr in (0x82000730, 0x82000734, 0x82000738, 0x82000750, 0x8200075C,
             0x82000778, 0x8200081C, 0x82000B48, 0x82000F1C, 0x82001B94):
    raw = u32(addr)
    f = struct.unpack(">f", struct.pack(">I", raw))[0]
    print("  0x%08X = %r (0x%08X)" % (addr, f, raw))
