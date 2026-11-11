import struct

img = open(r'W:\Decomp\Banjo 3\app\_audit\out\xex_image.bin', 'rb').read()
BASE = 0x82000000
TEXT_START = 0x820A0000 - BASE
TEXT_END = 0x825F39C8 - BASE

# who reads/writes the global 0x8269EE20:
# pattern: lis rX,0x826A (0x3D6?_826A) followed within +/-16 bytes by a memory op with disp 0xEE20
hits = []
for off in range(TEXT_START + 0x1E00, TEXT_END - 8, 4):
    ins = struct.unpack('>I', img[off:off + 4])[0]
    if ins & 0xFFFF == 0xEE20 and (ins >> 26) in (32, 34, 36, 40, 44, 46):
        # lwz/lfs/stw/stfs/lhz/sth with displacement 0xEE20
        # check register base was set via lis rX,0x826A within the previous 64 bytes
        start = max(TEXT_START, off - 64)
        ok = False
        for j in range(start, off, 4):
            jins = struct.unpack('>I', img[j:j + 4])[0]
            if (jins >> 16) == 0x3D60 or (jins >> 16) == 0x3D60:
                pass
            if (jins & 0xFFFF0000) >> 16 == 0x826A and (jins >> 26) == 15:
                # addis rX,r0/value,0x826A
                rd = (jins >> 21) & 31
                if rd == (ins >> 16) & 31:
                    ok = True
            elif (jins >> 26) == 15 and ((jins & 0xFFFF) == 0x826A):
                rd = (jins >> 21) & 31
                if rd == (ins >> 16) & 31:
                    ok = True
        if ok:
            hits.append((off, hex(ins)))

for off, ins in hits:
    print('0x%08X: %s' % (off + BASE, ins))
print('total hits:', len(hits))