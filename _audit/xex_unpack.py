"""MISSÃO 01 — unpack the XEX2 image (default.xex) into a flat raw binary.

Mirrors the logic of XEXLoaderWV (Warranty Voider), verified against its
source shipped with the user's Ghidra extension:
  - optional headers: 8 bytes each (id, value); id&0xFF semantics decide
    whether value is in-place, a pointer to (id&0xFF)*4 bytes, or a pointer
    to a [len u32][data] blob (id&0xFF == 0xFF)
  - BASE_FILE_FORMAT id 0x000003FF: encryption/compression nibbles
  - loader info at offsetSecuInfo: imageSize, loadAddress, fileKey, ...
  - peImage = file data from offsetPE (raw when encryption==0, compression==0)
  - peImage contains a PE: DOS header -> e_lfanew -> NT headers -> sections
    loaded at imageBaseAddress + VirtualAddress

Reads only. Writes the flat image and a manifest JSON into _audit/out/.
"""
import json
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
XEX = r"W:\Decomp\Banjo 3\Banjo Tooie BR\default.xex"
IMAGE = os.path.join(OUT, "xex_image.bin")
MANIFEST = os.path.join(OUT, "xex_image_manifest.json")


def main():
    with open(XEX, "rb") as f:
        data = f.read()

    magic, flags, offset_pe, reserved, offset_sec, n_opt = struct.unpack_from(
        ">6I", data, 0)

    assert magic == 0x58455832, "not a XEX2"
    print(f"flags=0x{flags:08X} offsetPE=0x{offset_pe:X} "
          f"offsetSecInfo=0x{offset_sec:X} optHeaders={n_opt}")

    # ---- optional headers
    entry_point = None
    image_base = None
    bff = None  # base file format: (encryption, compression)
    imports = None  # raw import library blob
    pos = 24
    opt_dump = []
    for _ in range(n_opt):
        oid, oval = struct.unpack_from(">II", data, pos)
        low = oid & 0xFF
        if low == 0xFF:
            blen = struct.unpack_from(">I", data, oval)[0]
            blob = data[oval: oval + blen]
        elif low < 2:
            blob = data[pos + 4: pos + 8]
        else:
            blob = data[oval: oval + low * 4]
        kind = oid >> 8
        if kind in (0x101, 0x102, 0x103, 0x3):
            opt_dump.append({
                "id": f"0x{oid:X}", "value": f"0x{oval:X}",
                "blob_len": len(blob), "blob_head": blob[:16].hex(),
            })
        if kind == 0x101:
            entry_point = oval
        elif kind == 0x102:
            image_base = oval
        elif kind == 0x103:
            imports = blob
        elif kind == 0x3:
            enc = struct.unpack_from(">H", blob, 4)[0]
            comp = struct.unpack_from(">H", blob, 6)[0]
            bff = (enc, comp)
        pos += 8

    print(f"entry=0x{entry_point:X} imageBase=0x{image_base:X} "
          f"baseFileFormat(enc,comp)={bff}")
    enc, comp = bff
    assert enc == 0, f"encrypted image not supported (enc={enc})"
    assert comp == 0, f"compressed image not supported (comp={comp})"

    # ---- loader info (security info)
    img_size = struct.unpack_from(">I", data, offset_sec + 4)[0]
    load_addr = struct.unpack_from(">I", data, offset_sec + 0x110)[0]
    print(f"loaderInfo.imageSize=0x{img_size:X} loadAddress=0x{load_addr:X}")

    pe = data[offset_pe:]
    assert pe[:2] == b"MZ", "peImage has no DOS header"

    e_lfanew = struct.unpack_from("<I", pe, 0x3C)[0]
    assert pe[e_lfanew:e_lfanew + 4] == b"PE\0\0", "no NT header"
    n_sections = struct.unpack_from("<H", pe, e_lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", pe, e_lfanew + 20)[0]
    sec_table = e_lfanew + 24 + opt_size

    sections = []
    for i in range(n_sections):
        s = sec_table + i * 40
        name = pe[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
        virt_size = struct.unpack_from("<I", pe, s + 8)[0]
        virt_addr = struct.unpack_from("<I", pe, s + 12)[0]
        raw_size = struct.unpack_from("<I", pe, s + 16)[0]
        raw_ptr = struct.unpack_from("<I", pe, s + 20)[0]
        chars = struct.unpack_from("<I", pe, s + 36)[0]
        sections.append({
            "name": name, "virt_addr": virt_addr, "virt_size": virt_size,
            "raw_size": raw_size, "raw_ptr": raw_ptr,
            "chars": f"0x{chars:08X}",
            "r": bool(chars & 0x40000000), "w": bool(chars & 0x80000000),
            "x": bool(chars & 0x20000000),
        })
        print(f"  section {name:<8} va=0x{virt_addr:X} vsize=0x{virt_size:X} "
              f"raw=0x{raw_size:X}@{raw_ptr} chars=0x{chars:08X}")

    # ---- build flat image: file offset 0 == loadAddress
    high = 0
    for s in sections:
        high = max(high, s["virt_addr"] + s["virt_size"])
    image_len = high
    image = bytearray(image_len)
    for s in sections:
        va = s["virt_addr"]
        vsz = min(s["virt_size"], len(pe) - va)
        if vsz > 0:
            image[va: va + vsz] = pe[va: va + vsz]

    with open(IMAGE, "wb") as f:
        f.write(image)

    # ---- .pdata function list (each entry: u32 address, u32 size)
    pdata = None
    pdata_funcs = []
    for s in sections:
        if s["name"] == ".pdata":
            pdata = s
    if pdata:
        va = pdata["virt_addr"]
        n = (pdata["virt_size"] // 8) * 8
        for off in range(0, n, 8):
            addr, size = struct.unpack_from(">II", image, va + off)
            if addr:
                pdata_funcs.append({"addr": f"0x{addr:08X}",
                                    "size": f"0x{size:08X}"})
    print(f"pdata functions: {len(pdata_funcs)}")

    # ---- import libraries (names only, for reference)
    libs = []
    if imports:
        try:
            ipos = 4
            str_size = struct.unpack_from(">I", imports, ipos)[0]
            lib_count = struct.unpack_from(">I", imports, ipos + 4)[0]
            ipos += 8
            strings = []
            cur = b""
            for b in imports[ipos: ipos + str_size]:
                if b:
                    cur += bytes([b])
                else:
                    if cur:
                        strings.append(cur.decode("ascii", "replace"))
                    cur = b""
            ipos += str_size
            for _ in range(lib_count):
                ipos += 0x24
                name_idx, count = struct.unpack_from(">HH", imports, ipos)
                ipos += 4
                libs.append({
                    "name": strings[name_idx],
                    "records": [
                        f"0x{struct.unpack_from('>I', imports, ipos + j * 4)[0]:08X}"
                        for j in range(count)],
                })
                ipos += count * 4
        except Exception as e:  # noqa: BLE001
            libs = [{"error": str(e)}]
    print(f"import libraries: {len(libs)}")
    for lib in libs:
        if "name" in lib:
            print(f"  {lib['name']}: {len(lib['records'])} records")

    manifest = {
        "source": XEX,
        "flags": f"0x{flags:08X}",
        "offset_pe": offset_pe,
        "offset_sec_info": offset_sec,
        "entry_point": f"0x{entry_point:08X}",
        "image_base": f"0x{image_base:08X}",
        "load_address": f"0x{load_addr:08X}",
        "image_size_loader": f"0x{img_size:X}",
        "base_file_format": {"encryption": enc, "compression": comp},
        "optional_headers_parsed": opt_dump,
        "sections": sections,
        "flat_image": {"path": IMAGE, "len": image_len,
                       "file_offset_0_is": f"0x{load_addr:08X}"},
        "pdata_function_count": len(pdata_funcs),
        "pdata_functions_sample": pdata_funcs[:20],
        "import_libraries": libs,
    }
    with open(MANIFEST, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    print("wrote", IMAGE, f"({image_len} bytes)")
    print("wrote", MANIFEST)


if __name__ == "__main__":
    main()
