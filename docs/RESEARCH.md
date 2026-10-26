# Research log and references

## External sources (identity, history)

See `GAME_IDENTITY.md` §Sources for the full list. Summary:

- Otomot.NET, 16 Apr 2007 — announcement of the ToyotaSA traffic-education game, developer Martı Animasyon,
  MEB protocol, free CD at Toyota Plaza dealers.
- Technopat Sosyal 2013 thread — users looking for the CD; confirms distribution as a physical CD.
- r/lostmediatr "[KAYIP] Toyota Trafik Oyunu" — listed as lost media; no prior public preservation found.
- software.informer entry "Toyota Trafik Oyunu v2" — publisher ToyotaSA, confirms the v2 release name.

No source code, SDK, mod tools or earlier technical analyses of this game were found.

## Third-party technology identified in the binary

| Component | Evidence | Reference |
|---|---|---|
| Borland Delphi 7 VCL | DFM resources, RTTI, `Borland` RTL strings | — |
| GLScene (OpenGL scene graph for Delphi) | source paths `D:\Delphi\Birimler\Arsiv\GLScene\Source\*.pas` | <https://sourceforge.net/projects/glscene/> |
| oxGLODE 1.5b / ODE | `oxGLODE.dll` export names (`dWorldCreate`, `dJointCreateHinge2`, …) | <https://www.ode.org/>, <https://github.com/odedevs/ode> |
| RX Library | `TRxSpeedButton` DFM classes | — |
| VCL `TMediaPlayer` (MCI) | DFM objects `mp1`/`mp2`, `winmm.dll` imports | Win32 MCI docs |
| Indeo Video 4.1 | `cancan.avi` FourCC `IV41` | MS Security Advisory 954157 (Indeo removal) |

## Reverse-engineering method

1. Static: `pefile` imports/resources, `tools/dfm2txt.py` (Delphi DFM → text), `tools/delphi_vmt.py`
   (VMT/class table recovery) and Ghidra 12 headless (`tools/ghidra/*.java`) with recovered class/method names.
2. Partial emulation: Unicorn 2.1.4 runs map builders natively with GLScene/VCL/ODE stubbed and the
   created scene-object transforms recorded (private `work/re/MapRE/emu.py`), producing `data/maps.json`.
3. Dynamic oracle: the original under Wine 11.20 in an isolated prefix (`scripts/tto_ctl.sh`), used for
   screenshots, input timing and confirming findings.

## Findings documents (detailed, with addresses and confidence tags)

| Document | Content |
|---|---|
| `re/MAPS.md` | map index → builder, units/axes, road graph, signs, light placement; Talim complete |
| `re/DRIVING.md` | key bindings, ODE world parameters, `TGLOXDynCar` bodies/joints, input model, camera, HUD, audio |
| `re/RULES.md` | menu flow, licence quiz, `UserData.dat` layout, clock, question panel, penalties/bonuses, map end, report |
| `re/WORLD.md` | traffic-light phases, AI vehicles and violations, pedestrians, actors, train/level crossing |
| `ASSET_FORMATS.md` | file formats used by the native reimplementation |

Confidence tags used throughout: **[C]** confirmed by disassembly/emulation/runtime, **[L]** likely, **[G]** guess.
