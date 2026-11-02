# Cancan ile Toyota Trafik Oyunu — native Linux/Windows port

A native reimplementation and preservation project for *Toyota Trafik Oyunu*, a 2006–07 Turkish traffic-education game for primary-school children.

[![ci](https://github.com/sergen213/toyota-trafik-oyunu/actions/workflows/ci.yml/badge.svg)](https://github.com/sergen213/toyota-trafik-oyunu/actions/workflows/ci.yml)

## What it is

The original game was commissioned by ToyotaSA and developed by Martı Animasyon (Delphi 7 + GLScene + oxGLODE). It is abandoned. On modern systems it fails on an obsolete Indeo 4.1 video codec, code-page-dependent text and an 800×600 exclusive mode switch (see [docs/FAILURE_ANALYSIS.md](docs/FAILURE_ANALYSIS.md)).

This repository contains:

- **A native reimplementation** (`toyota-trafik`, C++23, SDL3, Vulkan 1.3 or OpenGL 3.3, ODE physics). It reads your own copy of the game: meshes, textures, menu layout and glyphs from the executable's form resources, the question bank, `HG.vdd`, speech and music. The map layouts were recovered from the executable's map builders ([data/maps.json](data/maps.json)).
- **A recovery setup for the original**: `scripts/wine_recover.sh` builds a reversible Wine setup. It re-encodes a copy of the intro clip and runs the game with a Turkish code page in a virtual desktop.
- Reverse-engineering notes ([docs/re/](docs/re/)), format documentation, tools and tests.

### Status

All five maps are playable. Details:

| Area | State |
|---|---|
| Start screen, help, driver profiles, licence quiz, map selection | working; layout and assets taken from the user's executable and files |
| Eğitim Sahası (map 1): driving, AI traffic, traffic lights, HUD, sign questions, red-light and litter violators, penalties, timer, F4 pass, report, menu question, unlock, continue to map 2 | working; covered by automated playthrough tests |
| Kasaba, Şehir, Şehir Merkezi, Otoban (debug map 5) | working: AI spawner/culling (≤ 11 cars), Otoban spawner, billboards, water sound |
| Map actors | train + 10 level crossings with booms, bell and blocking box (Şehir); school bus unloading with lit `Dur` sign and −7 overtaking rule; ambulance/police with sirens, beacons and priority; wrong-parked violators (code 3) |
| Rules | all penalties of [RULES.md §3.4](docs/re/RULES.md), incl. right of way (−4, Sağdan/Karşıdan/Soldan) and hospital horn (−5) |
| Pedestrians | skeletal walk cycle (`yuru.smd`/`yuruk.smd`), clothes tint, red-light violator question (code 5) |
| Cutscenes | intro `cancan.avi` (Indeo 4.1) after the licence quiz and the fail / map 2 / map 4 car cutscenes, via FFmpeg |
| Menus / input | music-list editor (TfrmPlaylister), joystick mode (`j`), radio volume `z`/`x`, `t`/`y` take over a car |
| Rendering | Vulkan 1.3 (default) with optional ray-traced shadows, AMD FSR 3.1.4 upscaling and frame generation; OpenGL 3.3 fallback |

## You need the original game

**No game files are included.** You need your own copy of the CD (`Kurulum2` = v2 is recommended). The game, its executables, artwork, models, audio, video and texts are © ToyotaSA / Martı Animasyon. This is an unofficial project, not affiliated with or endorsed by them. See [docs/LEGAL.md](docs/LEGAL.md).

## Download

[GitHub Releases](https://github.com/sergen213/toyota-trafik-oyunu/releases) contain:

- a Linux x86_64 AppImage,
- a Windows x64 installer (`…-Setup.exe`: per-user, no administrator rights, Start menu/desktop shortcuts, uninstaller),
- the same Windows files as a portable zip.

Each file has a `.sha256` checksum. None of them contains game files.

## Requirements

- **Linux:** x86_64. The AppImage is built on Arch Linux and needs an equally recent glibc. glibc, the Vulkan loader and GPU drivers come from your system.
- **Windows:** 64-bit Windows.
- **GPU:** a Vulkan 1.3 driver for the default renderer; otherwise OpenGL 3.3 is used.

## Quick start

Start the AppImage. It opens the **launcher**: choose your copy of the game, set the display (window/fullscreen, size) and graphics (renderer, ray tracing, FSR upscaling, frame generation), then press *Play*. Started with arguments (or with `TTO_APPIMAGE_ENTRY=toyota-trafik`), the AppImage runs the game directly.

The game finds your copy on first start, or asks for one of:

- **Installed game folder**: the folder containing `TrafikOyunu.exe` (used in place, never written),
- **Setup folder (CD)**: the CD or a copy of it (`Kurulum1`/`Kurulum2`),
- **Setup .rar file**: an archive of that CD.

Setup media is imported once into the profile folder (`~/.local/share/toyota-trafik/game`, on Windows `%APPDATA%\toyota-trafik\game`). v2 (`Kurulum2`) is preferred when both versions are present. The choice is remembered. From a terminal, use `--game <folder>` or `--import <setup folder | .rar>`.

### Controls

Controls follow the original: arrows drive, `f` brake, SPACE pause and question mode (click signs, offending cars or pedestrians), `c`/`v` signals, `h` horn, `k`/`l`/`o` camera, `m`/`n`/`z`/`x` radio, `j` joystick, `t`/`y` take over the car ahead / back to Afacan, F1–F4, ESC. F11 toggles fullscreen.

### Native extras

Not in the original:

- ray-traced shadows (on when supported; `--no-rt`, F9),
- enhanced graphics: volumetric clouds and parallax relief on road, wall and ground textures (on, both renderers; `--no-enhanced`, F7),
- AMD FSR 3.1.4 upscaling (`--fsr native|quality|balanced|performance|ultra`, F10 cycles),
- FSR 3.1.4 frame generation (off; `--framegen`, F8).

`--renderer gl` forces the OpenGL fallback.

## Build from source

```sh
scripts/check_toolchain.sh                       # GCC/Clang C++23 sanity check
cmake -S . -B build -G Ninja && ninja -C build
build/toyota-trafik --game "/path/to/Toyota Trafik Oyunu v2" --check   # validate data
build/toyota-trafik --game "/path/to/Toyota Trafik Oyunu v2"
```

Dependencies, options, install and release packaging: [docs/BUILDING.md](docs/BUILDING.md).

## Documentation

- [docs/BUILDING.md](docs/BUILDING.md) — dependencies, build, install, game data, release packaging, running the original under Wine.
- [docs/TESTING.md](docs/TESTING.md) — test suite and playthrough tests.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — code layout.
- [docs/FAILURE_ANALYSIS.md](docs/FAILURE_ANALYSIS.md) — why the original fails and how it was recovered.
- [docs/GAME_IDENTITY.md](docs/GAME_IDENTITY.md) — what the game is, versions and files.
- [docs/ASSET_FORMATS.md](docs/ASSET_FORMATS.md) — file formats.
- [docs/re/](docs/re/) — reverse-engineering notes: [driving](docs/re/DRIVING.md), [maps](docs/re/MAPS.md), [rules](docs/re/RULES.md), [world](docs/re/WORLD.md).
- [docs/ROADMAP.md](docs/ROADMAP.md) — project phases.
- [docs/LEGAL.md](docs/LEGAL.md) — legal and asset-handling policy.
- [CHANGELOG.md](CHANGELOG.md) — release history.

## Contributing

Changes are accepted only as pull requests, reviewed and merged by the maintainer. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Source-available, all rights reserved. See [LICENSE](LICENSE). Versions published before this license under a different license remain available under that earlier license. Third-party components (ODE, AMD FidelityFX SDK, bundled system libraries) keep their own licenses; see [docs/LEGAL.md](docs/LEGAL.md).
