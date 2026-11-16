#!/usr/bin/env python3
"""SGX の像を作る（2026-10-24、docs/sgx_abi.md）: 再配置の情報を残して結んだ ELF（-Wl,-q）から、像（見出し・コード・定数・初めの値）と
再配置の表を書き出す。

  使い方: sgx_link.py <in.elf> <out.sgx>

- 像は番地 0 に結んである（sdk/sgx/sgx.ld）。ファームは PSRAM のどこかに置き、表の位置の 32 ビットの語に置いた番地を足す
- 表に入れるのは R_ARM_ABS32・R_ARM_TARGET1（初めの値の中の絶対番地）。絶対のシンボル（SHN_ABS: 像の大きさなど）と、
  定まっていない弱いシンボル（0 のまま）は足さない。PC 相対のもの（呼び出し・PREL31 など）は足さなくてよい
- MOVW / MOVT の絶対番地（R_ARM_THM_MOVW_ABS_NC など）があれば止める（表は 32 ビットの語だけ。-mpure-code などを使わないこと）
- ファイル = 像（image_size バイト）+ 表（reloc_count 個の 32 ビット、像の頭からのバイトの位置）。見出しの reloc_count を書き込む
"""
import struct, sys

R_ARM_ABS32, R_ARM_TARGET1 = 2, 38
BAD = {43: "R_ARM_MOVW_ABS_NC", 44: "R_ARM_MOVT_ABS", 47: "R_ARM_THM_MOVW_ABS_NC", 48: "R_ARM_THM_MOVT_ABS"}
SHN_UNDEF, SHN_ABS = 0, 0xFFF1
HDR_RELOC_COUNT = 68                       # sgx_header_t の reloc_count の位置（sgx_api.h と合わせる）


def main():
    elf_path, out_path = sys.argv[1], sys.argv[2]
    d = open(elf_path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
        sys.exit("sgx_link: not a 32-bit little-endian ELF")
    e_shoff, = struct.unpack_from("<I", d, 32)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 46)
    secs = []
    for i in range(e_shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<IIIIIIIIII", d, e_shoff + i * e_shentsize)
        secs.append(dict(name=name, type=typ, flags=flags, addr=addr, off=off, size=size, link=link, info=info, entsize=entsize))
    shstr = secs[e_shstrndx]
    def sname(s):
        o = shstr["off"] + s["name"]
        return d[o:d.index(b"\0", o)].decode()
    # 像: 読み込むもの（SHF_ALLOC）で、中身のあるもの（PROGBITS）を番地の順に。.bss（NOBITS）は像に入れない
    alloc = [s for s in secs if (s["flags"] & 2) and s["type"] != 8 and s["size"]]
    if not alloc:
        sys.exit("sgx_link: no sections")
    end = max(s["addr"] + s["size"] for s in alloc)
    img = bytearray(end)
    for s in alloc:
        img[s["addr"]:s["addr"] + s["size"]] = d[s["off"]:s["off"] + s["size"]]
    # シンボルの表
    symtab = next((s for s in secs if s["type"] == 2), None)
    if not symtab:
        sys.exit("sgx_link: no symbol table")
    syms = []
    for i in range(symtab["size"] // 16):
        st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", d, symtab["off"] + i * 16)
        syms.append((st_value, st_info, st_shndx))
    # 再配置（REL）: 読み込む節に当たるものだけ
    relocs = set()
    for s in secs:
        if s["type"] != 9:                 # SHT_REL
            continue
        target = secs[s["info"]]
        if not (target["flags"] & 2):
            continue
        if target["type"] == 8:            # .bss に向けた再配置は無いはず
            continue
        for k in range(s["size"] // 8):
            r_offset, r_info = struct.unpack_from("<II", d, s["off"] + k * 8)
            typ, sym = r_info & 0xFF, r_info >> 8
            if typ in BAD:
                sys.exit("sgx_link: %s at 0x%x in %s (only 32-bit absolute words can be relocated)" % (BAD[typ], r_offset, sname(target)))
            if typ not in (R_ARM_ABS32, R_ARM_TARGET1):
                continue
            value, info, shndx = syms[sym]
            if shndx == SHN_ABS:
                continue                   # 絶対の値（像の大きさなど）
            if shndx == SHN_UNDEF:
                continue                   # 定まっていない弱いシンボル（0 のまま）
            if r_offset + 4 > end:
                sys.exit("sgx_link: relocation at 0x%x is outside the image" % r_offset)
            relocs.add(r_offset)
    relocs = sorted(relocs)
    struct.pack_into("<I", img, HDR_RELOC_COUNT, len(relocs))
    magic, = struct.unpack_from("<I", img, 0)
    if magic != 0x31584753:
        sys.exit("sgx_link: the image does not start with the SGX header")
    image_size, = struct.unpack_from("<I", img, 16)
    if end < image_size <= end + 64:                # リンカの台本の __sgx_image_end は整列した後ろ: 間は 0 で埋める（2026-10-25、mode7.sgx で 4 バイト）
        img += bytes(image_size - end)
        end = image_size
    if image_size != end:
        sys.exit("sgx_link: image_size in the header (%d) does not match the image (%d)" % (image_size, end))
    with open(out_path, "wb") as f:
        f.write(img)
        f.write(struct.pack("<%dI" % len(relocs), *relocs))
    print("sgx_link: %s: image %d bytes, %d relocations" % (out_path, end, len(relocs)))


if __name__ == "__main__":
    main()
