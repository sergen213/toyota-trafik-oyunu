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

## Known gaps (not implemented)

Evidence for most of these exists in `re/WORLD.md` / `re/RULES.md`; they are listed by priority.

1. Pedestrians (`TYaya`/`TInsan`): walking, pedestrian hit −17, pedestrian red-light violator question (code 5),
   "Dikkat Yayaya Öncelik Tanı" message. Pedestrian meshes and textures already resolve.
2. Map-specific actors: train and level crossing (map 3, `DingDing`), school bus stop arms and the overtaking rule
   (−7), ambulance/police sirens, wrong-parking violator (code 3; needs a car re-placement).
3. Right-of-way rule (−4) and the hospital-zone horn rule (−5).
4. Cutscene videos (`Video/*.mpg`, intro `cancan.avi`) — needs an FFmpeg-based player; speech clips do play.
5. Exact AI lane/turn bias, tailgating distance gate (6 m chosen) and green-light credit geometry are marked [L]/[G]
   in the RE notes.
6. Camera: only the chase camera was observed under Wine at map start, so the native port starts in mode 1. Code
   reading says mode 3 (interior) — unresolved.
7. Music-list editor (TFormDiger), joystick mode, online score links (the sites are defunct; the key is shown in
   the İletişim popup).
8. Handling has not been compared frame by frame with the original.
9. Four static collision boxes on map 3 whose size the emulator did not recover are skipped.

## Next concrete action

Implement pedestrians (gap 1) from `re/WORLD.md` §3, then the map 3 train.
