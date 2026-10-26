#!/usr/bin/env python3
"""Decode Delphi binary form resources (TPF0 streams) from a PE file into text.

Usage: dfm2txt.py <exe> <outdir>
Writes one <FORMNAME>.dfm.txt per RT_RCDATA resource that starts with b"TPF0".
Read-only on the input.
"""
import struct
import sys
from pathlib import Path

import pefile

VA_NULL, VA_LIST, VA_INT8, VA_INT16, VA_INT32, VA_EXTENDED, VA_STRING, VA_IDENT, \
    VA_FALSE, VA_TRUE, VA_BINARY, VA_SET, VA_LSTRING, VA_NIL, VA_COLLECTION, \
    VA_SINGLE, VA_CURRENCY, VA_DATE, VA_WSTRING, VA_INT64, VA_UTF8STRING, VA_DOUBLE = range(22)


class Reader:
    def __init__(self, data: bytes):
        self.d = data
        self.p = 0

    def u8(self):
        if self.p >= len(self.d):
            raise ValueError("truncated DFM stream")
        v = self.d[self.p]
        self.p += 1
        return v

    def take(self, n):
        if self.p + n > len(self.d):
            raise ValueError("truncated DFM stream")
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def sstr(self):
        return self.take(self.u8()).decode("cp1254")


def value(r: Reader, ind: str) -> str:
    t = r.u8()
    if t == VA_NULL:
        return "<null>"
    if t == VA_LIST:
        items = []
        while r.d[r.p] != 0:
            items.append(value(r, ind + "  "))
        r.u8()
        return "(" + " ".join(items) + ")"
    if t == VA_INT8:
        return str(struct.unpack("<b", r.take(1))[0])
    if t == VA_INT16:
        return str(struct.unpack("<h", r.take(2))[0])
    if t == VA_INT32:
        return str(struct.unpack("<i", r.take(4))[0])
    if t == VA_EXTENDED:
        raw = r.take(10)
        mant, exp = struct.unpack("<QH", raw)
        sign = -1 if exp & 0x8000 else 1
        exp &= 0x7FFF
        if exp == 0 and mant == 0:
            return "0"
        return repr(sign * mant / (1 << 63) * 2.0 ** (exp - 16383))
    if t in (VA_STRING,):
        return repr(r.sstr())
    if t == VA_IDENT:
        return r.sstr()
    if t == VA_FALSE:
        return "False"
    if t == VA_TRUE:
        return "True"
    if t == VA_BINARY:
        n = struct.unpack("<I", r.take(4))[0]
        b = r.take(n)
        return f"{{binary {n} bytes: {b[:32].hex()}{'...' if n > 32 else ''}}}"
    if t == VA_SET:
        items = []
        while True:
            s = r.sstr()
            if not s:
                break
            items.append(s)
        return "[" + ", ".join(items) + "]"
    if t == VA_LSTRING:
        n = struct.unpack("<I", r.take(4))[0]
        return repr(r.take(n).decode("cp1254"))
    if t == VA_NIL:
        return "nil"
    if t == VA_COLLECTION:
        out = ["<"]
        while r.d[r.p] != 0:
            if r.d[r.p] in (VA_INT8, VA_INT16, VA_INT32):
                out.append(ind + "  [" + value(r, ind) + "]")
            if r.u8() != VA_LIST:
                raise ValueError(f"collection item without list marker at {r.p - 1}")
            out.append(ind + "  item")
            props(r, ind + "    ", out)
            out.append(ind + "  end")
        r.u8()
        out.append(ind + ">")
        return "\n".join(out)
    if t == VA_SINGLE:
        return repr(struct.unpack("<f", r.take(4))[0])
    if t == VA_CURRENCY:
        return str(struct.unpack("<q", r.take(8))[0] / 10000)
    if t == VA_DATE:
        return repr(struct.unpack("<d", r.take(8))[0])
    if t == VA_WSTRING:
        n = struct.unpack("<I", r.take(4))[0]
        return repr(r.take(2 * n).decode("utf-16-le"))
    if t == VA_INT64:
        return str(struct.unpack("<q", r.take(8))[0])
    if t == VA_UTF8STRING:
        n = struct.unpack("<I", r.take(4))[0]
        return repr(r.take(n).decode("utf-8"))
    if t == VA_DOUBLE:
        return repr(struct.unpack("<d", r.take(8))[0])
    raise ValueError(f"unknown value type {t} at {r.p - 1}")


def props(r: Reader, ind: str, out: list):
    while True:
        name = r.sstr()
        if not name:
            return
        out.append(f"{ind}{name} = {value(r, ind)}")


def component(r: Reader, ind: str, out: list):
    flags = r.d[r.p]
    if flags & 0xF0 == 0xF0:
        r.u8()
        if flags & 2:
            value(r, ind)
    cls = r.sstr()
    name = r.sstr()
    out.append(f"{ind}object {name}: {cls}")
    props(r, ind + "  ", out)
    while r.d[r.p] != 0:
        component(r, ind + "  ", out)
    r.u8()
    out.append(f"{ind}end")


def decode(data: bytes) -> str:
    if data[:4] != b"TPF0":
        raise ValueError("not a TPF0 stream")
    r = Reader(data)
    r.p = 4
    out = []
    component(r, "", out)
    return "\n".join(out) + "\n"


def main():
    exe, outdir = Path(sys.argv[1]), Path(sys.argv[2])
    outdir.mkdir(parents=True, exist_ok=True)
    pe = pefile.PE(str(exe))
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if t.id != pefile.RESOURCE_TYPE["RT_RCDATA"]:
            continue
        for n in t.directory.entries:
            for lang in n.directory.entries:
                e = lang.data.struct
                data = pe.get_data(e.OffsetToData, e.Size)
                name = str(n.name) if n.name else str(n.id)
                if data[:4] == b"TPF0":
                    (outdir / f"{name}.dfm.txt").write_text(decode(data), encoding="utf-8")
                    print("form", name, len(data))
                else:
                    (outdir / f"{name}.bin").write_bytes(data)


if __name__ == "__main__":
    main()
