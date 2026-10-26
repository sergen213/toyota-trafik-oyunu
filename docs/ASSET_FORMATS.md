# Asset formats

Survey of the installed v2 folder (`Toyota Trafik Oyunu v2`, 1,268 files): 470 jpg, 257 wav, 178 3ds,
119 gif, 78 bmp, 67 Thumbs.db, 51 smd, 9 mp3, 6 skin, 6 mpg, 3 sfk, 2 vdd, 2 dat, 1 md3, 1 avi.
No archives, packers or compression: every asset is a loose file in a standard or simple format.
Paths inside the data are Windows-style and case-insensitive. `GameFs` (`src/core/fs.cpp`) resolves them on
Linux. All text is CP1254 (Turkish ANSI) and is converted with `cp1254_to_utf8`.

| Format | Where | Parser / reader | Tests |
|---|---|---|---|
| 3D Studio `.3ds` | `Bolumler/**` (map scenery, signs, lights), `Arabalar/**` (wheels, some vehicles), `Insanlar/**` | `src/formats/tds.cpp` | `test_tds.cpp` (synthetic quad; truncation sweep; bad chunk id; out-of-range face index), `test_realdata.cpp` (all 178 real files parse) |
| Half-Life SMD text `.smd` | `Arabalar/<car>/<car>.smd` (car bodies), pedestrians | `src/formats/smd.cpp` | `test_smd.cpp` (bones/frames/triangles; malformed lines) |
| HG.vdd key tree | `HG.vdd` (vehicle/pedestrian parameters, timings, colours) | `src/data/vdd.cpp` | `test_vdd.cpp` (nesting, lists, comma decimals, CP1254, bad indentation), `test_realdata.cpp` |
| SoruBank.dat | `Data/SoruBank.dat` (150 questions) | `src/data/sorubank.cpp` | `test_sorubank.cpp` (records, CP1254, overlong strings, truncation sweep), `test_realdata.cpp` (71 Ehliyet + 79 Trafik) |
| UserData.dat | `Data/UserData.dat` (profiles) | `src/data/userdata.cpp` | `test_userdata.cpp` (round trip, truncated file, score key) |
| JPEG / BMP / GIF | textures, menu art | `stb_image` (`src/render/image.cpp`) | covered by runtime loading |
| WAV (PCM 16-bit, 44.1/22.05 kHz) | `sesler/**`, `Arabalar/**/korna.wav`, engine loops | SDL3 `SDL_LoadWAV` (`src/platform/audio.cpp`) | runtime |
| MP3 (MPEG-1/2 layer III) | music (`sesler/*.mp3`, `playlist.m3u`) | libmpg123 (`src/platform/audio.cpp`) | runtime |
| AVI (Indeo 4.1) / MPEG-1 `.mpg` | `Video/` intro and car clips | not played natively (see ROADMAP) | — |
| `.skin` / `.md3` | `Arabalar/Teker/` | not parsed: wheel meshes used by the native game are `Arabalar/Teker/teker1..6.3DS`; the `.md3`/`.skin` pair appears to be an unused leftover (no reference found by `strings`; [L]) | — |
| `.sfk` | `sesler/ikinci_etabi_gecme/` | Sound Forge peak caches, not game data | — |

## 3DS (`.3ds`)
These are standard Autodesk chunk files: `u16 id, u32 length (including the 6-byte header)`. The reader handles:
- main `0x4D4D` → editor `0x3D3D`
- materials `0xAFFF`: name `0xA000`, diffuse `0xA020` (colour `0x0010`/`0x0011`), transparency `0xA050`, two-sided `0xA081`, texture `0xA200`/`0xA300`
- objects `0x4000` → triangle mesh `0x4100`: vertices `0x4110`, faces `0x4120` with material groups `0x4130` and smoothing `0x4150`, UVs `0x4140`.

Every chunk length is checked against its parent's end, and every face index against the vertex count. Any
violation throws `std::runtime_error` with the offset. Vertices stay in 3DS space (Z up). Converting to world
space (GLScene, Y up; scale and axis swap) is documented in `re/MAPS.md` §2.1.

## SMD (`.smd`)
This is the Valve studiomdl text format as read by GLScene `GLFileSMD`: `version 1`, `nodes` (`id "name" parent`),
`skeleton` (`time N` then `bone x y z rx ry rz`), and `triangles` (texture file line + 3 vertex lines
`bone x y z nx ny nz u v`). Car bodies use one bone and units of ~1/100 m (e.g. x = −100.7). The renderer scales
them with the per-car `HG.vdd` values. Errors report the line number.

## HG.vdd
This is a tab-indented tree of `Key=Value` lines. A line one tab deeper belongs to the previous key, and `=Value`
lines are anonymous list items (colour triples `r;g;b`). Numbers may use `,` or `.` as the decimal separator.
The game reads keys by `-`-separated paths, e.g. `Ayarlar-Harita1Sure` and `Arabalar-Rav4-Radius`. Top-level
sections are `demo`, `Insanlar`, `Arabalar` and `Ayarlar`. The key semantics are in `re/DRIVING.md` §5.

## SoruBank.dat
This is a Delphi `file of` record, **1310 bytes** per record, packed:

| Off | Type | Field |
|---|---|---|
| 0x000 | int32 LE | id |
| 0x004 | ShortString[255] | question |
| 0x104 / 0x204 / 0x304 | ShortString[255] | answers 1..3 (answer 1 is always the correct one) |
| 0x404 | ShortString[20] | category `Ehliyet` / `Trafik` |
| 0x419 | byte | group (sign-question point multiplier) |

A file size that is not a multiple of 1310, or a length byte larger than the field, is rejected.

## UserData.dat
This is a Delphi `file of TUser`, 271-byte records. The byte-exact layout is in `re/RULES.md` §2 (id, name,
surname, city as ShortString[50]; age; score at 0xA1; licence flag 0xA5; unlocked maps 0xA6; key
ShortString[100] at 0xAA). The native game keeps its own copy under `$XDG_DATA_HOME` and never writes into the
original install.

## Map layout (`data/maps.json`)
The map geometry (road graph, signs, lights, colliders, scenery placement) is created **in code** by the
original executable, not loaded from a data file. It was recovered by emulating the map builders (`re/MAPS.md`)
and is stored as project-owned numeric data in `data/maps.json`, loaded by `src/data/mapdata.cpp`. That file
references the user's 3DS files by path.
