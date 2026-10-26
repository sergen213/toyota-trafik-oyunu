# Architecture and recovery strategy

## Options considered (goal.txt §6)

| Option | Evidence | Verdict |
|---|---|---|
| A. Repair the original | The defects are environmental: missing Indeo 4.1 decoder, CP1254 text, an 800×600 exclusive mode switch. A copy-only transcode plus a Wine prefix runs the full game (`FAILURE_ANALYSIS.md`). | **Done** as `scripts/wine_recover.sh`, kept as the reference oracle. It still depends on Windows emulation and an obsolete GL/MCI stack, so it is not the long-term target. |
| B. Re-implement platform dependencies (GLScene/VCL/MCI/ODE DLL shims) | Game logic is in a 2.2 MB Delphi executable tied to the VCL message loop and GLScene objects. Shimming them natively means re-implementing Delphi VCL + GLScene. | Rejected: larger and less maintainable than C. |
| C. Re-implement the engine using the original data | All assets are loose standard or simple files (3DS, SMD, JPG/BMP, WAV/MP3, VDD, fixed records). Game rules, physics constants, HUD and map layout were recovered by disassembly and emulation (`re/*.md`). | **Chosen** for the native port. |
| D. Clean-room replacement | Not needed: the original assets are usable. | — |

## Native program

The program is C++23, built with CMake + Ninja. It is 2D/3D but lightweight: the original is a low-poly
fixed-function GLScene game, so the renderer is OpenGL 3.3 core. Vulkan was not chosen because it would add
complexity without visual benefit at this fidelity (goal.txt §8 allows this choice for lightweight games).

```
src/core      fs (case-insensitive Windows paths, CP1254), log
src/formats   3DS, SMD, Delphi DFM/PE resources            ┐
src/data      HG.vdd, SoruBank, UserData, maps.json, validator ├─ tto_core (no SDL/GL; unit-tested)
src/game      ODE car (TGLOXDynCar port), traffic sim, rules, quiz ┘
src/render    OpenGL renderer, images (stb), text (SDL3_ttf + fontconfig)
src/platform  audio (SDL3 streams, mpg123)
src/app       App lifecycle + script runner, menu (TGirisForm), game (TFormOyun), scene drawing
```

- **Data ownership:** user-visible texts, menu layout, button glyphs and captions come from the user's own
  `TrafikOyunu.exe` (DFM resources and string literals at recovered addresses; `ExeStrings`, `parse_dfm`), with
  built-in fallbacks. Only numeric map layout data recovered from code is shipped (`data/maps.json`).
- **Physics:** ODE 0.16 with the original world and car parameters (gravity −10, QuickStep 20, step 0.05 ×
  multiply 7, Hinge2 wheels, Mu 30). `dJointCorrectHinge2` (an oxGLODE-only function) is emulated by re-aligning
  the rear-wheel axes.
- **Timing:** the HUD clock counts simulated seconds and stops while paused, as the original's `TimerFPS` does.
  `--fixed-step` makes runs deterministic for tests.
- **Coordinates:** the HUD uses an 800×600 canvas and the menu 1024×768, letterboxed into any window size.
- **Saves:** `UserData.dat` uses the identical 271-byte layout in `$XDG_DATA_HOME/toyota-trafik`, written atomically
  (temporary file + rename).
- **Testability:** the script runner drives the real input handlers (clicks, keys, autopilot via arrow keys). It is
  used by ctest playthroughs.
