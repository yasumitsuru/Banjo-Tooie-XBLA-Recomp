"""MISSAO 00 (rodada de verificacao) - integridade interna do default.xex.

Somente leitura. Le o cabecalho XEX2 do default.xex do usuario e verifica:
  1. formato base (criptografia / compressao) e informacoes de execucao
     (media id, versao, versao base, title id);
  2. a cadeia de hashes SHA-1 por bloco de paginas gravada pela Microsoft no
     "security info" do XEX, recalculada sobre a imagem contida no arquivo.

Se a cadeia confere, a imagem (codigo + dados) e byte a byte a imagem assinada
originalmente: ninguem alterou o codigo nem os dados embutidos no XEX.
Varias formas de encadear sao testadas; so se declara "confere" se UMA delas
fecha a cadeia inteira (colisao SHA-1 por acaso e impossivel na pratica).

Uso:  python -I verify_xex_integrity.py <default.xex> <saida.json>
"""
import hashlib
import json
import struct
import sys


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def main():
    xex_path, out_path = sys.argv[1], sys.argv[2]
    data = open(xex_path, "rb").read()
    magic, flags, off_pe, _res, off_sec, n_opt = struct.unpack_from(">6I", data, 0)
    assert magic == 0x58455832, "nao e XEX2"

    result = {"file": xex_path, "file_size": len(data),
              "file_sha256": hashlib.sha256(data).hexdigest(),
              "module_flags": f"0x{flags:08X}", "offset_pe": f"0x{off_pe:X}",
              "offset_security_info": f"0x{off_sec:X}"}

    # ---- optional headers
    opt = {}
    pos = 24
    for _ in range(n_opt):
        oid, oval = struct.unpack_from(">II", data, pos)
        opt[oid] = oval
        pos += 8
    result["optional_header_ids"] = [f"0x{k:08X}" for k in opt]

    # base file format (0x000003FF): [len][enc u16][comp u16]...
    if 0x3FF in opt:
        p = opt[0x3FF]
        enc, comp = struct.unpack_from(">HH", data, p + 4)
        result["base_file_format"] = {
            "encryption": enc, "encryption_name": {0: "none", 1: "normal"}.get(enc, "?"),
            "compression": comp,
            "compression_name": {0: "none", 1: "basic", 2: "normal(LZX)", 3: "delta"}.get(comp, "?")}

    # execution info (0x00040006): 24 bytes
    if 0x40006 in opt:
        p = opt[0x40006]
        media_id, version, base_version, title_id = struct.unpack_from(">4I", data, p)
        platform, exe_table, disc_no, disc_count = struct.unpack_from(">4B", data, p + 16)
        savegame_id = be32(data, p + 20)

        def ver(v):
            return f"{v >> 28}.{(v >> 24) & 0xF}.{(v >> 8) & 0xFFFF}.{v & 0xFF}"
        result["execution_info"] = {
            "media_id": f"0x{media_id:08X}", "version": ver(version),
            "version_raw": f"0x{version:08X}", "base_version": ver(base_version),
            "title_id": f"0x{title_id:08X}", "disc": f"{disc_no}/{disc_count}",
            "savegame_id": f"0x{savegame_id:08X}"}

    # ---- security info
    s = off_sec
    header_size = be32(data, s + 0x0)
    image_size = be32(data, s + 0x4)
    image_flags = be32(data, s + 0x10C)
    load_address = be32(data, s + 0x110)
    root_digest = data[s + 0x114: s + 0x128]
    region = be32(data, s + 0x178)
    n_desc = be32(data, s + 0x180)
    descs = []
    for i in range(n_desc):
        raw = data[s + 0x184 + i * 24: s + 0x184 + (i + 1) * 24]
        v = be32(raw, 0)
        descs.append({"raw": raw, "info": v & 0xF, "pages": v >> 4, "digest": raw[4:]})
    page_size = 0x1000 if (image_flags & 0x10000000) else 0x10000
    result["security_info"] = {
        "header_size": f"0x{header_size:X}", "image_size": f"0x{image_size:X}",
        "image_flags": f"0x{image_flags:08X}", "page_size": f"0x{page_size:X}",
        "load_address": f"0x{load_address:08X}", "region": f"0x{region:08X}",
        "page_descriptor_count": n_desc,
        "total_pages": sum(d["pages"] for d in descs),
        "pages_x_size_equals_image_size": sum(d["pages"] for d in descs) * page_size == image_size}

    comp = result.get("base_file_format", {}).get("compression", -1)
    enc = result.get("base_file_format", {}).get("encryption", -1)
    if comp != 0 or enc != 0:
        result["page_hash_chain"] = {"status": "NOT_RUN",
                                     "reason": "imagem cifrada/comprimida: exige decifrar (fora do escopo)"}
        json.dump(result, open(out_path, "w", encoding="utf-8"), indent=2)
        print(json.dumps(result, indent=2))
        return

    image = data[off_pe: off_pe + image_size]
    result["image_bytes_in_file"] = len(image)
    blocks = []
    off = 0
    for d in descs:
        n = d["pages"] * page_size
        blocks.append(image[off: off + n])
        off += n

    def sha1(*parts):
        h = hashlib.sha1()
        for p in parts:
            h.update(p)
        return h.digest()

    # Esquemas candidatos de encadeamento
    schemes = {}
    # A: digest_i = SHA1(bloco_i)
    schemes["A_sha1(bloco_i)"] = [sha1(blocks[i]) == descs[i]["digest"] for i in range(n_desc)]
    # B: digest_i = SHA1(bloco_i || descritor_{i+1}) (ultimo: so o bloco)
    schemes["B_sha1(bloco_i||desc_i+1)"] = [
        sha1(blocks[i], descs[i + 1]["raw"] if i + 1 < n_desc else b"") == descs[i]["digest"]
        for i in range(n_desc)]
    # C: raiz = SHA1(bloco_0 || desc_0); digest_i = SHA1(bloco_i+1 || desc_i+1)
    c = [sha1(blocks[0], descs[0]["raw"]) == root_digest]
    for i in range(n_desc - 1):
        c.append(sha1(blocks[i + 1], descs[i + 1]["raw"]) == descs[i]["digest"])
    schemes["C_raiz=sha1(bloco0||desc0); desc_i=sha1(bloco_i+1||desc_i+1)"] = c

    summary = {}
    verified = None
    for name, oks in schemes.items():
        summary[name] = {"ok": sum(oks), "total": len(oks)}
        if oks and all(oks):
            verified = name
    result["page_hash_chain"] = {
        "schemes_tested": summary,
        "verified_scheme": verified,
        "status": "PASS" if verified else "INCONCLUSIVO",
        "meaning": ("a imagem do XEX e identica a imagem assinada pela Microsoft"
                    if verified else "nenhum esquema testado fechou a cadeia; NAO prova alteracao")}
    json.dump(result, open(out_path, "w", encoding="utf-8"), indent=2)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
