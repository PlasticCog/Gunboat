# Simulation subsystem (GB.EXE `0919` update tree, input and key handlers)

Target: `reverse_engineering/out/GB_unp.exe`, DGROUP `1B73`. Addresses as in `RE_GUIDE.md`
(`SSSS:OOOO` file segments; `DS:xxxx`). Read with `tools/fn.py` (index facts, Ghidra C,
disassembly). Ghidra's C is a guide only: many of these routines pass arguments in registers
(`AL`, `CX`, `BX`, `SI`) that its signatures omit, and its switch recovery fails in
`05bd:000a`. The disassembly is authoritative. Symbols: `spec/simulation_symbols.csv`.

Confidence tags: **verified** (read instruction by instruction, or differential-tested by the
Codex-era Unicorn tests), **likely**, **guess**.

## 1. Overview

Gunboat simulates **once per drawn frame**, synchronously, inside the mission loop
`05bd:000a` (`mission_run`, world spec). There is **no frame limiter** while a 3D station is
shown: the original ran as fast as the PC could draw it. All speeds, timers and the mission clock
are per simulation pass, not per unit of real time. **verified**

### 1.1 Mission loop (the parts that drive the simulation)

```
mission_run 05bd:000a  (called by main; world spec for set-up and screens)
  once: load mission (05bd:14d6 mission_load), reset boat state, set up the station screen
  loop:
    if station DS:0086 == 9 (quit to menu): return
    if station / detached camera / look direction changed: redraw that station's screen
    if station == 5 (tactical map): draw the map markers
    input_read_key 0000:07a8 -> DS:EE9C          §3.1
    EA86 = F10B; ECA8 = F132; F10B = B83C; F132 = B839   (flash counters for the cockpit)
    game_frame 0919:8930                          §1.2
    if detached camera DS:D96B: copy the view window, else if station < 5: 05bd:1fd4 (present)
    key_dispatch 0919:038e                        §3.2
    if no joystick (DS:0070 == 0) and time compression DS:B7F1 != 0:
        input_read_key; key_dispatch                (a second key per frame when compressed)
    random()                                      (one RNG step per loop pass)
    if station >= 5: while (DS:08C0 - loop_start_ticks) <= 2: random()
```

`DS:08C0` is a tick counter advanced by the timer interrupt (platform spec). With the PC-speaker
driver installed it advances 4 times per 13 interrupts of a 236.695 Hz timer (**72.83 Hz**);
under `timer_install` (divisor 3400h) it advances at **89.63 Hz**. Only the full-screen stations
(map, damage report, menus: station ≥ 5) wait on it, for at least 3 ticks per loop pass, and they
advance the RNG while waiting. The number of RNG calls therefore depends on machine speed on
those screens. **verified** (`05bd:0437..045b`, `121b:0ce2`, `12ed:00b1`)

> **PORT:** the 3D stations need a frame pacing rule, as in TD3 (PLAN decision 4). Keep the
> original per-frame simulation and add a configurable minimum frame period. The waits on the
> full-screen stations keep their tick rule on the emulated 72.83 Hz counter.

### 1.2 `game_frame` (0919:8930) **verified**

```
game_frame()                                      far, preserves SI ES
  DS:D9B3++                                       (frame counter, word)
  screen_shake_step()                              render3d
  if station DS:0086 >= 5: return
  engine_sound_update()                            sound spec (0919:3cc9)
  0919:8408()                                      terrain cells around the boat (render3d; likely)
  -- camera heading for this station --
  al = DS:D96C                                     (detached-camera heading)
  if DS:D96B == 0:                                 (not detached)
     al = heading[station]  (DS:B81D + station: B81E hull, B81F bow, B820 midship, B821 stern)
     if station == 1 and look DS:F346 != 1:        (pilot looking to one side)
        al -= 0x28; if DS:F346 != 0: al += 0x50    (F346 0: -28h, 1: ahead, else +28h)
  DS:D191 = al                                     (view heading, 256 per turn)
  DS:D192 = heading_fraction[station] (DS:B821 + station)
  DS:D190 = ror8(DS:D192, 3)                       (view heading low byte)
  n = (1 << (DS:DA39 ? 2 : DS:B7F1)) - 1           (time compression 0/1/2 -> 1/2/4 passes)
  repeat n+1 times:                                 §8 world update
     0919:2038()  0919:32fb()  0919:2cad()  0919:2ea8()  0919:1528()
  if DS:D8FD: DS:D8FD = 0; 0919:2d0f(); DS:D96C += 0x80
  0919:17d4()                                      §9 message line
  if station >= 5: goto done                       (a key handler may have changed it)
  repeat n+1 times:                                 §4–§7 boat update, in this order
     reload_tick()           0919:1cf9             §6
     0919:7ebb()             wake, boat_move, camera position   §5
     camera_pitch_bob()      0919:80e0             §5
     0919:2273()             propulsion + headings + gauges    §4
     0919:1ac0()             pilot / autopilot controls        §4
     mission_clock_tick()    0919:1d30             §9
  0919:18af()                                      §6 computer gunners
  0919:2786()  0919:2324()  0919:000e()            cockpit instruments (hud)
  DS:007A = 1; gfx_set_draw_page(1)
  0919:2c35()                                      (hud)
  0919:7158()                                      terrain projection and draw; shore contact §10
  0919:6e5c()                                      objects: update and draw (§8, render3d)
  projectile_tick()          0919:38cc             §7, once per frame
  muzzle_flash_tick()        0919:81f8             §6, once per frame
done:
  DS:007A = 0; gfx_set_draw_page(0)
```

Consequences a port must keep:

* Time compression multiplies the world group and the boat group, but **not** the gunners, the
  terrain contact test, the object update inside `0919:6e5c`, projectiles or muzzle flashes.
  Projectiles fly at the same speed per frame whatever the compression. **verified**
* Motion (`0919:7ebb`) runs **before** propulsion (`0919:2273`) inside each pass, so thrust
  computed in pass *k* moves the boat in pass *k+1*. **verified** (Codex, ORIGINAL_PHYSICS.md)
* Shore contact is found while projecting the terrain for drawing (§10); its effect on speed
  applies to the next frame's motion.

## 2. State

All state is in DGROUP at fixed addresses. Names are in `spec/simulation_symbols.csv`.

### 2.1 Mission record (`DATn.DAT`, 6468 bytes at DS:B84C..D18F) **verified**

`mission_load` (`05bd:14d6`) loads `DAT1..4.DAT` by region `DS:B503` (0 → DAT1, 1 → DAT2,
2 → DAT3, 3 → DAT4; the practice mission forces region 3). Offsets are file offsets.

| DS | File | Size | Content |
|---|---|---|---|
| B84E | 2 | 187 | World grid 17 × 11 cells: `(rotation << 6) | tile` (ORIGINAL_WORLD_FORMAT.md) |
| B959 | 10D | 2 | Count of authored objects; tile scenery is appended from here (OBJECT_FORMAT.md) |
| B95D | 111 | 1000 words | **Object word**: low byte = kind, high byte = flags (bits 0–2 facing, 3–5 damage `0x38`, …) |
| C12D | 8E1 | 1000 words | **Object X** (map units; the boat is object 0) |
| C8FD | 10B1 | 1000 words | **Object Y** |
| D0CD | 1881 | 195 | not yet identified |

Objects are addressed by **byte offset** `2*i` throughout the code (`BX`/`SI` = 2*i).

### 2.2 Boat record (`DAT10.DAT` / `DAT11.DAT`, 189 bytes at DS:D502..D5BE)

Loaded by `mission_load`: `DAT11.DAT` when `DS:F110` is set (practice), otherwise `DAT10.DAT`.
Known fields (Codex-era tests; **verified** for the listed routines):

| DS | Field |
|---|---|
| D503/D504 | Engine indicator/state flags, port/starboard (bit 2 = locked, low 3 bits = mode) |
| D506/D507 | Fuel leak level (3 = none) |
| D508/D509 | Engine condition (2 = cannot start) |
| D50A/D50B | Waterjet damage (3 = intact, 2 = quarter thrust) |
| D513 | Read by the computer gunners (§6) |
| D521/D522 | Engine switch flags (bit 0 = on) |
| D525/D526 | Bow mount condition (bit 0 in either blocks firing) |
| D529/D52A | Midship mount condition |
| D52C | Control-rate index (bits 0–1): waterjet step `DS:D62E[i]`. Not the pilot's health. |
| D52E/D52F | Stern mount condition |

### 2.3 Boat and stations **verified** unless marked

| DS | Size | Field |
|---|---|---|
| 0086 | word | **Station / screen**: 1 pilot, 2 bow, 3 midship, 4 stern, 5 tactical map, 6 chase view screen, 7 damage report, 8 assignment, 9 quit to menu. `< 5` = a 3D station. |
| 0088 | dword | RNG state (§11); bytes 8A/8B are read directly as random bytes |
| 007A | word | Current draw page for the graphics library |
| B503 | word | Region 0–3 |
| B505 | word | Mission number within the region (likely) |
| B7F1 | byte | Time compression 0 / 1 / 2 |
| B804 / B807 / B806 | byte | Weapon fitted at bow / midship / stern (§6) |
| B808/B809 | byte | Engine state, port/starboard: 0 off, 1 running, else start/stop countdown |
| B80A/B80C | word | Fuel, port/starboard (initial C544h) |
| B816/B817 | byte | Throttle (8 = idle) |
| B818 | byte | Waterjet angle: bits 0–6 nozzle (64 = ahead), bit 7 reverse |
| B819 | byte | Speed band `|speed| >> 3` |
| B81C/B81D | byte | Throttle maximum (59; 103 upgraded engines) |
| B81E..B821 | byte ×4 | Heading: hull, bow gun, midship gun, stern gun (256 per turn) |
| B822..B825 | byte ×4 | Heading fractions (eighths) |
| B826/B827 | byte | X/Y fractions of the boat position (1/256) |
| B828 | s8 | Speed |
| B82A | s8 | Wave bob velocity |
| B82B | word | Wave bob position (high byte B82C = camera pitch) |
| B82D | byte | Pitch reference |
| B830 / B831 | byte | Reload counters, stern / midship (ready values 8 / 48) |
| B836 / B837 / B838 | byte | Gun elevation, midship / bow / stern |
| B839 / B83A B83B / B83C | byte | Muzzle flash counters: midship / bow barrels / stern |
| B83D | word | Count of entries in the near-object list `DS:523E` (§8) |
| C12D / C8FD | word | Boat X / Y (object 0) |
| D190 / D191 / D192 | byte | View heading low / high byte, raw heading fraction |
| D193 | byte | View pitch |
| D1BC | 32 bytes | Projectile flight countdowns (§7) |
| D1DC | 32 × 8 | Projectile records: weapon, X, Y, heading fraction, heading, range (§7) |
| D644 | byte | Bow barrel alternation |
| D6B4 | byte | Boat locked while passengers move (§5.2) |
| D70C | byte | Frame counter used by fire rules and AI pacing |
| D8BC | byte | Scene rebuild this frame; enemies and projectiles skip it (§11.2) |
| D8FD | byte | Boat lost next frame (pilot practice, §10): `boat_destroyed` after the world group |
| D967..D969 | byte | Wave bob countdown, period, phase (§5.4) |
| D96B / D96C | byte | Detached camera on / its heading |
| D982 | s16 | Pitch impulse |
| D9B3 | word | `game_frame` counter |
| DA39 | byte | Temporary time compression (held key): forces 4 passes |
| EE9C | byte | Key code for `key_dispatch` |
| F110 | word | Practice mission (invulnerable) |
| F346 | word | Pilot look direction: 1 ahead (set by `mission_load`), 0 = view heading −28h, 2 = +28h (which side is left: to confirm with the key handlers) |

## 3. Input

### 3.1 Key codes and held keys **verified**

The keyboard ISR `kbd_isr` (`121b:0a9c`, platform spec; same code as TD3's) translates
scancodes through code-segment tables `121b:0952` (unshifted), `121b:09C2` (shifted) and
`121b:093E` (E0-prefixed keys) into a **key code** in `DS:DA42`, and keeps a **held-key
bitmask** in `DS:DA43`:

| Key | Code | Held bit (DA43) |
|---|---|---|
| Esc | 80h (pause) | |
| F1–F10 | 81h–8Ah | |
| F11, F12 | 8Bh, 8Ch | |
| Keypad 7 8 9 / 4 5 6 / 1 2 3 | 91h–99h | 7: up+left (05), 8: up (01), 9: up+right (09), 4: left (04), 6: right (08), 1: down+left (06), 2: down (02), 3: down+right (0A) |
| Grey arrows (E0) | own codes | same bits as the keypad |
| Backquote (29h), backslash (2Bh) | 92h, 94h | treated as keypad 8 (up) and 4 (left) |
| Keypad − / + | 98h / 96h | down (02) / right (08) |
| Enter, keypad Enter | 0Dh | 10h (fire) |
| Backspace | 08h | sets **DS:DA39** while held: temporary 4× time compression |
| Ctrl (1Dh) | | sets DS:DA3A while held (Ctrl+Q quits) |
| letters, digits, punctuation | ASCII (Caps Lock and Shift handled) | |

`input_read_key` (`0000:07a8`, `far void(u16 *key)`) runs once per mission-loop pass
(twice with time compression, §1.1). It:

* takes the code from `DS:DA42` and clears it; with no code and the **joystick** enabled
  (`DS:F394`, from `GUNBOAT.CFG`, game_flow §2), reads it through `joystick_read` (`1473:0008`,
  port 201h with calibration limits at `DS:DD17`): its direction becomes a code 91h–99h (the
  keypad codes) and direction bits in **DS:B7F0** (1 up, 2 down, 4 left, 8 right), and the button
  gives Enter (0Dh, +10h in `B7F0`);
* Ctrl+Q (`DS:DA3A` set): `0000:021e` (quit to DOS, game_flow spec);
* `E`: toggles DS:0080; `S`: plays effect 12 and toggles sound mute **DS:007E**;
* Esc (80h) **pauses**: message 33h "Raid suspended." (via `0919:1589`, only in a 3D station
  during a mission, `DS:0082 == 3`), then loops until the next Esc, shows message 34h, and
  records the tick counter in DS:D69F;
* writes the code to **DS:EE9C** (and to `*key`) for `key_dispatch`.

In **demo mode** (`DS:0070 != 0`, set by the title sequence `00f2:000e`) the key comes from
`demo_next_key` (`08e1:006e`) instead: a recorded script of (count, key) byte pairs in the code
segment, at 08e1:0008 + `DS:0C68` (the title sets 0C68 = 8: the first pair is at 08e1:0010). A
pair makes the next `count` calls return 0 and the call after them the key; keys ≥ E0h set the
held bits instead, `DA43 = key − E0h`. The pair 00 00 restarts at 08e1:0008 (four pairs ending
with 'D', then the script again). Any real key press ends the demo: `DS:0070 = 0`, the key
consumed, station 9. Several station locks below are skipped in demo mode so that
the scripted demo can visit every station.

### 3.2 `key_dispatch` (0919:038e) **verified**

```
key_dispatch()                                   far
  code = DS:EE9C
  if code in 01h..7Fh: call near [CS:0290 + 2*(code-1)]    (127 entries, default 0919:03c7)
  elif code in 81h..8Ah and station (word) < 5: call near [DS:D615 + 2*(code-81h)]
  else: controls_poll()                            (0919:0953; also codes 0 and 80h)
```

The default handler `0919:03c7` also calls `controls_poll`. So the held controls are polled on
every pass **except** when a key with its own handler was pressed. **verified** (port, differential
test of every code)

Registers: the handlers are near assembly routines. F1 at the pilot's station (`0919:0520`) and F9
(`0919:0465`) leave SI changed (the engine last switched; the object offset of the last entry in
view), and `mission_run` does not reload SI before `game_frame`, which passes it on (enemy_update
and incoming_fire store it in `DS:D6F6`): the port returns SI from these routines.

### 3.3 Key handlers **verified** (actions); messages are the in-game texts (§9.1)

| Key | Handler | Action |
|---|---|---|
| Z / X / C | `0919:0762/0769/0770` → `0777` | Pilot station, look `F346` = 0 / 1 / 2. Refused in shooting practice (`F110` 1 or 2, not demo): message 19h "Practice shooting, OK?"; captain dead (`D514 & 3 == 2`): 12h "-he's dead, sir." |
| V | `0919:07c0` | Bow station. Refused when `F110 >= 2` (19h, or 1Bh "Practice piloting, OK?" if `F110 != 2`) or bow gunner dead (`D513`). |
| N | `0919:07f6` | Midship station. Refused in any practice (not demo): 19h, or 1Bh if `F110 == 3`; or crewman dead (`D512`). |
| B | `0919:0833` | Stern station. Refused when `F110` is odd (not demo), or gunner dead (`D515`). |
| M | `0919:074a` | Station 5, tactical map; chase view off |
| `.` `>` | `0919:0756` | Station 8, assignment (mission text, world spec §3) |
| `/` `?` | `0919:0871` | Station 7, damage report |
| `,` `<` | `0919:090d` | **Chase boat view** (message 25h): station 2, `D96B = 1`, `D96C = D191` (current view heading), `D8BC = 1`. Ignored if already on. |
| `+` `=` | `0919:087d` | Time compression `B7F1` 0→1→2→0 (messages 2Ch/2Dh/2Eh); going up sets bit 20h of the byte at `DS:6E54 + [DS:6E54]`, going back to 0 leaves the byte as it is (enemy_update toggles that bit, §8.3); panel switch 0Bh set from `D52B & 3` (0→1, 1→2, else 0). 3D stations only. |
| `-` `_` | `0919:0503` | Next **control rate**: panel switch 0Ch (`D52C`) cycles 0→1→2→0. 3D stations only. |
| D | `0919:0930` | Detail level: toggles `D6BF` (0 high, 1 low; messages 36h/37h); `D6C0` = FFh (high) or 16h (low); `D8BC = 1` |
| Tab | `0919:08be` | Message 7 "Return to base.", `B801 = 6`. If `B7FF >= 10h`, or both engines are unusable (`(D508 & D506 & 3) == 2` for each): mission status `B545 = 2` and `B52C = 02d2:2b4a(B52C)` (game_flow) |
| F1 | `0919:0520` | Panel switch per station (`0919:03cb`, table `CS:0270`). Pilot: switch 0, the **main switch** (`D520`, §4.4); switching it off also switches off each running engine (`0919:0608`). Bow: entry 12h, midship: 10h, stern: 1Ch. |
| F2 | `0919:05ae` | Pilot, main switch on: toggle each engine that has fuel and is not wrecked (`0608`). Bow: entries 16h then 14h; midship 0Eh; stern 1Ah. |
| F3 | `0919:056b` | Panel switch: pilot 4, bow 2, midship 6, stern 18h. **Quirk:** entry 4 is toggled whenever AL is 1 after the gun station's toggle (the AX `panel_switch_toggle` returns), as it is for the pilot (station low byte 1). |
| F4 | `0919:0667` | "Pilot, reverse course." (1Eh): toggles `D684`, recomputes the route (`8754`/`87e8`, §4.4). Not ported yet (needs `route_point`, `route_advance`). |
| F5 / F6 | `0919:0649/0658` | "Pilot, branch left / right." (20h / 23h): `D685 = 1 / 2` |
| F7 | `0919:06ee` | "Pilot, slower." (30h): computer-pilot throttle `D680 -= 15`, minimum 8 |
| F8 | `0919:06a9` | "Pilot, faster." (1Ch): `D680 += 15`, at most `B81C` and `B81D` |
| F9 | `0919:0465` | "Identify target." (4): nearest identifiable object in view (§6) |
| F10 | `0919:043c` | Crew fire at will `B82E`: "Open fire!" (6) / "Cease fire." (9) |

The pilot commands (F4–F8) queue the crew's answer (`0919:0713`): `B800 = 1` and `B802` =
21h "-I can't, you're piloting." (at the pilot station, not in chase view), 12h "-he's dead,
sir." (captain dead), 24h "-we're not moving, sir." (speed 0), else 2 "Aye-aye, sir!". F8 at
the maximum answers 32h "-maximum speed, sir." (only when the port maximum `B81C` capped the target,
not the starboard one: quirk; a queued 24h becomes 2 first). F7 down to idle sets `B802 = 24h`. While
`B800 == 2` all messages are suppressed (§9). The reply is 21h at the pilot station only when the
station's low byte is 1 and the chase view is off.

Panel switches (`0919:03cb`, `0919:0178`, `0919:0027`) set the switch byte `DS:D520 + n` and
redraw it. The table at `CS:0270` gives, per entry, the switch number and the indicator to redraw
(FFh = none). The drawing belongs to the hud spec. **verified** (port, differential test)
`panel_switch_toggle(BX = entry offset)`: the switch is set (switch_draw mode 00h) to its byte xor 1,
then its lamp to 3 while the switch is on (bit 0 clear), 0 when off. A switch number above 80h (none
in the table) would drive two lamps n and n + 1 with opposite values. The entries: 00h switch 0 /
lamp 0; 02h 8 / 16h; 04h 4 / none; 06h 0Dh / 1Ah; 08h 1 / 1 and 0Ah 2 / 2 (the engines,
`engine_switch`); 0Ch 0 / 0 (the pilot's main switch); 0Eh 0Ah / 19h; 10h 9 / 18h; 12h 5 / 17h;
14h 6 / 14h; 16h 7 / 15h; 18h 10h / 1Dh; 1Ah 0Fh / 1Ch; 1Ch 0Eh / 1Bh. It returns the AX the lamp
routine leaves (00FFh when there is no lamp); the callers pass AX on (`panel_redraw_all` after F1
stores its AH in `scratch_b7e3`).

### 3.4 `controls_poll` (0919:0953) **verified**

```
controls_poll()
  bits = DS:DA43 | DS:B7F0                 (held keys | joystick)
  if station >= 5 or (bits & 1Fh) == 0: return
  if chase view (D96B): up/down: view distance D96D -= 8 (min 28h) / += 8 (not past 0);
                        left/right: D96C += 4 / -= 4; return
  if main switch off (D520 bit 0 set): bits &= 0Fh; if 0: return     (no firing)
  rate = D52C & 3
  station 1: if main switch off: return
             if bits & 10h: pilot_slow_down()          0919:0e19
             pilot_throttle_controls(bits, rate)        0919:0bbd  (§4.1)
  station 2: if bits & 10h: fire_bow()          then aim_bow(bits, rate)        0919:0b34
  station 3: if bits & 10h: fire_station3()     then aim_midship(bits, rate)    0919:0aab
  station 4: if bits & 10h: fire_station4()     then aim_stern(bits, rate)      0919:0a22
```

Aiming:

* Up (1): `elevation += DS:D629[rate]`, not applied if it carries past FFh. Down (2):
  `elevation -= D629[rate]`, applied only if the result is ≥ 60h. Range 60h–FFh.
* Left (4) / right (8): the gun heading steps by `D629[rate]` eighths (`0919:0f42` / `0f0b`,
  §4.3). The move is **refused** if the new heading relative to the hull `B81E` falls in the
  blocked arc: bow gun `(rel − 60h) ≤ 41h` (rel 60h–A1h, behind); midship and stern
  `(rel + 20h) ≤ 41h` (rel E0h–21h, ahead). Each move (refused or not) clears `D965`/`D966`
  (`view_sky_top`: the view rows to redraw, render3d §3.1).
* `DS:D629` = 20h, 10h, 04h, 02h for control rates 0–3 (eighths of a heading unit per poll).

Firing happens **before** aiming in the same poll. With the main switch off, the gunners can aim
but the fire bit is dropped.

## 4. Pilot, engines and propulsion

The kernels in this section are ported (`src/game/sim_motion.cpp`) and differential-tested against
the original on all memory (`test_core.py`, `test_frame.py`).

### 4.1 Pilot controls **verified**

* `pilot_throttle_controls` (`0919:0bbd`, bits in CL, rate in BX): four rounds of
  `throttle_step(0)`, `throttle_step(1)` (`0919:0c6a`). A throttle only moves while its engine
  runs (`B808+i == 1`). Up (1) raises it toward `B81C+i` in forward, or lowers it toward the
  other engine's throttle in reverse; at idle (8) the reverse bit `B818` bit 7 flips and the
  throttle rises again. Down (2) is the mirror. With left/right: `0919:3479` (decrements the low
  five bits of the eight timers `DS:D70E..D715` that are nonzero), then the jet angle moves by
  `DS:D62E[rate]` (8, 4, 1, 1) with a detent at 64 (ahead) and limits 0 and 7Dh; the jet
  indicator phase `D632` advances (left −1, wrapping to 2; right +1, modulo 3) and the indicator
  is redrawn (`0919:0f77`, hud). Hitting a limit skips the phase change and the redraw.
* `pilot_slow_down` (`0919:0e19`, Enter at the pilot station): four times, each throttle above
  idle that is not below the other is lowered by one.

### 4.2 Engines **verified**

`engine_switch(si)` (`0919:0608`) toggles panel entry `8 + 2*si` (switch 1 / 2 and lamp 1 / 2).
The engine reacts only when its lamp's low 3 bits (`D503+si`) are 0 or 3 (a lamp of 3 is first
set to 2); any other lamp value leaves the countdown alone. In a countdown (`B808+si & FCh`
nonzero) the countdown reverses: `B808 = (B808 & FCh) xor FFh`. Otherwise it starts: **7Fh when the
switch is now on** (it counts down by 2 to 1: running) and **80h when off** (it counts up by 2 to 0:
stopped). (Earlier drafts had the two values swapped.) **verified** (port, differential test)

`engine_thrust(si)` (`0919:2529`, returns CX = forward thrust, DX = turning thrust) **verified**
(port, differential test):

```
if fuel tank condition D506+si & 3 != 3:            (leaking)
   fuel B80A+2si -= 16 * condition; at or below 0: fuel_out(si)
state = B808+si
  0:                   stopped: throttle = 0, jet reverse bit cleared, CX = DX = 0
  80h..FFh (stopping): state += 2 (+1 if that reaches 0)
  2..7Fh (starting):   state -= 2 (-1 from 2); reaching 1 from 2 or 3 sets the engine lamp to 3
  after a countdown step: state 0 -> stopped as above; else throttle = 8 - ((state & 70h) >> 4),
                       jet reverse bit cleared, CX = DX = 0
  1 (running):         p = max(throttle - 9, 0) (vec_factor B7E5)
                       j = jet_angle (bit 7 included) + DS:D6B6[si] (10h, F0h), |j| as a signed
                       byte (B7E2); f = j, or 80h - j above 40h (B7E3)
                       CX = hi(p * 80h * sine[4f]), halved and negated in reverse (jet bit 7)
                       DX = hi(p * 80h * sine[100h - 4f]), negated when j > 40h
if (DS:008B & 1Fh) == 0: fuel -= throttle >> 2; at or below 0: fuel_out(si)
if station low byte == 1 (quirk: only at the pilot's station) and waterjet D50A+si & 3 != 3:
   CX, DX >>= 1 (arithmetic); twice for a destroyed waterjet (2)
fuel_out(si): fuel = 0; engine_switch(si) if its switch is on; switch byte D521+si = 15h;
              lamp D503+si = 95h
```

### 4.3 Propulsion (`0919:2273`) and headings **verified**

```
propulsion()
  -- wake (outside the Codex test) --
  if detail high (D6BF == 0) and --D6BA == 0:
     D6BA = 11h
     t = port throttle if > 0Bh, else starboard throttle if > 0Bh, else skip the wake
     D6BA = 11h - ((t-8) >> 3)
     BX = free_temp_object()                           0919:6f2a
     object word = 3Fh | (46h - ((t-8) >> 1)) << 8;    object X/Y = boat X/Y
  -- Codex-verified part (image B451..B4A8) --
  (fwd0, turn0) = engine_thrust(0); (fwd1, turn1) = engine_thrust(1)
  target = high byte of (fwd0 + fwd1); accelerate_speed(target) twice      0919:29f9
  torque = turn0 + turn1; B7E2 = (torque < 0); amount = min(|torque| >> 9, 37h)
  DS:D62D = amount                                    (step-table slot 4; the helpers use BX = 4)
  if torque < 0: rotate_headings_plus()    0919:0e95, all four headings
  else:          rotate_headings_minus()   0919:0e48, all four headings
                 B819 = |speed| >> 3                  (written only on this branch)
  if station == 1: 0919:2810                         (pilot gauges, hud)
```

* `free_temp_object` (`0919:6f2a`) returns BX = 2·i for the first object `i` in 1..35 whose
  kind byte is 0. **Quirk:** object 35 (BX = 46h) is returned whether it is free or not, so when
  1..34 are all busy the caller overwrites object 35. **verified** (port, differential test)
* Heading steps (`0919:0f0b` plus, `0919:0f42` minus; AL = fraction in eighths, AH = heading,
  BX = table index): the step `DS:D629[BX]` is added to (subtracted from) the fraction; an
  unrolled chain then carries at most **8** whole units into AH, and the fraction keeps its low 3
  bits. The 8-bit add can overflow for large steps; the port must reproduce the chain exactly.

### 4.4 Main switch **verified**

Panel switch 0 (`DS:D520`) is the **main switch**. As with every panel switch, **bit 0 set =
off**. Normal missions start with it off (`DAT10.DAT`: D520 = 11h), and with both engine
switches (`D521`/`D522` = 11h) and all gun mounts (`D525/26` = 21h, `D529/2A` = 41h,
`D52E/2F` = 09h) off. Practice missions (`DAT11.DAT`) start with the main switch, the port
engine and the stern mounts on. While the main switch is off, `controls_poll` ignores the pilot
controls and the fire key, and the crew pilot does not steer (§4.5). Switching it off (F1 at the
pilot station) also switches off every running engine.

### 4.5 Crew pilot (`0919:1ac0`, every boat pass) **verified**

When the player is not steering, the captain follows the river route network (§4.6).

```
crew_pilot()                                          0919:1ac0
  if not demo and station == 1 and not chase view:    (player is piloting)
     D682 = D683 = 0; D681 = 2 (search the route next time)
     D680 = max(8, min(B816, B817))                   (crew throttle follows the player's)
     return
  c = D514 & 3                                        (captain condition)
  if c == 2: return                                   (dead: nobody steers)
  if (DS:0088 low byte & DS:B832[c]) != 0: return     (reaction gate; B832 = 00 01 07 00)
  crew_pilot_decide()                                 0919:1b2d
  if main switch off: return
  pilot_throttle_controls(bits = D682, rate = D683)   (same routine as the player, §4.1)
```

`crew_pilot_decide` (`0919:1b2d`) **verified** (port, differential test and route walks):

* Throttle: compares `min(B816, B817) & FCh` with `D680 & FCh`: above → "down" (2); below →
  "up" (1) if `D680 > 8`, else nothing. Stored in `D682`.
* State `D681`: 0 = idle (throttle only), 1 = following a waypoint, 2 = search
  (`route_find`, §4.6; no steering on that pass); above 2: nothing.
* Following: `bearing = atan(D68B − X, D68D − Y)` (`0919:3712`), `rel = bearing − B81E + 80h`.
  `rel ≤ 48h` → jet left (4), rate index `D683 = 0`; `rel ≥ B8h` → jet right (8), `D683 = 0`;
  otherwise `D683 = 1`, or 2 when `rel` is within 70h–90h, and the target jet angle
  `((rel − 40h) · 2 + 2) & FCh` is compared with `B818 << 1`: equal → no jet key, less → left,
  more → right. The chosen bit is ORed into `D682`. **`D683` is used as the control-rate index**:
  the captain turns hardest when the waypoint is far off the bow.
* Within 16 units of the waypoint on both axes (`((pos − wp + 10h) & FFE0h) == 0`), the next
  waypoint is taken: `route_point(D686, D689)`, then `route_advance`.

Crew pilot state: `D680` throttle target, `D681` state, `D682` key bits, `D683` rate index,
`D684` direction along the route (0/1), `D685` branch command (0 straight, 1 left, 2 right),
`D686` waypoint index, `D687` previous index, `D688` link byte, `D689` grid cell, `D68B/D68D`
waypoint X/Y.

### 4.6 River route network **verified** (data layout; port, differential test), **likely** (link meanings)

The TILE.BIN entries that ORIGINAL_WORLD_FORMAT.md calls "collision entries" (three bytes, count
in header byte 3) are **route waypoints**: `link, x, y` in tile-local coordinates.

* `route_point(cl = index, bx = cell)` (`0919:8754`): cell = `row*17 + col`; origin
  `D9A9 = col*400h`, `D9AB = (28h − 4*row) << 8`; tile = `DS:B84E[bx] & 3Fh`, rotation =
  bits 6–7 (`B7E2`). If `index >= waypoint count` → `DH = FFh` (AX = 2·tile, CX unchanged).
  Otherwise `link` is rotated
  when it is a positive direction value: `((link − 1 + rotation) & FBh) + 1`; the local point
  is rotated by `0919:8711` (the tile rotation of ORIGINAL_WORLD_FORMAT.md) and returned as
  `AX = x*8 + D9A9`, `CX = y*8 + D9AB`, `DH = link` (BX and ES kept; DL = 28h − 4·row, SI
  changed). The record: the three counts of header bytes 0–2 summed in 8 bits, times 4 bytes,
  come before the waypoints. The row and column come from `DIV` by 17: a cell of 4352 or more
  (a negative cell) is a divide error, R6003 (see the north edge below).
  The shipped TILE.BIN has waypoints with link FFh (e.g. four in tile 17, open water); they read
  as "no waypoint" (DH = FFh) everywhere.
* `atan` (`0919:3712`, CX = dx, DX = dy) → AL = bearing (256 per turn), via an octant fold, a
  ratio `(small << 8) / large` and the table `CS:3606`; side results in `B7E2`/`B7E3`/`D730`.
* `route_find` (`0919:1c00`, state 2): cell `D689` from the boat position, `row = 10 − (Y >> 10)`
  (8 bits, times 17), `col = X >> 10`. No waypoint 0 (DH = FFh): `D683 = 0`, stays in state 2.
  Else it scans the waypoints from index 0 until the first DH = FFh (the end, or a waypoint with
  link FFh): a waypoint whose bearing (atan) is within ±28h of the hull heading
  (`(bearing − B81E + 28h) & FFh ≤ 50h`) is a candidate; the nearest by `|dx| + |dy|` (16 bits)
  wins (`B7DC`, `B7EA`). In real missions (`F110 == 0`) every candidate sets the direction
  `D684`: 0 when it is not nearer than the best so far, 1 when it is nearer than an earlier
  candidate (the first one leaves it). `B7ED` counts the candidates after the first, from FFh:
  with exactly two candidates (`B7ED == 0`) the direction is then set from the winner's link:
  1, or 0 when its bit 6 is set (in practice too). No candidate: `D683 = 0`, state 2. Else the
  winner is the waypoint (`D686`, `D68B/D68D`, `D688 = 0`), state 1.
* `route_advance` (`0919:87e8`, DH = link, CL = index, BX = cell; returns DH, which F4 tests):
  no route (`DH == FFh`): unless the mission is ending (`B800 == 2`) the answer 22h "-I don't
  know where to go!" (`B800 = 1`, `B802 = 22h`, `B801 = 0Ch`); state 2. Otherwise the step
  (added to the index in 8 bits):

  | link | forward (`D684 == 0`) | backward (`D684 != 0`) |
  |---|---|---|
  | 0 | +1 | −1 |
  | 1–3Fh | leave the tile | −1 |
  | 40h–7Fh | +1 | leave the tile |
  | 80h–BFh (fork) | `link & 3Fh` if `D685 == 1`, else +1 | after a fork (`D688 ≥ F0h`): +1 and `D684 = 0`, or −1 (`D684` stays 1) if `D685 == 2`; else −1, or `link & 3Fh` and `D684 = 0` if `D685 == 2` |
  | C0h–FEh | +1 | the link as a negative number |

  The new index goes to `D686` (the old one to `D687`), the link to `D688`, the cell to `D689`,
  the point (`route_point`, which may say FFh at the end of a route: then `D68B` = 2·tile and
  `D68D` = CX with CL the index) to `D68B/D68D`; state 1.
  Leaving the tile through edge `(link − 1) & 3`: `D688 = 0`, the cell moves by the word
  `DS:D68F[edge]` (+1, +17, −1, −17), and the new tile's waypoints are searched from index 0
  for the one whose `(X >> 3) & 7Fh` equals the entry `DS:D697[edge]` low byte (4, 0, 7Ch, 0),
  or, when that is 0, whose `(Y >> 3) & 7Fh` equals its high byte (7Ch or 4). Found: it becomes
  the waypoint in the new cell with the direction 1 when its link is below 40h, else 0. None:
  `D684 ^= 1` (the boat turns round) and `route_advance` runs again (recursively) on the current
  waypoint `route_point(D686, D689)`. With `D684` 0 or 1 (all its writers) and the callers
  passing the current waypoint's link, the recursion ends at the next level (with another `D684`
  value `^= 1` never clears it and the recursion does not end).
  F4 "reverse course" toggles `D684` and re-runs this logic.

**Bug of the original (port-verified): the north edge.** Leaving a tile of row 0 northward
(edge 3, −17) gives a negative cell, and `route_point`'s `DIV` by 17 overflows: R6003, the game
ends. The shipped worlds have such exits: Mare Island (practice) at cell 1 (waypoint 0, link 44h)
and cell 10 (waypoint 6, link 04h); the Vietnam map (all eight missions) at cell 9 (waypoint 0,
link 44h) and cell 11 (waypoint 6, link 04h), at Y = 2BE0h. The captain reaches them when he
steers up there: whenever the player is not in the pilot's seat (a gun station, the map), and he
searches the route afresh each time the player leaves it. Leaving cell 0 westward (−1) fails the
same way; the other west and east exits wrap to the neighbouring row, and the south exits of row 10
read the bytes after the grid as tiles (no error). **The port does not keep it** (PORT, since
2026-10-01, after three players' reports of R6003 there, GitHub issues #1-#3): `route_advance`
treats a cell of 4352 or more as a tile without an entry waypoint, so the captain turns round at
the map's edge. `route_point` itself still divides as the original (its tests unchanged); the
differential tests accept the turn where the original's divide comes from `route_advance`'s scan
(`DivideStop.turned`).

## 5. Motion, mission stops and camera

### 5.1 `boat_motion` (0919:7ebb, every boat pass) **verified**

```
boat_motion()
  object[0].flags (DS:B95E) = (B81E + 90h) >> 5      (boat sprite facing, for the chase view)
  if speed B828 == 0: mission_stop()                  0919:1e87  (§5.2)
  else:               boat_move()                     0919:7ee8  (Codex-verified, ORIGINAL_PHYSICS.md)
  (CX, DX) = camera_position()                        0919:8229  (§5.3)
  D972 = CX; D974 = DX                                (camera in 1/4 units, renderer input)
```

`camera_pitch_bob` (`0919:80e0`): §5.4.

#### `boat_move` (0919:7ee8) and its helpers **verified** (disassembly; port differential test)

All values are bytes, all arithmetic 8-bit with the carries shown. `sine_table` (`CS:3502`) has
a 4-byte stride; only the high byte of each word is used. `B7E2..B7EA` are shared scratch bytes
(names in `simulation_symbols.csv`).

```
vec_scale(AL)                                         0919:3706
  AX = AL * vec_factor B7E5 (MUL, unsigned); B7E9 = AL; B7EA = AH; return AX

heading_vector(AL = angle)                            0919:7e5f
  vec_angle B7E7 = angle
  i = angle & 3Fh; if angle & 40h: i = 40h - i       (as (i xor FFh) + 41h)
  vec_sin B7E8    = high byte of sine_table[i]
  vec_factor B7E5 = high byte of sine_table[40h - i]
  a = speed B828; if a < 0: vec_angle ^= 80h; a = -a (8-bit, -80h stays 80h)
  return vec_scale(a)                                 (AX; BX is left as a table address)

boat_move()
  heading_vector(hull heading B81E)                   |speed| x cos -> B7EA
  -- Y: B7E2 = (B7EA >> 5) & 3, B7E3 = B7EA << 3 (bit 7 of B7EA is lost)
  if (B7E7 & C0h) is 40h or 80h:                      (moving towards smaller Y)
     B827 -= B7E3, borrow -> B7E2++
     w = boat_y C8FD; lo(w) -= B7E2
     if borrow: if hi(w) < 5: skip the store (clipped) else hi(w)--
  else:
     B827 += B7E3, carry -> B7E2++
     w = C8FD; lo(w) += B7E2
     if carry: if hi(w) >= 27h: skip the store else hi(w)++
  C8FD = w (unless skipped; the fraction B827 is kept even when the store is skipped)
  -- X: vec_factor B7E5 = vec_sin B7E8; vec_scale(|speed|); B7E2 and B7E3 as for Y
  if B7E7 & 80h: B826 -= B7E3 ... boat_x C12D, low limit hi < 5
  else:          B826 += B7E3 ... C12D, high limit hi >= 3Fh
  -- water marks (render3d §3.2): the view's heading relative to the hull
  heading_vector(heading[station - 1] - B81E)         (DS:B81D + station; stations above 4 read
                                                        the bytes after the heading array)
  B7E2 = 0
  if (B7E7 & C0h) is 40h or 80h:
     B7E2 = B7EA >> 6; B7E3 = B7EA << 2
     water_phase_fraction B829 -= B7E3, borrow -> B7E2++
     water_phase D94D += B7E2
  else:
     B7E2 = B7EA >> 6; B829 += (B7EA << 2), carry -> B7E2++
     D94D -= B7E2
  B7E5 = B7E8; vec_scale(|speed|)
  n = B7EA >> 3; if n == 0: return
  k = (D94D >> 2) & 1Fh                               (the mark of the first horizon row)
  for row in 0..15:                                   (two marks per row entry)
     e = water_shift_table CS:7DBF[n + 10*row]        (n can exceed 9: the read then runs into
                                                        the next rows, and past the 160-byte table
                                                        into the code of heading_vector)
     whole = e & 3; frac = e & FCh
     twice (k, then k = (k+1) & 1Fh):
        if B7E7 & 80h: D984[k] += frac, carry -> whole+1; D90D[k] -= whole+carry
        else:          D984[k] -= frac, borrow -> whole+1; D90D[k] += whole+borrow
     k = (k+1) & 1Fh
```

The Y and X steps are 1/256 of a map unit per unit of `B7EA` × 8 (10-bit step, integer part 0–3),
the water phase uses × 4.

### 5.2 `mission_stop` (0919:1e87): passengers **verified**

Only for mission types `DS:B509` = 2 (insertion) and 4 / 0Ah (extraction); `B505` is the
mission number, `row = DS:B909 + 10*B505` (five object offsets, §2.1 mission table).

```
if not locked (D6B4 == 0):
   if (D505 & 3) != 2: return                       (objective not pending)
   obj = row[0]; if |obj.X - boat.X| >= 80h or |obj.Y - boat.Y| >= 80h: return
   D6B4 = 1 (boat locked); D6B5 = 5
   for k in 0..4:   object[1+k]:
      flags 5 - k (5, 4, 3, 2, 1: the loop counter is the word's high byte)
      insertion: kind 25h, start = boat position with X spread (X + (word >> 4) - 20h)
      extraction: kind 24h, start = position of object row[0] + 2k (the row's first object and
                  the four objects after it, not row[k])
else (locked):
   for each passenger k in objects 1..5 that is still present:
      target = boat (kind 24h) or object row[k] (kind 25h)
      if |dx| + |dy| < 10h: remove it (word = 0); if --D6B5 == 0: done
      else: move_toward(target, step 2)              0919:1fc3
   done: D6B4 = 0; B544 = 5; D505 = (D505 & FCh) | 3; sfx_play(4);
         message 16h "MISSION ACCOMPLISHED!" (via 0919:1565)
```

While `D6B4` is set, `accelerate_speed` does not change the speed (Codex: `movementLocked`).
The `mission_stop` pseudocode is **verified** (port, differential test). It leaves SI at the last
row object it loaded (and DI changed); the port returns SI.
`move_toward` (`0919:1fc3`, BX = object offset, CX/DX = target, steps `D6A2`/`D6A4`): the step
on the axis with the smaller distance is halved; each axis moves by at most its step toward the
target. **verified** (port, differential test) Exactly:

```
move_toward(bx, cx, dx)
  ddx = cx - X[bx]; ddy = dx - Y[bx]                   (16-bit; |.| by the sign of the result)
  if |ddx| != |ddy|: halve (shr, in memory) D6A2 if |ddx| < |ddy|, else D6A4
  if ddx < 0: D6A2 = -D6A2 (in memory); mx = ddx if ddx > D6A2 (unsigned) else D6A2
  else:       mx = ddx if ddx < D6A2 (unsigned) else D6A2
  X[bx] += mx; the same for Y with D6A4
```

Both callers (`mission_stop`, `missile_update`) reload the steps before each call, so the halving
and the negation stored in `D6A2`/`D6A4` (a sign that flips on every call toward smaller values)
never accumulate in the game.

### 5.3 `camera_position` (0919:8229) **verified**

```
chase_view_collision()                              0919:82de, only in chase view, every 8th D70C frame:
   if terrain_rect_test(boat position in 1/4 units) hits (0919:7c38, B7E2 != 0):
        boat X/Y = last good position D976/D978
   else D976/D978 = boat X/Y
tries = 0; heading = D96C
loop:
   offset = chase view ? polar_small(D96C, distance D96D) : (0, 0)        0919:8331
   camera = boat + offset; if not chase view: D976/D978 = camera
   D96E/D970 = camera (map units); CX/DX = camera << 2 | fraction bits B826/B827 >> 6
   if chase view and terrain_rect_test(CX, DX) hits:
        tries++; if tries > 20h: chase view off (D96B = 0), D8BC = 1, D96A = 1; return
        D96C = D191 = original heading −8, +8, −16, then +16, +24, +32, … (try n); retry
return CX, DX
```

The ordinary boat does **not** use `terrain_rect_test`; it collides through the projection
(§10). In chase view the boat also gets this coarse test every eighth frame.

### 5.4 `camera_pitch_bob` (0919:80e0, every boat pass) **verified** (port, differential test)

```
camera_pitch_bob()
  p = D982 (s16): decays by 4 toward 0 (a step past 0 gives 0); clamp to -18h..18h; D982 = p
  B82D = (-(low byte of 2p + 80h)) & F8h                   (pitch reference)
  step = (B82A << 8) sar 1                                  (bob velocity / 2, in 1/256)
  sum = step + B82B (16-bit, carry c)
  store if step >= 0: no carry; if step < 0: a carry and sum != 0
  if store: B82B = sum
  else:     D968 = D968 - D967; D967 = 1                    (the swing ends now)
  v = B82C (after the store)
  if station low byte >= 2: v = elevation (bow B837 for 2, midship B836 for 3, stern B838 above)
                                - (station high byte : B82D) + v; if negative: 8
  D193 = (clamp(v, 8, 1F0h) >> 3) + 43h                    (view pitch, unsigned)
  if --D967 != 0: return
  D967 = D968; D969 = (D969 + 1) & 3
  if D969 odd: B82A = -B82A; return                          (the swing turns back)
  m = (DS:008A * 101h) & bob_masks[sea state B4FF]        (word table DS:D97A, not bounded)
  period = low(m) + 1Dh - 3 * speed band B819 (8-bit), 0 -> 1; D968 = D967 = period
  B82A = rol(high(m), 2) + 1 + B819, negated when D969 & 2
```

`bob_masks` = 0001h, 4003h, 4007h, C003h for sea states 0-3.

## 6. Guns, gunners and identification

### 6.1 Weapons and firing **verified** (Codex, ORIGINAL_WEAPONS.md: 4,000 + 10,240 cases)

| Station | Fitted `B80x` | Weapon ID | Sound | Rule | Likely weapon |
|---|---|---|---|---|---|
| 2 bow (`fire_bow` 0919:0dbd) | `B804 == 0` | 4 | 1 | barrels alternate (`D644`), flash `B83A`/`B83B` = 2 | twin M2HB .50 |
| 2 bow | `B804 != 0` | 5 | 3 | every request, flash `B83A` = 2 | minigun |
| 3 midship (`fire_station3` 0919:0d45) | `B807 == 0` | 3 | 8 | only when reload `B831 == 30h`, which it decrements | mortar |
| 3 midship | `B807 == 1` | 4 | 1 | when time compression is on, or on odd `D70C` frames | .50 |
| 3 midship | `B807 >= 2` | 1 | 2 | every request | M60 |
| 4 stern (`fire_station4` 0919:0cf4) | `B806 == 0` | 2 | 8 | only when reload `B830 == 8` | M129 grenade launcher |
| 4 stern | `B806 != 0` | 1 | 2 | every request | M60 |

Weapon names are **guesses** from the sounds, the reload values and the cockpit art (`BG`/`BF`
bow, `BM`/`BR`/`B6` midship, `BGR`/`B6` stern, §2 mission_load). The mount condition bytes
(bit 0 = switched off or disabled) block firing. Each shot: `B7F8 = elevation`,
`projectile_launch(CH = heading, CL = fraction, BX = weapon)` (`0919:0ee2`, §7), flash counter
set, `sfx_play(sound)`. Firing is requested once per `controls_poll` or gunner pass; there is no
other rate limit, heat or ammunition.

`reload_tick` (`0919:1cf9`, every boat pass): a reload counter below its ready value counts down
to 0, then is restored to 8 / 30h; each step redraws the mount's lamp (19h midship, 1Ch stern) with
2 (reloading) or 3 (ready). **verified** (port) `muzzle_flash_tick` (`0919:81f8`, once per frame)
counts the flash counters down.

### 6.2 Crew gunners (`0919:18af`, once per frame) **verified**

Active only with fire at will (`B82E != 0`, F10) and not in chase view. Order: bow, stern,
midship.

```
for gun in (bow 18c7: heading B81F, fraction B823, elevation B837, index 0, memory D67D, crewman D513, rng bit 20h)
           (stern 190d: B821, B825, B838, 1, D67E, D515, 40h)
           (midship 1953: B820, B824, B836, 2, D67F, D512, 80h; skipped when B807 == 0):
   skip if the player is at this station (not in demo)
   skip if the crewman is dead (condition 2)
   if wounded (condition 0 or 1) and (DS:0088 & rng bit) != 0: skip this frame
   gunner_aim(gun)                                   0919:19a0
```

`gunner_aim` scans the visible-object list (§8.2) from its last entry down to 0. The list is
sorted far to near, so the last entries are the nearest: **the scan ends** (and the gun sweeps) at
the first entry whose distance class `DS:4C97[2i]` is above 12h (earlier drafts made it a filter).
**verified** (port, differential test)

* candidate: kind (`B95D` low byte of object `DS:523E[i]`)
  in 01h..17h and not 10h or 11h; relative bearing
  `a = 4E00[i] + D191 − gun heading − 38h`, accepted if `a <= 28h` (unsigned);
  line of sight clear (`0919:348e` returns CH = 0).
* first candidate found: `a −= 14h` (0 = centred).
  * `|a| > 4` (unsigned, `a` in 5..FBh): turn left if `a >= 80h` else right, rate index 0 (fast).
  * else fine bearing `(a·8 + 4EB5[i] + D192 − gun fraction) & F8h`; nonzero → turn left/right at
    rate 2.
  * aligned: elevation error `e = sat(4F6A[i] + 0Eh) + gun elevation` (carry → down);
    `(|e| & FCh) == 0` → **fire**; else up/down at rate 1 (rate 2 when `|e| < 8`). Wanting to go
    down with the elevation already ≤ 68h also **fires**.
* no candidate: sweep. Direction `left` if `(memory & 1) == 0` else `right`, rate 1; if the
  heading did not change (the arc limit refused it), flip the memory bit.

Turning and elevation use the player's aiming routines (`0919:1aa3` → `aim_bow`/`aim_stern`/
`aim_midship`, §3.4) with the chosen rate index, so the arc limits apply to the crew too.

The gun is described in shared scratch variables: `DS:B7DE` (the hud's `gauge_row`) holds the
address of the gun's heading byte, `B7E3` its fraction, `B7E2` the gun (0 bow, 1 stern, 2 midship),
`B7E9` its elevation, `B7DC` the address of its sweep byte (`gunner_sweep`, `DS:D67D..D67F`).

Registers: `crew_gunners` loads AX = the word `B82E` and returns it when the crew does not fire at
will (or in chase view); otherwise AX is what the last gunner leaves (AH is carried through, AL the
last value loaded; after an aim or a shot the AX the aiming / fire routine leaves: `aim_*` return AL
= the stepped fraction and AH = the relative heading when they turn, `projectile_aim` AH from its
last product and AL = 1 in the fourth quadrant). SI is left at the last object offset or 2·entry the
scan loaded. `game_frame` passes AX to `jet_marker`, `throttle_needles` and `panel_blink` (whose
lamps store AH in `scratch_b7e3`) and SI to `projectile_tick` (`DS:D6F6`). **verified** (port,
differential test of AX and SI)

### 6.3 Identify (F9, `0919:0465`) **verified**

Scans the visible-object list from the end down to entry `DS:B83F`. For the pilot looking
sideways the entry's bearing is offset by ±28h. An entry is in view when
`((4E00[i] − 44h) << 3 | 4EB5[i]) >> 1 < 40h` (arithmetic shift, sign from bit 10). Rules by
kind: 0 skip; objects 0..35 skip (boat, passengers, temporary effects); distance class 0 skip;
kind 3Ch → keep as candidate; kind > 39h skip; kind 39h → stop and use the candidate so far;
kind < 28h → **select immediately**; 28h–38h → keep as candidate if none yet. The result goes to
`D60F` (object offset or 0) and message 4 prints the identification (§9).

## 7. Projectiles, hits and damage

### 7.1 Projectiles **verified** (Codex)

32 slots. Flight countdown `DS:D1BC[p]` (0 = free); record `DS:D1DC + 8p`: weapon (1), impact
X (2), impact Y (2), heading fraction (1), heading (1), range (1). `projectile_alloc`
(`0919:37af`) takes the highest free slot, or slot 0 when full. `projectile_aim` (`0919:37c7`)
turns the elevation `B7F8` and the gun heading into a flight time and an impact point with the
sine table at image `C692` and the aim limits `DS:D63E[weapon]`. Projectiles are **aimed impact
points**, not rays. Exactly (**verified**, port differential test):

```
projectile_launch(bx = weapon, ch = heading, cl = fraction)           0919:0ee2
  (slot, si = 8*slot) = projectile_alloc()                            0919:37af
  e = elevation_add_clamped(B82C - B82D)                               0919:3b3b: signed AL + B7F8,
                                                                        clamped to 0..FFh by the carry
  e = min(e, weapon_elevation_limit D63E[weapon])                      (E9 E7 EB EB E9 for 1..5)
  projectile_aim(al = e, ah = weapon, bx = slot, cx, si)               0919:37c7
projectile_aim
  D1BC[slot] = max((e >> 3) - 0Eh, 0) + 1                              (sign test of the byte result)
  record: weapon, fraction, heading; range r = max(-e - 13h, 0) (8-bit)
  k = 7FFFh if r <= 1 else FFFFh / r
  B7F5 = heading >> 6 (quadrant); i4 = 4 * (heading & 3Fh); f = fraction << 6
  s = sine[i4] (words at CS:3502 + i4), interpolated toward sine[i4 + 2]: + half the difference
      if f bit 7, + a quarter if f bit 6 (logical shifts of the 16-bit difference) -> B7DC
  c = sine[100h - i4], interpolated the same way toward sine[FEh - i4] (subtracted)
  dx = hi(k * c); cx = hi(k * s)                                       (MUL, unsigned)
  q >= 1: swap(dx, cx), dx = -dx;  q >= 2: cx = -cx, swap(dx, cx);  q == 3: swap(dx, cx), dx = -dx
  impact X = boat X + (cx sar 3), impact Y = boat Y + (dx sar 3)
```

`projectile_tick` (`0919:38cc`, once per frame): does nothing while `DS:D8BC` is set (§11).
Otherwise, for p = 31 down to 0, a nonzero countdown is decremented, and on reaching 0
`projectile_impact(p)` runs.

### 7.2 `projectile_impact` (0919:38ed) **verified**

```
w = weapon; D749 = heading.fraction word; D74B = range byte
slot = free_temp_object()                                   (§4.3, same overflow quirk)
object[slot] = kind 42h (4Bh for weapons 2 and 3), flags 3, at the impact point  (explosion)
mark_near_objects(): every visible entry (§8.2) with kind < 19h and distance class < 7
                     gets flag bit 80h (object word bit 15): alerted             0919:3948
hit_objects()                                                                     0919:3977
```

`projectile_impact` also puts the weapon in `DS:B7EA`; the explosion is kind 42h, or 4Bh for weapons
2 and 3, with flags 3. `projectile_tick`, `projectile_impact` and `mark_near_objects` take the
caller's SI, which `mark_near_objects` stores in `DS:D6F6` (`caller_si`; `hit_objects` reloads SI
from it for its "friendly" message).

`hit_objects` walks the visible list from the last entry down; **the first entry the shot hits
takes it and the routine ends there** (only wrecks 30h/31h and the ignored kinds ≥ 17h let the shot
go on to the next entry). **verified** (port, differential test)

* Skip empty, kind ≥ 3Fh, and objects 0..35 (boat and temporary objects) **except** kind 12h.
* `hit_test` (`0919:3bbc`, view space, BX = entry, returns AH): the difference between the
  entry's bearing (`4E00`:`4EB5` + view heading `D191`:`D192`) and the shot's bearing `D749`,
  offset by 4C00h and folded to its absolute value, is scaled down (`>> 6` after a 3-bit rotate
  of its low byte) and must be < 28h and within the sprite's half width. The shot's range byte
  must match the entry's elevation `4F6A` within the sprite's height, and the sum within the
  sprite's size. Sprite sizes come from the sprite descriptor (`CS:583D` table, banks
  `DS:D883`/`D885`). No hit → next entry. Exactly (**verified**, port differential test):
  descriptor `d` = `sprite_slots[s - 1]` in segment `D883` when the sprite index `s >= 69h`, else
  `D885`; `w = d[3]`, `h = ((d[4] + d[6]) << 2)` (bytes); `c` (above) `<= 2w`;
  `e = 4F6A[entry] - D74B + 2` must be 0..7Fh and `<= 2h`; `c + e <= (w + h) << 1` (8-bit).
* Hit on kind > 31h: stop (nothing further is hit). Kinds 30h/31h: ignore (already wrecks).
* Kind 12h: damage flags forced to 30h, and the chase/hunter state `D6A8`, `D6A6`, `D6A0` reset
  (§8).
* Kinds ≥ 17h: 75% ignored (`(DS:0089 & 0Ch) != 0`). Kinds **19h–27h are friendly**:
  `B546++` (friendly hits), fire at will off (`B82E = 0`), message 15h "HEY! That's friendly!".
* Damage: the Codex-verified transition (`CB8C..CC5B`): class byte `DS:541B + 2*kind`, matrix
  `CS:378F[(class & 18h) | weapon]`, damage bits 38h of the flags. Below 38h the object is
  "Good shot." (0Eh). At 38h it is **destroyed**: the kind becomes its wreck variant from the
  class byte `c`: `c & 7` = 0–3 → 30h + n; 4 → 0 (the object vanishes); 5 → 38h; 6 → 37h;
  7 → 4Bh if `c == 7`, else 18h. Variant 4Bh also sets flags 1 and plays sound 8;
  class 1 has a separate rule for weapons 2 and 3 (kind becomes 31h, or 30h below kind 2Ah).
* The matrix (`CS:378F`, 32 bytes, read from the image) decoded, with the weapon IDs of §6.1 and the
  number of hits that destroy an undamaged object (damage levels 0..7, 7 = destroyed; "OR" = sets
  bit 08h, i.e. one level up from an even level only, so on its own it stops at level 1, but it
  finishes an object another weapon brought to level 6). Every hit below level 7 says "Good shot.",
  also when nothing changed. Armour classes of the fortifications (class bytes, the same in the four
  regions' `DATnB.DAT`): machine gun 07h C8h → 1, mortar nest 08h D0h → 2, enemy fortification
  (bunker) 09h 98h → 3. The manual says the same (p. 35, 42): "Don't bother firing [the M60] at
  bunkers or houses", the grenades can't do much to bunkers, and for mortar nests "go for your own
  mortar". The stations' weapons: §6.1 (the midship's default fit is the mortar, the stern's the
  M129: game_flow.md, the armament menu).

  | Armour (`class & 18h`) | 1 M60 | 2 M129 grenade | 3 mortar | 4 M2HB .50 | 5 minigun |
  |---|---|---|---|---|---|
  | 0 (00h) | +2 (4 hits) | destroyed | destroyed | +4 (2 hits) | destroyed |
  | 1 (08h) | 0 (never) | +2 (4 hits) | destroyed | +1 (7 hits) | OR |
  | 2 (10h) | 0 (never) | +1 (7 hits) | destroyed | OR | OR |
  | 3 (18h) | 0 (never) | OR | +1 (7 hits) | OR | OR |

* On destruction (`0919:3aba..3b26`): `terrain_structure_break` (`0919:3c51`); a fire object
  (word 0448h: kind 48h, flags 4) is placed at the wreck unless the wreck is 32h, 33h or 37h or
  the old kind was 12h/13h (when the temporary slots are full it avoids the hunter's slot
  `D6A0`); if `DS:5401[old kind]` is nonzero, `score_add` updates the score word
  `DS:B52E + 2*that value` (BCD, game_flow) and `B7E2` is set to the old kind, **or to 04h when a
  fire object was placed** (the AH of `MOV AX,0448h`: quirk); `mission_target_check` (`0919:3b51`).
  The damage bits of the object are kept in `B7F8` (shot_elevation) meanwhile and its kind in
  `DS:D74C` (`hit_kind`, for the friendly test of the message). Class byte 0 ends the routine with
  no message.
* Message (`0919:1565`) unless the target was friendly: "Target destroyed." (0Fh), or
  "MISSION ACCOMPLISHED!" (16h, below).

`mission_target_check` (`0919:3b51`): if the object is one of the five current objective
objects `DS:B841..B849`, `B544++`; at **3** the objective indicator is drawn (`0919:0027(3,3)`
on page 0), sound 4 plays and the message is 16h "MISSION ACCOMPLISHED!".

### 7.3 Terrain structures (`DS:D0CD`, end of the mission record) **verified**

`D0CD` = count *n* ≤ 32, then *n* terrain-piece words at `D0CF`, X at `D10F`, Y at `D14F`.
`terrain_structure_break` (`0919:3c51`, AH = old kind, SI = the object) runs when the old kind
was 10h, 11h, 20h or 21h (bridge-type objects). The list is scanned from its last entry down; the
**first** structure found in the object's 1024-unit cell (`X & FC00h`, `Y & FC00h`) with a piece
below 62h (except 5Dh) changes, and the scan stops there (**only one structure per hit**; earlier
drafts said every one): `< 5Ch` → 62h, `5Ch` → removed (0), `5Dh..5Fh` → 5Dh, `60h/61h` → 63h (the
word's high byte is kept). Then `D9AD = FFFFh` forces the terrain to be rebuilt (render3d). With a
count of 0 the loop still examines the word before the lists once (index −1). **verified** (port,
differential test)

## 8. World: objects, enemies, incoming fire and boat damage

### 8.1 Object kinds and the per-kind bytes **verified** (roles **likely**)

The world B data (`DATnB.DAT`, copied to `DS:53A8`) holds two bytes per kind: the **behaviour
byte** `DS:541A + 2*kind` and the **class byte** `DS:541B + 2*kind`.

| Behaviour byte bits | Meaning |
|---|---|
| 0–1 | Shot accuracy `a`: threshold `8a + 6` (6, 14, 22, 30 out of 0..63) |
| 2–4 | Weapon class (also the boat damage class, §8.5); 5 = salvo |
| 5–7 | Behaviour: 0 inert (ignored by the AI), 1 static, 2–3 patrol (reverse every 64 passes), 4–5 circle one way, 6–7 circle the other way; odd values move fast |

| Class byte bits | Meaning |
|---|---|
| 0–2 | Wreck variant when destroyed (§7.2) |
| 3–4 | Armour class for the damage matrix |
| 5 | Can move |
| 6–7 | Fire-rate class (0 = never fires) |

Kind ranges used by the code: 0 empty; 01h–17h hostile targets (10h, 11h and 20h, 21h are
bridge-type objects, §7.3); 12h homing missile; 14h and 17h can launch missiles; 18h and
30h–38h wrecks; 19h–27h **friendly**; 24h/25h passengers (§5.2); 28h–29h, 33h–34h two-frame
animations; 3Fh–41h wakes; 42h–51h explosions and splashes; 48h fire; 52h–54h muzzle flashes.
Objects 1–35 are the temporary slots (§4.3); the mission objectives are ordinary objects.

### 8.2 The visible-object list (render3d, input to the AI)

Built while drawing (`0919:6e5c` tree, render3d spec) and read by the simulation in the next
frame: `B83D` entries; per entry `i`: object offset `DS:523E[2i]`, distance class
`DS:4C97[2i]` (small = near), bearing `DS:4E00[i]` and fine bearing `DS:4EB5[i]` (screen
angle, relative to the view heading `D191`:`D192`), elevation `DS:4F6A[i]`, sprite index
`DS:5189[i]`. `B83F` is the first entry used by identification (list building, projection and
sorting: render3d §5.1–5.3; object 35 is never listed). The gunners (§6.2), hit tests
(§7.2), spotting and firing (§8.3) only see objects in this list, so **a faithful port must
reproduce the renderer's list exactly**. `line_of_sight` (`0919:348e`, SI = 2·entry) returns
CH = 0 when no later entry of the list (an object at offset ≥ 48h, i.e. not the boat or a
temporary object; not kind 35h or 10h; with a sprite) covers the entry's bearing: with
`d = 4E00[entry] − 4E00[k]` (signed byte) and `w = descriptor[3] >> 2` of k's sprite (descriptor
as in §7.2 `hit_test`), k covers it when `−w ≤ d < w`; CH = 1. **verified** (port, differential
test)

### 8.3 `enemy_update` (0919:2ea8, world group) **verified** (control flow), **likely** (roles)

```
if D8BC: return
D6F6 = SI (the caller's)
toggle bit 20h of the byte at DS:6E54 + [DS:6E54] once per frame (every 2^n passes)
D70C++                                                     (world-pass counter)
for each visible entry, last to first (the entry number kept in B7DC and reloaded from there:
   quirk, a message printed meanwhile, "Salvo coming in!" or "MISSILE coming our way!", leaves its
   last character '!' (21h) in B7DC's low byte, and the scan goes on from entry 20h):
   kind 0: skip
   effects (kind >= 3Fh), exact (0919:2f39..2f86):
       wakes 3Fh -> 40h -> 41h -> 3Fh, the kind stored on even passes only; at 3Fh the flags
       byte (lifetime) counts down first, and the wake is removed at 0
       other effects: while the flags byte is nonzero it counts down (a flags byte of 0 never
       changes); at 0 the kind advances with a new count: 43h, 44h -> removed at 44h (count 2);
       45h..47h (2), removed at 48h; 49h, 4Ah (4); removed at 4Bh; 4Ch..51h (1); removed from 52h
   kinds 28h <-> 29h (every 16 passes); 33h -> 34h -> 35h (every 8 passes; 35h stays: not a toggle)
   other kinds >= 19h: skip
   hostile (01h..18h) within distance class < 18h, with behaviour bits 5–7 != 0:
      d = distance class >> 1; w = behaviour byte; c = class byte; f = flags byte
      if not alerted (f bit 7 clear) or not active (bit 6 clear, d = d >> 2): spotting:
          phase gate: ((entry scrambled) + D70C) & F8h & DS:B3F4[region] must be 0
          s = (night ? 4 : 0) + d; if B816 + B817 >= 88h: s >>= 2; if D905: s >>= 1
          if s < 12h and RNG low byte (DS:0088) < DS:D6FA[s]: f |= C0h (alerted, active)
      elif c & C0h and this is not the missile source D6A6:
          phase gate with DS:B3F4[region] (masked C1h for weapon classes ≤ 2)
          RNG gate: (DS:008A & DS:B3F8[(c >> 6) − (f bit 3 ? 1 : 0)]) == 0
          B7E6 = B7E7 = bearing + view heading − 48h
          weapon class 0: no fire; class 1: only within d < 6; others: d < DS:B41C[region];
          not while f bit 5; line of sight clear
          kind 14h (25%) or kind 17h at distance class >= 3 (1/16), no missile in flight, and a
          free temporary slot below 46h: launch_missile()   (§8.4)
          else schedule_shot(w)                             0919:3217
      (the phase gates scramble a byte by SHR 1 and three RCR 1, OR 10h when the last carry is set:
       the entry's 2*i for firing, the low byte of the object offset for spotting)
      movement, if behaviour >= 2, f bit 4 clear and c bit 5 set:
          circling (4–7): every 8 passes a wake (0919:31f1, kind 3Fh flags 0Ah); every 16 passes
                          facing (f bits 0–2) += 1 (−1 for 6–7)
          patrol (2–3):   every 64 passes facing ^= 4 (turn around)
          X += DS:B3FC[2·facing (+10h if odd behaviour)], Y += next byte   (signed)
```

Region tables: `B3F4` = 7Fh, 3Fh, 1Fh, 3Fh (Vietnam, Colombia, Panama, Mare Island; smaller
fires more often); `B3F8` = 07h, 03h, 01h, 00h; `B41C` (max range) = 0Eh, 0Fh, 10h, 0Fh;
`B3FC` slow = (0,−1) (−1,−1) (−1,0) (−1,1) (0,1) (1,1) (1,0) (1,−1), fast = the same ×2/×3;
`D6FA` (spotting thresholds) = 20 1C 18 10 0C 08 06 04 03 03 02 02 02 01 01 01 01 01. The region
names are **likely** (manual order).

`schedule_shot(w)` (`0919:3217`): weapon class 5 is a **salvo** (message 35h "Salvo coming
in!"): if incoming slots 8–15 are all free, eight shots with descriptors `A0h | (DS:008B & 18h)`
+ k and timers `d/2 + 1 + 7k`, and the boat heading and speed at launch are saved in
`D6F8`/`D6F9`. Otherwise the first free slot 0–7 gets descriptor `((w << 3) & E0h) | accuracy`
and timer `d/2 + 1`, `D70D++`, and a muzzle flash object appears at the shooter (kind 53h; 54h
for shooter kinds 0Dh–0Fh; 52h for weapon classes above 2; flags 1). `d` is `B7E2`, the distance
class / 2 that `enemy_update` left; the accuracy is `8·(w & 3) + 6` (kept in `B7E9`, w in `B7E3`).

The `enemy_update` and `schedule_shot` pseudocode is **verified** (port, differential test on combat
states: every entry of real visible lists, all world-pass phases). The movement reads the flags
byte again after spotting (an object spotted in this pass moves with its new flags) and the
behaviour, class and flags copies `B7EB`, `B7EC`, `B7E8` from memory at each test.

### 8.4 Homing missile (`0919:2038`, world group) **verified**

`launch_missile` (inside `enemy_update`): a temporary slot becomes kind 12h at the shooter,
`D6A0` = its offset, `D6A6` = the shooter, `D6A8 = 1`, target `D6A9/D6AB` = boat + (2, 1),
message 2Fh "MISSILE coming our way!".

```
missile_update()                                          0919:2038
  if D6A8 == 0: return
  every 8 passes D6A8++                                   (age)
  if the shooter is now a wreck (kind 18h or 31h): explode
  if the shooter is no longer in the visible list: explode  (looked up at the entry cached in
                             DS:D6B2 first, then from the last entry down; D6B2 = the entry found;
                             SI is left there: game_frame passes it to incoming_fire)
  every 4 passes, with line of sight from the shooter: retarget to the boat's position
  move_toward(target, step D6AD = 5)                      0919:1fc3
  if not at the target and D6A8 < 42h: return
explode:
  missile object = 034Bh (explosion); D6A8 = D6A6 = D6A0 = 0
  if it is exactly on the boat and (DS:0089 & 20h) == 0:
      boat_hit(7) ×2, +1 in region ≥ 1, +2 more in region ≥ 2
```

A gun hit on the missile (kind 12h) destroys it (§7.2).

### 8.5 Incoming fire (`0919:32fb`, world group) **verified**

16 incoming slots: timer `DS:D71F[k]`, descriptor `DS:D70E[k]` (bits 5–7 class, 0–4 accuracy);
`D70D` counts pending shots. For each slot whose timer reaches 0:

* sound 0Bh (class ≥ 3) or 0Ah.
* **Salvo** (`descriptor & E0h == A0h`): `rel = CS:2E88[low bits] + D6F8 − B81E`; the speed change
  since launch `|D6F9 − speed|`… the shot hits when `rel + ((58h − |Δspeed|) >> 3) ≤
  (58h − |Δspeed|) >> 2` (exact arithmetic `0919:3344..3376`). Hit: sound 7, `boat_hit(5)`,
  explosion 034Bh 10h units from the boat on that bearing; miss: splash 0244h there.
* **Ordinary**: hit when `random() & 3Fh <= accuracy` and, at night (`B7FC == 0`), RNG bit
  `DS:0089 & 1` is clear. Hit: sound 7 (class ≥ 3) or 9, `boat_hit(descriptor >> 5)`; small arms
  (class ≤ 2) may crack the cockpit window (§8.6), heavier hits put an effect within ±3 units
  (0242h, 024Bh). Miss: an effect within ±15 units (0242h, or 0244h for class ≥ 3).

**Evasion:** each jet move by the pilot or crew (§4.1, `0919:3479`) lowers the accuracy of
slots 0–7 by one.

### 8.6 Boat damage (`0919:2a37 boat_hit(class)`) **verified**

```
boat_hit(class)                       class = AL & 7
  B7F2 = 2, or 8 above class 2                            (shake steps, render3d §7)
  r = random() & 0Fh; if r >= DS:D194[class]: return      (D194 = 00 04 07 10 10 10 10 10)
  if class > 2: D9B5 = 3                                  (screen shake)
boat_hit_component(AL = component, CL = message)          0919:2a69 (also the ramming, CL = 0Ch)
  component = r; D194 = r (last component, used by the message); component 0Ch: B7F2 = 8
  idx = DS:D1AC[component]; if condition D502[idx] & 3 == 2: return   (already destroyed)
  message CL: 0Ah "Hit to " + component name (CS:1457[component])
  if practice (F110 low byte) or mission B505 low byte <= 1: return   ("No damage possible.")
  port / starboard engine (idx 6 / 7): B81C / B81D halved, throttle clipped to it
     (quirk: after the starboard one the halved maximum is compared with 6, so a halved B81D
      of 6 also halves B81C)
  idx 4..7: 25% B7FE++ (leak, random() & 3 == 0)
  condition 3 → 1 (damaged); 0 or 1 → 2 (destroyed); indicator redrawn; damage report refreshed
  if not destroyed: return
  destroyed:
     engines or fuel tanks (components 0Bh–0Eh) and (DS:008A & 0Ch) == 0: boat_destroyed()
     captain (0Fh): main switch forced off, B545 = 1, message 0Dh "Sir, you just died!"
     engineman (3) / gunner's mate (9) / seaman (4): the player at station 4 / 2 / 3 is sent to the
     damage report (station 7)
     port / starboard engine (0Bh / 0Ch): panel 1 / 2 = 5, engine B808 / B809 = 0
     secondary indicator DS:D19C[component] redrawn if nonzero
```

`0919:2bb6` (`B52A = 1` for a damaged captain) is **unreachable**: the branch is only entered
with the condition already 2. The hull (component 10) is never "destroyed" into a loss here; it
sinks the boat through §8.7. The "damage report refreshed" step is `damage_panel_refresh`
(`0919:2bbd`): a spotlight hit (condition index 0Ch front, 0Dh rear, 0Ah middle) at the gun
station whose panel shows it (bow, midship, stern) redraws the gun panel, `gun_panel_copy(the
condition byte)` on page 0. **verified** (port, differential test of every component)

| Component | Name (message) | Condition byte |
|---|---|---|
| 0, 5 | front spotlight | D50E |
| 1 | rear spotlight | D50F |
| 2, 8 | middle spotlight | D50C |
| 3 | engineman (stern gunner) | D515 |
| 4 | the seaman (midship gunner) | D512 |
| 6, 7 | radar | D50D |
| 9 | gunner's mate (bow gunner) | D513 |
| 10 | the hull | D510 |
| 11 / 12 | port / stbd engine | D508 / D509 |
| 13 / 14 | port fuel tank / starboard fuel | D506 / D507 |
| 15 | the captain | D514 |

Conditions: 3 intact, 1 damaged, 2 destroyed (0 also counts as damaged).

`window_hit` (`0919:2ce3`, small-arms hits): with `(DS:0089 & 18h) == 0`, sets bit
`1 << (DS:0089 & 7)` in `DS:0B49 + station` and redraws the cockpit cracks (`05bd:3114`).

### 8.7 Sinking and loss (`0919:2cad`, `0919:2d0f`) **verified**

`sinking_update` (world group): if the hull condition `D510 <= 2`, `B7FE = max(B7FE, D510)`.
When `D70C == 5Bh` (once per 256 world passes): if `B7FE − 1` is above 0 as a signed byte, the
water `B7FF + B7FE − 1` (8-bit) at 10h or more loses the boat (`B7FF` unchanged), else it is
stored and message 0Bh "We're sinking!". **verified** (port)

`boat_destroyed` (`0919:2d0f`): `B545 = 1`; chase view looking back at the boat (distance 38h,
heading `D191 + 80h`), `D8BC = 1`; sound 7, shake 3; speed, throttles and `D680` = 0; the boat
becomes a wreck (kind 30h); eight fire and explosion objects around it (0248h, 054Bh, 084Bh,
0B48h, 0648h at ±3/±5 units); main switch forced off; message 3 "We're goners!" (ends the
mission, §9); `B52A = 1`.

## 9. Messages, mission clock and mission end

### 9.1 Messages **verified**

Table `CS:0FEB` (0919), 38h entries: a near pointer to `attribute byte, text` ending with a
byte ≥ 80h. Texts are quoted throughout this spec. `show_message(al)` (`0919:1594`):

```
if B800 == 2: return                                 (mission ending: line locked)
B802 = 0; clear the message line (text_goto(row DS:D64A, column 6); 00f2:0e10 print_text of
          DS:D65F, 28 blanks); D645 = D647 = FFFFh (the heading readout is redrawn); text_goto again
if al == 0: B800 = 0; return                         (message 0 = clear)
print the text (character by character: each stored in DS:B7DC, then 121b:03d8 text_draw_char)
attribute:
  0      → none
  1, 3   → queue reply 2 "Aye-aye, sir!"
  2      → queue 12h "-he's dead, sir." (D60F = 0) if the bow gunner is dead, else 5 (identification)
  4      → print the component name CS:1457[D194]; queue 26h "No damage possible." in practice
           or missions 0–1
  5      → print the name of object D60F (kind; 3Ch shown as 39h): text at DS:6E54 + [DS:547A + 2*kind];
           D60F = 0
  6      → B52C = bcd_inc(B52C) (02d2:2b4a); then as 7
  7 and above → B800 = 2, B801 = 1Eh: the mission ends 30 world passes later (returns here)
  (0–5)  → B801 = 0Ch (display time, world passes), B800 = 1
  (3 queues 2 like 1; 5 with D60F = 0 prints the name of kind 0)
```

`message_sequencer` (`0919:1528`, world group): while `B801 > 0` count it down; then
`B800 == 0`: idle; `B800 == 2`: **end the mission** (`0919:0906`: station 9); `B800 == 1`: show
the queued reply `B802`, or message 5 if nothing is queued and an identification is pending
(`D60F`), else message 0 (clear). Message timing therefore scales with time compression.

The readouts of the message line (`message_line_draw` 0919:17d4, hud: the clock at the pilot's
station, the compass heading elsewhere) print numbers with these helpers (**verified**, port
differential test): `print_colon` (`0919:1558`) prints `DS:D64E` (':'); `print_bcd_2digits`
(`0919:174e`, AL = BCD) converts to binary and prints 2 digits (`B7E3 = 1`); `print_3digits`
(`0919:1769`, AL, CF) prints CF·256 + AL as 3 digits (`B7E3 = 0`); `print_digits_hundreds`
(`0919:176e`) puts the hundreds digit (blank for none) in `D64B`: with CF it starts from 200 + (AL
+ 38h) and a carry there gives '3' but loses the 256 (**quirk**: 456–511 print as 300–355; the
heading readout stops at 359); `print_digits_tens` (`0919:1799`, AL, CL = digits counted so far)
puts the tens (blank when zero and CL = 0) and units in `D64C`/`D64D` and prints `3 − B7E3`
characters from `D64B + B7E3` (00f2:0e44). The code at `0919:1738` (a BCD twin of `174e` without
`B7E3 = 1`) is unreached.

`0919:1565` shows a message on the visible page 0 and returns to drawing page 1 (used from the
drawing part of the frame). `0919:1589` is its far entry (`input_read_key`'s pause messages).

`message_line_draw` (`0919:17d4`, once per frame) **verified** (port, differential test):

```
text colours (4, 0)                         (2 in CGA mode 4, EED2 == 4)
cell (column, text row): chase view (11h, 4Eh); bow (11h, 85h), or (22h, 64h) with bow weapon 1;
     midship and stern (20h, 84h); any other station above 4: return (the colour stays 4)
pilot (station low byte 0 or 1):
   looking left or ahead (F346 low byte <= 1): the clock at (0Fh left / 2 ahead, B0h), redrawn
      only when the minutes differ from D649 (then D649 = minutes): hours (BCD, 2 digits), ':',
      minutes (print_digits_tens with CL = 1, B7E3 = 1)
   the readout at (22h left / 15h ahead / 8 right, B0h)
readout: heading = the hull's B81E at station (word) 1, else the view heading D191; with the view
   fraction D192 as AH, redrawn when the word differs from D647 (then D647 = it): heading_readout
text colours (0Fh, 0)
```

`heading_readout` (`0919:16e3`, AL = heading, AH = fraction byte): the compass letters
`D64F[((AL + 10h) >> 4) & 0Eh]` (two characters, `print_chars`), ':', then the degrees: the 14-bit
value `(AL >> 2) : ((AL & 3) << 6 | AH >> 2 ...)` exactly as `SHR AL,1 / OR AH,8 / SHL AH,4 / SHR
AL,1 / RCR AH,1` builds it, `(value << 16) / 5B00h >> 7`, printed by `print_3digits` with the carry
= bit 8 (§ above; 0919:1729 is the `print_3digits` call used by the S1 test).

### 9.2 Mission clock and time of day **verified**

`mission_clock_tick` (`0919:1d30`, every boat pass):

```
if ++D69F >= 15: D69F = 0; if ++B549 (seconds) >= 60:
   B549 = 0; ++B54A (minutes); time_of_day()
   if B54A >= 60: B54A = 0; B54B (hours, BCD) = bcd_inc(B54B), 24h → 0; B52E = bcd_inc(B52E)
       if word B516 == 24h: message 13h "I trust you bought my game"; busy-wait 300h ticks of
       DS:08C0 (≈ 10.5 s); message 14h "-was it worth .50 an hour?"
   if (B547, B548) != 0 and B54A == B547 and B54B == B548: message 1Fh "Our time is up!" (ends)
```

`time_of_day` (`0919:1dd6`, **verified** by the port's differential test; the twilight stages
have `B7FC = 0`; D9B0/D9B1 = 18h/10h in VGA, 8/4 in other modes, 1/1 in CGA mode 4): hour < 6 →
night (`B7FC = 0`, `B7FD = 0`); 6:00–6:03 dawn stage 1,
6:04–6:06 stage 2, from 6:07 day (`B7FC = 1`); hours up to 19h (BCD) day; 19:55–19:57 dusk
(stage 2), 19:58–19:59 stage 1; after that night. On a change: `D9AD = FFFFh` (terrain rebuild)
and the four scene colours `D94F..D952` = `DS:B420 + 4·(1 − B7FC + B7FD)` (+10h in CGA mode 4).
`B547`/`B548` (deadline) and the start time are set by `0919:3d78` (mission objects set-up,
world spec).

### 9.3 Mission status

| DS | Meaning |
|---|---|
| B544 | Objective progress: `++` per objective object destroyed (§7.2); 5 when passengers are delivered (§5.2); 3 = accomplished for strike missions |
| B545 | Result: 3 in progress (set by `mission_run`), 2 aborted with a crippled boat (Tab), 1 boat lost or captain killed |
| B546 | Friendly objects hit |
| B52A..B541 | Twelve score words (BCD, `02d2:2b4a`); `B52A = 1` boat lost; `B52C` +1 on mission end messages |
| B547/B548 | Deadline minutes / hours |
| B549/B54A/B54B | Mission clock seconds / minutes / hours (BCD) |
| B7FC / B7FD | Day flag / twilight stage |
| B7FE / B7FF | Leak level / water taken on (sinks at 10h) |

The debrief and roster that read these are in the game_flow spec.

## 10. Shore contact **verified** (Codex kernels + this frame integration; port, differential test)

Shore contact comes from the terrain projection (`0919:7158`, render3d), not from a collision
map:

```
terrain_frame()                                     0919:7158 (simulation-relevant parts)
  terrain_setup (0919:736c)
  if nothing moved (camera D972/D974 == D906/D908, view heading == D90A, pitch == D90C) and
     not D8BC: skip to drawing                          (no new contact test on a still frame)
  D900 = D8FF (previous contact); D8FF = 0; D901 = D903 = FFFFh
  project both vertex groups (0919:7523; candidates recorded at 10755..107A0 into D901/D903)
  if chase view: skip the test
  shore_contact_test(D901); if no contact: shore_contact_test(D903)       0919:79e1
  if contact colour D8FF != 0 and != previous:
     speed = −16 (speed ≥ 0, also when stopped) or +16 (reversing)       (Codex: shoreline_response)
     colour c = D8FE & 3Fh
     pilot practice at Mare Island (station 1, region 3: low bytes) and c == 6: D8FD = 1 (boat lost
        next frame)
     elif (DS:0088 & 0Eh) == 0 and mission B505 > 1 (word):
        c == 0Eh: waterjet r = DS:0088 & 1 (D50A + r), message 17h/18h "Damage to port/stbd waterjet."
        c == 6:   hull D510, message 1 "Damage to the hull."
        condition (low 2 bits) 3 → 1, 0/1 → 2, the whole byte replaced (no change and no message if
        already 2); the message through show_message_page0 (0919:1565, on page 0)
```

The candidates depend on the exact vertex order the renderer uses, so shore contact is only
faithful if `terrain_frame`'s projection is ported exactly (render3d spec).

## 11. Random numbers, scene rebuild and call order

### 11.1 RNG **verified** (Codex)

`random` (`0000:0780`): `DS:0088 = DS:0088 * 41C64E6Dh + 3039h` (32-bit), returns
`(state >> 16) & 7FFFh`. Many routines read the state bytes directly instead of calling it:

| Byte | Read by |
|---|---|
| DS:0088 | crew pilot gate (§4.5), crew gunner gates (bits 20h/40h/80h, §6.2), spotting (§8.3), shore damage (§10) |
| DS:0089 | hit skip for kinds ≥ 17h (§7.2), missile dodge (§8.4), night miss (§8.5), window hit (§8.6) |
| DS:008A | AI fire gate (§8.3), boat loss roll (§8.6), wave bob (Codex); word 008A: incoming-fire scatter (§8.5) |
| DS:008B | salvo descriptors (§8.5), fuel burn in `engine_thrust` (Codex) |

The state
only changes through `random()`: once per mission-loop pass (§1.1), in the waits of the
full-screen stations, and in `boat_hit`, `incoming_fire` and missile/spot logic. **A faithful
port must call `random()` at exactly the same points** and read the same bytes.

### 11.2 Scene rebuild flag `D8BC` **verified**

Set by: chase view on (`,`), detail toggle (D), `boat_destroyed`, the chase camera giving up
(§5.3), and `0919:8408` when the boat enters a new 1024-unit world cell (`D9A5`). While set:
`terrain_frame` re-projects everything, the object renderer resets its sprite cache
(`0919:69a9`), and **`enemy_update` and `projectile_tick` do nothing**. `0919:6e5c` (objects,
at the end of the frame) clears it. Every cell crossing therefore freezes enemies and projectiles
for one frame.

### 11.3 Order of state changes within a frame (summary)

1. Mission loop: `input_read_key` (previous frame's key) → `game_frame` → `key_dispatch` / held
   controls (affect the next frame) → `random()`.
2. `game_frame`: world group ×2ⁿ (missile, incoming fire, sinking, enemy AI and effects,
   messages) → message line → boat group ×2ⁿ (reload, motion/mission stop/camera, bob,
   propulsion, crew pilot, clock) → crew gunners → cockpit → terrain and shore contact → objects
   (builds the next visible list, **ramming** test against the nearest object ahead,
   render3d §5.4, clears `D8BC`) → projectiles → muzzle flashes.

## 12. Function table

Names are in `spec/simulation_symbols.csv` and merged into `reverse_engineering/symbols.csv`.

| Address | Name | Section |
|---|---|---|
| 0000:07a8 | input_read_key | §3.1 |
| 08e1:006e | demo_next_key | §3.1 |
| 0919:038e | key_dispatch | §3.2 |
| 0919:03c7 | key_default | §3.2 |
| 0919:03cb | panel_switch_toggle | §3.3 |
| 0919:043c … 0930 | key handlers (see §3.3) | §3.3 |
| 0919:0608 | engine_switch | §4.2 |
| 0919:0713 | pilot_command_reply | §3.3 |
| 0919:0777 | key_pilot_station | §3.3 |
| 0919:0953 | controls_poll | §3.4 |
| 0919:0a22 / 0aab / 0b34 | aim_stern / aim_midship / aim_bow | §3.4 |
| 0919:0bbd | pilot_throttle_controls | §4.1 |
| 0919:0c6a | throttle_step | §4.1 |
| 0919:0cf4 / 0d45 / 0dbd | fire_station4 / fire_station3 / fire_bow | §6.1 |
| 0919:0e19 | pilot_slow_down | §4.1 |
| 0919:0e48 / 0e95 | rotate_headings_minus / _plus | §4.3 |
| 0919:0ee2 | projectile_launch | §7.1 |
| 0919:0f0b / 0f42 | heading_step_plus / _minus | §4.3 |
| 0919:1528 | message_sequencer | §9.1 |
| 0919:1558 | print_colon | §9.1 |
| 0919:1565 / 1589 / 1594 | show_message_page0 / show_message_far / show_message | §9.1 |
| 0919:174e / 1769 / 176e / 1799 | print_bcd_2digits / print_3digits / print_digits_hundreds / print_digits_tens | §9.1 |
| 0919:16e3 | heading_readout | §9.1 |
| 0919:17d4 | message_line_draw | §9.1 |
| 0919:18af | crew_gunners | §6.2 |
| 0919:18c7 / 190d / 1953 | gunner_bow / gunner_stern / gunner_midship | §6.2 |
| 0919:19a0 | gunner_aim | §6.2 |
| 0919:1aa3 | gunner_key | §6.2 |
| 0919:1cf9 | reload_tick | §6.1 |
| 0919:1ac0 | crew_pilot | §4.5 |
| 0919:1b2d | crew_pilot_decide | §4.5 |
| 0919:1c00 | route_find | §4.6 |
| 0919:1d30 | mission_clock_tick | §9.2 |
| 0919:1dd6 | time_of_day | §9.2 |
| 0919:1e87 | mission_stop | §5.2 |
| 0919:1fc3 | move_toward | §5.2 |
| 0919:2038 | missile_update | §8.4 |
| 0919:2273 | propulsion | §4.3 |
| 0919:29f9 | accelerate_speed | §4.3 |
| 0919:2529 | engine_thrust | §4.2 |
| 0919:2a37 | boat_hit | §8.6 |
| 0919:2a69 | boat_hit_component | §8.6 |
| 0919:2bbd | damage_panel_refresh | §8.6 |
| 0919:2cad | sinking_update | §8.7 |
| 0919:2ce3 | window_hit | §8.6 |
| 0919:2d0f | boat_destroyed | §8.7 |
| 0919:2ea8 | enemy_update | §8.3 |
| 0919:31f1 | spawn_enemy_wake | §8.3 |
| 0919:3217 | schedule_shot | §8.3 |
| 0919:32fb | incoming_fire | §8.5 |
| 0919:3479 | evade_incoming | §4.1, §8.5 |
| 0919:348e | line_of_sight | §8.2 |
| 0919:3712 | atan | §4.6 |
| 0919:37af / 37c7 | projectile_alloc / projectile_aim | §7.1 |
| 0919:38cc | projectile_tick | §7.1 |
| 0919:38ed | projectile_impact | §7.2 |
| 0919:3948 | mark_near_objects | §7.2 |
| 0919:3977 | hit_objects | §7.2 |
| 0919:3b3b | elevation_add_clamped | §7.1 |
| 0919:3b51 | mission_target_check | §7.2 |
| 0919:3b9f | score_add | §7.2 |
| 0919:3bbc | hit_test | §7.2 |
| 0919:3c51 | terrain_structure_break | §7.3 |
| 0919:6f2a | free_temp_object | §4.3 |
| 0919:7158 | terrain_frame | §10 |
| 0919:7ebb | boat_motion | §5.1 |
| 0919:80e0 | camera_pitch_bob | §5.4 |
| 0919:81f8 | muzzle_flash_tick | §6.1 |
| 0919:8229 | camera_position | §5.3 |
| 0919:82de | chase_view_collision | §5.3 |
| 0919:8331 | polar_small | §8.5 |
| 0919:8711 | route_rotate | §4.6 |
| 0919:8754 | route_point | §4.6 |
| 0919:87e8 | route_advance | §4.6 |
| 0919:8930 | game_frame | §1.2 |
| 05bd:000a | mission_run | §1.1 |
| 05bd:14d6 | mission_load | §2.1 |

## 13. Open questions

* `DS:B516` (easter-egg counter), `DS:0080` (`E` toggle), `DS:D905` (spotting modifier; a
  spotlight?), `DS:D0CD`-list piece kinds, the meaning of flag bits 3–4 of enemy objects, and the
  exact fork rules of `route_advance`: resolve while porting, with the differential test.
* Weapon and region names are inferred (sounds, reload values, manual order); confirm against the
  outfitting screens (game_flow) and DOSBox.
* `0919:0591` (cycles panel switch 3, `D523`) is in neither key table (`CS:0290` holds 13
  handlers besides the default, `DS:D615` the ten F-key ones) and has no caller: dead code, not
  ported.
* The panel switch table `CS:0270` is listed in §3.3; the switch/indicator art belongs to the hud
  spec; what the gun stations' switches (2, 6, 0Eh, 10h, 12h, 14h, 16h, 18h, 1Ah, 1Ch) mean is to be
  named there.
* Registers across the mission loop: `mission_run` does not reload SI before `game_frame`, which
  passes it on through the world group (`missile_update` changes it; `incoming_fire` and
  `enemy_update` store it in `DS:D6F6`) and the drawing part (`crew_gunners` changes it;
  `projectile_tick` → `mark_near_objects` stores it). The ports of `game_frame` and `mission_run`
  have to thread SI (and AX after `crew_gunners`) as the S2a routines return them. `mission_stop`
  also leaves DI changed; no reader of DI after it is known.
