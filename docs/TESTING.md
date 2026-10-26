# Testing

```sh
cmake -S . -B build -G Ninja && ninja -C build
cd build && ctest --output-on-failure                     # unit tests; data and playthrough tests skip
TTO_GAME_DIR="/path/to/Toyota Trafik Oyunu v2" xvfb-run -a -s "-screen 0 1280x1024x24" ctest --output-on-failure
```

## Unit tests (`tto_tests`, GoogleTest)

| Suite | Covers |
|---|---|
| Tds | 3DS chunks, materials, UVs, smoothing normals; truncation sweep, bad chunk, out-of-range face index |
| Smd | bones, frames, triangles grouped by texture; malformed lines |
| Vdd | nesting, lists, comma decimals, CP1254; bad indentation, missing `=` |
| SoruBank | record layout, CP1254, overlong strings, truncation sweep |
| UserData | 271-byte profile round trip, truncated file, score key |
| Dfm | TPF0 values (int, string, set, binary, bool), glyph extraction, truncation sweep, bad PE |
| Rules / Quiz | penalty/bonus values, messages, report counters, school-bus reset quirk; quiz pass/fail rules, Ehliyet-only pool, no immediate repeat |
| Vehicle | input ramps and clamps, forward = +Z on ground, positive steer turns right (ODE car) |
| MapData | `data/maps.json` loads |
| RealData (needs `TTO_GAME_DIR`) | 71+79 questions, HG.vdd, all 178 real 3DS files parse, real TGIRISFORM/TFORMOYUN forms |

## Playthrough tests (need `TTO_GAME_DIR` and an X display; exit 77 = skipped)

`tests/run_playthrough.sh` runs the real binary with `--fixed-step --seed 3 --script`. Screenshots and logs go to
`build/playthrough/<name>/`.

| Script | Path |
|---|---|
| `talim_playthrough` | title → help → new driver → licence quiz (8 correct) → map select → loader → Eğitim Sahası → drive → sign question answered (+40) → 240 s time-out → report → ENTER → map continues |
| `talim_pass` | same start, then autopilot driving with sign answers until ≥ 150 points → F4 → "Tebrikler" menu question → unlock map 2 → report → Kasaba loads |
| `map1`..`map5` | each map loads and drives 8 s via autopilot |

Script commands are documented at the top of `App::load_script` (`src/app/app.cpp`). `autodrive` only presses
the arrow keys and drives through the normal physics and rules.

## Comparison with the original

The original was run under Wine with the same path (`work/runs/recover_verified/`, private). Its report text, HUD
layout and menu layout served as references. The fonts differ (Noto instead of MS Sans Serif), so pixel-exact
comparison is not done. Physics constants are copied from the disassembly; handling was not measured
frame by frame against the original, so feel can differ (open item in ROADMAP).

## Legal check before publishing

```sh
git ls-files | grep -iE '\.(exe|dll|msi|cab|3ds|smd|jpg|bmp|gif|wav|mp3|avi|mpg|dat|vdd)$'   # must print nothing
```
