# Maps, road graph and world-object placement

Scope: the five map builders of `TrafikOyunu_v2.exe`: road graph, signs, traffic-light placement, colliders, scenery meshes, and player/AI start slots.
Data: `work/re/maps.json`.
WorldRE owns traffic-light phase logic, the AI spawner, trains, `TDingDing`, and pedestrians. RulesRE owns violations and scoring. DriveRE owns physics.

Confidence tags: **[C]** confirmed by disassembly or emulation, **[L]** likely, **[G]** guess.

## 0. How the data was obtained

Static analysis is followed by partial emulation of each builder. No game run was needed.

- `work/re/MapRE/emu.py` loads the PE into unicorn (`work/re/MapRE/venv`). It runs the builder *natively*, including the graph and effect classes (`THarita`/`TNokta`/`TBaglanti`/`TAsfalt`/`TKaldirim`/`TEtki*`) and the Delphi RTL/`TList`/`VectorGeometry` code.
  - GLScene, VCL and ODE calls are stubbed. Created scene objects become tracked fakes whose Position/Direction/Up/Scale/size setters are recorded.
  - `TGLBaseSceneObject.SetTurnAngle` (0x485134) and `TGLCoordinates.SetVector` (0x47b508) run natively.
  - `Random()` (0x403308) returns 0 and logs its call site.
  - Run: `work/re/MapRE/venv/bin/python work/re/MapRE/emu.py <builder> <placement> > work/re/MapRE/emu_<builder>.json`
- `work/re/MapRE/build_maps.py` composes world transforms by walking the parent chains and writes `work/re/maps.json`.
- Every Talim value below was also cross-checked by hand against `objdump` at the addresses cited.

## 1. Map index → builder [C]

The map index `TFormOyun+0x200` is dispatched by the jump table at 0x52dddb (in `DataModuleCreate`). The placement table is at 0x52fa2d, run when the map starts. `[0x553000]` holds the *previous* map index and is written after placement (0x52fa66).

| idx | name (loader text) | builder | placement | sehir dir | loader image | time key (default) | speed limit `[0x55de78]` |
|---|---|---|---|---|---|---|---|
| 1 | Eğitim Sahası (Talim) | 0x52cd5c | 0x52d430 | `IlkHarita\sehir4` | `Bolumler\Diger\Talim.jpg` | `Ayarlar-Harita1Sure` (240) | 50 |
| 2 | Kasaba | 0x527524 | 0x528de0 | `sehir2` | `Kasaba.jpg` | `Harita2Sure` (600) | 50 |
| 3 | Şehir | 0x504eac | 0x506888 | `sehir3` | `Sehir.jpg` | `Harita3Sure` (600) | 50 |
| 4 | Şehir Merkezi | 0x51f80c | 0x52115c | `sehir1` | `SehirMerkezi.jpg` | `Harita4Sure` (600) | 50 |
| 5 | Otoban | 0x52928c | 0x52a87c | `sehir5` | `Otoban.jpg` | `Harita5Sure` (600) | 90 |

- The config read is `0x4e77a4(cfg, key, default)`; the default sits in ECX. Talim stores the float at `[0x54aaa8]` (0x52cdc6).
- Each builder writes the speed limit (`0x42480000` = 50.0, Otoban `0x42b40000` = 90.0) through `[0x54ab74]` → `0x55de78`. It is read at 0x54644b: a speed above `limit + limit/10` triggers a −9 point penalty. That penalty is RulesRE's area.

## 2. Units and axes [C]

- GLScene is Y-up and right-handed. One unit is one metre: roads are 6 wide, sign plates 0.45×0.7, lamp heads about 3.4 m high.
- Graph coordinates (`TNokta` x,y,z) are **world coordinates**. Every node has y = 0.
- "Right of travel" for heading `f` is `f × (0,1,0)`. Heading +Z → right = −X; heading −X → right = −Z. Traffic drives on the right.
- Every builder parents its decor under two `TGLDummyCube`s, created in Talim by 0x52b67c.
  - `D_vis` `[0x55a1a0]` at (3, −0.22, 3) carries lights, scenery and tiles.
  - `D_col` `[0x55a19c]` at (3, 0.5, 3) carries the ODE colliders.
  - Both are children of `[0x54ac74]`. Their children's local coordinates are pre-compensated, so world positions line up with the graph. `maps.json` holds world values only.

### 2.1 3DS file → world [C]

All map meshes are `TGLFreeForm` (or `TGLOXStaMesh`) objects with Scale = 0.01 and Direction = (0,1,0), loaded unchanged. In GLScene's local matrix the rows are X = Up×Dir, Y = Up and Z = Dir. Setting Direction to +Y forces Up to (0,0,−1). So for a raw 3DS vertex (x, y, z), which is Z-up:

```
world = origin + 0.01 * ( x, z, -y )
```

`origin` is (3, 0.28, 3) for Talim scenery and the city tiles (`D_vis` + local y 0.5). City dag/deniz/orman use (3, 0.10, 3); Şehir Merkezi uses (3, 0.37, 3), with tiles at (3, 0.28, 2.5). The exact origin per mesh is in `maps.json.meshes[]`.

Example: Kasaba tile a1 has raw X −13410..−6990 and Y 11180..21820. That maps to world X −131.1..−66.9 and Z −215.2..−108.8, exactly the block between graph nodes x ∈ {−132, −66} and z ∈ {−216, −108}.

The `+Y` up-vector choice is confirmed empirically: only `(x, z, −y)` centres all 16 tiles on the graph blocks. A mirrored X would put the blocks at x = ±105.

Tile naming: rows a..d run from z = −216 to +216 (a = most negative z). Columns 1..4 run from x = −132 to +132.

## 3. Road-graph classes and API

### 3.1 Class layouts

The class-reference variable sits at VMT − 0x4C: `[0x5379b8]` = TAsfalt, `[0x537a48]` = TKaldirim, `[0x537ae0]` = TEtkiKesisme, `[0x53790c]` = TEtkiIsik, `[0x5374e8]` = TLevha, `[0x537658]` = TLParkYasak, `[0x537b94]` = TNokta, `[0x537864]` = TBaglanti.

| class (size) | field | meaning |
|---|---|---|
| THarita (56) | +0x30 | node list (`TNoktalar`); `0x53c674(h,i)` gets item i, `0x53c688` gives the count [C] |
| | +0x34 | connection list (`TBaglantiListesi`); `0x53c668(h,i)` gets item i, `0x53c694` gives the count [C] |
| TParca (all) | +4 | owning collection; `0x53c3e4(n)` → `n+4 → +8` = THarita [C] |
| | +8 | Name (AnsiString); "" for nodes, "Isik" for lights [C] |
| TZemin (20) | +0x10 | list of `TEtki`s attached to this node or connection (`0x53a1fc` adds) [C] |
| TNokta (64) | +0x14 | list of `TBagParca` connection ends [C] |
| | +0x1c/+0x20/+0x24 | x, y, z [C] |
| | +0x28/+0x2c/+0x30/+0x34 | neighbour slots 1..4 (`0x53c430(n,k,node)` writes slot k at `+0x24+4k`) [C] |
| TBaglanti (88) | +0x20 | list of vehicles currently on it; `0x53bc90` adds and sets `car+0x28` [C] |
| | +0x24 / +0x28 | node A / node B [C] |
| | +0x2c / +0x30 | `TBagParca` ends, put into B's and A's +0x14 lists [C] |
| | +0x34 | per-tick sorted vehicle array (`0x53bce0`) [L] |
| | +0x38 | length = \|A−B\| (`0x53bf58` → `0x536064`) [C] |
| | +0x3c | width (base default 2.0) [C] |
| | +0x48 | byte: TKaldirim *crossing* flag [C] |
| | +0x49 | byte = 1 [C] |
| | +0x4c | int = −1; purpose unknown |
| TAsfalt (104) | +0x3c | width; ctor default 6.0, then `float(arg3)` [C] |
| | +0x5b | A→B allowed [C] |
| | +0x5c | B→A allowed [C] |
| | +0x60 / +0x64 | adjacent TKaldirim (left/right), linked by `0x53c584` [L] |
| TKaldirim (92) | +0x3c | width = 2.0 [C] |
| | +0x48 | crossing flag (arg3 of `0x4fd304`) [C] |
| | +0x50 | −1.0 on block-perimeter walks (written by the builders), else 0 [C] |
| | +0x58 | owning TAsfalt [C] |
| TEtki (44) | +0x14 | zemin (node or connection) it is attached to [C] |
| | +0x1c | "toward" node [C] |
| | +0x24 | distance (float) [C] |
| | +0x28 | byte flag [C] |
| TLevha (88) | +0x2c | `node(p4)`; always node 0 in the shipped maps, purpose unknown [C] |
| | +0x34 | texture name `Lnn` [C] |
| | +0x38 / +0x3c | front / back TGLPlane [C] |
| | +0x40 | root TGLDummyCube [C] |
| | +0x44 | pole (TGLCube) [C] |
| | +0x54 | byte: left side [C] |
| TEtkiIsik (104) | +0x4c | head dummy [C] |
| | +0x50 / +0x54 / +0x58 | red / yellow / green `lamba.3ds` [C] |
| TEtkiKesisme (48) | +0x2c | the crossing connection [C] |

Wrong-way check [C] (0x539bb8 / 0x538410, used by vehicles): moving toward B needs `+0x5b ≠ 0`; moving toward A needs `+0x5c ≠ 0`. With both flags 0 the road is closed to motor traffic. Closed roads: Şehir 2–7, 7–12, 12–17, 17–22 (the railway column x = 0); Kasaba 10–11, 11–12, 12–13, 13–14 (the z = 0 row).

`TEtki.GetNode` (0x53a3dc) [C]: for a node-attached effect it returns that node. For a connection-attached one it returns the end that is *not* `+0x1c`.

### 3.2 Builder API [C]

Delphi register convention applies. Floats are pushed left-to-right.

| addr | signature | effect |
|---|---|---|
| 0x53c6a0 | `AddNode(harita, name:string, x, y, z:Single)` | appends a `TNokta`; index = insertion order; name is always `''` |
| 0x508110 | `Node(i)` | `harita.node[i]`, with harita = `[0x552fdc]` |
| 0x53bfc8 | `FindConn(nA, nB)` | connection between two nodes, or nil |
| 0x4fd2b4 | `Road(a, b, w:int)` | `TAsfalt` A=a, B=b, width = w (6 everywhere); length computed; both directions allowed |
| 0x4fd304 | `Walk(a, b, crossing:byte)` | `TKaldirim`, width 2.0 |
| 0x4fd354 | `SetNeighbours(n, p2, p3, p4, p5)` | slot1 = p2, slot2 = p3, **slot3 = p5, slot4 = p4**; −1 = none |
| 0x4fd3ec | `OneWay(p, q)` | on conn(p,q): clears +0x5b if p == A, else +0x5c, so travel *from p to q* is forbidden |
| 0x4fd438 | `AttachEtki(cls, zemin=EDX, toward=ECX idx, push dist:Single, push flag:byte)` | creates the effect, attaches it to zemin, calls vmt+0x1c (scene parent `[0x54ac74]`) and vmt+0x28 (place) |
| 0x4fd770 | `Sign(cls, zemin, toward, push p4:int, push dist:Single, push tex:string, push b28, push left)` | `AttachEtki` plus `+0x2c = node(p4)`, `+0x34 = tex`, `+0x54 = left`, then vmt+0x24 (material) and vmt+0x28 (place) |
| 0x4fd7f8 | `NoParking(a, b)` | two `TLParkYasak` (`L19` texture string at 0x4fd88c): on node a toward b and node b toward a, dist = len − 20 |
| 0x4fd510 | `Crossing(a, b, c, d, w:int)` | `TEtkiKesisme` on road(a,b) toward b, linked to walk(c,d); and on walk(c,d) toward d, linked to road(a,b); dist = w (6) |
| 0x53c584 | `LinkSidewalks(harita)` | for each TAsfalt, uses its crossings (via 0x4fd5fc) to set +0x60/+0x64 |
| 0x508800 | `Trees(...)` | for the last connection: 4 tries, each 75 % (`Random(100) ≥ 25`), a `TGLAgac` sprite (texture `"Agac"+(Random(4)+1)`) at `d = Random(5) + len/k`, clamped to [3, len−3]; **random per run** |
| 0x544fdc | `PlaceCar(car, a, b, push dist:Single, push speed:Single)` | car vmt+0x88 (0x544cc4): on conn(a,b), at `dist` from a, offset `width/4` to the right of a→b, heading a→b [L]; `car+0x34 = speed` [L: cruise or limit speed, km/h] |

Neighbour slots are in rotational order. In the Talim and city grids, slot1 = −Z, slot2 = +X, slot3 = +Z, slot4 = −X, which is clockwise seen from above. `0x53c438` steps through them with a wrap-around (`0x536788(1,4,…)`) to pick left, straight or right [L].

## 4. Signs (`TLevha` and subclasses)

Geometry: `TLevha` ctor 0x546ef8, material 0x547078, place 0x5470c0 [C].

- **Root** `TGLDummyCube`, parent `[0x54aa7c]` (world).
  - Front `TGLPlane`: scale (0.45, 0.70), local y = 0.35, faces local +Z.
  - Back `TGLPlane`: same, Direction (0,0,−1).
  - Pole `TGLCube`: scale (0.07, 1.0, 0), local y = −0.5.
  - The front plate's material is the library material `Lnn` in `[0x54b140]`. Those are loaded from `<exe>\Bolumler\Diger\TIsaret\Levha<nn>.jpg` (0x506d4c; `"L"+n` names, case-insensitive files).
- **Placement**, with N = GetNode, T = toward, d = min(dist, len(N,T) − 6.0):
  - `P1 = N + (T−N)·d/len`
  - `pos = P1 + side·(T−N)/|T−N| × (0,1,0)⁻¹`, i.e. the perpendicular `(−v.z, 0, v.x)/|v|` with `v = P1 − T`; side = +4.0, or −4.0 if `left`
  - `pos.y = 1.0`; Direction = normalize(T − N), computed before the move (0x536364).
- **Meaning** [L]: the sign faces drivers travelling **T → N**, stands 4 m to their right and sits `d` before N.
- `TLHastahane` (hospital) is attached to a *connection*, and `TLParkYasak` comes from `NoParking`. Their placement is identical. Vmt+0x2c of TLHastahane (0x547244) does extra logic; that is RulesRE's area.

## 5. Traffic lights (`TEtkiIsik`) — placement only

- **Ctor 0x53b3e0** [C]: head dummy (+0x4c) with three `Bolumler\Diger\lamba.3ds` at local (0, +0.28, 0.05) red, (0, 0, 0.05) yellow and (0, −0.28, 0.05) green, scale 0.022. Off colours (r,g,b,a): (0.1,0,0,1), (0.1,0.1,0,1), (0,0.1,0,1).
- **Created by `AttachEtki`** with dist 8.3 (`0x4104cccd`) on node N toward T. vmt+0x28 points the head along T−N (`facing` in JSON).
- **Meaning** [L]: the light governs traffic arriving at N from T. Its stop line is 8.3 m from N on road N–T, and the head stands at the right of that approach.
- **Groups** (`TIsiklar`, ctor 0x53d638): `+0x10` lights, `+0x14` TStringList of phases `"RYG:ms"`. Each group is added to `TIsikKontrol [0x552fe0]` (0x53db14). The phase strings are added once per light, so the 4-step cycle repeats. Phase logic belongs to WorldRE.
- **Head positions in Talim** come from a precomputed table [C].
  - 0x52ac10 fills `T[a][b][c][d]` at 0x55ae64, with stride a·0x780 + b·0x180 + c·0x30 + d·0x10, for column a (x = 66a − 132), row b (z = 108b − 216), corner c and lamp d.
    - `x = 66a + offX[c] − 1.2`, `z = 108b + offZ[c] − 0.1`, `y = {3.63, 3.35, 3.08}[d] + 0.083`.
    - The world values cancel `D_vis`.
    - offX/offZ at 0x55d3e4: c0 (−129.975, −211.7), c1 (−134.015, −220.26), c2 (−127.435, −217.9), c3 (−135.485, −214.1).
  - Corner selection: toward +X uses c2, toward −X c3, toward +Z c0, toward −Z c1, always lamp d = 1.
  - 0x52c310 then nudges the heads:
    - for i in 0..5: G1[i+6].x += 1.34; G0[i].x += 0.66; G0[i+6].x += 0.68; G1[i+6].z += 0.04; G0[i].z += 0.69; G0[i+6].z −= 0.65.
    - then G1[6].x += 1.1; G0[0].x −= 0.37; G1[9].x += 0.93; G0[1].x −= 1.43; G1[6].z −= 0.2; G0[0].z += 0.39; G1[9].z += 0.39; G0[1].z += 0.47.
  - `maps.json` holds the final world values.
- A second 5×5×8 table at 0x55a1a4 (0x52ab4c, y = 1.98, offsets at 0x55ae24) is never read: dead code [C].
- **City maps** add 16 `isik.3ds` poles per map (TurnAngle applied natively, so the JSON axes are already rotated). Pedestrian lights (dist 8.0) sit on crossing walks. Şehir has a 5th group of 26 railway lights at x ≈ 0 with no phase strings in the builder; those are driven by the train/DingDing code (WorldRE).

## 6. Talim (map 1) — complete

Builder 0x52cd5c [C]. Order: graph (0x52ce73–0x52cff3) → signs 0x52c8a0 → `LinkSidewalks` → dummies 0x52b67c → billboard table 0x52b988 → billboards 0x52b708 → lights 0x52aefc → block colliders 0x52ba54 → walls 0x52c008 → ground 0x52b9cc → light nudge 0x52c310 → scenery 0x52c168 → sun lights 0x52c7ec.

- **Nodes** (3×3, y = 0): id = 3·row + col, x = −132 + 66·col, z = 108·row.
- **Roads**: 12 TAsfalt, width 6, two-way: 0–1, 0–3, 1–2, 1–4, 2–5, 3–4, 3–6, 4–5, 4–7, 5–8, 6–7, 7–8.
- **Neighbours** `[slot1 −Z, slot2 +X, slot3 +Z, slot4 −X]`: N0 [−1,1,3,−1], N1 [−1,2,4,0], N2 [−1,−1,5,1], N3 [0,4,6,−1], N4 [1,5,7,3], N5 [2,−1,8,4], N6 [3,7,−1,−1], N7 [4,8,−1,6], N8 [5,−1,−1,7].
- **No sidewalks, crossings, trees or tiles.** Visual scenery is `bolumler\ilkharita\sehir4\dag.3DS` plus `deniz.3ds` under a dummy: world = (3, 0.28, 3) + 0.01·(x, z, −y), material lib `[0x55a14c]`. There is no road mesh.
- **Colliders** (`TGLOXStaBox`; `[vmt+0x3c](False)` hides them):
  - Ground: 400 × 0 × 500 at (3, 0, 3).
  - Four walls, height 2, centre y = 1.38: x = 4 and x = −136 (depth 222, z = 108); z = −4 and z = 220 (width 138, x = −66). Drivable area: x ∈ [−135, 3], z ∈ [−3, 219].
  - Four 0.4-high kerb blocks, y = 0.1..0.5, centred (−99|−33, 54|162). Each is 58 × 100 plus 1-wide edge strips (±29.5 in x, ±50.5 in z) and four 1.414² corner boxes rotated 45°.
- **Lights**: 24, in two groups.
  - G0 = the 12 approaches along X, phases `100:8000, 110:2000, 001:8000, 010:2000` (starts red).
  - G1 = the 12 approaches along Z, phases `001:8000, 010:2000, 100:8000, 110:2000` (starts green). The cycle is 20 s.
  - G0 index 0..5 = nodes 0,1,3,4,6,7 toward n+1; 6..11 = nodes 1,2,4,5,7,8 toward n−1.
  - G1 index 0..5 = nodes 0..5 toward n+3; 6..11 = nodes 3..8 toward n−3.
- **Signs**: 23 TLevha, all right side, `p4 = 0`, `b28 = 0` (0x52c8a0).
- **Billboards** (0x52b708; table 0x55a154 = {5,1,20}, {2,0,10}, {7,1,10}):
  - Dummy at node pos, y = 2.5.
  - kind 0: z += off, x += 6, Direction (1,0,0). kind 1: x −= off, z += 6, Direction (0,0,−1).
  - `billboard.smd` at scale 0.01; panel cube 4.88 × 3.07 × 0.3 at local y 1.38, material `"1"` or `"2"` (Random(2)+1) from `[0x54a9a8]`.
  - World: (−20, 2.5, 114), (6, 2.5, 10), (−76, 2.5, 222).
- **Scene lights** (0x52c7ec): two `TGLLightSource` at (−700, 700, 700) and (700, 700, −700).
- **Cars** (0x52d430), using `PlaceCar` args (a → b, dist, speed):
  - Player `cars[2]` (`[0x54b088]`): 2→5, 20.0, 60.0. Start ≈ (−1.5, ·, 20), heading +Z [L].
  - AI `cars[0]` 1→2, `cars[1]` 8→7, `cars[3]` 5→2; each dist 0, speed `Random(50)+25`.
  - Then, if the previous map index is 0, the camera mode is set: `0x531d0c(cam, 3)`.

Light heads (world, after nudges):

| # | on | toward | G/idx | pos | facing |
|---|---|---|---|---|---|
| 0 | N0 | 1 | 0/0 | (−128.345, 3.433, −0.92) | +X |
| 1 | N0 | 3 | 1/0 | (−131.175, 3.433, 4.2) | +Z |
| 2 | N1 | 2 | 0/1 | (−63.405, 3.433, −0.84) | +X |
| 3 | N1 | 0 | 0/6 | (−70.005, 3.433, 1.15) | −X |
| 4 | N1 | 4 | 1/1 | (−65.175, 3.433, 4.2) | +Z |
| 5 | N2 | 1 | 0/7 | (−4.005, 3.433, 1.15) | −X |
| 6 | N2 | 5 | 1/2 | (0.825, 3.433, 4.2) | +Z |
| 7 | N3 | 4 | 0/2 | (−127.975, 3.433, 106.69) | +X |
| 8 | N3 | 6 | 1/3 | (−131.175, 3.433, 112.2) | +Z |
| 9 | N3 | 0 | 1/6 | (−132.775, 3.433, 103.48) | −Z |
| 10 | N4 | 5 | 0/3 | (−61.975, 3.433, 106.69) | +X |
| 11 | N4 | 3 | 0/8 | (−70.005, 3.433, 109.15) | −X |
| 12 | N4 | 7 | 1/4 | (−65.175, 3.433, 112.2) | +Z |
| 13 | N4 | 1 | 1/7 | (−67.875, 3.433, 103.68) | −Z |
| 14 | N5 | 4 | 0/9 | (−4.005, 3.433, 109.15) | −X |
| 15 | N5 | 8 | 1/5 | (0.825, 3.433, 112.2) | +Z |
| 16 | N5 | 2 | 1/8 | (−1.875, 3.433, 103.68) | −Z |
| 17 | N6 | 7 | 0/4 | (−127.975, 3.433, 214.69) | +X |
| 18 | N6 | 3 | 1/9 | (−132.945, 3.433, 212.07) | −Z |
| 19 | N7 | 8 | 0/5 | (−61.975, 3.433, 214.69) | +X |
| 20 | N7 | 6 | 0/10 | (−70.005, 3.433, 217.15) | −X |
| 21 | N7 | 4 | 1/10 | (−67.875, 3.433, 211.68) | −Z |
| 22 | N8 | 7 | 0/11 | (−4.005, 3.433, 217.15) | −X |
| 23 | N8 | 5 | 1/11 | (−1.875, 3.433, 211.68) | −Z |

Signs (texture `Lnn` → `TIsaret\Levha<nn>.jpg`; pos = root, plate centre is +0.35 y):

| # | on | toward | dist | tex | pos | facing |
|---|---|---|---|---|---|---|
| 0 | N0 | 3 | 15 | L23 | (−128, 1, 15) | +Z |
| 1 | N1 | 0 | 9 | L10 | (−75, 1, 4) | −X |
| 2 | N1 | 2 | 9 | L12 | (−57, 1, −4) | +X |
| 3 | N1 | 4 | 9 | L17 | (−62, 1, 9) | +Z |
| 4 | N2 | 1 | 46 | L34 | (−46, 1, 4) | −X |
| 5 | N2 | 5 | 83 | L12 | (4, 1, 83) | +Z |
| 6 | N2 | 5 | 9 | L13 | (4, 1, 9) | +Z |
| 7 | N3 | 6 | 9 | L60 | (−128, 1, 117) | +Z |
| 8 | N3 | 4 | 9 | L25 | (−123, 1, 104) | +X |
| 9 | N4 | 5 | 9 | L27 | (−57, 1, 104) | +X |
| 10 | N4 | 3 | 9 | L8 | (−75, 1, 112) | −X |
| 11 | N4 | 1 | 9 | L7 | (−70, 1, 99) | −Z |
| 12 | N5 | 2 | 9 | L37 | (−4, 1, 99) | −Z |
| 13 | N5 | 8 | 20 | L22 | (4, 1, 128) | +Z |
| 14 | N5 | 4 | 9 | L18 | (−9, 1, 112) | −X |
| 15 | N6 | 3 | 99 | L20 | (−136, 1, 117) | −Z |
| 16 | N6 | 3 | 20 | L26 | (−136, 1, 196) | −Z |
| 17 | N6 | 3 | 9 | L19 | (−136, 1, 207) | −Z |
| 18 | N7 | 8 | 20 | L37 | (−46, 1, 212) | +X |
| 19 | N7 | 6 | 57 | L36 | (−123, 1, 220) | −X |
| 20 | N7 | 6 | 9 | L36 | (−75, 1, 220) | −X |
| 21 | N7 | 4 | 20 | L15 | (−70, 1, 196) | −Z |
| 22 | N7 | 4 | 9 | L19 | (−70, 1, 207) | −Z |

## 7. Other maps (complete in `maps.json` except where noted)

- **Kasaba / Şehir / Şehir Merkezi** share one template.
  - Nodes 0..24 form a 5×5 grid: x = −132..132 step 66, z = −216..216 step 108, id = 5·row + col. Roads: 40 TAsfalt, width 6.
  - Nodes 25..124 are sidewalk corners at ±6 from road centrelines, 4 per block.
    - 180 TKaldirim: 64 block perimeters with f50 = −1, 80 zebra crossings, 36 other.
    - 156–158 `TEtkiKesisme` link each road to its zebra.
  - 16 `isik.3ds` poles; 224 lights in 4 groups (2 vehicle, 2 pedestrian). Pedestrian phases are `100:8000, 100:2000, 001:8000, 100:2000` and the reverse.
  - Meshes are 16 tiles `a1..d4`, `cepli_alt.3ds` (visual) and `cepli.3ds` (static ODE mesh, origin y 0.23), plus `dag`/`deniz`/`orman`. Şehir adds `engel.3ds` at (135, 0.43, −108.4) with scale 0.005, and 10 `TDingDing` crossings (`DingDing.3ds`/`DingDing2.3ds`; transform is local to the native `TDingDing`, which WorldRE owns).
  - Extra nodes: 120..124 at x = −138; Şehir 125..127 at (150|165|175, −108); Şehir Merkezi 125..127 at (0, 235|250|260). These are spurs and dead-ends; see `roads`.
  - One-way and closed roads are in `roads[].a_to_b / b_to_a`.
  - Player starts: Kasaba 22→17 (24 m), Şehir 14→13 (20 m), Şehir Merkezi 6→11 (20 m), all with speed 60. City AI is spawned by 0x52a90c (WorldRE).
  - Tree sprites are random (0x508800), so they are not in the JSON.
- **Otoban**: 39 nodes form a single serpentine chain 0–1–…–38 (38 TAsfalt), running z ≈ +32 to −1700. 55 TLevha, no lights.
  - Meshes: `sehir5\sehir.3ds` (visual) and `bar.3ds` (`TGLOXStaMesh`), both at origin (0, −1.42, 0).
  - Ground box 300 × 0 × 2000 at (−100, −0.23, −900). 172 random `TGLAgac`; 5 billboards.
  - Player: 1→2, dist 0, speed 90. If the previous map was 4 (Şehir Merkezi, `[0x553000]`): 37→36 [C, 0x52a87c].

## 8. `maps.json` field reference

`maps[]` (one entry per index 1..5):

| field | meaning |
|---|---|
| `index, name, builder, placement` | §1 |
| `sehir_dir, loading_image, time_key, time_default_s` | §1 |
| `speed_limit_kmh` | `[0x55de78]` |
| `counts` | summary sizes |
| `nodes[] {id, x, y, z, nb[4]}` | world position; `nb` = neighbour slots 1..4 (−1 = none), §3.2 |
| `roads[] {id, a, b, width, len}` | `id` = connection index in THarita+0x34 |
| `roads[] {a_to_b, b_to_a}` | +0x5b / +0x5c |
| `roads[] {sidewalk_left, sidewalk_right}` | connection ids from +0x60/+0x64 [L] |
| `sidewalks[] {id, a, b, width, len, crossing, f50, road}` | TKaldirim; `crossing` = zebra; `road` = owning TAsfalt connection id |
| `light_groups[] {group, phases[4], phases_raw_count, lights}` | phases `"RYG:ms"` (R = red, Y = yellow, G = green bit) |
| `lights[] {on, toward, dist}` | `on` = `N<i>` (node) |
| `lights[] {group, index_in_group, pos, facing}` | world head centre; facing = local +Z of the head |
| `signs[] {on, toward, dist, kind, tex, left_side, pos, facing}` | `on` = `N<i>`, or `C<id>` for connection-attached signs; `pos` = root, §4 |
| `crossings[] {on_road, toward, dist, flag28, walk}` | TEtkiKesisme; `on_road` = connection id (road or walk) |
| `meshes[] {file, cls, origin, x_axis, y_axis, z_axis}` | world = origin + x·x_axis + y·y_axis + z·z_axis for raw file vertex (x,y,z); `file` relative to the game dir; `relative_to` set when the parent is a native game object |
| `static_boxes[] {center, size, facing}` | ODE static boxes: world centre, full size (w,h,d), facing = local +Z (45° for corner boxes) |
| `misc[]` | billboards (dummy pos, facing) and the two `TGLLightSource`s |
| `car_placement[] {car, a, b, dist_from_a, p5, site}` | `PlaceCar` calls; `car2` = player; `p5` = speed arg (Random = 0 → 25 for AI) |
| `random_call_sites` | addresses whose `Random()` result was forced to 0 (billboard texture, trees, AI speed) |

Lamp meshes (`lamba.3ds`) are omitted from `meshes`. Their layout is fixed relative to each light head (§5).

## 9. Runtime checks requested (parent's Wine harness)

1. Screenshot Talim from above and confirm the light heads (e.g. N2 toward 5 at (0.83, 3.43, 4.2)) and the signs match §6.
2. Confirm the player start at about (−1.5, ·, 20) heading +Z (node 2 → 5), on the right lane.
3. Confirm that `dag.3DS` lies under the grid with the (x, z, −y) mapping. A flipped mountain would mean the up-vector rule is wrong.
4. Find which sign textures (Lnn) map to which SoruBank question when clicked. That is RulesRE's area; the `tex` here is the key.

## 10. Open questions

- Purpose of `TLevha+0x2c` (always node 0) and `TBaglanti+0x4c` (−1).
- The exact semantics of `car+0x34`, the `p5` argument: target cruise speed versus maximum speed. DriveRE or WorldRE.
- The left/right assignment of `TAsfalt+0x60/+0x64`. Derived through 0x4fd5fc; not verified.
- Phase source of Şehir light group 4 (railway). WorldRE.
- The `TDingDing` world transforms (Şehir) and the train path. WorldRE.
- Tree (`TGLAgac`) and billboard-texture placement is random per run. A deterministic port must seed or choose its own.
