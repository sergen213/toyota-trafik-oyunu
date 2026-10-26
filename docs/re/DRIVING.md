# DRIVING — player vehicle, input, physics, camera, HUD, audio

Source: static analysis of `work/ghidra/TrafikOyunu_v2.exe` (v2, ImageBase 0x400000), objdump + Ghidra. Throwaway disassembly under `work/re/DriveRE/`. Constants: `work/re/driving.json`.
Confidence tags: **[C]** confirmed from asm, **[L]** likely (asm seen, semantics of a helper inferred), **[G]** guess.

Notation: `dt` = GLCadencer deltaTime (seconds, double, stored in global `0x54a970` by `GLCadencerProgress` 0x52e124); `now` = cadencer newTime (global `0x54b1c4`). Floats are decoded IEEE singles; "push X" literals are noted with their value.

---

## 1. Object model

| Item | Fact | Ref |
|---|---|---|
| Physics component | Every road vehicle (`TMotorlu`/`TAraba`, VMTs 0x537268/0x537388) owns a **`TGLOXDynCar`** (oxGLODE "GLOXOde" unit, VMT 0x4d093c, size 644) at `self+0x1a4`. | 0x540a64 (TMotorlu.Create) **[C]** |
| ODE world | One **`TGLOXOdeEngine`** ("OdeManager", VMT 0x4cdd20), pointer in global `[0x54ab58]`. | 0x5070f2 **[C]** |
| Vehicle type | `self+0x211` byte = index 1..16 into the vehicle table below; tested by `0x539d60(obj,type,negate)` = `(obj is TAraba && obj.type==type) xor negate`. | 0x4fe1de, 0x539d60 **[C]** |
| Controlled car | `[0x54af9c]+0x48` (TCanlilar.Controlled). `0x53b208(car)` = "car is controlled". Input code runs only for it. | 0x53b208, 0x542bf4 **[C]** |
| Player car | Vehicle array at 0x54f8dc (ptr in `[0x54adc4]`), one car per type; `[0x54adc4]+8` = type 3 **YARIS "Afacan"**. Key `y` switches control back to it. All five maps use it (the map init routines only re-target the camera to `+8`). | 0x4fe0f0 loop, 0x52e39a, 0x52d451 **[C]** |

Vehicle table (folder names `0x54a76c[]`, display names `0x54a7ac[]`) **[C]**:

| type | folder | name | notes |
|---|---|---|---|
|1|Rav4|Gezgin|has reverse/brake light sub-meshes (types 1-4)|
|2|CAMRY|Bilgin||
|3|YARIS|Afacan|**player**; only type allowed interior camera (mode 3)|
|4|SEDAN|Dost||
|5|01_minnos|Minnoş||
|6|02_osman|Haylaz|"big" vehicle → chase cam lifts (§6)|
|7|03_kaptan|Kaptan|big|
|8|04_katil|Katil||
|9|05_mba|Düzenli Amca||
|10|06_neseli|Neşeli||
|11|07_yorgun|Yorgun||
|12|12_ambulans|Ambulans|engine sound = `Arabalar\ambulans.wav`; big|
|13|Polis|Polis|engine sound = `Arabalar\polis.wav`|
|14|OkulTasiti|Okul Taşıtı|loads `Dur.3ds` stop arms; big|
|15|13_albay|Albay|front-wheel traction (ModeTraction=mdFront)|
|16|CiftTeker|Çift Teker|two-wheeler; only type whose `Boy/En/Yukseklik` are read|

---

## 2. Keyboard / joystick bindings

Two polling helpers: `0x4a11b4 IsKeyDown(VK)` (GetAsyncKeyState<0) and `0x4a1188 IsKeyDown(Char)` (VkKeyScan → GetAsyncKeyState). All bindings below are **polled** (async state), either every frame or inside `GLFullScreenViewerKeyDown` 0x52e2ec (i.e. on any key-down event, so effectively edge-triggered-ish). **[C]**

In-game help string (shown by F1, literal at 0x52f700) **[C]**:
```
Yardım Menüsü : "F1"
Bölüm Hakkında : "F2"
Çıkış : "esc"
İleri/Geri(Fren) : "İleri Ok" , "Geri Ok"
Sola/Sağa Dön : "Sol Ok" , "Sağ Ok"
Fren : "f"
Kamera Modu : "k" , "l"
Kamera Takip Modu : "o"
Öndeki Taşıta Geç : "t"
Afacan'a Geç : "y"
Radyoyu Aç/Kapat : "m"
Radyo Kanalını Değiştir : "n"
Radyonun Sesini Aç/Kıs : "z" , "x"
Korna Çal : "h"
Oyunu Durdur : [Boşluk]
Sol/Sağ Sinyal : "c" , "v"
Joystick/Klavye : "j"
```

| Key | Where | Effect | Ref |
|---|---|---|---|
| ↑ (VK 0x26) | per frame | accelerate forward (§4) | 0x5417f9 **[C]** |
| ↓ (0x28) | per frame | brake, then reverse | 0x5416cc **[C]** |
| ← / → (0x25/0x27) | per frame | steer | 0x541526 **[C]** |
| `f` | per frame | brake (toward 0, never reverses) | 0x5416da **[C]** |
| Space (0x20) | per frame | `MotorRunning:=false; Speed:=0` (handbrake while held) | 0x541670 **[C]** |
| Space | KeyDown | toggle pause/question mode (`0x508250` IsPaused; pause `0x508120`, resume `0x5081a8`); also joystick button 2 in joystick mode | 0x52e83d **[C]** |
| Esc (0x1b) | KeyDown (`Key` param) | quit to menu (sets form+0x20d,+0x20c, calls 0x5325fc, 0x5332b8("Menu")) | 0x52e464 **[C]** |
| F1 (0x70) | KeyDown | toggle controls help (`0x52f5e8(self,0x70)`) | 0x52e8a4 **[C]** |
| F2 (0x71) | KeyDown | toggle map info | 0x52e917 **[C]** |
| F3 (0x72) | KeyDown | toggle HUD text form+0xa4 ("F1 - Oyun Kontrolleri / F2 - Bölüm Hakkında Bilgi / F3 - Gizle/Göster") and its shadow +0xa8 | 0x52e556 **[C]** |
| F4 (0x73) | KeyDown | next level `0x530770`, only if not paused and points(form+0x18f) ≥ required(form+0x204) | 0x52e454 **[C]** |
| `k` / `l` | KeyDown | camera next / previous (§6) | 0x52e588→0x532470 / 0x52e59f→0x532394 **[C]** |
| `o` | KeyDown | camera mode 1 (chase) | 0x52e5b6 **[C]** |
| `j` | KeyDown | toggle joystick mode `[0x54ae54]`; message "Joystick Modu Açık" / "Klavye Modu Açık" | 0x52e5cf **[C]** |
| `t` | KeyDown | take control of the vehicle ahead (`0x5385a4`) unless it is `polis`/`12_ambulans` or flagged +0x210 | 0x52e619 **[C]** |
| `y` | KeyDown | take control of Yaris (if its vmt+0x84 returns true) | 0x52e38d **[C]** |
| `h` | KeyDown | horn on controlled car `0x5412a4` | 0x52e813 **[C]** |
| `v` | KeyDown | toggle **right** signal (car+0x1be, lamp `sari_sag.3ds`) | 0x52e9b4 **[C]** |
| `c` | KeyDown | toggle **left** signal (car+0x1bf, lamp `sari_sol.3ds`) | 0x52e9f2 **[C]** |
| `m` / `n` / `z` / `x` | KeyDown | radio on/off (`playlist.m3u`), next track, volume +/- | 0x52e736, 0x52e77d, 0x52e7df, 0x52e7f9 **[C]** |
| Enter (0x0d) | KeyDown when dialog flag `[0x54afac]` | close dialog and resume | 0x52e31b **[C]** |
| `1`..`5`, `7`..`9` | KeyDown only if debug flag `[0x54ac9c]` | jump to map 1..5 / 0x52ff8c(1..3) | 0x52e4aa..0x52e53c **[C]** (debug) |
| W,S,A,D,Space,Ctrl (TKamera +0x118..+0x11d) + mouse | per frame, camera mode 0 only | free-fly debug camera | 0x531304, 0x531ab8 **[C]** |

Signal toggle also records `car+0x20c := car+0x28` (current road node) and `car+0x204 := 0x53b068(car)` (position along road) when switched on, 0 when off — consumed by rule checks (RulesRE). **[C]**

**Joystick** (`TJoystick` 0x4fc6cc, DFM: Interval 10 ms, Threshold 10, Capture): axes normalized to percent: `X = round((raw-cx)*100/cx)`, same for Y → ≈ −100..+100 at form `Joystick1+0xd0/+0xd4`; buttons bitmask at `+0xd8` (bit0=button1, bit1=button2…). **[C]** 0x4fd17c..0x4fd1ea, 0x4fcba0.
`OnJoystickButtonChange` (0x530cc0) just calls the KeyDown handler with Key=0. **[C]**

---

## 3. Physics world (TGLOXOdeEngine)

Created at 0x5070f2 (map common setup) **[C]**:

| Property | Value | Ref |
|---|---|---|
| ContactNum | 12 | 0x50711b |
| Modes (default surface) | {mdSoftERP} (byte 0x08 at 0x507abc; enum TOXMode = mdSlip2,mdSlip1,mdSoftCFM,mdSoftERP,mdBounce,mdFDir1,mdMu2) | 0x507128 |
| Mu | 10.0 | 0x507134 |
| MultiplyTime | 7.0 (HG.vdd `Ayarlar-MPTime=7` is **not read** by v2: no such key string in the exe) | 0x507140 |
| QuickStepNum | 20 (dWorldSetQuickStepNumIterations) | 0x50714c |
| RemoveTypeContact | true | 0x507155 |
| Soft_erp | 0.2 | 0x507161 |
| StepTime | 0.05 (set to 0 while paused 0x52fb47, restored 0.05 at 0x52fd09) | 0x50716d |
| SurfaceLayer | 0.001 (only applied if flag +0x171 true; default false → **not applied** [L]) | 0x507179, 0x4d3ba4 |
| Gravity | (0, −10, 0) (engine default was (0,0,−9.81)) | 0x50717e, 0x4d3a94 |
| World CFM | 1e-5 (dWorldSetCFM in ctor) | 0x4d3aeb |
| Step function | `+0x140 = 0` → **dWorldQuickStep** | 0x4d3a31, 0x4d5dab |
| Space | dHashSpaceCreate; contacts in a joint group emptied every step | 0x4d3ad1, 0x4d5e0e |

**Stepping (`TGLOXOdeEngine.DoProgress` 0x4d5cf8)** **[C]**:
```
h = min(dt, 0.04)                      // 0x4d5d2d  (TBYTE 0.04; clamp value 0.04)
if MultiplyTime <= 0: MultiplyTime = 1
acc += h * MultiplyTime                // acc at +0x118
while acc >= 0.1:                      // TBYTE 0.1 at 0x4d5f4c
    acc -= 0.1
    dSpaceCollide(space, nearCallback 0x4d6eac)
    dWorldQuickStep(world, StepTime)   // 0.05
    dJointGroupEmpty(contacts)
    for each registered TGLOXDynCar: car.UpdateJoints()   // 0x4e06cc
    for each object of class [0x5480d0]: 0x4e42b0
```
⇒ 70 ODE steps of 0.05 s per real second = **3.5× real-time physics**; below 25 fps the game slows (dt clamp).

Contacts: near-callback copies the colliding object's `dSurfaceParameters` (11 dwords at obj+0xd0) into the contact, OR-ing mode bits from the object's Approx flags; calls `OnCollisionEvent` (= `TFormOyun.CarpSesi`) per pair (0x4d5ad2). Car geoms therefore use the car surface (Mu 30, soft_erp 0.5, {mdSoftERP}); static world uses its own. **[L]**

Static collision geometry (world): `TGLOXStaBox` ground slabs (Talim: 400×0×500 box at Y=−0.5, 0x52b9cc) + other boxes, and `TGLOXStaMesh` trimeshes `sehir1\cepli.3ds` (0x51c576), `sehir2\cepli.3ds` (0x524c3e), `sehir3\cepli.3ds` (0x5022ee), `bolumler\ilkharita\sehir5\bar.3ds` (0x5290ea). Full list belongs to MapRE. **[C]**

---

## 4. Vehicle physics (TGLOXDynCar)

### 4.1 Parameters (ctor default → value set by game)

TGLOXDynCar ctor 0x4de5cc, game overrides in TMotorlu.Create 0x540a64 and TMotorlu load 0x53e814. Field offsets are RTTI-published properties. **[C]**

| Property (offset) | ctor default | game value | Ref |
|---|---|---|---|
| ZOOM (+0x200) / ZOOM_CUBED (+0x204) | 1 / 1 | — | 0x4de606 |
| LENGTH (+0x208) | 2.8 | 2.8 (HG `Boy`, default 2.8, read **only for CiftTeker**) | 0x53ea6f |
| WIDTH (+0x20c) | 0.95 | 0.95 (HG `En`, CiftTeker only) | |
| HEIGHT (+0x210) | 0.7 | 0.7 (HG `Yukseklik`, CiftTeker only) | |
| RADIUS (+0x214) | 0.5 | HG `Radius`, default 0.25 (Yaris 0.26) | 0x53eb08 |
| CMASS (+0x1f8) | 5 | 5 | |
| WMASS (+0x1fc) | 1 | 1 | |
| Density (+0x120) | 1 | 1 | |
| WHEEL_WOBBLE (+0x21c) | 0 | 0 | |
| FRONT/BACK SUSPENSION_ERP (+0x224/+0x22c) | 0 | **0.5 / 0.5** | 0x540b47 |
| FRONT/BACK SUSPENSION_CFM (+0x220/+0x228) | 0 | **0.03 / 0.03** | 0x540b57 |
| WHEEL_OFFSET (+0x230) | 0.032 | — | |
| ACCEL (+0x234) | 0.002 | — (unused by player code) | |
| TURN_SPEED (+0x238) | 0.05 | 0.08 at create; **0.8·dt every frame** for the controlled car | 0x540b1a, 0x54144b |
| Steer (+0x23c) | 0 | input | |
| Speed (+0x240) | 0 | input (wheel angular-velocity target) | |
| WheelsParamFMax (+0x244) | 100 | 100 | |
| WheelsFrontLoHiStop (+0x248) | 1 | **3** | 0x540b37 |
| WheelsBackLoHiStop (+0x24c) | 0 | 0 | |
| FrontWheelLimit (+0x108) | 1 | **2** | 0x540aea |
| FrontWheelTurnDegre (+0x10c) | 3 | **5** | 0x540ada |
| FrontWheelBackUp (+0x110) | 0.25 | 0.25 | |
| FrontBackWheelRunForce (+0x114) | 5 | **50** | 0x540afa |
| FrontBackWheelAltForce (+0x118) | 1 | **10** | 0x540b0a |
| ModeTraction (+0x280) | 2 mdBoth | 2 (all-wheel drive); type 15 → 0 mdFront | 0x540b30, 0x53ecae |
| Mu (surface) | — | **30** | 0x540aca |
| Soft_erp (surface) | — | **0.5** | 0x540b98 |
| Modes (surface) | — | {mdSoftERP} (0x08) | 0x540b8d |
| MotorRunning (+0x264) | false | set true by throttle/brake keys | |
| DisableThreshold/DisableSteps | 0.001 / 15 | — | 0x4de7ef |

### 4.2 Bodies, geoms, joints (`TGLOXDynCar.Init` 0x4deb58) **[C]**

Local frame: X = right, Y = up, **Z = forward** (steered wheels A/B at +L/2; chase camera sits at local −Z; see §6). **[L]**

- Chassis body (+0x124): box mass `dMassSetBox(m, Density, W, H, L)` then `dSetPoids(m, CMASS)` (oxGLODE extension; "poids" = weight ⇒ total-mass adjust **[L]**), geom `dCreateBox(W,H,L)`.
- 4 wheel bodies (+0x128..+0x134): sphere mass radius R, `dSetPoids(m, WMASS)`, geom `dCreateSphere(R)`. Positions relative to the chassis centre:

| wheel | joint | position | HG keys |
|---|---|---|---|
| A | J0 (+0x138), steered | ( +W/2 + A.x, −H/2 + A.y, +L/2 + A.z ) | WheelPosA_X/Y/Z (defaults 0.1, −0.05, −0.4) |
| B | J1 (+0x13c), steered | ( −W/2 + B.x, −H/2 + B.y, +L/2 + B.z ) | B = (−A.x, A.y, A.z) (0x53eb91: X·−1) |
| C | J2 (+0x140), rear | ( +W/2 + C.x, −H/2 + C.y, −L/2 + C.z ) | WheelPosC_X/Y/Z (defaults 0.1, −0.05, 0.5) |
| D | J3 (+0x144), rear | ( −W/2 + D.x, −H/2 + D.y, −L/2 + D.z ) | D = (−C.x, C.y, C.z) |

  Yaris (HG): R=0.26, A=(0.04,−0.05,−0.6), C=(0.05,−0.05,0.5), BodyPosY=−0.55 ⇒ wheel centres A(±0.515, −0.40, 0.80), C(±0.525, −0.40, −0.90) with W=0.95,H=0.7,L=2.8.
- Hinge2 joint per wheel, anchor = wheel body position, axis1/axis2 from the chassis matrix (`0x484610`; axis1 = up, axis2 = right **[L]**). Init params: LoStop=HiStop=0, FMax=WheelsParamFMax(100); front: SuspensionERP=0.5, SuspensionCFM=0.03, Lo/Hi=±WHEEL_WOBBLE then ±WheelsFrontLoHiStop(±3); rear: ERP/CFM 0.5/0.03, Lo/Hi=±WheelsBackLoHiStop(0). (ODE param ids read from oxGLODE globals: LoStop 0, HiStop 1, Vel 2, FMax 3, Fudge 4, SuspERP 9, SuspCFM 10, Vel2 0x102, FMax2 0x103.)

### 4.3 Per-ODE-step joint update (`0x4e06cc`, called inside the step loop) **[C]**
```
J0.LoStop = -FrontWheelLimit (-2) ;  J1.HiStop = +FrontWheelLimit (+2)   // oxGLODE sets only these two (quirk)
for J in (J0,J1):
    J.Vel   = (Steer - dJointGetHinge2Angle1(J)) * FrontWheelTurnDegre   // *5
    J.Fudge = FrontWheelBackUp                                            // 0.25
drive = {J0,J1} (mdFront) | {J2,J3} (mdBack) | all four (mdBoth = game default)
if MotorRunning:
    for J in drive: J.Vel2 = Speed ; J.FMax2 = FrontBackWheelRunForce * ZOOM_CUBED   // 50
    dJointCorrectHinge2(J2); dJointCorrectHinge2(J3)
else:
    for J in drive: J.Vel2 = 0 ; J.FMax2 = FrontBackWheelAltForce                   // 10 (rolling brake)
```
Steering is therefore a P-controller on the steering angle, |Steer| ≤ 1 rad (≈57°) at the wheel. Speed is the **wheel angular velocity** (rad per ODE-second); **negative = forward** (confirmed by `0x4e451c SetKmh(v)`: `Speed = −v / RADIUS / 3.6 / 3`). **[C]**

### 4.4 Speed readout / km/h **[C]**
`TGLOXDynCar.GetKmh` 0x4e44b8: `kmh = |dBodyGetLinearVel(chassis)| · 3.6 · 3.0` (3.6 = TBYTE at 0x4e4510; 3.0 = global single 0x54a30c). Exposed as `TMotorlu` vmt+0x30 (0x540f3c).
Top forward speed (no slip): 35 · R · 10.8 → Yaris 35·0.26·10.8 ≈ **98 km/h**; reverse 10·0.26·10.8 ≈ **28 km/h**. No gears/transmission exist in physics; the "gears" are only engine-sound pitch bands (§8).

### 4.5 Player input model (`TMotorlu.PlayerInput` 0x541410, called every frame from TMotorlu.Progress 0x5429a4 when the car is controlled) **[C]**
```
A = 3*dt                                  // global 0x54adc0 (0x54143e)
car.TURN_SPEED = 0.8*dt                   // TBYTE 0.8 at 0x541a74
clamp Steer to [-1, +1]                   // 0x54146b..0x5414b1
clamp Speed to [-35, +10]                 // 0x5414b1..0x5414f7  (-35 = max forward, +10 = max reverse)
if !paused && !form.0x1fd:
  if keyboard mode:
    if Right:  Steer += (Steer < 0 ? 3*TURN_SPEED : TURN_SPEED)
    elif Left: Steer -= (Steer > 0 ? 3*TURN_SPEED : TURN_SPEED)
    else:      Steer *= 0.5               // per frame (0x541a94)
  else (joystick):
    Steer = sign(X) * (X/10)^2 / 300       // X in -100..100 → |Steer| ≤ 0.333
if Space held:            MotorRunning=false; Speed=0              // 0x54167e
elif !paused && !form.0x1fd:
  if keyboard mode:
    if Down or 'f':
       MotorRunning = true
       if 'f':  if |Speed| < 0.1: Speed = 0
                elif Speed < 0: Speed += 5A   else if Speed > 0: Speed -= 5A
       else:    Speed += (Speed < 0 ? 5A : A)      // brake, then reverse
    elif Up:
       MotorRunning = true
       Speed -= (Speed > 0 ? 5A : A)               // brake reverse, then accelerate forward
    else:
       Speed *= 0.993                              // per frame coast (TBYTE 0.993 at 0x541aac)
       if kmh < 2: Speed = 0
  else (joystick, Y in -100..100, negative = forward):
    MotorRunning = true
    k = floor(Y/10) / 100
    Speed += (Y*Speed < 0 ? 4*k : k)               // opposite direction = braking ×4
    if floor(Y/10) == 0: Speed *= 0.993
    if 'f' or button1 (bit0): same brake ramp as 'f' above
```
Accel time from rest to −35 with Up held: 35/3 ≈ 11.7 s of real time (target velocity; actual car lags via FMax2 = 50).

Light logic for the controlled car (`0x54101c`, start of Progress) **[C]**:
- Brake lamps (`0x540e28(on)`, proxies +0x1c4 `kirmizi_sag.3ds`, +0x1d0 `kirmizi_sol.3ds`, or materials of sub-meshes +0x14c/+0x150/+0x164/+0x168): ON when Space, `f`, joystick button1, Up while Speed>0, or Down while Speed<0; OFF when no drive key pressed (0x542c49) or Down while reversing.
- Reverse lamps (`0x540eec(on)`, types 1-4 only, sub-meshes +0x15c/+0x160/+0x174/+0x178): ON while Down with Speed ≥ 0 or Up with Speed > 0.
- Turn signals: `0x542874(car,+0x1be)` right / `0x542958(car,+0x1bf)` left; blink: visible = `odd(round(3*now))` (period 2/3 s). Proxies `sari_sag.3ds` (+0x1c0) / `sari_sol.3ds` (+0x1cc); fallback sub-meshes +0x158/+0x170 (right), +0x154/+0x16c (left). Material value 0 = on, 2 = off (0x54286c/0x542870).
- Headlight proxies `beyaz_sag.3ds` (+0x1c8), `beyaz_sol.3ds` (+0x1d4) — created at 0x541d2c/0x5422c8; no player key toggles them.

---

## 5. HG.vdd car keys — how they are used (`TMotorlu.Load` 0x53e814, light loader 0x541ac4) **[C]**

Key path = `"Arabalar-" + folderName + "-" + key` (0x540f74, `0x543d38(self,key,default)` reads a float).

| Key | Use | Default |
|---|---|---|
| Radius | `TGLOXDynCar.RADIUS` (wheel sphere radius) | 0.25 |
| WheelPosA_X/Y/Z | WheelPosA; WheelPosB = mirrored X | 0.1 / −0.05 / −0.4 |
| WheelPosC_X/Y/Z | WheelPosC; WheelPosD = mirrored X | 0.1 / −0.05 / 0.5 |
| BodyPosY | Y offset of the visual body actor (+0x188) | −0.5 |
| Boy / En / Yukseklik | LENGTH / WIDTH / HEIGHT — **only if folder == "ciftteker"** (0x53ea52 compare) | 2.8 / 0.95 / 0.7 (doubles) |
| kfarx/y/z | position of `kirmizi_sag.3ds` (right brake/tail lamp proxy, tex `kirmizi.jpg`) | 0.1 each |
| k1farx/y/z | `kirmizi_sol.3ds` (left brake lamp) | 0.1 |
| sfarx/y/z | `sari_sag.3ds` (right indicator, `sari.jpg`) **and** `beyaz_sag.3ds`/`beyaz_sol.3ds` (headlamps, `beyaz.jpg`) | 0.1 |
| s1farx/y/z | `sari_sol.3ds` (left indicator) | 0.1 |
| KornaSes | horn wav (default `Arabalar\korna.wav`) | |
| MotorSes | engine wav (defaults `Arabalar\motor.wav`; type 12 `Arabalar\ambulans.wav`, type 13 `Arabalar\polis.wav`) | |
| Renkler | body colour list (`0x4e7650` read at 0x54032c) | |
| loc | **not referenced** by v2 (no string) **[L]** | |

Lamp proxies are scaled 0.007 (0.008 for `13_albay`) and rotated with Up=(0,1,0) (0x541b5c..). Visual body: `<folder>.smd` actor at scale 0.007 (0.008 for `13_albay`), Direction (0,1,0), Up (0,0,−1) (0x53f259..0x53f2c6). Type-3 (Yaris) also loads interior `Bolumler\Diger\araba.3ds`, seat `koltuk.3ds`, and a driver actor `Bolumler\Diger\Anim\ref.smd` with animations `surus, konus, kafa_geri_bak, kafa_ileri_bak, radyo, sag_03..15, sola_03..15` (steer-angle driven, interior camera only).

---

## 6. Camera (TKamera, VMT 0x530e64, global `[0x54aacc]`)

Fields: target `+0xd0` (set by `0x531258`), mode `+0xd4`, chase dummy `+0xe0` (parented to world root `[0x54ac74]`), look dummy `+0xe4` (local position (0,0,4) when parented to the car, 0x5313e9), free-cam dummies +0xd8/+0xdc. **[C]**

`SetMode(m)` 0x531d0c: ignored while paused; if target is a TMotorlu and type≠3 and m==3 → m=1 (interior only for Yaris). Then `ApplyMode` 0x5314e0 (always first sets 0x487864(1.0)). **[C]**

Initial mode on every map = **3** (calls at 0x506908, 0x5211f4, 0x528e51, 0x52a906, 0x52d542, 0x52dda0) → Yaris interior/driver view. `o` → 1.

Mode cycle: `k` (0x532470): 0→1→2→3(→4 if not Yaris)→4→5→7→8→10→1 (6→7, 9→10). `l` (0x532394): reverse: 10→8→7→5→4→3(→2 if not Yaris)→2→1→10; 0→10, 6→5, 9→8. **[C]** (jump tables 0x53248c / 0x5323b0)

| mode | setup (0x5314e0; positions are local to the parent) | per-frame (0x530fc4) | focal* |
|---|---|---|---|
|0|free cam, parent +0xdc, pos 0|mouse + WSAD fly (0x531ab8)| — |
|1 chase|parent chase dummy; dummy := target abs pos & direction, `Move(−5)`, PointTo(target); camera Y=1; target = car|`0x531d64` 6× per frame: dummy.PointTo(car); d=dist(dummy,car); dummy.Y = 2 − d/6; if d>6: dummy.Move(d−6) (keep 6 units behind)|100|
|2 rigid chase|parent = car, pos (0,1,−5), Up (0,1,0), target car|—|100|
|3 interior (Yaris)|parent = car, Direction (0,0,1), Up (0,1,0), no target, pos (0,0.26,−0.81), camera 0x487864(0.4); shows interior meshes (0x543da0(car,true))|—|100|
|4 side|parent = car, pos (W/2+1, −0.15, −0.8) (W = car vmt+0x48/2 +1 → 0x540f1c), target = look dummy (0,0,4)|—|90|
|5 front|parent = car, pos (0,1,5), target car|—|105|
|6 high|parent = car, pos (−1,100,−15), target car|—|90|
|7 top-down|parent world, Direction (0,−1,0), Up (0,0,−1)|`0x531eec`: pos = car pos with Y=75|90|
|8 front-high|parent = car, pos (0,1.5,5), target car|`0x531f6c` (sin/cos orbit)|90|
|9 orbit|parent = car, pos (−2,1,−4), target car, stores mouse pos|`0x5322d0` mouse orbit|90|
|10 roadside|parent world, target car|`0x531ecc`: when car's road node (+0x28) changes, place camera on the segment, Y=4 (0x531e10)|90|

*value passed to `0x4876f0` (TGLCamera focal length/FOV setter **[L]**).

Modes 1 and 2 extra (0x530fc4): find a vehicle ahead within 12 units on the same road (0x5386dc); if it is type 6,7,12 or 14 (big vehicles), let d = dist(that car, camera); `h = approach(h, d<3 ? 3−d : 0, 0.5)` per frame (`0x536708(step,target,current)`); camera Y = 1 + h. **[C]**

---

## 7. HUD (TFormOyun DFM + update `0x5343ec` each frame) **[C]**

Font: `GLWindowsBitmapFont1` = MS Sans Serif, height −16, bold, TURKISH_CHARSET, chars 0x20..0xFF. Screen 800×600 (GLFullScreenViewer, aa2xHQ). Each text has a shadow copy drawn first, offset (−1,−1), ModulateColor (0.3,0.3,0.3); shadow text is copied from the main text each update.

| Object | pos (x,y) | align | colour | content | Ref |
|---|---|---|---|---|---|
| GLHUDTextHiz | 780,560 | right | (0,1,0.498) | `IntToStr(trunc(kmh)) + " km/h"` of camera target | 0x5344e2 |
| GLHUDTextPuan | 780,535 | right | (0,1,0.498) | `"Puan : " + IntToStr(form+0x18f)` | 0x534444 |
| GLHUDTextOyunSure | 780,510 | right | (0,1,0.498) | `"Süre : " + FormatDateTime('hh:nn:ss', [0x54acfc]/86400)` | 0x534486 |
| GLHUDTextMiniHelp | 780,10 | right | (0,1,0.498) | `Soruları yanıtlamak için [BOŞLUK] tuşuna basın...` | DataModuleCreate 0x52da18 |
| GLHUDTextSH | 780,30 | right | (0,1,0.498) | `Sonraki bölüme geçmek için [F4] tuşuna basın...`, blinking (`0x5367b4(700,500)`) when points ≥ required and map≠4 | 0x52e1f0 |
| GLHUDTextF1F2 | 30,510 | left | (1,1,0) | `F1 - Oyun Kontrolleri\r\nF2 - Bölüm Hakkında Bilgi\r\nF3 - Gizle/Göster` (F3 toggles) | 0x52da18 |
| GLHUDTextHelp / Sifre | 30,30 | left | (1,1,0) | hidden; help/password panels (F1 text) | |
| GLHUDTextRapor | 30,25 | left | (0.439,0.859,0.576) | message/report line (`0x5084d8` messages e.g. "Daha Dikkatli Ol (-15)") | |
| GLHUDTextDeneme | 100,100 | left | — | hidden debug | |

(`trunc` = `0x46ec10` **[L]**.)

---

## 8. Audio triggers

Sound objects (DelphiX TAudioStream-like; methods `0x4ebacc` load file, `0x4eb2ac` set looping, `0x4eb2a0` set frequency (Hz), `0x4eb404` set volume (DirectSound hundredths of dB, ≤0), `0x4eb2cc` set pan, `0x4eb0dc` play, `0x4eb54c` stop) **[L]**.

**Engine** (per car, `+0x1b0`, looped, base freq 11025 Hz set at load 0x53e984) — in Progress 0x5438b6 **[C]**:
```
d = distance(car, camera)
if d > 45: stop; pan 0
else:
  if type not in {12,13}:                 // ambulance/police keep their siren pitch
     base = kmh < 20 ? 0 : (kmh < 40 ? 20 : 40)       // 20<kmh<40 → 20 ; kmh>40 → 40
     freq = round((kmh - base)*300 + 11500)          // Hz
  vol = (car is camera target) ? round(max(d,6)) * -200 : round(d) * -100
  if not playing and (type not in {12,13} or car.+0x11c siren flag): play
```
**Horn** (`h`, 0x5412a4): sound `+0x1b4` (KornaSes, 11025 Hz, not looped): volume = round(d)·−100, pan 0, play; marks car+0x148=1. If controlled and `now < lastHorn + 5` → message `Nedensiz Yere Korna Çalıyorsun (-3)` and points −3; lastHorn := now (initial now−10). **[C]**
**Crash** (`TFormOyun.CarpSesi` 0x52fd98 = ODE OnCollisionEvent): only when one of the two geoms belongs to the camera-target's chassis and **both geoms have bodies** (dynamic–dynamic; static world hits are ignored); if `|v1 − v2| ≥ 0.5` (linear velocities of the two bodies): play `sesler\crash<Random(12)+1>.wav` on the shared crash channel `[0x552ff8]` (only if it isn't already playing, 0x508924); if `now > lastCrash + 1.0` (single at 0x54aa5c): lastCrash := now, message `Daha Dikkatli Ol (-15)`, crash counter `[0x54ad8c]++`, player points −15. **[C]**
Other: `sesler\Tren.wav`, `Su.wav`, `yaya.wav`, `DingDing.wav` (world, WorldRE); radio = MCI playlist.

---

## 9. Frame order (`GLCadencerProgress` 0x52dfec) **[C]**
Start-up: form+0xd8 (double) = 0 → set to Now; after (Now−start)·3600·24·1000 > 500 ms → start game (camera, music "giris", etc.), form+0xd8 := −1. Normal frames (form+0xd8 ≤ −1): store dt/now → 0x508f78, 0x50912c → if not paused 0x53db40 (traffic lights) → `0x53d294` (all vehicles' Progress incl. player input) → HUD 0x5343ec → timeout check → radio volume fade → map-specific (map 5 spawner 0x52a90c; map 3 0x508afc; map 2 0x508ed4) → 0x5095b8 → F4 hint blink. ODE stepping runs from the scene graph (`TGLOXOdeEngine.DoProgress`).

---

## 10. ODE import table (oxGLODE.dll, 65 imports) — call sites

IAT 0x55eb38..0x55ec38, thunks 0x4b9364..0x4b9564 (`jmp [IAT]`). All game physics goes through the GLOXOde unit (0x4b9000–0x4e5000); the game unit itself calls ODE directly only at `0x52fe29/0x52fe3a` (dGeomGetBody), `0x52fe4b/0x52fe5a` (dBodyGetLinearVel) in CarpSesi, `0x542b8e` (dJointGetHinge2Angle1, CiftTeker handlebar) (all other vehicle/physics access goes through TGLOXDynCar methods). Full per-import call-site lists are in `work/re/driving.json` (`ode_imports`). Key roles:

| Import | Role (call sites) |
|---|---|
| dWorldCreate, dHashSpaceCreate, dJointGroupCreate, dWorldSetCFM(1e-5), dWorldSetGravity | engine ctor 0x4d3abf..0x4d3b15; gravity change callback 0x4d6613 |
| dWorldSetQuickStepNumIterations | SetQuickStepNum 0x4d68ed |
| dWorldSetContactMaxCorrectingVel / SurfaceLayer | engine init 0x4d3b9c / 0x4d3bba (flag-gated, off) |
| dSpaceCollide, dWorldQuickStep / dWorldStepFast1 / dWorldStep, dJointGroupEmpty | DoProgress 0x4d5da3, 0x4d5dca/0x4d5de8/0x4d5dff, 0x4d5e0e |
| dCollide, dAreConnected(Excluding), dGeomGetBody, dJointCreateContact, dJointAttach | near callback 0x4d3c25..0x4d5ccc |
| dBodyCreate, dBodyDisable, dMassSetZero/Box/Sphere, dSetPoids, dBodySetMass, dGeomSetBody, dCreateBox/Sphere (wrappers 0x4b96c0/0x4b9690) | car init 0x4deb58 (0x4defeb..0x4df2a9); other dyn objects 0x4d9e3b..0x4e3853 |
| dJointCreateHinge2, dJointSetHinge2Anchor/Axis1/Axis2/Param | car init 0x4df310..0x4df58d; car reposition 0x4e499e..0x4e49cf |
| dJointSetHinge2Param, dJointGetHinge2Angle1, dJointCorrectHinge2 | car joint update 0x4e06cc |
| dBodyGetLinearVel | GetKmh 0x4e44c8; CarpSesi; other objects |
| dBodySetLinearVel/AngularVel/Force/Torque, dBodyEnable/IsEnabled/Get*Vel | reset / auto-disable of dynamic objects (0x4d4346.., 0x4e4a6b..0x4e4b19) |
| dCreateTriMesh, dGeomTriMeshDataCreate/BuildSimple/Destroy/ClearTCCache | static/dyn mesh 0x4b9710, 0x4d8ed4, 0x4d8eff, 0x4d8f73 |
| dGeomSetPosition/Rotation, dBodyBothTransforme, dGeomGet* , dGeomTransformGetGeom | GL↔ODE transform sync 0x4d38d3, 0x4d395d, 0x4b98f0.. |
| dGeomDestroy, dBodyDestroy, dJointDestroy, dSpaceDestroy, dJointGroupDestroy, dWorldDestroy, dCloseODE | destructors 0x4d6de5..0x4d6e05, 0x4d767f.. |

---

## Runtime checks requested
1. Initial camera: code selects mode 3 (interior) for the Yaris on every map; Main reported a chase camera — confirm which view appears first and what `k`/`l` cycle through.
2. Steering sign: does → (Steer increasing) turn the car right?
3. Time scale: confirm the world runs ~3.5× real time (e.g. 0→60 km/h time with ↑ held at a high frame rate; expected model ≈ a few seconds) and that km/h tops out near 98 (Yaris, flat road).
4. Does Space while driving act as an instant handbrake + question/pause toggle at the same time (both code paths fire)?

## Open questions
- `dSetPoids` is an oxGLODE-specific export; assumed = dMassAdjust (set total mass). Verify by disassembling oxGLODE.dll if exact inertia matters.
- Hinge2 axis vectors come from `0x484610` on the chassis GL object; assumed axis1 = chassis up, axis2 = chassis right.
- Exact contact surface mixing (car vs. world parameters) — near-callback copies the first matching object's surface; which one wins for car–ground is **[L]**.
- `0x4876f0` argument (100/90/105) interpreted as focal length/FOV; `0x487864` (1.0 / 0.4) unknown camera parameter (NearPlaneBias?).
- `trunc` vs `round` for the km/h text (`0x46ec10`).
- Behaviour of `0x5322d0` (mode 9 mouse orbit) and `0x531f6c` (mode 8) per-frame motion not decoded in detail.
