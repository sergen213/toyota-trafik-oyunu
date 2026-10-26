# Cancan ile Toyota Trafik Oyunu — preservation and native Linux/Windows port

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
| Kasaba, Şehir, Şehir Merkezi, Otoban (debug map 5) | working: AI spawner/culling (≤ 11 cars), Otoban spawner, billboards, water sound |
| Map actors | train + 10 level crossings with booms, bell and blocking box (Şehir); school bus unloading with lit `Dur` sign and −7 overtaking rule; ambulance/police with sirens, beacons and priority; wrong-parked violators (code 3) |
| Rules | all penalties of RULES.md §3.4, incl. right of way (−4, Sağdan/Karşıdan/Soldan) and hospital horn (−5) |
| Pedestrians | skeletal walk cycle (`yuru.smd`/`yuruk.smd`), clothes tint, red-light violator question (code 5) |
| Cutscenes | intro `cancan.avi` (Indeo 4.1) after the licence quiz and the fail / map 2 / map 4 car cutscenes, via FFmpeg |
| Menus / input | music-list editor (TfrmPlaylister), joystick mode (`j`), radio volume `z`/`x`, `t`/`y` take over a car |
| Rendering | Vulkan 1.3 (default) with optional ray-traced shadows, AMD FSR 3.1.4 upscaling and frame generation; OpenGL 3.3 fallback |

## Download and play

Releases (<https://github.com/sergen213/toyota-trafik-oyunu/releases>) contain a Linux AppImage, a Windows x64 installer
(`…-Setup.exe`, per-user, no administrator rights, Start menu/desktop shortcuts, uninstaller) and the same Windows files as a
portable zip.
Neither contains any game file: on first start the program finds your copy, or asks for one of

- **Installed game folder**: the folder containing `TrafikOyunu.exe` (used in place, never written), or
- **Setup folder (CD)**: the CD or a copy of it (`Kurulum1`/`Kurulum2`), or
- **Setup .rar file**: an archive of that CD.

Setup media is imported once into the profile folder (`~/.local/share/toyota-trafik/game`, on Windows
`%APPDATA%\toyota-trafik\game`); v2 (`Kurulum2`) is preferred when both versions are present. The choice is remembered.
From a terminal: `--game <folder>` or `--import <setup folder | .rar>`.

The AppImage is built on Arch Linux and needs an equally recent glibc. Vulkan needs a Vulkan 1.3 driver; otherwise
OpenGL 3.3 is used.

## Building

```sh
scripts/check_toolchain.sh                       # GCC/Clang C++23 sanity check
cmake -S . -B build -G Ninja && ninja -C build
build/toyota-trafik --game "/path/to/Toyota Trafik Oyunu v2" --check   # validate data
build/toyota-trafik --game "/path/to/Toyota Trafik Oyunu v2"
```

`scripts/package_release.sh` builds both release artifacts (see `docs/BUILDING.md`).

Controls follow the original: arrows drive, `f` brake, SPACE pause and question mode (click signs or
offending cars or pedestrians), `c`/`v` signals, `h` horn, `k`/`l`/`o` camera, `m`/`n`/`z`/`x` radio, `j` joystick,
`t`/`y` take over the car ahead / back to Afacan, F1–F4, ESC. F11 toggles fullscreen.

Native extras (not in the original, off by default unless noted): ray-traced shadows (on when supported, `--no-rt`,
F9), AMD FSR 3.1.4 upscaling (`--fsr native|quality|balanced|performance|ultra`, F10 cycles) and FSR 3.1.4 frame
generation (`--framegen`, F8). `--renderer gl` forces the OpenGL fallback.

Documentation: `docs/BUILDING.md`, `docs/TESTING.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/LEGAL.md`.
