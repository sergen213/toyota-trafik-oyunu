# Roadmap and progress

Milestones from goal.txt §14. Evidence is in brackets.

| # | Milestone | State |
|---|---|---|
| 1 | Identify and preserve | **Done** [GAME_IDENTITY.md, private `work/inventory/sha256.txt`] |
| 2 | Reproduce the failure | **Done** [FAILURE_ANALYSIS.md: IV41 codec → MCI error, CP1254, mode switch; H1–H3 tested] |
| 3 | Recover the original | **Done under Wine** [`scripts/wine_recover.sh`, verified menu → quiz → map 1 → report]. Real Windows 10/11 not tested (no machine). |
| 4 | Recover the data | **Done** [parsers for 3DS, SMD, VDD, SoruBank, UserData, DFM; all 178 real 3DS files parse] |
| 5 | Native foundation | **Done** [CMake/Ninja build, ctest] |
| 6 | Display actual content | **Done** [all 5 maps render original scenery/cars/signs; menus from the user's executable] |
| 7 | Playable gameplay | **Done for Eğitim Sahası** [playthrough tests: quiz → map 1 → pass with 150+ points → report → map 2] |
| 8 | Complete and preserve | **Partial**: see gaps below; no public release yet (needs owner approval) |

## Implemented since 0.1.0

Train and level crossings, school bus, emergency vehicles, wrong parking, AI spawner/culling (city maps and Otoban),
right-of-way and hospital-horn rules, pedestrian walk animation / tint / code-5 question, cutscene videos (FFmpeg),
music-list editor, joystick mode, billboards, water sound, wheels, Yaris driver poses, key panel on map 4, `t`/`y`,
AI turn bias in the lane function. Renderer: Vulkan backend with ray-traced shadows, FSR 3.1.4 upscaling and frame
generation; OpenGL kept as fallback.

## Remaining gaps and guesses

Marked [G] in code comments; none blocks play.

1. Right of way is a simplified right-hand rule (the full decision table of `0x5390d0` is only partly decoded).
2. Level-crossing post/boom placement, train start position (beyond node 22), school-bus `Dur` sign position,
   billboard panel offset and water-sound axis are not recovered exactly.
3. Emergency vehicles always run with the siren on (the switch-on site was not found).
4. The city spawner uses distance bands instead of the original camera-visibility test.
5. Interior camera (mode 3) offsets put the Yaris driver outside the field of view; mode 3 vs 1 at start unresolved.
6. Cutscene video plays continuously while its clips play (the original restarts the clip position per clip).
7. Handling has not been compared frame by frame with the original; four map-3 static boxes are still skipped.
8. Online score links open the defunct ToyotaSA URLs (the key is shown in the map 4 panel and the İletişim popup).

## Next concrete action

Runtime comparison against the Wine oracle for the [G] placements (crossing, bus sign, billboards).
