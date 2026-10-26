#!/usr/bin/env python3
"""Diagnostic helper: answer the ORIGINAL game's licence quiz under Wine/Xvfb.

OCRs the quiz area, matches the question against Data/SoruBank.dat (the first
stored answer is the correct one) and clicks the OCR line that best matches it.
Usage: quiz_bot.py <SoruBank.dat> <rounds> <outdir>
"""
import difflib
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import sorubank  # noqa: E402  # pyright: ignore[reportMissingImports]

CTL = str(Path(__file__).resolve().parent / "tto_ctl.sh")
CROP = (228, 462, 560, 290)


def norm(s):
    tr = str.maketrans("çğıöşüÇĞİÖŞÜ", "cgiosuCGIOSU")
    return "".join(ch for ch in s.translate(tr).lower() if ch.isalnum() or ch == " ")


def ocr_lines(png):
    tsv = subprocess.run(["tesseract", png, "-", "--psm", "6", "tsv"], capture_output=True, text=True).stdout
    lines = {}
    for row in tsv.splitlines()[1:]:
        f = row.split("\t")
        if len(f) < 12 or not f[11].strip():
            continue
        key = (f[2], f[3], f[4])
        x, y, w, h = map(int, f[6:10])
        t, bx = lines.get(key, ("", None))
        box = (x, y, x + w, y + h) if bx is None else (min(bx[0], x), min(bx[1], y), max(bx[2], x + w), max(bx[3], y + h))
        lines[key] = ((t + " " + f[11]).strip(), box)
    return list(lines.values())


def main():
    try:
        data = Path(sys.argv[1]).read_bytes()
    except OSError as e:
        sys.exit(f"cannot read {sys.argv[1]}: {e}")
    qs = [q for q in sorubank.parse(data) if q["category"] == "Ehliyet"]
    out = Path(sys.argv[3])
    for n in range(int(sys.argv[2])):
        full, crop = out / f"quiz_{n}.png", out / f"quiz_{n}_c.png"
        subprocess.run([CTL, "snap", str(full)], check=True)
        x0, y0, w, h = CROP
        subprocess.run(["magick", str(full), "-crop", f"{w}x{h}+{x0}+{y0}", "+repage", "-resize", "200%", str(crop)], check=True)
        lines = ocr_lines(str(crop))
        text = " ".join(t for t, _ in lines)
        q = max(qs, key=lambda q: difflib.SequenceMatcher(None, norm(q["question"]), norm(text[:len(q["question"]) + 20])).ratio())
        best = max(lines, key=lambda ln: difflib.SequenceMatcher(None, norm(q["answers"][0]), norm(ln[0].split(")", 1)[-1])).ratio())
        bx = best[1]
        cx, cy = x0 + (bx[0] + bx[2]) // 4, y0 + (bx[1] + bx[3]) // 4
        print(f"{n}: Q{q['id']} -> '{q['answers'][0]}' via OCR '{best[0]}' click {cx},{cy}", flush=True)
        subprocess.run([CTL, "click", str(cx), str(cy)], check=True)
        time.sleep(4)


if __name__ == "__main__":
    main()
