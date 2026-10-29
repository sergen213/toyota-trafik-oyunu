# Changelog

## 0.2.7 — 2026-10-28

- Launcher: new Updates tab. It checks the latest GitHub release at startup (checkbox, on by default) or on
  "Check for updates"; when newer, the AppImage downloads the release's `-x86_64.AppImage`, verifies it against its
  `.sha256`, replaces itself in place and restarts. Non-AppImage builds only report the new version with a link.

## 0.2.6 — 2026-10-28

- Wheels match the original (checked against the game under Wine and its setup at 0x53fc55): left tyres take a 180°
  roll about the forward axis, right tyres none — proper rotations instead of the old yaw + mirror pair, which made
  every tyre roll backwards. Steered front wheels also turned the wrong way (ODE's hinge2 Angle1 is the negated yaw).
- Enhanced graphics (an addition, not original behaviour; on by default, `--no-enhanced`, F7 or the launcher
  checkbox turn it off for the exact original look), on both renderers: a raymarched volumetric cloud layer
  (350–950 m, sun-lit with Beer-Lambert extinction and a forward-scattering lobe, drifting with the wind) over the
  skybox, and parallax occlusion mapping with self-shadowing on asphalt, kerbs, pavements, brick, stone, roof-tile
  and dirt textures (height from texture luminance, tangent frame from screen derivatives, fading out by 40 m).

## 0.2.5 — 2026-10-28

- Launcher: the AppImage now opens a Qt 6 launcher (`toyota-trafik-launcher`) instead of starting the game straight
  away. It selects the installed game folder (checked with `--check`) or imports the setup CD / `.rar`, and sets
  window/fullscreen, window size, renderer, ray tracing, FSR 3.1 upscaling, frame generation and mute before *Play*.
  Settings persist in `~/.config/toyota-trafik/launcher.conf`. With arguments the AppImage still runs the game directly.
- Animated sea (an addition, not original behaviour): `deniz.jpg` surfaces get travelling waves, Fresnel sky
  reflection, a sun glint and crest highlights on both renderers; with Vulkan ray tracing on, scenery in the mirrored
  direction darkens the reflection.

## 0.2.4 — 2026-10-26

- Right-side tyres spin again with the rim dish outboard. Two compounding bugs: the far-side mirror matrix negated a whole GLM column
  (x, y, z and w), shearing the mesh and warping perspective so the right wheels drew as a dark, frozen blob; and the far-side rotation
  was conjugated by that mirror — a proper transform that flips the dish back inboard. The mirror is now diag(−1,1,1) via `M[0][0]`
  and the far side composes as the true mirror twin of the verified near-side look (`P*F*R*M`): rims out on both flanks, steering yaw
  mirrored, tyres rolling forward.
- Rear-left tyre wiggle: wheel rotation is rebuilt from the hinge2 steering/spin angles about the chassis axes; the raw wheel-body
  quaternion carries free-axis wobble (suspension and contact impulses) that visibly rocked the pancake rim.

## 0.2.3 — 2026-10-26

- Car wheels fixed: the right-side tyres were being drawn on top of the left ones (rim facing inboard), leaving the
  right side bare. The far-side mesh is now mirrored across its own centre plane, so each tyre sits under its own
  arch with the rim plate pointing out; steering toe is preserved and mirrored meshes keep outward-facing normals.
- Mirrored wheel animation: the right-side tyres were frozen (the mirroring kept only the steering angle). The full
  per-wheel rotation — steering and rolling spin together — is now mirrored about the wheel centre plane, so both
  sides roll forward and the rim dish stays outboard on every wheel.

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
