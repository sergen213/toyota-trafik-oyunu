#!/usr/bin/env python3
"""List Delphi (D3-D7 layout) classes and their published methods from a PE image.

A VMT is recognised by vmtSelfPtr (VMT-76) pointing at VMT. For each class prints
class name, parent, instance size and published method name -> virtual address.
Usage: delphi_vmt.py <exe> [--methods-only]
"""
import struct
import sys

import pefile


def main():
    pe = pefile.PE(sys.argv[1])
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    size = len(img)

    def u32(rva):
        return struct.unpack_from("<I", img, rva)[0]

    def sstr(rva):
        n = img[rva]
        return img[rva + 1:rva + 1 + n].decode("latin-1")

    classes = {}
    for rva in range(0, size - 80, 4):
        vmt_va = u32(rva)
        if vmt_va != base + rva + 76:
            continue
        vmt = rva + 76
        name_va = u32(vmt - 44)
        if not base <= name_va < base + size:
            continue
        name = sstr(name_va - base)
        if not name or not name.replace("_", "a").isalnum():
            continue
        parent_va = u32(vmt - 36)
        methods = []
        mt_va = u32(vmt - 52)
        if mt_va and base <= mt_va < base + size:
            p = mt_va - base
            count = struct.unpack_from("<H", img, p)[0]
            p += 2
            for _ in range(count):
                esize, addr = struct.unpack_from("<HI", img, p)
                methods.append((sstr(p + 6), addr))
                p += esize
        classes[base + vmt] = (name, parent_va, u32(vmt - 40), methods)

    for va, (name, parent_va, isz, methods) in sorted(classes.items()):
        parent = "nil"
        if parent_va:
            pv = u32(parent_va - base) if base <= parent_va < base + size else 0
            parent = classes.get(pv, ("?",))[0]
        if "--methods-only" in sys.argv and not methods:
            continue
        print(f"class {name} vmt={va:#x} parent={parent} size={isz}")
        for m, a in methods:
            print(f"  {a:#010x} {m}")


if __name__ == "__main__":
    main()
