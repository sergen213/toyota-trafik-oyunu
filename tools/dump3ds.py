#!/usr/bin/env python3
"""Summarise a 3D Studio .3DS file: materials (+texture maps) and objects
(vertex/face counts, bounds, material groups). Read-only.
Usage: dump3ds.py <file.3ds> [--chunks]
"""
import struct
import sys

NAMES = {0x4D4D: "MAIN", 0x3D3D: "EDIT", 0x4000: "OBJECT", 0x4100: "TRIMESH", 0x4110: "VERTS",
         0x4120: "FACES", 0x4130: "FACEMAT", 0x4140: "UV", 0x4160: "MATRIX", 0xAFFF: "MATERIAL",
         0xA000: "MATNAME", 0xA200: "TEXMAP", 0xA300: "MAPFILE", 0xB000: "KEYF", 0x4600: "LIGHT",
         0x4700: "CAMERA", 0x0002: "VERSION", 0x3D3E: "MESHVER", 0x0100: "SCALE", 0xA010: "AMBIENT",
         0xA020: "DIFFUSE", 0xA030: "SPECULAR", 0x4150: "SMOOTH"}
CONTAINERS = {0x4D4D, 0x3D3D, 0x4100, 0xAFFF, 0xA200, 0xB000, 0xA010, 0xA020, 0xA030}


def walk(d, start, end, depth, out):
    p = start
    while p + 6 <= end:
        cid, ln = struct.unpack_from("<HI", d, p)
        if ln < 6 or p + ln > end:
            out.append(("BAD", depth, cid, p, ln))
            return
        out.append(("CHUNK", depth, cid, p, ln))
        body = p + 6
        if cid == 0x4000:
            z = d.index(b"\0", body)
            walk(d, z + 1, p + ln, depth + 1, out)
        elif cid == 0x4120:
            n = struct.unpack_from("<H", d, body)[0]
            walk(d, body + 2 + 8 * n, p + ln, depth + 1, out)
        elif cid in CONTAINERS:
            walk(d, body, p + ln, depth + 1, out)
        p += ln


def cstr(d, p):
    return d[p:d.index(b"\0", p)].decode("cp1254")


def main():
    d = open(sys.argv[1], "rb").read()
    out = []
    walk(d, 0, len(d), 0, out)
    if "--chunks" in sys.argv:
        for kind, depth, cid, p, ln in out:
            print("  " * depth + f"{NAMES.get(cid, hex(cid))} @{p} len={ln}{' BAD' if kind == 'BAD' else ''}")
        return
    obj = None
    for kind, depth, cid, p, ln in out:
        b = p + 6
        if cid == 0xA000:
            print(f"material {cstr(d, b)!r}")
        elif cid == 0xA300:
            print(f"   map {cstr(d, b)!r}")
        elif cid == 0x4000:
            obj = cstr(d, b)
            print(f"object {obj!r}")
        elif cid == 0x4110:
            n = struct.unpack_from("<H", d, b)[0]
            vs = [struct.unpack_from("<3f", d, b + 2 + 12 * i) for i in range(n)]
            lo = [min(v[i] for v in vs) for i in range(3)] if vs else []
            hi = [max(v[i] for v in vs) for i in range(3)] if vs else []
            print(f"   verts {n} min={[round(x, 2) for x in lo]} max={[round(x, 2) for x in hi]}")
        elif cid == 0x4120:
            print(f"   faces {struct.unpack_from('<H', d, b)[0]}")
        elif cid == 0x4130:
            print(f"   facemat {cstr(d, b)!r}")
        elif cid in (0x4600, 0x4700):
            print(f"   {NAMES[cid]}")


if __name__ == "__main__":
    main()
