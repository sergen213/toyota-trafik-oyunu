# WORLD — traffic lights, AI traffic, pedestrians, actors, environment (v2 exe)

Static analysis of `work/ghidra/TrafikOyunu_v2.exe`. Addresses are VAs in that file.
Confidence tags: **[C]** confirmed from asm, **[L]** likely (structure clear, a detail inferred), **[G]** guess.
Float literals are decoded from the binary (`work/re/WorldRE/consts.py`, extended x87 constants decoded too).
Throwaway scripts/dumps: `work/re/WorldRE/` (decompiles `d*.c`, `dis.txt`). Machine-readable summary: `work/re/world.json`.

Map index used everywhere (`TFormOyun+0x200`, `*0x553000`) **[C]** from the `Ayarlar-HaritaNSure` key each builder reads:
1 Talim/Eğitim Sahası (builder 0x52cd5c), 2 Kasaba (0x52928c→Kasaba.jpg, key Harita2Sure @0x528c44), 3 Şehir (key Harita3Sure @0x5066c0), 4 Şehir Merkezi (Harita4Sure @0x520fb4), 5 Otoban (Harita5Sure @0x52a610).

Time bases **[C]**: `*(double*)0x552fcc` = cadencer deltaTime (s), `*(double*)0x552fd4` = cadencer newTime (s); both written by `TFormOyun.GLCadencerProgress` 0x52e124..0x52e141 (`PTR 0x54a970`→dt, `PTR 0x54b1c4`→time). **Traffic-light phases use the wall clock `Now()` (0x40ac5c) instead** (see §1).

Per-frame world update order in `GLCadencerProgress` **[C]** (0x52e144..0x52e24e): 0x508f78/0x50912c (HUD frame quads) → if not paused (`0x508250()==0`) `IsikKontrol.Progress` 0x53db40 → `Canlilar.Progress` 0x53d294 → 0x5343ec … → map 5: Otoban spawner 0x52a90c → map 3: train 0x508afc → map 2: water sound 0x508ed4 → billboard scroll 0x5095b8.

---------------------------------------------------------------------------------------------------

## 1. Traffic lights

### 1.1 Classes **[C]**
| class | VMT | role |
|---|---|---|
| `TIsikKontrol` | 0x538178 | singleton "IsikKontrol" (global 0x552fe0, `PTR 0x54ad44`), created 0x5072d8. +0x30 = list of `TIsiklar`. `Progress` 0x53db40 calls every group's vmt+0x14 (0x53d920). |
| `TIsiklar` | 0x5380e4 | a **signal group**: +0x10 list of `TIsikTut`→`TEtkiIsik` (lights driven together), +0x14 `TStringList` of phase strings, +0x18 current phase index (int, starts 0), +0x20 double phase start (`Now`), init −1.0 (ctor 0x53d638). Add light 0x53d6d0. Group registered with controller by 0x53db14. |
| `TIsikTut` | 0x538054 | list item holding a `TEtkiIsik` (+8). |
| `TEtkiIsik` | 0x537958 | one signal head = a road **effect** (`TEtki`) attached to an intersection node. |

### 1.2 Phase strings and timing **[C]** (`TIsiklar.Progress` 0x53d920, apply 0x53d84c)
* Phase string format: `"RYG:<ms>"` — char[0] red, char[1] yellow, char[2] green, `'0'`=off, anything else on; duration = `StrToInt(Copy(s,5,MaxInt))` milliseconds.
* Algorithm each frame (only while not paused):
  ```
  if count(phases)==0: return
  if start == -1.0: start = Now(); apply(phase[idx])
  if StrToInt(phase[idx][5..]) < Round((Now()-start)*24*3600*1000):   // 0x5365a8: days→ms
      idx = (idx >= count-1) ? 0 : idx+1 ; start = Now(); apply(phase[idx])
  ```
  Consequence: during pause the group is not updated; after unpause at most one phase is skipped (wall clock).
* apply: for every light in the group, lamp 0xC (red)=char0, 0xD (yellow)=char1, 0xE (green)=char2 via 0x53bb10 / 0x53bb90 / 0x53bc10.
* Each lamp switch-on stores the cadencer time: red-on time `light+0x30`, yellow-on `+0x38`, green-on `+0x40` (doubles) and sets effect-flag bits `0x1000/0x2000/0x4000` in `light+0x18`.

Phase tables built by the map builders (all 4 signalled maps use identical timing) **[C]**:

| group role | phases (cycle 20 s) | Talim | Kasaba | Şehir | Ş.Merkezi |
|---|---|---|---|---|---|
| vehicles A (start red) | `100:8000`,`110:2000`,`001:8000`,`010:2000` | 0x55d428 | 0x559e3c | 0x552d04 | 0x55683c |
| vehicles B (start green) | `001:8000`,`010:2000`,`100:8000`,`110:2000` | 0x55d42c | 0x559e40 | 0x552d08 | 0x556840 |
| pedestrians ∥ A | `100:8000`,`100:2000`,`001:8000`,`100:2000` | – | 0x559e44 | 0x552d0c | 0x556844 |
| pedestrians ∥ B | `001:8000`,`100:2000`,`100:8000`,`100:2000` | – | 0x559e48 | 0x552d10 | 0x556848 |

Builders: Talim 0x52aefc (strings 0x52b644..), Kasaba 0x5218f4 (0x523850..), Şehir 0x4fed98 (0x500d90..), Ş.Merkezi 0x51912c (0x51b09c..). Otoban has no lights.
So red 8 s → red+amber 2 s → green 8 s → amber 2 s; B is the inverse; pedestrian heads are green exactly while the parallel vehicle group is green and red otherwise (no amber). All groups start in the same frame, hence stay in sync. In Talim every intersection of the 3×3 grid is signalled: group A = links along X, group B = links along Z **[C]** (loop indices at 0x52b031..0x52b60b; placement details are MapRE's).
Şehir also builds an extra group `0x54f964` containing the lights whose `i%5==2` but never registers it with IsikKontrol (static, open question).

### 1.3 Geometry **[C]** (ctor 0x53b3e0)
* `+0x4c` parent dummy (positioned by the builder from per-map tables; vmt+0x20 returns it), three child freeforms each loading `Bolumler\Diger\lamba.3ds`, scale 0.022 (×3):
  red `+0x50` at local (0, +0.28, 0.05), yellow `+0x54` (0, 0, 0.05), green `+0x58` (0, −0.28, 0.05).
* Material: diffuse `(0.1,0,0)`, `(0.1,0.1,0)`, `(0,0.1,0)`; when a lamp is on its emission is set to red `(1,0,0,1)`, yellow `(1,1,0,1)`, green `(0,1,0,1)`; off = `(0,0,0,1)` (0x53bb10..0x53bc8c).
* `Bolumler\Diger\isik.3DS` (pole) is loaded by the Kasaba/Ş.Merkezi builders (strings 0x518da4, 0x52156c) **[L]**.
* Effect placement 0x4fd438(cls, ownerNode, towardIdx, dist(+0x24), anyDir(+0x28)): owner = intersection node (`+0x14`), `+0x1c` = neighbour node of the approach link, lights use dist = **8.3 m** (`0x4104cccd`), stop line is 8.3 m before the intersection node.

### 1.4 Who obeys / `TEtkiIsik.Etkile` 0x53b6d8 **[C]**
Applied to every `TCisim` (cars and pedestrians) whose *origin* node equals `light+0x1c` (0x53a570) i.e. approaching the owner node from that neighbour. `remaining` = distance of the object to its target node (0x53b068).
* zone test 0x53a4dc(light,obj,range): `d = remaining`; returns `d−8.3` if `8.3 < d < 8.3+range`, else −1.
* If red **or** yellow is on:
  * `light+0x48==0` → emergency vehicle (kind 12/13) with siren on (`+0x11c`) ignores the light (0x539c34).
  * **AI** (not the player): `r = zone(range = speed+1.0)`; if `r ≥ 0`:
    * normal car (`obj+0xf0 == 0`): add light to obj stop list (0x53ac18); `limit = remaining−8.3` (0 when `remaining < 8.3+1.0`); `targetSpeed = min(targetSpeed, max(limit,0))`, `<0.01 → 0` (0x5394d4). I.e. target speed (km/h-units) ≤ metres left to the stop line.
    * **red-light violator** (`obj+0xf0 != 0`): keeps driving; when `zone(range=1.0) ≥ 0` sets violation code `obj+0xb0 = 1`, time `obj+0xa8`(double)=now, `obj+0xb4 = light`.
  * **player**: once per 2 s (`obj+0xb8`+2.0 < now) when `8.3−1.0 < remaining < 8.3` (just crossed the line): red on → `"Kırmızı Işıkta Geçtin (-10)"`, score −10, counter `*0x54ad48`++; yellow only and yellow on for > 1.0 s → `"Sarı Işıkta Geçtin (-3)"`, −3, `*0x54b204`++; light remembered in `obj+0x48`.
* Green on and player passes (`remaining < 8.3`, light not already `+0x44/+0x48`) → `*0x54af98`++, score +2, light stored in `obj+0x44`.
(Scoring is RulesRE's domain; listed so the light logic is complete.)

### 1.5 Menu icons
`\maps\lambakirmizi.bmp`, `lambasari.bmp`, `lambayesil.bmp` (0x515da0..) are used only by the menu/help form (`TGirisForm`) **[L]**. `Arabalar\lamba_*.jpg` are **car lamp** textures, not traffic lights (§2.10).

---------------------------------------------------------------------------------------------------

## 2. AI vehicles

### 2.1 Fleet **[C]** (factory 0x4fe0f0, tables 0x54a76c / 0x54a7ac)
16 `TAraba` objects are created once (`*0x54f8dc[16]`), `kind = index+1` stored at `car+0x211`:

| kind | model key (HG.vdd `Arabalar-<key>`) | plate / display name |
|---|---|---|
| 1 | Rav4 | Gezgin |
| 2 | CAMRY | Bilgin |
| 3 | YARIS | **Afacan** (player car) |
| 4 | SEDAN | Dost |
| 5 | 01_minnos | Minnoş |
| 6 | 02_osman | Haylaz |
| 7 | 03_kaptan | Kaptan |
| 8 | 04_katil | Katil |
| 9 | 05_mba | Düzenli Amca |
| 10 | 06_neseli | Neşeli |
| 11 | 07_yorgun | Yorgun |
| 12 | 12_ambulans | Ambulans |
| 13 | Polis | Polis |
| 14 | OkulTasiti | Okul Taşıtı |
| 15 | 13_albay | Albay |
| 16 | CiftTeker | Çift Teker |

Model path `Arabalar\<loc>.smd` (`loc` from HG.vdd). Idle pool = every car whose vmt+0x84 (IsActive) is false (0x4fe37c → list `*0x54f91c`); `0x4fe32c` returns a random pool member. Body colour for kinds 1..4: random entry of `Arabalar-<key>-Renkler` (0x544fa1, parser 0x539f00; empty list → random r,g,b = random(N)/1000); YARIS list is only `1;0;0` ⇒ player is always red **[C]**.

### 2.2 Spawning
* **Placement API** `0x544fdc(car, fromIdx, toIdx, distParam, cruise)` → `TAraba` vmt+0x88 = `PlaceOnRoad` 0x544cc4 **[C]**: link=from→to, `target=to`, `car+0x34 = cruise` (cruise speed, km/h-like units), `car+0x18=2.0`; `distParam` positions the car along the link (0x544c00: point = `distParam` from the *from* node **[L]**). Sets `Canlilar+0x50 = now`, adds car to active list `Canlilar+0x40` (0x53cf94).
* Pedestrian/vehicle limits: **active cars < `EtkinArabaSayisi`** (HG.vdd, default 11, var 0x549984 read at 0x506f3a); active pedestrians < **10** (constant 0x549988, no config key).
* **Talim** (0x52d430) **[C]**: player YARIS 2→5 (dist 20, cruise 60), AI Rav4 1→2, CAMRY 8→7, SEDAN 5→2, each dist 0, cruise `random(50)+25` (25..74). `Canlilar+0x6c=+0x6d=0` (no dynamic spawn/culling).
* **Otoban** (0x52a90c, every frame) **[C]**: `t = index(player.target)`, `p = index(player.origin)`; heading down (`t<p`): spawn from `t−6` to `t−5`, else from `t+6` to `t+5`; only if that link is empty (0x53bf00==0), node index in range, active<limit and a pool car exists; dist 0, cruise `random(10)+90` (90..99). Despawn: any non-player car *behind* the player (by node index along the heading) farther than **300** m → vmt+0x8c. Player start on Otoban 1→2, dist 0, cruise 90 (0x52a892).
* **Other maps (Kasaba/Şehir/Ş.Merkezi)**, `Canlilar+0x6c=1` **[L]**: each frame 0x53ce88 marks every camera-visible road link (`Kamera` visible list 0x532008/0x532014) with `link+0x50 = Canlilar+0x68` (= `random(2000)` re-rolled each frame) and visits the TAsfalt links branching left/right (node slot ±1/±3) off its ends (0x53ccd4). For such a link that is **not visible** (0x53cc08), if traffic may enter toward the visible end (one-way flags `link+0x5b/+0x5c`), no overtake in progress on it (`+0x5d==0`), active<limit and `Canlilar+0x50 + 0.3 s < now` (0x53c8c0): take a pool car, find a free slot at k·L−L/2 (k=2..4, L=car length) and place it heading toward the visible area with cruise `random(65)+25` (25..89).
* **Culling** (`TCisim.Progress` 0x53a78c, needs `Canlilar+0x6d`) **[L]**: an AI object on a TAsfalt link that is neither visible nor touched this frame becomes `frozen (+0x10)`; `TAraba.Progress` 0x5450d4 then recycles it (vmt+0x8c 0x54484c: unlinked, ODE disabled, parked far away at x=rand(3000)−1500, y=rand(4000)+1000, z=rand(3000)−1500, returned to pool).

### 2.3 Route following **[C]**
* Car state: `+0x28` current link, `+0x2c` target node, origin = other end (0x53a720), `+0x24` route queue of next nodes (0x53a6bc).
* Queue kept at ≥3 nodes (0x53ab54). Next node 0x538444(prev, cur): candidates = the far ends of every link at `cur`; pick uniformly at random, reject unless 0x53833c returns 1:
  6 no link, 7 link not drivable (`link+0x49==0`), node turn-restriction table (`node` list entries {from,to,code}) overrides, 4 = no-entry sign (class at 0x5480d0) toward it, 2 = same node, 3 = U-turn (back to prev), 5 = TAsfalt one-way against travel (`+0x5b`/`+0x5c`), 1 = OK. If nothing is OK the queue stops growing and the car ends with no target.
* Turn class 0x539584(prev, node, next) from neighbour slot indices 1..4: Δ=0→0xB (U), Δ=+1/−3→**8**, Δ=±2→**9** (straight), Δ=−1/+3/+4→**10**. (8 = one side, 10 = the other; with right-hand traffic 8 behaves as the near-side turn, 10 as the far-side turn — see lane offset.) **[L]** for which is left/right.
* Base progress 0x53a78c: every frame `targetSpeed(+0x38) = cruise(+0x34)`, then link effects and target-node effects (`Etkile`, vmt+0x18) clamp it; when the end of the link is reached the car moves onto `(target, queue[0])`.

### 2.4 Lane position, steering and speed control (AI branch of `TMotorlu.Progress` 0x5429a4) **[C]**
* Lane function `TKaraYolu` vmt+0x90 = 0x545cbc: `d = clamp(remaining, 10, 21)` (d>20→21, d<10→10); `k = (turn∈{8,10} && d<20) ? (20−d)/27 : 0`; turn 10 → `k = −2k`; `offset = linkWidth(+0x3c)/4 + k`; if `carWidth/2 > offset` → `offset = 2·offset − carWidth/2`. (TAsfalt default width 6.0 → lane centre 1.5 m right of the centre line.)
* Each frame `lateral(+0x1c) = Approach(lateral, laneFn(), 2.0·dt)` (`Approach` 0x536708 = move toward target by at most step). The steer target point is vmt+0x78; steering = angle to it (ArcCos of normalised dot, sign from cross, ×180/π) fed to the ODE car 0x4df750.
* Curve slow-down: if not overtaking and `targetSpeed > 5`: `targetSpeed = min(targetSpeed, max(10, targetSpeed − |steer|·(targetSpeed−1.5)))`, `steer` = ODE car `+0x23c` (−1..1).
* Acceleration `+0x40` is reset to **25** every frame by `Canlilar` (0x53d220). If current speed > target: `accel = max(accel, 100)` on Otoban, `max(accel, 50)` elsewhere. ODE car speed command = `Approach(speed, targetSpeed, accel·dt)` unless flag `+0x14 bit16` (parked) or global stop `*0x54ad10`.
* Following: `+0xf8 != 0` (blocked) → `targetSpeed ≤ max(ownSpeed/2, 0)`. Car ahead on same link (0x539bfc): if gap (`me.remaining − ahead.front`) < 25 and the car ahead is an unloading school bus → stop behind it.
* Brake lights (0x54101c): on when `(speed − target)/speed > 0.0025` or `speed < 2`. Turn signals: within 30 m of the node, signal toward the next route node (slot +1 → 0x542958, slot +3 → 0x542874); off when `remaining ≥ 30`. Signals blink with `Round(…) mod 2`.
* Otoban: lateral simply follows the lane function (no overtake state machine).

### 2.5 Overtaking (non-Otoban) **[C]**
Fields: `+0x100` slow car ahead (candidate), `+0xfc` car being overtaken, flag `0x20000` in `+0x14`.
Start when `+0xfc==0 && +0x100!=0 && 0x5398f8(self,cand)`: same link & target; candidate parked (flag 0x10000) **or** (candidate remaining > 40, candidate speed + 10 < link speed (0x5398a0), my remaining > 4) **and** no other moving car in the look-ahead list (0x538610). Then `link+0x5d=1`, set flag, `cand.overtakenBy=self` (0x545dd0) and **honk** (0x5412a4: horn sound volume −100·dist).
While overtaking: signal (0x542958) on, `+0x118 = now`; while the other car is still ahead (`other.remaining > my.front`) `lateral = −linkWidth/4` (opposite lane), else return to lane via the lane function with the other signal (0x542874). End when 0x539a50 (my rear passed its front + margin, or remaining < 20): signals off, `link+0x5d=0`, clear flag. Signals auto-off 2 s after `+0x118`.

### 2.6 Deliberate AI violations (things the player can report) **[C]**
Generator in `Canlilar.Progress` 0x53d294, only when the camera target (player) is a `TAraba` and `now > Canlilar+0x60 + 15.0`:
* `r = random(3)`; `r==2` → `Canlilar+0x30 = 1` (arm a **wrong-parking** car for the next PlaceOnRoad);
* else take the car directly ahead of the player on its link (0x539bfc) if it has no violation (`+0xb0==0`) and is not kind 12/13/14: `Canlilar+0x60 = now`; `r==0` → `car+0xf0 = 1` (**will run the next red/yellow**, code 1 set in §1.4); `r==1` → 0x53b258 **litter**: spawns a `TCop` (ODE box, class 0x506aac) at the car, code `+0xb0 = 2`, time `+0xa8`.
* Wrong parking (in PlaceOnRoad, only map≠5, not player, `Canlilar+0x30`, not kind 12/13/14, not already signalling): `random(100) > 50` → park in free slot `Round(linkLen/distParam)`∈1..3 (`link+0x10+4·slot`), lateral −2.7 m; else if the target node holds a `TLParkYasak` sign for this approach → park at lateral −0.5 m. 0x544a3c then sets flag 0x10000 (parked), `+0xb0 = 3`, `Canlilar+0x30 = 0`, `Canlilar+0x60 = now`. A parked car overlapping any `TLevha` is un-parked (0x544a90). Parked cars keep ODE disabled/static (0x4e4a4c).
* Codes → question text built by 0x532c5c: 1 `Bu araç ışık ihlali yaptı.`, 2 `Bu araç etrafı kirletiyor.`, 3 `Bu araç yanlış park etmiş.`, 5 `Bu yaya kırmızı ışıkta geçti.` (wrong answers `Bu aracın son muayene süresi geçmiş.`, `Aracın farları kırık.`, `Bu yaya karşıdan karşıya geçerken koşuyordu.`, `Yaya etrafa çöp attı.`). Scoring/UI = RulesRE.

### 2.7 Emergency vehicles **[C]**
* Kind 12 ambulance: loop sound `MotorSes`=`Arabalar\ambulans.wav`; kind 13 police: `Arabalar\polis.wav`; others `Arabalar\motor.wav` (0x53e87d..0x53e95f, key default via 0x4e7720). Base frequency 11025 Hz.
* Beacons: kind 12 two lights `+0x1d8,+0x1dc`; kind 13 three `+0x1e4,+0x1e8,+0x1ec` (0x544254). Siren flag `+0x11c` (0x544644, also sets `+0xe8` = 101 ambulance / 100 police); when on, beacons alternate each frame parity `Round(x) mod 2` (0x543e40) **[L]** and the siren sound plays; siren-on emergency cars ignore lights (§1.4). Emergency cars never become violators/parkers.

### 2.8 School bus (kind 14) **[C]** (0x5434f8..0x5436ea)
* Not on Otoban. When not overtaking/stopping, `remaining > 40`, `front-distance > 10`, a new link, and `random(100)==50` (≈1 %/frame): set flag `0x80000` ("öğrenci indiriyor"), `+0x134=link`, `+0x12c=−1`, `+0x130=8.0` s, `+0x128=0`.
* While flagged and `remaining ≥ 8`: pull right `lateral = Approach(lateral, 2.2, dt)` until `lateral ≥ 2.0`, then stopped (`+0x128=1`, `+0x12c=now`); target speed `Approach(speed,0,10·dt)`; `Dur` sign (`+0x120`, `Arabalar\OkulTasiti\Dur.3ds` + `Dur.bmp`) emission `(2,0,0,1)`; after 8 s or when `remaining < 8` clear flag. Not flagged → emission `(0,0,0,1)`.
* Others: AI stops behind it (§2.4); player overtaking it → `"Öğrenci İndiren Okul Taşıtını Solladın (-7)"` −7, or warning `"Dikkat okul taşıtı öğrenci indirirken sollanamaz!"` (0x542a55..).

### 2.9 Engine / horn sound **[C]** (end of 0x5429a4)
* Camera distance > **45** → stop engine sound.
* Non-emergency: gear offset `g = speed>40 ? 40 : speed>20 ? 20 : 0`; frequency `Round((speed−g)·300 + 11500)` Hz.
* Volume (1/100 dB): player `−200·max(dist,6)`, others `−100·dist`. Plays when not playing and (non-emergency or siren on).
* Horn `KornaSes` per car (HG.vdd, default `Arabalar\korna.wav`), object `+0x1b4`.

### 2.10 Car lamps and LOD **[C]**
* Lamp materials found by texture name in the car's libraries (0x53dc38): `lamba_beyaz(.jpg)`/`_sol` reverse, `lamba_kirmizi`/`_sol` brake, `lamba_sari`/`_sol` indicators (`Arabalar\` and per-car `Kaliteli/Normal`).
* Two material libraries per car: `+0x17c` "Kaliteli" (paths `Arabalar\Teker\;Arabalar\Sinyaller\;Arabalar\<loc>\Kaliteli\;Bolumler\Diger\` + `Arabalar\Logo\K` or `Arabalar\Normal\K`) and `+0x180` "Normal" (…`\Normal\` + `Logo\N`/`Normal\N`). The `Logo` folder is chosen by HG.vdd `Logo` (default true, 0x50a1ec) — `toyota_logo.jpg`.
* **`KapUzaklik`** (HG.vdd, default 15, float at 0x54d4f8, read 0x506f59): body actor `+0x188` uses the Kaliteli library when camera distance ≤ KapUzaklik, else Normal (0x542a07..0x542a4b). It is a *texture LOD* distance, nothing to do with doors.

---------------------------------------------------------------------------------------------------

## 3. Pedestrians

### 3.1 Roster **[C]** (factory 0x4fe3f4, 15 `TInsan` in `*0x54f924[15]`, pool `*0x54f920`)
| i | type `+0x120` | colour idx `+0x124` | model |
|---|---|---|---|
| 1..5 | 1 | i | `Insanlar\a<i>.smd` |
| 6..10 | 2 | i−5 | `Insanlar\k<i−5>.smd` |
| 11..15 | 3 | i−10 | `Insanlar\ak<i−10>.smd` |

* Actor `+0x114` = `TGLActorTrafik` (owner back-pointer at actor+0x120 for mouse picking), material lib `+0x10c` path `Insanlar\`, AnimationMode 2.
* Walk animation `Insanlar\yuru.smd` (types 1,3) or `Insanlar\yuruk.smd` (type 2) (0x546963).
* Clothes tint from HG.vdd `Insanlar-<type>-Renkler` entry `[idx−1]` (fallback random list entry/RGB): applied to materials whose texture is `ceket.jpg` (type 1), `gomlek.jpg` (type 2), `kazak.jpg` (type 3) (0x5467af..0x54692c) **[L]** for exact texture↔type pairing.

### 3.2 Movement **[C]** (`TYaya.Progress` 0x545a84; place 0x546bbc)
* Live on sidewalk (`TKaldirim`) and crossing links; cruise `+0x34 = 4.0`, lateral always 0.8 (0x545a70).
* `onCrossing(+0x108) = link+0x48 && distFromStart > 2.8 && distAlong > 2.8`; model Y = 0.6 on the road part, 0.7 on kerb.
* Stops (`targetSpeed=0`) if the pedestrian ahead on the same link/target is closer than 0.7.
* Walk speed `+0xf8 = Approach(+0xf8, targetSpeed, 10·dt)`; moves forward speed·dt; actor frame interval `max(30, Round(0.4/dt·3/speed))`; speed 0 → animation stopped (frame 0), else looping.
* Lights: pedestrian heads are ordinary `TEtkiIsik` on crossing approaches; pedestrians obey them exactly like AI cars (§1.4); no code path sets `+0xf0` for pedestrians.
* Traffic gap (`TEtkiKesisme`, pairs made by 0x4fd510 linking a road link and a crossing link): on the **crossing** side 0x5457a8 makes the pedestrian wait (`targetSpeed=0`) if its next position overlaps a car; if a car (the player) is already on its spot → `…(-17)` score −17, counter `*0x54b108`. On the **road** side 0x546cfc: zone range `speed + crossingWidth/2 + 2`; if any pedestrian is on the road part: AI `targetSpeed ≤ max(d−2,0)`; player gets `"Dikkat Yayaya Öncelik Tanı"` and every 5th time `sesler\yaya.wav` (0x509304).
* Spawning 0x53c780 (per off-screen sidewalk adjacent to visible links, ≥0.2 s apart, link occupancy < random(3)+1, active < 10): start position `max(0, random(Round(len)) − len/5)`. Initial fill at load 0x53a054 around the player (radius 5..1000).

### 3.3 Violation code
Every `TYaya` is created with `+0xb0 = 5` (0x5459ce) and recycled to 0 (0x546b0c, vmt+0x8c). See open questions.

---------------------------------------------------------------------------------------------------

## 4. Animated actors

* `TGLActorTrafik` (VMT 0x5369d4, size 292) = `TGLActor` + owner pointer `+0x120` used by picking (`Ehliyet_MouseDown`).
* **Driver** (player YARIS only; created in TMotorlu load 0x53f5e7..0x53fb8f) **[C]**: actor `+0x18c`, base mesh `Bolumler\Diger\Anim\ref.smd`, local position (0.04, −0.5, −0.33), scale 0.01, FrameInterval 25, mode loop, animations added from `Bolumler\Diger\Anim\`: `surus, konus, kafa_geri_bak, kafa_ileri_bak, radyo, sag_03, sag_06, sag_09, sag_12, sag_15, sola_03 … sola_15`; attached to `TFormOyun.AnimationControler1` with `radyo`.
* Steering animation (only when camera mode `Kamera+0xd4 == 3` (in-car), not paused) **[C]**, s = ODE steer `+0x23c`:
  `[-0.1,0.1]→surus`, `(0.1,0.3]→sag_03`, `(0.3,0.5]→sag_06`, `(0.5,0.7]→sag_09`, `(0.7,0.9]→sag_12`, `>0.9→sag_15`, `[-0.3,-0.1)→sola_03`, `[-0.5,-0.3)→sola_06`, `[-0.7,-0.5)→sola_09`, `[-0.9,-0.7)→sola_12`, `<-0.9→sola_15`; switch (smoothly) only if the current animation name differs (0x542cb0..0x5432b3).
* `kafa_geri_bak` / `kafa_ileri_bak` are triggered from `TFormOyun` (strings 0x52f560/0x52f58c; look-back key) and `konus`/`radyo` from `ActorAnimEndFrameReached` 0x52f2b4 / radio key `m` **[L]**.
* Car interior extras: `Bolumler\Diger\araba.3ds`, `koltuk.3ds` (0x5406a4/0x5406c8).

---------------------------------------------------------------------------------------------------

## 5. Environment

### 5.1 Train + level crossing (map 3 Şehir only) **[C]** (0x508afc; init 0x507569, 0x5089f0, 0x508a40)
* Actor `Arabalar\tren.smd` (lib `MLTren`, path `Arabalar\`), scale 0.01, under dummy `*0x553014`.
* Idle → when `now > lastEnd(0x553008) + 20 s`: place the dummy on the line node17→node22 at −208 along it (0x536284), face node 2, Y=1.15, visible, speed `v = random(60)+60` (units/s).
* Running: move `v·dt`; when `train.z − 28 < node2.z − 150` → stop, hide, `lastEnd = now`.
* Sound `sesler\Tren.wav` (loop, 11025 Hz): `d = max(4, XZ distance camera→train segment [z−28, z+28])`; `d > 90` stop, else volume `−60·d`.
* Every `TDingDing` (list `*0x54acf8`) is **closed** while `train.z − 28 − 2v < crossing.z ≤ train.z + 28`, else open (0x50a0b8).
* `TDingDing` (VMT 0x506c7c; ctor 0x5096e8): models `Bolumler\Diger\DingDing.3ds` (post) and `DingDing2.3ds` + `DingDing2.JPG` (boom), sound `sesler\DingDing.wav`; 3 linked `TEtkiIsik` (+0xd8/+0xdc/+0xe0). Closing: lights red on/green off, boom rotates at 60°/s to 0°, then an ODE blocking box is created (0x509ce0; a player caught inside is reset to node 0→1 with message `"Bunu Tekrarlama !!!"`). Opening: lights green, boom rotates 50°/s to −70°, box freed. Bell audible ≤ 45 m, volume −50·dist (min dist 3), plays while the boom moves.

### 5.2 Water sound (map 2 Kasaba) **[C]** (0x508ed4; init 0x508dc8)
`sesler\Su.wav` loop; `d = max(4, distance of camera from a fixed line near the origin (0x536800 with constants ±5, uses camera z only))`; `d > 50` stop else volume `−60·d`. Exact axis **[G]** (≈ |camera.z|).

### 5.3 Billboards (Otoban) **[C]** (create 0x509388 ×5 from 0x529a65.., scroll 0x5095b8)
* `bolumler\diger\billboard.smd` (lib `MLBillBoard`, 2 materials from code 0x5071f7/0x507230 with texture offset), placed at the midpoint of two given points facing the road, random texture `1` or `2`.
* Scroll: state hold/scroll; scroll `offset.y += dt/6` until `Trunc(offset·2+1)` changes (one ad = half the texture), then hold 3 s; applied to texture offset Y of 3 materials.

### 5.4 Trees
`0x508800` (called by Kasaba/Şehir/Ş.Merkezi builders): for i=1..4, with 75 % (`random(100)>24`) a `TGLAgac` (lib `MLAgaclar`, `Bolumler\Diger\Agac1..4.bmp`, `Cicek1.bmp`) at `len/i + random(5)` along the link clamped to [3, len−3] **[L]**.

### 5.5 Sky, fog, lighting
* `TFormOyun.GLScene` has `GLSkyBox` (lib `GLMLBulutlar`, materials Ust/Alt/Bati/Dogu/Kuzey/Guney, `Style=sbsTopHalfClamped`, `CloudsPlaneOffset=0.2`, `CloudsPlaneSize=32`) and a hidden `GLEarthSkyDome` (SunElevation 75, Turbidity 15, 48×24) (TFORMOYUN.dfm).
* 0x506efc: if HG.vdd `Ayarlar-Bulutlar` (default **1**) → load skybox textures `Bolumler\Diger\GokYuzu\{Bati,Kuzey,Dogu,Guney,Ust,Alt}.jpg` into materials 0..5 and free the sky dome; else free the skybox and show the dome. The `GokYuzu` dir **exists** in both installs but file names are lower-case (`bati.jpg` …) — needs case-insensitive lookup on Linux.
* No fog: no fog property in the DFM and no reference to `TGLFogEnvironment` setters from game code (glFog only imported by GLScene) **[L]**. No explicit light sources created by game code were found **[G]**; lighting comes from the map `.3ds` files / GLScene defaults (DriveRE/MapRE may confirm).
* Viewer: `GLFullScreenViewer` 800×600, `AntiAliasing=aa2xHQ`.
* ODE world (0x5070f7): gravity (0,−10,0), step params 0.2/0.05/0.001, 20 iterations (raw values: 10.0, 7.0, 0x14, 0.2, 0.05, 0.001) **[L]** for which is which.
* Collision sound: `CarpSesi` 0x52fd98 → 0x508924 plays `sesler\crash<random(12)+1>.wav` (strings 0x5089d0/0x5089e8) if not already playing **[C]**.

### 5.6 Quality / v1 vs v2
* v2 (this exe): texture LOD by `KapUzaklik`, `billboard.smd`, pedestrian colours, car lamp textures, `Logo`/`demo` keys.
* v1 exe (`Kurulum1`, 2,209,280 bytes) has no `KapUzaklik`, `billboard`, `Insanlar-`, `lamba_*` strings but contains `_l.smd`; its data has `Arabalar/{camry,rav4,sedan,yaris}/*_l.smd` → v1 uses low-poly model LOD instead of texture LOD **[L]** (v1 not disassembled). v1 HG.vdd has only `Logo=True` + car blocks.

---------------------------------------------------------------------------------------------------

## 6. Open questions
1. Pedestrians get violation code 5 at creation and 0 after recycling; is any pedestrian ever *deliberately* crossing on red? No writer of `TYaya+0xf0` exists. Is code 5 only meaningful together with a time window in the reporting code (RulesRE)?
2. Turn class 8 vs 10 ↔ left/right depends on node slot orientation (MapRE: which neighbour slot is N/E/S/W).
3. Exact semantics of `distParam` in PlaceOnRoad (distance from origin vs before target) — the Talim AI uses 0.
4. Units of `cruise`/speed (`+0x34`, vmt+0x30): assumed the same km/h shown on the HUD; DriveRE should confirm the ODE car speed scale.
5. Şehir extra light group `0x54f964` (lights with `i%5==2`) is never registered/phased — used by something else?
6. Which light objects set `TEtkiIsik+0x48` (force even emergency vehicles to stop)? Probably DingDing lights.
7. Water sound line orientation (0x536800 argument order).

## 7. Runtime checks requested (parent's Wine harness)
* Time one full light cycle at a Talim intersection: expect 8 s red, 2 s red+amber, 8 s green, 2 s amber; perpendicular approach inverted.
* Pause (Space) for >8 s during a red phase and verify the light jumps exactly one phase on resume.
* Count simultaneous AI cars in Kasaba/Şehir (≤11) and pedestrians (≤10); confirm they vanish when leaving view.
* In Şehir watch the train cadence (≥20 s gap) and the boom (raised ≈ −70°).
* Approach the school bus: unloading lasts 8 s with the red `Dur` sign lit.
