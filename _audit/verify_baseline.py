"""MISSAO 00 - confere que os arquivos do jogo continuam identicos a linha de base.

Somente leitura. Recalcula o sha256 de cada arquivo da pasta do jogo e compara
com o inventario gravado na auditoria (inventory.json). Lista tambem arquivos
novos ou faltando.

Uso: python -I verify_baseline.py <pasta_do_jogo> <inventory.json> <saida.json>
"""
import hashlib
import json
import os
import sys


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    root, inv_path, out = sys.argv[1:4]
    inv = json.load(open(inv_path, encoding="utf-8"))
    expected = {f["rel"].replace("/", "\\"): f for f in inv["files"]}
    found = {}
    for dp, _dn, fn in os.walk(root):
        for n in fn:
            p = os.path.join(dp, n)
            found[os.path.relpath(p, root)] = p
    rows = []
    for rel, f in sorted(expected.items()):
        if rel not in found:
            rows.append({"rel": rel, "status": "FALTANDO"})
            continue
        got = sha256(found[rel])
        rows.append({"rel": rel, "size": os.path.getsize(found[rel]),
                     "status": "IGUAL" if got == f["sha256"] else "DIFERENTE",
                     "sha256": got})
    for rel in sorted(set(found) - set(expected)):
        rows.append({"rel": rel, "status": "NOVO", "sha256": sha256(found[rel])})
    summary = {"root": root, "expected": len(expected), "found": len(found),
               "iguais": sum(r["status"] == "IGUAL" for r in rows),
               "diferentes": [r["rel"] for r in rows if r["status"] == "DIFERENTE"],
               "faltando": [r["rel"] for r in rows if r["status"] == "FALTANDO"],
               "novos": [r["rel"] for r in rows if r["status"] == "NOVO"]}
    summary["status"] = "PASS" if (summary["iguais"] == len(expected) and not summary["novos"]) else "FAIL"
    json.dump({"summary": summary, "rows": rows}, open(out, "w", encoding="utf-8"), indent=2)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
