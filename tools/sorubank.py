#!/usr/bin/env python3
"""Decode Data/SoruBank.dat (Toyota Trafik Oyunu question bank) to JSON or text.

Record layout (1310 bytes, little endian, Delphi packed record, CP1254 strings):
  0x000 int32       id (1-based)
  0x004 string[255] question
  0x104 string[255] answer A (the correct answer; the game shuffles at runtime)
  0x204 string[255] answer B
  0x304 string[255] answer C
  0x404 string[20]  category ("Ehliyet" = licence quiz, "Trafik" = sign questions)
  0x419 uint8       group (1..5)
  0x41A ...         unused (zero / editor garbage)
Usage: sorubank.py <SoruBank.dat> [--json]
"""
import json
import sys

REC = 1310


def pstr(b: bytes, off: int, cap: int) -> str:
    n = b[off]
    if n > cap:
        raise ValueError(f"string length {n} exceeds capacity {cap} at {off}")
    return b[off + 1:off + 1 + n].decode("cp1254")


def parse(data: bytes):
    if len(data) % REC:
        raise ValueError(f"size {len(data)} is not a multiple of {REC}")
    out = []
    for i in range(len(data) // REC):
        r = data[i * REC:(i + 1) * REC]
        out.append({
            "id": int.from_bytes(r[0:4], "little"),
            "question": pstr(r, 0x004, 255),
            "answers": [pstr(r, o, 255) for o in (0x104, 0x204, 0x304)],
            "category": pstr(r, 0x404, 20),
            "group": r[0x419],
        })
    return out


def main():
    qs = parse(open(sys.argv[1], "rb").read())
    if "--json" in sys.argv:
        json.dump(qs, sys.stdout, ensure_ascii=False, indent=1)
        return
    for q in qs:
        print(f"{q['id']:3} [{q['category']}/{q['group']}] {q['question']}")
        for k, a in zip("+--", q["answers"]):
            print(f"     {k} {a}")


if __name__ == "__main__":
    main()
