"""Decode PPC (A2ALT 32-bit addr) instruction windows around the PC-hook sites.

Reads the flat image (read-only). Prints each instruction with resolved .rdata
float constants (real XEX addresses = file_offset + 0x82000000).
"""
import struct

IMAGE = r'W:\Decomp\Banjo 3\app\_audit\out\xex_image.bin'
BASE = 0x82000000
img = open(IMAGE, 'rb').read()

def u32(off):
    return struct.unpack('>I', img[off:off + 4])[0]

def s16(v):
    return v - 0x10000 if v & 0x8000 else v

def s32(v):
    return v - 0x100000000 if v & 0x80000000 else v

def f32_at(off):
    try:
        return struct.unpack('>f', img[off:off + 4])[0]
    except Exception:
        return None

class Ctx:
    def __init__(self):
        self.regs = {}  # rN -> int value (from lis/addis)

def dec(ins, off, ctx, labels):
    op = ins >> 26
    out = []
    if op == 15:  # addis
        rd, ra = (ins >> 21) & 31, (ins >> 16) & 31
        imm = ins & 0xFFFF
        base = ctx.regs.get(ra, 0) if ra else 0
        ctx.regs[rd] = (base + (imm << 16)) & 0xFFFFFFFF
        out.append('addis r%d,r%d,0x%04X' % (rd, ra, imm))
    elif op == 14:  # addi / li
        rd, ra = (ins >> 21) & 31, (ins >> 16) & 31
        imm = s16(ins & 0xFFFF)
        out.append('addi r%d,r%d,%d' % (rd, ra, imm) if ra else 'li r%d,%d' % (rd, imm))
    elif op == 24:  # ori
        rs, ra = (ins >> 21) & 31, (ins >> 16) & 31
        imm = ins & 0xFFFF
        ctx.regs[ra] = (ctx.regs.get(rs, 0) & 0xFFFF0000) | imm
        out.append('ori r%d,r%d,0x%04X' % (ra, rs, imm))
    elif op == 48:  # lfs
        fd, ra = (ins >> 21) & 31, (ins >> 16) & 31
        disp = s16(ins & 0xFFFF)
        base = ctx.regs.get(ra) or 0
        addr = (base + disp) & 0xFFFFFFFF
        val = f32_at(addr - BASE) if (addr - BASE) >= 0 else None
        txt = 'lfs f%d,0x%X(r%d)' % (fd, disp & 0xFFFF, ra)
        if val is not None:
            txt += '  @0x%08X = %r' % (addr, val)
        out.append(txt)
    elif op == 50:  # lfd
        fd, ra = (ins >> 21) & 31, (ins >> 16) & 31
        disp = s16(ins & 0xFFFF)
        out.append('lfd f%d,0x%X(r%d)' % (fd, disp & 0xFFFF, ra))
    elif op == 52:  # stfs
        fs, ra = (ins >> 21) & 31, (ins >> 16) & 31
        out.append('stfs f%d,0x%X(r%d)' % (fs, ins & 0xFFFF, ra))
    elif op == 54:  # stfd
        fs, ra = (ins >> 21) & 31, (ins >> 16) & 31
        out.append('stfd f%d,0x%X(r%d)' % (fs, ins & 0xFFFF, ra))
    elif op == 32:  # lwz
        rd, ra = (ins >> 21) & 31, (ins >> 16) & 31
        out.append('lwz r%d,0x%X(r%d)' % (rd, ins & 0xFFFF, ra))
    elif op == 36:  # stw
        rs, ra = (ins >> 21) & 31, (ins >> 16) & 31
        out.append('stw r%d,0x%X(r%d)' % (rs, ins & 0xFFFF, ra))
    elif op == 44:  # sth
        rs, ra = (ins >> 21) & 31, (ins >> 16) & 31
        out.append('sth r%d,0x%X(r%d)' % (rs, ins & 0xFFFF, ra))
    elif op == 18:  # b
        disp = s32((ins & 0x3FFFFFF) << 2) | ((ins >> 30) & 1)
        tgt = (off + disp) & 0xFFFFFFFF
        out.append('b relative -> 0x%08X' % tgt)
    elif op == 16:  # bc
        bo = (ins >> 21) & 31
        bi = (ins >> 16) & 31
        crbit = bi
        disp = s32((ins & 0xFFFC) & 0xFFFF)
        disp = (disp << 16) >> 14
        tgt = (off + disp) & 0xFFFFFFFF
        cond = 'beq' if (bo == 12 and crbit == 2) else 'bne' if (bo == 4 and crbit == 2) else 'bge' if (bo == 12 and crbit == 0) else 'bc bo=%d,cr%d' % (bo, crbit)
        out.append('%s relative -> 0x%08X' % (cond, tgt))
    elif op == 19:  # xop
        xo = ins & 0x7FE
        rd, ra = (ins >> 21) & 31, (ins >> 16) & 31
        if xo == 0x210:  # rlwinm
            rs = (ins >> 21) & 31
            sh = (ins >> 11) & 31
            mb = (ins >> 6) & 31
            me = (ins >> 1) & 31
            out.append('rlwinm r%d,r%d,%d,%d,%d' % (ra, rs, sh, mb, me))
        elif xo == 0x0C8:  # mr (or)
            rs = (ins >> 21) & 31
            out.append('mr r%d,r%d' % (ra, rs))
        else:
            out.append('xop19 xo=0x%X' % xo)
    elif op == 31:  # xop
        xo = ins & 0x7FE
        rd, ra = (ins >> 21) & 31, (ins >> 16) & 31
        rb = (ins >> 11) & 31
        if xo == 0x014:  # add
            out.append('add r%d,r%d,r%d' % (rd, ra, rb))
        elif xo == 0x050:  # subf
            out.append('subf r%d,r%d,r%d' % (rd, ra, rb))
        elif xo == 0x114:  # fsubs
            out.append('fsubs f%d,f%d,f%d' % (rd, ra, rb))
        else:
            out.append('xop31 xo=0x%X' % xo)
    elif op == 59:  # fops
        d = (ins >> 21) & 31
        a = (ins >> 16) & 31
        b = (ins >> 11) & 31
        xo = ins & 0x1F
        fmt = 0x1F if True else 0
        if xo == 0x15:  # fadds
            out.append('fadds f%d,f%d,f%d' % (d, a, b))
        elif xo == 0x18:  # fdivs
            out.append('fdivs f%d,f%d,f%d' % (d, a, b))
        elif xo == 0x19:  # fmuls
            out.append('fmuls f%d,f%d,f%d' % (d, a, b))
        elif xo == 0x14:  # fsubs
            out.append('fsubs f%d,f%d,f%d' % (d, a, b))
        else:
            out.append('fop xo=0x%X' % xo)
    elif op == 63:  # fdouble
        xo = ins & 0x1F
        d, a, b = (ins >> 21) & 31, (ins >> 16) & 31, (ins >> 11) & 31
        if xo == 0x15:
            out.append('fadd f%d,f%d,f%d' % (d, a, b))
        elif xo == 0x19:
            out.append('fmul f%d,f%d,f%d' % (d, a, b))
        elif xo == 0x12:
            out.append('fdiv f%d,f%d,f%d' % (d, a, b))
        else:
            out.append('fdbl xo=0x%X' % xo)
    elif op == 7:  # mulli
        rd, ra = (ins >> 21) & 31, (ins >> 16) & 31
        out.append('mulli r%d,r%d,%d' % (rd, ra, s16(ins & 0xFFFF)))
    elif op == 11:  # cmpi
        bf = (ins >> 23) & 7
        ra = (ins >> 16) & 31
        out.append('cmpwi r%d,%d' % (ra, s16(ins & 0xFFFF)))
    elif op == 10:  # cmpli
        ra = (ins >> 16) & 31
        out.append('cmplwi r%d,%d' % (ra, ins & 0xFFFF))
    else:
        out.append('?? op=%d ins=%08X' % (op, ins))
    return out


def window(real, before=3, after=2):
    off = real - BASE
    ctx = Ctx()
    lines = []
    # pre-scan lis/ori in a larger window to seed register base values
    for i in range(real - BASE - 16, off, 4):
        if i < 0:
            continue
        try:
            ins = u32(i)
        except Exception:
            continue
        if (ins >> 26) == 15:
            rd = (ins >> 21) & 31
            ctx.regs[rd] = (ins & 0xFFFF) << 16
    for i in range(off - before * 4, off + (after + 1) * 4, 4):
        if i < 0:
            continue
        ins = u32(i)
        mark = ' >>>' if i == off else '    '
        try:
            txt = dec(ins, i, ctx, {})[0]
        except Exception as e:
            txt = 'ERR %s' % e
        lines.append('0x%08X%s %s' % (i + BASE, mark, txt))
    return '\n'.join(lines)


SITES = [
    (0x82276E28, 'time_setFrameStep (li r9,2 site at 0x82276E34?)'),
    (0x822F9F10, 'present_interval lwz r11,0x351C(r31) site'),
    (0x822C8928, 'K site f0 (0.05 -> 0.2)'),
    (0x821A22E0, 'K sites f5/f3/f2 (3.0/-0.01/-1.0 -> 9.0/-0.03/-3.0)'),
    (0x821A2320, 'K sites f5/f3/f2 (3.0/0.01/1.0 -> 9.0/0.03/3.0)'),
    (0x821A2354, 'K site f0 (90.0 -> 100.0)'),
    (0x8213E2E8, 'K sites f5/f3/f2 (3.0/-0.01/-1.0 -> 11.0/-0.03/-3.90625)'),
    (0x8213E328, 'K sites f5/f3/f2 (3.0/0.01/1.0 -> 11.0/0.03/3.75)'),
    (0x8213E33C, 'K site f0 (-0.01 -> -0.03)'),
    (0x8213E35C, 'K site f0 (200.0 -> 220.0)'),
    (0x821129B4, 'K site f0 (0.028 -> 1/70)'),
    (0x8225D8A4, 'dialog speed fadds f13,f13,f29'),
    (0x82240DF0, 'draw distance (slwi r3,r3,2)'),
    (0x8223F710, 'skip range check A'),
    (0x822A9F80, 'skip range check B'),
    (0x822DF11C, 'pause menu page0 last entry call'),
]

for real, note in SITES:
    print('=' * 78)
    print('%s  (%s)' % (hex(real), note))
    print(window(real))