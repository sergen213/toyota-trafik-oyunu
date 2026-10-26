# Cancan ile Toyota Trafik Oyunu — preservation and native Linux port

*Toyota Trafik Oyunu* is a 2006–07 traffic-education game for primary-school children. It was commissioned by
ToyotaSA and developed by Martı Animasyon (Delphi 7 + GLScene + oxGLODE). The game is abandoned. On modern systems
it fails on an obsolete Indeo 4.1 video codec, code-page-dependent text, and an 800×600 exclusive mode switch
(see `docs/FAILURE_ANALYSIS.md`).

This repository contains:

- **Recovery of the original**: `scripts/wine_recover.sh` builds a reversible, verified Wine setup. It re-encodes a
  copy of the intro clip, then runs the game with a Turkish code page in a virtual desktop.
- **A native Linux reimplementation** (`toyota-trafik`, C++23, SDL3 + OpenGL 3.3, ODE physics). It reads your own
  installed game folder: meshes, textures, menu layout and glyphs from the executable's form resources, the
  question bank, HG.vdd, speech and music. The map layouts were recovered from the executable's map builders
  (`data/maps.json`).
- Reverse-engineering notes (`docs/re/`), format documentation, tools and tests.

**No game files are included.** You need your own copy of the CD (`Kurulum2` = v2 is recommended).

## Status

| Area | State |
|---|---|
| Start screen, help, driver profiles, licence quiz, map selection | working; layout and assets taken from the user's executable and files |
| Eğitim Sahası (map 1): driving, AI traffic, traffic lights, HUD, sign questions, red-light and litter violators, penalties, timer, F4 pass, report, menu question, unlock, continue to map 2 | working; covered by automated playthrough tests |
| Kasaba, Şehir, Şehir Merkezi, Otoban (debug map 5) | load and drive with scenery, signs, lights and AI cars; map-specific actors are missing (see ROADMAP) |
| Pedestrians, train/level crossing, school bus, emergency vehicles, wrong parking, right-of-way rule, hospital horn rule, cutscene videos, music-list editor, joystick | **not implemented** |

## Quick start

```sh
scripts/check_toolchain.sh                       # GCC/Clang C++23 sanity check
cmake -S . -B build -G Ninja && ninja -C build
build/toyota-trafik --game "/path/to/Toyota Trafik Oyunu v2" --check   # validate data
build/toyota-trafik --game "/path/to/Toyota Trafik Oyunu v2"
```

Controls follow the original: arrows drive, `f` brake, SPACE pause and question mode (click signs or
offending cars), `c`/`v` signals, `h` horn, `k`/`l`/`o` camera, `m`/`n` radio, F1–F4, ESC. F11 toggles fullscreen.

Documentation: `docs/BUILDING.md`, `docs/TESTING.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/LEGAL.md`.
