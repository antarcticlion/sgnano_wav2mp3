#!/usr/bin/env python3
"""SGX のオーバーレイのパックを作る（2026-11-06、include/sgx_overlay.h）。

  使い方: sgx_ovl_pack.py <out.ovl> <name>=<module.elf> [<name>=<module.elf> ...]

- モジュールの ELF は sgx_ovl.ld で番地 0 に結び、-Wl,-q で再配置の情報を残したもの（SGX_OVL_MODULE で見出しを置く）
- 像（見出し・コード・定数・初めの値）と再配置の表（R_ARM_ABS32 / R_ARM_TARGET1 の位置）を、パックの頭の表と一緒に書く
- 止める場合: MOVW / MOVT の絶対番地（表は 32 ビットの語だけ）、定まっていないシンボルへの絶対の参照（常駐のシンボルに結んだ）、
  見出しが無い・像の大きさが合わない、名前が 16 バイトを越える・重なる
"""
import struct, sys

R_ARM_ABS32, R_ARM_TARGET1 = 2, 38
BAD = {43: "R_ARM_MOVW_ABS_NC", 44: "R_ARM_MOVT_ABS", 47: "R_ARM_THM_MOVW_ABS_NC", 48: "R_ARM_THM_MOVT_ABS"}
SHN_UNDEF, SHN_ABS = 0, 0xFFF1
PACK_MAGIC, MOD_MAGIC, VERSION = 0x4F584753, 0x4D584753, 1
ENTRY = struct.Struct("<16sIIII")
HEADER = struct.Struct("<IIII")


def module_image(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
        sys.exit("sgx_ovl_pack: %s: not a 32-bit little-endian ELF" % path)
    e_shoff, = struct.unpack_from("<I", d, 32)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 46)
    secs = []
    for i in range(e_shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<IIIIIIIIII", d, e_shoff + i * e_shentsize)
        secs.append(dict(name=name, type=typ, flags=flags, addr=addr, off=off, size=size, link=link, info=info))
    shstr = secs[e_shstrndx]

    def sname(s):
        o = shstr["off"] + s["name"]
        return d[o:d.index(b"\0", o)].decode()
    alloc = [s for s in secs if (s["flags"] & 2) and s["type"] != 8 and s["size"]]
    if not alloc:
        sys.exit("sgx_ovl_pack: %s: no sections" % path)
    end = max(s["addr"] + s["size"] for s in alloc)
    img = bytearray(end)
    for s in alloc:
        img[s["addr"]:s["addr"] + s["size"]] = d[s["off"]:s["off"] + s["size"]]
    bss = sum(s["size"] for s in secs if (s["flags"] & 2) and s["type"] == 8 and sname(s).startswith(".bss"))   # .data の詰め物だけの節も NOBITS になる
    symtab = next((s for s in secs if s["type"] == 2), None)
    if not symtab:
        sys.exit("sgx_ovl_pack: %s: no symbol table" % path)
    strtab = secs[symtab["link"]]
    syms = []
    for i in range(symtab["size"] // 16):
        st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", d, symtab["off"] + i * 16)
        o = strtab["off"] + st_name
        syms.append((st_value, st_info, st_shndx, d[o:d.index(b"\0", o)].decode()))
    relocs = set()
    for s in secs:
        if s["type"] != 9:
            continue
        target = secs[s["info"]]
        if not (target["flags"] & 2) or target["type"] == 8:
            continue
        for k in range(s["size"] // 8):
            r_offset, r_info = struct.unpack_from("<II", d, s["off"] + k * 8)
            typ, sym = r_info & 0xFF, r_info >> 8
            if typ in BAD:
                sys.exit("sgx_ovl_pack: %s: %s at 0x%x in %s (only 32-bit absolute words can be relocated)" % (path, BAD[typ], r_offset, sname(target)))
            if typ not in (R_ARM_ABS32, R_ARM_TARGET1):
                continue
            value, info, shndx, nm = syms[sym]
            if shndx == SHN_ABS:
                continue
            if shndx == SHN_UNDEF:
                if (info >> 4) == 2:            # 弱いシンボル（0 のまま）
                    continue
                sys.exit("sgx_ovl_pack: %s: refers to '%s' outside the module (use the host table)" % (path, nm))
            if r_offset + 4 > end:
                sys.exit("sgx_ovl_pack: %s: relocation at 0x%x is outside the image" % (path, r_offset))
            relocs.add(r_offset)
    while len(img) % 4:
        img.append(0)
    magic, version, image_size, bss_size = struct.unpack_from("<IIII", img, 0)
    if magic != MOD_MAGIC or version != VERSION:
        sys.exit("sgx_ovl_pack: %s: the image does not start with the module header (SGX_OVL_MODULE)" % path)
    if image_size != len(img) and not (len(img) - 8 < image_size <= len(img) + 8):
        sys.exit("sgx_ovl_pack: %s: image_size in the header (%d) does not match the image (%d)" % (path, image_size, len(img)))
    if image_size > len(img):
        img += bytes(image_size - len(img))
    img = img[:image_size]
    if bss_size < bss:
        sys.exit("sgx_ovl_pack: %s: bss_size in the header (%d) is smaller than .bss (%d)" % (path, bss_size, bss))
    return bytes(img), bss_size, sorted(relocs)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out = sys.argv[1]
    mods = []
    for a in sys.argv[2:]:
        if "=" not in a:
            sys.exit("sgx_ovl_pack: give modules as name=file.elf (%s)" % a)
        name, path = a.split("=", 1)
        nb = name.encode()
        if not nb or len(nb) > 16:
            sys.exit("sgx_ovl_pack: module name '%s' must be 1 to 16 bytes" % name)
        if any(m[0] == nb for m in mods):
            sys.exit("sgx_ovl_pack: module name '%s' is used twice" % name)
        mods.append((nb,) + module_image(path))
    table_off = HEADER.size
    off = table_off + ENTRY.size * len(mods)
    entries, blobs = [], []
    for nb, img, bss, rel in mods:
        entries.append(ENTRY.pack(nb, off, len(img), bss, len(rel)))
        blob = img + struct.pack("<%dI" % len(rel), *rel)
        blobs.append(blob)
        off += len(blob)
    with open(out, "wb") as f:
        f.write(HEADER.pack(PACK_MAGIC, VERSION, len(mods), table_off))
        for e in entries:
            f.write(e)
        for b in blobs:
            f.write(b)
    total = sum(len(m[1]) for m in mods)
    print("sgx_ovl_pack: %s: %d modules, %d bytes of images (largest %d + bss), %d bytes" %
          (out, len(mods), total, max(len(m[1]) for m in mods), off))


if __name__ == "__main__":
    main()
