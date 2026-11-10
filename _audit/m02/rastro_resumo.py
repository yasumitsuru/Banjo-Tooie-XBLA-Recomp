"""MISSAO 02 - resume o rastro de E/S: quantos bytes de cada arquivo do jogo foram lidos."""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "m02"))
import traducao as T  # noqa: E402

log, game = sys.argv[1], sys.argv[2]
files, fails = T.parse_trace(log)
sizes = {}
for root, _d, fs in os.walk(game):
    for n in fs:
        p = os.path.join(root, n)
        sizes[os.path.relpath(p, game).lower()] = os.path.getsize(p)
out = {}
for k, v in sorted(files.items()):
    out[k] = {"aberturas": v["aberturas"], "leituras": len(v["leituras"]), "bytes_lidos": v["bytes"],
              "tamanho": sizes.get(k)}
    print(f"{k:34} aberturas={v['aberturas']} leituras={len(v['leituras']):4} bytes={v['bytes']:>10} de {sizes.get(k)}")
print("falhas:", fails)
if len(sys.argv) > 3:
    json.dump({"arquivos": out, "falhas": fails}, open(sys.argv[3], "w", encoding="utf-8"), indent=1)
