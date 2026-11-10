"""MISSAO 02 - resume o rastro de E/S do runtime (--log_noisy --log_level trace).

Para cada arquivo aberto pelo jogo: quantas aberturas, quantas leituras, bytes pedidos e bytes
entregues (iosb_info), e o tamanho do arquivo no disco. Tambem lista aberturas que falharam e
consultas nao implementadas. Somente leitura.

Uso: python -I file_trace.py <log.txt> <pasta_do_jogo> <saida.json>
"""
import json
import os
import re
import sys

log, game_dir, out = sys.argv[1:4]
re_open = re.compile(r"\[NtCreateFile\] path=(\S+) ")
re_open_res = re.compile(r"\[NtCreateFile\] -> (0x[0-9a-f]+) handle=(0x[0-9a-f]+)")
re_fail = re.compile(r"\[NtCreateFile\] FAILED: path='([^']+)' -> (0x[0-9a-f]+)")
re_read = re.compile(r"\[NtReadFile\] handle=(0x[0-9a-f]+) .* len=(0x[0-9a-f]+)")
re_read_res = re.compile(r"\[NtReadFile\] -> (0x[0-9a-f]+) .*iosb_info=(\d+)")
re_unimpl = re.compile(r"(\w+\(\w+\)) unimplemented")

files, handles, fails, unimpl = {}, {}, {}, {}
pending_path = None
pending_read = None
for line in open(log, encoding="utf-8", errors="replace"):
    m = re_open.search(line)
    if m:
        pending_path = m.group(1)
        continue
    m = re_open_res.search(line)
    if m and pending_path:
        key = pending_path.split(":", 1)[-1].lstrip("\\").lower()
        f = files.setdefault(key, {"path_jogo": pending_path, "aberturas": 0, "leituras": 0,
                                   "bytes_pedidos": 0, "bytes_entregues": 0})
        f["aberturas"] += 1
        handles[m.group(2)] = key
        pending_path = None
        continue
    m = re_fail.search(line)
    if m:
        fails[m.group(1)] = m.group(2)
        pending_path = None
        continue
    m = re_read.search(line)
    if m:
        pending_read = (m.group(1), int(m.group(2), 16))
        continue
    m = re_read_res.search(line)
    if m and pending_read:
        key = handles.get(pending_read[0])
        if key:
            f = files[key]
            f["leituras"] += 1
            f["bytes_pedidos"] += pending_read[1]
            f["bytes_entregues"] += int(m.group(2))
        pending_read = None
        continue
    m = re_unimpl.search(line)
    if m:
        unimpl[m.group(1)] = unimpl.get(m.group(1), 0) + 1

for key, f in files.items():
    p = os.path.join(game_dir, *key.split("\\"))
    f["tamanho_disco"] = os.path.getsize(p) if os.path.isfile(p) else None
    f["lido_por_inteiro"] = f["tamanho_disco"] is not None and f["bytes_entregues"] >= f["tamanho_disco"]

all_files = []
for dp, _dn, fn in os.walk(game_dir):
    for n in fn:
        all_files.append(os.path.relpath(os.path.join(dp, n), game_dir).lower())
never = sorted(set(all_files) - set(files))
result = {"arquivos_abertos": files, "aberturas_falhas": fails, "consultas_nao_implementadas": unimpl,
          "nunca_abertos": never}
json.dump(result, open(out, "w", encoding="utf-8"), indent=2, ensure_ascii=False)
for key, f in sorted(files.items()):
    print(f"{key:34} aberturas={f['aberturas']} leituras={f['leituras']:5} entregues={f['bytes_entregues']:>10} disco={f['tamanho_disco']}")
print("falhas:", fails)
print("nao implementadas:", unimpl)
print("nunca abertos:", len(never), [n for n in never if not n.endswith(".png")])
