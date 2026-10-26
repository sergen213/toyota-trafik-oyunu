# Failure analysis — why the original does not run cleanly

Scope: v2 ("Hızlı Bilgisayarlar İçin") `TrafikOyunu.exe`, plus static checks of v1. No Windows machine or VM was
available; all dynamic evidence comes from Wine 11.20 in an isolated prefix (`work/wine/prefix`) on a private
Xvfb display, using a working copy of the installed files. Windows-specific conclusions are therefore marked
as inferences.

## Executable facts (static, confirmed)

| Item | Finding |
|---|---|
| Format | PE32 GUI, i386, Delphi 7 (fixed linker timestamp 0x2A425E19, VCL RTTI/DFM resources) |
| Engine units | GLScene (`D:\Delphi\Birimler\Arsiv\GLScene\Source\...`), GLOXOde + `oxGLODE.dll` 1.5b (ODE), RX library speed buttons, VCL `TMediaPlayer` |
| Imports | kernel32, user32, gdi32, advapi32, ole32/oleaut32, comctl32, comdlg32, shell32, mpr, version, **OpenGL32, GLU32**, **oxGLODE.dll** (65 ODE functions), **winmm** (`mciSendCommandA`, `mciGetErrorStringA`, joystick) |
| Not used | No DirectX/DirectShow/DirectSound import (strings for DDraw/DSound/DInput are GLScene leftovers resolved only on demand), no .NET, no network API, no licence/activation code found |
| Graphics | `TGLFullScreenViewer` 800×600, `RefreshRate = 0`, v2 requests `aa2xHQ` multisampling |
| Installer | InstallShield-generated `setup.exe` + MSI 2.0 package; `Setup.ini` lists supported OS only up to Windows XP/2003 (with `SuppressWrongOS=Y`) |
| Codec | `Video/cancan.avi` is **Indeo Video 4.1 (FourCC IV41)** 600×300; `iv5setup.exe` (Ligos Indeo 5 installer) ships next to the game |

## Reproduction procedure

```sh
# private copies only; the purchased files are never touched
cp -a "Toyota Trafik Oyunu/Kurulum2/program files/ToyotaSA/Toyota Trafik Oyunu v2" work/wine/game/v2
Xvfb :77 -screen 0 1024x768x24 &
TTO_WINEDEBUG=err+all,+mci,+mciavi scripts/tto_ctl.sh start v2 work/runs/x/w.log
scripts/tto_ctl.sh snap shot.png
```

## Observed symptoms

1. **Startup error dialog**: "There is an undetectable problem in loading the specified device driver."
   MCI trace: `MCI_OPEN ...\video\cancan.avi` → `MCIAVI_OpenVideo fcc IV41` → `Can't locate codec for the file`
   → `MCI_OPEN_DRIVER` fails with `0x10A` (`MCIERR_CANNOT_LOAD_DRIVER`). Delphi's `TMediaPlayer.Open` raises
   `EMCIDeviceError` with the text from `mciGetErrorStringA`.
2. After dismissing it the menu works, but the intro clip area (`paVid`) stays black, and after passing the
   licence quiz a second dialog "No MCI device open." appears (the game calls `Play` on the player whose
   open failed).
3. Turkish text is garbled (e.g. "Aþaðýdakilerden") unless the process runs with a Turkish ANSI code page
   (`LC_ALL=tr_TR.UTF-8` under Wine). All strings are CP1254.
4. With a non-virtual 1024×768 X screen, `ChangeDisplaySettings(800×600)` fails (`DISP_CHANGE_BADMODE`) and the
   game stalls on the "Harita Yükleniyor" loader with no 3D view. Inside a Wine virtual desktop the same call
   is emulated and the 3D game runs.
5. With 3 fixes applied for the session (dialogs dismissed, Turkish code page, virtual desktop), **the full
   gameplay path works under Wine**: menu → profile → licence quiz → map select → Eğitim Sahası loads →
   driving with arrow keys, HUD (Süre/Puan/km/h), sign questions via SPACE + click (+40 points), end-of-map
   performance report, map restart. Screenshots: `work/runs/live9/` (private).

## Root-cause hypotheses and experiments

| # | Hypothesis | Experiment (one variable) | Result |
|---|---|---|---|
| H1 | Startup/quiz dialogs are caused by the missing Indeo 4.1 decoder, not by the game logic | Working copy: transcode `cancan.avi` to Cinepak (FourCC `cvid`, decoder built into Wine/Windows) with FFmpeg, nothing else changed | **Confirmed**: MCI opens the clip (`MCIAVI_OpenVideo fcc cvid`), no error dialog. Original file restored afterwards (SHA-256 re-checked). |
| H2 | Garbled text is a code-page issue | Same build, `LC_ALL=C` vs `LC_ALL=tr_TR.UTF-8` | **Confirmed** (readable Turkish with tr_TR). |
| H3 | The 3D stall is the fullscreen mode switch | Bare Xvfb (BADMODE) vs Wine virtual desktop (emulated switch) | **Confirmed** for Wine. |
| H4 | On modern Windows the same Indeo failure occurs | Not testable here | **Inference**: Microsoft removed/disabled the Indeo codecs (security advisory 954157, 2009) and 64-bit Windows never shipped IV41; `iv5setup.exe` installs Indeo 5 and is not guaranteed to provide IV41 on Vista+. |
| H5 | Modern Windows also suffers from MSI/InstallShield 2002 bootstrapper or 800×600 exclusive fullscreen problems | Not testable here | Open. The MSI only copies files (no services/drivers), so the installer is unlikely to be the blocker; exclusive 800×600 GL on modern drivers may produce a black screen on some systems. |

## Conclusions

- The game itself is intact: all files match the RAR, every referenced asset exists, and the complete gameplay
  loop runs once the environment is right. There is no online, activation or licensing dependency.
- The concrete defects are environmental: an obsolete video codec (hard error dialogs), code-page-dependent
  text, and reliance on an 800×600 exclusive mode switch.
- Recovery path A (repair) is feasible for Wine users (Cinepak/other re-encode of one AVI, tr_TR locale,
  virtual desktop), but it still depends on Windows emulation. The native Linux reimplementation is pursued
  as the preservation target (see `ARCHITECTURE.md`), with the Wine setup kept as a reference oracle.

## Next steps

- Test on real Windows 10/11 when available (record the exact dialog and whether `iv5setup.exe` fixes it).
- ~~Optional repair kit~~ done: `scripts/wine_recover.sh` (see below).

## Scripted recovery (verified 2026-10-26)

```sh
scripts/wine_recover.sh setup "<installed Toyota Trafik Oyunu v2 folder>" work/recover   # copy + Cinepak + new prefix
Xvfb :78 -screen 0 1024x768x24 &          # or run on a normal desktop
DISPLAY=:78 scripts/wine_recover.sh run work/recover
```

`setup` copies the game folder, re-encodes only the copy's `Video/cancan.avi` (IV41 → Cinepak, PCM audio),
re-checks the source file's SHA-256, and creates a fresh prefix with `winemenubuilder` disabled (no desktop-file
side effects). `run` starts the game with `LC_ALL=tr_TR.UTF-8` inside a 1024×768 Wine virtual desktop.
No `tr_TR` glibc locale is installed on the test machine; Wine still selects code page 1254 from the name.

Verified run, with no error dialogs and no MCI/codec errors in the log (screenshots in private
`work/runs/recover_verified/`):
start screen → help → new driver form → main menu → licence quiz, answered with `scripts/quiz_bot.py` (8/8 needed, wheels shown)
→ `cancan.avi` intro plays → map selection → Eğitim Sahası loads → driving with ↑ (HUD Süre/Puan/km/h updates)
→ after the 240 s time limit, the "PERFORMANSIN" report lists all counters and "Devam Etmek İçin [ENTER] Tuşuna Basın…".
F4 does nothing at 0 points, as documented in `re/RULES.md` §3.2 (it needs score ≥ required).
