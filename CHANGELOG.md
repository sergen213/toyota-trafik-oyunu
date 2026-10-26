# Changelog

## Unreleased

- Car wheels fixed: the right-side tyres were being drawn on top of the left ones (rim facing inboard), leaving the
  right side bare. The far-side mesh is now mirrored across its own centre plane, so each tyre sits under its own
  arch with the rim plate pointing out; steering toe is preserved and mirrored meshes keep outward-facing normals.

## 0.2.0 — 2026-10-26

- Map actors: train and level crossings (Şehir), school bus with −7 overtaking rule, ambulance/police with sirens,
  beacons and junction priority, wrong-parked violators, city/Otoban AI spawner and culling.
- Rules: right of way (−4) and hospital horn (−5); `t`/`y` car take-over.
- Pedestrians: skeletal walk cycle, clothes tint, red-light violator question (code 5).
- Cutscene videos and the licence intro clip through FFmpeg; music-list editor; joystick mode; radio volume.
- Billboards with scrolling ads, Kasaba water sound, wheels, driver poses, map 4 key panel.
- Renderer interface with a Vulkan 1.3 backend (default): ray-traced shadows, AMD FSR 3.1.4 upscaling and frame
  generation (FidelityFX SDK subset from the IKV port); OpenGL 3.3 fallback.
- Tests: Sim-level world tests, SMD skinning test, music-list editor, OpenGL and FSR+frame-generation playthroughs.
- Windows x64 build (MinGW-w64 cross build against MSYS2 ucrt64) as an NSIS installer and a portable zip, and a Linux
  AppImage: `scripts/package_release.sh`.
- First-start game discovery: remembered folder, default install folders, then a dialog offering the installed game
  folder, the setup CD folder or the setup `.rar` (imported once into the profile folder, v2 preferred); `--import`.
- Profiles and the base path come from SDL (`%APPDATA%\toyota-trafik` on Windows); invalid game folders show a
  message box.

## 0.1.0 — 2026-10-26 (unreleased)

- Identification, file inventory and failure analysis of the original (Indeo 4.1 codec, CP1254, mode switch).
- `scripts/wine_recover.sh`: reversible, verified Wine recovery of the original.
- Reverse-engineering notes for maps, driving, rules and world (`docs/re/`), with emulation-recovered `data/maps.json`.
- Parsers with corruption tests: 3DS, SMD, HG.vdd, SoruBank.dat, UserData.dat, Delphi DFM/PE resources.
- Native `toyota-trafik` (C++23, SDL3, OpenGL 3.3, ODE): menus from the user's executable, profiles, licence
  quiz, map selection, all five maps, ODE car, AI traffic, lights, rules and scoring, sign and violator questions,
  speech/music/engine audio, time limit, F4 pass, report, unlocks, data-folder validator (`--check`).
- Basic pedestrians on maps 2–4 (original models, sidewalks/crossings, lights, −17 hit rule).
- Script runner and ctest playthrough tests (Eğitim Sahası fail and pass paths, per-map smoke tests).
