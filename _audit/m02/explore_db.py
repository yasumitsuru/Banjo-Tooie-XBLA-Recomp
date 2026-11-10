"""MISSAO 02 - explora a tabela de assets do db360 como esta na memoria do jogo (dump do port)."""
import collections, struct, sys
d = open(sys.argv[1], "rb").read()
n = len(d)
ents = []
o = 8
prev = -1
while o + 4 <= n:
    v = struct.unpack_from(">I", d, o)[0]
    off = v >> 6
    if off < prev or off > n:
        break
    ents.append((v, off, v & 0x3F))
    prev = off
    o += 4
table_end = o
print("entradas monotonicas:", len(ents), "fim da tabela em 0x%X" % table_end)
print("primeiras:", [hex(e[0]) for e in ents[:8]])
print("ultimas:", [hex(e[0]) for e in ents[-6:]])
print("flags:", collections.Counter(e[2] for e in ents).most_common(10))
print("bytes apos a tabela:", d[table_end:table_end+32].hex())
# onde comecam os dados? testa base = fim da tabela
for base in (table_end, 8 + len(ents) * 4):
    for i in (0, 1, 2, 100, 1000):
        if i < len(ents):
            s = base + ents[i][1]
            print(f"base 0x{base:X} asset {i} flags {ents[i][2]} -> {d[s:s+16].hex()}")
