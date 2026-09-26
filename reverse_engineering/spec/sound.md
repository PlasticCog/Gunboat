# Sound: effects, music, devices (GB.EXE `12ed`, `1af5`, `1ace`, `1b37`, `1b5f`)

Target: `reverse_engineering/out/GB_unp.exe`, DGROUP `1B73`. Symbols: `spec/sound_symbols.csv`.
Confidence tags as in `simulation.md`. Unlike the platform and video code, Gunboat's sound code
is **not** Test Drive III's: TD3 has its own AdLib driver, Gunboat drives Ad Lib Inc.'s resident
driver and an MT-32.

Port: `gunboat-port/src/sound/` (the effects driver, the music sequencer, the PC speaker back end
and driver, the detection); tests `gunboat-port/tests/difftest/test_sound.py` with the hardware model
`soundmodel.py` (§3.4). Notation: `w[X]` / `b[X]` = word / byte at DS:X; DI = a voice offset.

## 1. Two systems

| | Effects (missions, menus) | Music (title) |
|---|---|---|
| Code | `12ed` | detection `1ace:00e8`, sequencer `1af5:0006`, back ends `1af5`, speaker driver `1b37` |
| Devices | PC speaker, Tandy 3-voice (parked) | MT-32, AdLib, Game Blaster (CMS), Tandy, PC speaker |
| Timer | 236.695 Hz, `sfx_timer_isr` `12ed:00a3` (platform §1) | 89.63 Hz, `121b:0ce2` |
| Data | 13 effect programs in DGROUP (`DS:DB1E`) | `VALKPC.MUS` (speaker), `VALK3V.MUS` (Tandy, CMS), `VALK12.MUS` (MT-32, AdLib) |

## 2. Effects (`12ed`) **verified** (ported: `sound/sfx.cpp`)

### 2.1 Programs

An effect is a program of 2-byte commands `(b0, b1)` in DGROUP; `sfx_programs` `DS:DB1E` holds the
13 addresses (DB6A DB3D DB48 DB53 DB75 DB8A DB9F DBB4 DBC2 DBFD DC10 DC1D DB38). Every program
starts `4D 10` (tempo 40h), then legato `0D 00` or gap 1 `0D 01`. Effect 12 (`DB38`) plays nothing:
it replaces the current effect (the key handler plays 12, then 0, a short click; game_flow §7).
Effect 6 (`DB9F`, the engine) loops forever with `7D 00` … `8D 14`; `engine_sound_update`
(`0919:3cc9`) rewrites its bytes DBA0..DBAF (tempo, loop count 1 = stop, notes). Callers:
simulation spec (weapons 1/2/3/8, hits 7/9/0Ah/0Bh, missile 4, sinking 7, …).

### 2.2 Installation and API

```c
engine_sound_on()  12ed:0000   if (b[DA46] == 0) { b[DA46] = 1; sfx_install(); }
engine_sound_off() 12ed:0063   if (b[DA46] == 1) { b[DA46] = 0; w[DAF0] = 0; all_off(); sfx_remove(); }
sfx_play_far(id)   12ed:0018   sfx_play(AX = id)
sfx_play(AX)       12ed:0025   if (b[DA46] == 0) return;
                               p = AX <= 12 ? w[DB1E + 2*AX] : AX;      // quirk: id > 12 is the address
                               w[DAD2] = p; if (Tandy) w[DAD4] = w[DAD6] = w[DAD8] = 6E55h;
                               w[DAF0] = 1;
sfx_install()      12ed:08c0   sfx_speaker_init(); v = b[DA8F];
                               far[DA54] = vector v (DOS 35h); vector v = 12ed:00a3 (DOS 25h);
                               PIT ch0: out 43h,36h; out 40h,B1h; out 40h,13h (236.695 Hz)
sfx_remove()       12ed:08f3   sfx_silence(); vector b[DA8F] = far[DA54] (DOS 25h); PIT ch0 = 0 (18.2 Hz)
sfx_speaker_init() 12ed:0800   w[DAF0] = w[DAFC] = w[DAF2..DAF8] = 0;
                               speaker: w[DA88] = DA58, b[DA8A] = 7, w[DA8B] = 1, w[DA8D] = 0, b[DA8F] = 8;
                                        out 43h,B6h (PIT ch2: low/high byte, square wave)
                               Tandy:   w[DA88] = DA70, b[DA8A] = 1, w[DA8B] = 4, w[DA8D] = 6, b[DA8F] = 1Ch;
                                        all_off(); port 61h |= 60h
sfx_silence()      12ed:089d   all_off()
all_off (inline)               speaker: port 61h &= FCh; Tandy: out C0h 9Fh BFh DFh FFh
voice_off(DI) (inline)         speaker: port 61h &= FCh; Tandy: out C0h, ((DI<<4)+80h)|0Fh
```

`sfx_timer_on` DS:DA46 is 1 while the timer is installed; `sfx_device_tandy` DS:DA47 (the title
clears it); `sfx_last_voice` DA8D = 2 × (voices − 1): every per-voice array is a word array indexed
by DI = 0, 2, … ≤ DA8D (signed compares).

### 2.3 The timer step

```c
sfx_timer_isr()  12ed:00a3 (DS = DGROUP, body at 12ed:00b1)
    if ((w[DA52] & 3) == 0) w[08C0]++;              // tick_counter: 4 of 13 = 72.83 Hz
    if ((s16)++w[DA52] >= 13) { w[DA52] = 0; pushf; call far [DA54]; }   // the BIOS: 18.21 Hz
    sfx_timer_tick(); out 20h,20h
sfx_timer_tick() 12ed:00eb
    if (w[007E]) { all_off(); return; }             // muted (the S key)
    if (w[DAFC]) { secondary sequence: see below; return; }
    if (w[DAF0] == 0) return;
    if (w[DAF0] == 1) { w[DAF0] = 2; for DI: w[DAF2+DI] = 1; }
    w[DAE2] = w[DADE]; w[DB1A] = w[DB16];           // tempo, tempo shift -> work
    for DI: st = w[DAF2+DI];
        if (st == 1 || (st != 0 && w[DAE4+DI] == 0)) sfx_channel_transfer(DI);
        else if (st != 0 && w[DB08] == 0 && w[DAE4+DI] == w[DB14]) voice_off(DI);   // quirk: work regs
        w[DAE4+DI]--;
    w[DADE] = w[DAE2]; w[DB16] = w[DB1A];
    if (any w[DAF2+DI]) sfx_tandy_envelope(); else { all_off(); w[DAF0] = 0; }
sfx_channel_transfer(DI) 12ed:02b8   work regs DADC/DAEE/DB08/DB14 <- DAD2/DAE4/DAFE/DB0A [DI];
                                     sfx_channel_step(DI); and back
sfx_channel_step(DI)     12ed:02f4   if (w[DAF2+DI] != 2) sfx_channel_start(DI); sfx_channel_run(DI)
sfx_channel_start(DI)    12ed:0302   b[DA51] = b[DA50] = 0; w[DB08] = 0; w[DB14] = 6; w[DB1A] = 0;
                                     w[DB1C] = 1; w[DABE+DI] = w[DAC8+DI] = 0; w[DAEE] = 0; w[DAF2+DI] = 2
sfx_channel_run(DI)      12ed:033d
    if (!Tandy) { r = w[DAEE];
        if (w[DB08] != 1 && (r == w[DB14] || (r < w[DB14] && r == 4))) { voice_off(DI); return; }
        if (r) return; }
    do sfx_parse_command(DI); while (w[DAF2+DI] && w[DAEE] == 0);
```

The secondary sequence (`sfx_secondary_state` DAFC ≠ 0; only ever set to 0 in the game): on state 1
voice 0's DAF2/DABE/DAC8/DAB4/DAAA/DA90 are saved to DAFA/DAC6/DAD0/DABC/DAB2/DA98; then with DI = 0
`w[DAF2] = w[DAFC]`, tempo/shift from DAE0/DB18, `sfx_secondary_step` (`12ed:0284`: as the transfer
with DADA/DAEC/DB06/DB12), results back to DAE0/DB18/DAFC; when DAFC becomes 0 the saved words return.

### 2.4 Commands (`sfx_parse_command` `12ed:03c4`; w[DAEE] = 0 first, w[DADC] += 2 last)

`b0` low nibble = op, high nibble = h; `b1` = argument.

| op | Action |
|---|---|
| 0 | rest: voice_off; `w[DABE+DI] = 1`; duration(b1) |
| 1..12 | note (`sfx_note_on` `12ed:0472`): divisor = `w[w[DA88] + 2(op−1)] >> h` → PIT ch2, gate on; `w[DABE+DI] = 0`; duration(b1). Tandy: octave h−1 (h < 1 counts `b[DA50]`), a divisor > 3FFh halved once (counts); voice 6 = noise (out E0h+op−1, envelope h−1) |
| 13 | control h (`sfx_control` `12ed:0655`), below |
| 14 | raw pitch (`12ed:0752`): b1 = 0 → voice_off, no duration; else divisor = `(w[w[DA88]] − (b1 << b[DA8A])) >> h`, gate on, `w[DAEE] = w[DB1C]` |
| 15 | end: `w[DAF2+DI] = 0`; voice_off |

duration(b1) (`sfx_note_duration` `12ed:0523`): `w[DAB4+DI] = 1`; `d = (w[DAE2] + w[DB1A]) >> (b1 & 7)`;
if `b1 & 8`: `d += d >> 1` (dotted) and — quirk — the tie test below uses the high byte of `d >> 1`
instead of b1; tie (`b1 & 10h`): `DAC8` 0/2→+1, 1→3, 3 stays; else `DAC8 += (DAC8 != 0)`; `w[DAEE] = d`.

Control sub-commands (argument b1): 0 legato (`sfx_cmd_legato` `06d1`: b1 = 0 → `w[DB08] = 1`, else
`w[DB08] = 0`, **byte** `DB14 = b1`); 1/2 tempo shift (`06ec`: `w[DB1A] = ±(w[DAE2] × b1 / 100`,
remainder > 49 rounds up; DIV); 3 raw length (`074e`: byte `DB1C = b1`); 4 tempo (`0726`:
`w[DAE2] = b1 × 4`); 5 envelope (`0732`, Tandy: `w[DA90+DI] = w[DC3C + (b1 << 1 & FFh)]`); 6 volume
(`0747`, Tandy: `w[DAAA+DI] = b1`); 7 loop count `w[DA48+DI] = b1`; 8 loop: unless the count runs out
(`--count == 0`; a count of 0 loops forever) `w[DADC] -= b1`. Other h: nothing.

### 2.5 Tandy envelopes (`sfx_tandy_envelope` `12ed:0586`) **verified** (memory), parked (chip)

For each voice (attenuation register 90h + 20h × voice): a new note restarts the envelope
(`DA9A = DA90`, `DAA2 = 0`) unless tied; bytes are read from `DA9A` until FFh (`DAA2 = 1`), then the
volume `DAAA`; rests, ended voices and released notes get 0Fh (silent).

## 3. Device detection (`sound_detect` `1ace:00e8`) **verified**

`sound_detect(mask, cms_buf far, adlib_buf far)`: `b[F290] = 0`, `speaker_music_reset()` (§4.4), then
the first device of the mask (byte test) that answers:

| `DS:F2A0` | Device | Test | Back end `F294` / `F298` / `F29C` (segment 1af5) | `F292` |
|---|---|---|---|---|
| 8 | Roland MT-32 (MPU-401 UART) | `mpu_reset` ≠ 0 | 013F / 0163 / 0125 | 10h |
| 4 | AdLib through `ADLIB.COM` | `adlib_driver_present` ≠ 0; then `adlib_driver_init`, `adlib_load_bin(adlib_buf)` | 01E5 / 01FE / 0213 | 9 |
| 2 | Game Blaster (CMS) | `cms_detect` ≠ 0 and `CMS.DRV` opens (handle > 0): read 12F7h bytes to cms_buf = `far[F0E8]`, close, `cms_driver_init` | 02CE / 02F0 / 030C | 8 |
| 1 | Tandy 3-voice | mask bit 0 and ROM byte `FC00:0000` = 21h | 033F / 0394 / 03B8 | 3 |
| 0 | PC speaker | otherwise | 033F / 0394 / 03B8 | 1 |

### 3.1 MPU-401 (`1af5:0192`, `01be`)

`mpu_command(cmd)`: up to 20000 reads of 331h until bit 6 is clear, out 331h,cmd; up to 20000 reads
until bit 7 is clear; AX = xxFEh if 330h reads FEh (AH = the caller's), else 0 (also on a time-out).
`mpu_reset()` = `mpu_command(FFh)` twice, the second's AX.

### 3.2 AdLib driver (`1af5:0290`, `0238`)

`adlib_driver_present()`: the INT 65h vector v (DOS 3565h); AX = `w[v.seg : v.off − 18h]` if the 19
bytes at `v.off − 16h` equal `"SOUND-DRIVER-AD-LIB"` (DS:E61A, REPE CMPSB), else 0 (so a driver whose
word is 0 counts as absent). `adlib_load_bin(buf)`: `mus_open("adlib.bin")`; returns FFFFh only for
handle 0 (quirk: an open error FFFFh goes on, the reads fail); 80h bytes to `DS:E59A`
(`music_program_map`, program → timbre), 340h bytes (16 timbres of 34h) to buf (`far[E596]`); the
file is never closed; AX = the last read's result.

### 3.3 Creative cards (`cms_detect` `1b5f:000e`, base `w[E56E]` = 220h)

All port offsets are added to DL only. AX = 1 if the Game Blaster latch reads back (out base+6 C6h,
out base+0Ah C5h, in base+0Ah = C6h; the same for 39h/3Ah); else 5 if a Sound Blaster DSP resets
(base+6 ← 1, 4 reads, ← 0; up to 20 polls of base+0Eh bit 7 then base+0Ah = AAh) or answers command
C6h with 39h (`cms_dsp_write`/`cms_dsp_read`, 200h polls each); then +2 if an OPL2 at base+8 runs
timer 1 (`cms_opl_write` reg/value with 5-read delays, `cms_opl_wait` up to 40h status reads for
00h, then C0h). Quirk: an OPL alone (+2) also selects the Game Blaster path.

### 3.4 The modelled machine **verified** (tests)

The port models a PC with a PC speaker and nothing else (`sound/hw.cpp`, `soundmodel.py`): 330h/331h
read FFh (the MPU waits time out: `mpu_reset` = 0 after 2 × 20000 reads), the INT 65h vector is
0000:0000 (the signature at 0000:FFEA does not match), the Creative ports read FFh (`cms_detect` = 0),
FC00:0000 = 0. So `sound_detect(0Fh)` gives device 0, one voice, the speaker back end, and
`music_start` loads `VALKPC.MUS`. The absent devices' probes do not pump the host (bounded loops).

## 4. Music **verified**

### 4.1 Files

`music_start` (`00f2:1044`, game_flow §3.1) picks `VALKPC.MUS` (device 0), `VALK3V.MUS` (1, 2) or
`VALK12.MUS` (4, 8). A `.MUS` file is `{u16 n; n bytes}`: `mus_open` (`1af5:03bd`: 3D00h, FFFFh on an
error), `mus_read(fh, &n, 2)` and `mus_read(fh, far[F5BE], n)` (`03dc`: 3Fh, 0 on an error),
`mus_close` (`03d0`), then `music_play(far[F5BE], 1)`.

### 4.2 Start and stop

```c
music_play(far p, loop)  1ace:000e   music_silence(); far[E570] = 0; if (w[F2A0] == 8) mpu_command(3Fh);
                                     far[F288] = p + 1 (the first byte is skipped); w[F28C] = p.off + 1;
                                     w[F28E] = 1; w[F2A2] = loop; b[F290] = 1
music_silence()          1ace:005c   b[F290] = 0; for (v = 0; v < w[F292]; v++) call far [F298](v);
                                     if (w[F2A0] == 2) cms_driver_init(); else if (== 8) mpu_command(FFh)
music_resume()           1ace:00b4   (unreached) if (w[F2A2] && !b[F290]) { if (w[F2A0] == 8)
                                     mpu_command(3Fh); w[F28E] = 1; b[F290] = 1; }
```

### 4.3 `music_tick` (`1af5:0006`) and the event format

Per menu timer interrupt: nothing unless `b[F290]`; on the CMS (`b[F2A0] == 2`) `call far [F5D8]`
first; `if (--w[F28E] != 0) return`; then events from `far[F288]` until a non-zero delta:

* A byte with bit 7 is a status, kept in `b[F291]` (running status); channel c = its low nibble;
  channels ≥ `w[F292]` are skipped.
* `9c n v`: note on `call far [F294](c, (s8)n, v & 7Fh)`; v & 7Fh = 0 is a note off.
* `8c`: note off `call far [F298](c)`; **no data bytes**.
* `Cc p`: program `call far [F29C](c, p & 7Fh)`.
* `Dc x`: loop mark: at this call's return (after a non-zero delta) `far[E570]` = the position.
* `FC`: end: continue at `far[E570]`, or `music_silence()` when it is 0.
* Other statuses: nothing, no data bytes.
* Then the delta in ticks (89.63 Hz): d < 80h; or two bytes d1 ≥ 80h, d2:
  `((d1 & 7Fh) >> 1) << 8 | (d2 & 7Fh)` — quirk: SHL AL / SHR AH / **ROR** AL (not RCR) loses bit 0
  of d1 (a MIDI variable length would be `(d1 & 7Fh) << 7 | d2`). 0 → the next event at once; else
  `w[F28E] = delta`, `w[F288]` = the position.

The songs start with program changes and a `D0 00` mark and end with `FC`; `VALKPC.MUS` (546
bytes) plays channel 0 (its channel-15 events are skipped). Its first delta `81 10` is 10h ticks, not
90h, by the quirk above.

### 4.4 PC speaker back end and the speaker music driver (`1af5:033f..03b8`, `1b37`)

```c
speaker_note_on(c, n, v) 1af5:033f   al = n − 18h (8-bit); octave = the number of 12s subtracted while
                                     the result stays >= 0; semitone = the remainder;
                                     voice = (b[F2A0] == 1) ? c : 0; s = w[E67E + 2 voice] (E64E/E65A/E666);
                                     w[s+1] = w[E636 + 2 semitone] >> octave; w[s+4] = v >> 3;
                                     speaker_voice_start(s, C8h, voice)
speaker_note_off(c)      1af5:0394   speaker_voice_start(E67D, C8h, (b[F2A0] == 1) ? c : 0)
speaker_program          1af5:03b8   nothing
```

Voice records: 3 × 16h bytes at DS:E684: +0 active (80h), +1 priority, +2 script (far), +6 wait
counter, +8 + 2r script registers r (0 = volume, 1 = PIT divisor), +0Eh saved FLAGS, +10h call depth,
+12h call stack.

`speaker_voice_start(script, prio, voice)` (`1b37:003c`): voice (byte) < 3 → that record; else the
first free one, else the lowest priority (last of equals) if below prio (byte), else nothing. The
record: active 80h, priority, script = DS:script, +6..+11h cleared. `1b37:00a6` (unreached) =
`speaker_voice_start(script, 64h, FFFFh)`. `speaker_music_reset` (`1b37:0002`, was `tandy_detect`):
clears **voice 0's record only**, port 61h &= FCh, `b[E6C8] = (FC00:0000 == 21h)`, a Tandy is silenced.

`speaker_music_tick` (`1b37:00c0`, per menu timer interrupt): each active voice whose wait counter
goes below 0 runs commands `c` (op = c & 7 through the table DS:E6D6, al = c >> 4) until one sets
carry, and stores its pointer. Then, if a voice ran (the pointer offset ≠ 0) and not on a Tandy
(`b[E6C8]`: out C0h/C1h only, parked): off if `b[E6C9]` = 0; else the active voice with the highest
priority (last of equals): off if its priority, volume byte or divisor is 0; otherwise port 61h |= 3,
and PIT ch2 (B6h, low, high) only when the divisor differs from `w[E6C6]` (then stored).

| Op | Handler | Action (DI = record, ES:SI = script) |
|---|---|---|
| 0 | `01e3` | end of this tick (carry) |
| 1 | `01e5` | register al = next word |
| 2 | `01f3` | wait counter = next word |
| 3 | `01fb` | return from a call; with depth 0: the voice ends (carry) |
| 4 | `0218` | register al += next word |
| 5 | `0226` | al 7 call, 6 jump, 0..5 jump on the saved flags (= ZF, < CF, ≠, >, ≤, ≥; tables E6CA/E6D0) to the next word, else skip it |
| 6 | `0266` | compare register al with the next word; PUSHF → +0Eh (the whole FLAGS word) |
| 7 | `027a` | byte, word off, word seg: the byte to seg:off |

The note script `11 dd dd 01 vv vv 75 72 E6 65 4E E6` sets the divisor and volume, calls E672
(`02 06 00` wait 6, `00`, `14 00 00` add 0, `02 06 00`, `00`, `03` return) and jumps back: a note
sustains until the release script E67D (`03`: the voice ends).

### 4.5 Parked back ends (not ported)

MT-32: `mt32_note_on` (`013f`: note kept in `E57C[c]`, 9c n v through `mpu_write` `01cf`, which waits
for 331h bit 6 and writes 330h), `mt32_note_off` (`0163`: 8c n 0, Bc 7Bh 0), `mt32_program` (`0125`:
Cc, `E59A[p−1]`). AdLib through INT 65h (`ES:BX` = the stacked arguments): `adlib_note_on` (`01e5`,
SI = 13h, the note − 3Ch), `adlib_note_off` (`01fe`, SI = 14h), `adlib_program` (`0213`:
`adlib_call(c, E59A[p] × 34h, seg of E596)` — quirk: the timbre buffer's offset is ignored),
`adlib_driver_init` (`02b0`, SI = 0), `adlib_call` (`02b9`, SI = 15h). CMS through the loaded
`CMS.DRV` at `far[F0E8]` (AH = function): `cms_note_on` (`02ce`: AH 8, AL 9c, DH n, DL v),
`cms_note_off` (`02f0`: AH 8, AL 8c, DH `E57C[c]`), `cms_program` (`030c`: nothing),
`cms_driver_init` (`0311`: function 2 with DX:CX = DS:F21A, 5 → `far[F5D8]` the driver's tick, 6
with DS:E62D, 9).

## 5. PORT decisions

* Effects and the speaker music run on `mem[]` as in the original; the PC speaker becomes
  `host_speaker(divisor, on)`: one call per completed PIT ch2 divisor and per change of port 61h
  bits 0-1 (on = both set). The PIT ch0 programming becomes `host_set_timer(13B1h, sfx_timer_isr)` and
  `host_set_timer(0, bios_tick)`; the chain to the saved vector supports the BIOS handler only.
* The Tandy chip is parked (port writes dropped, memory effects ported). MT-32, AdLib and CMS paths
  are unreachable on the modelled machine and fatal if reached (`sound_parked`).
* AdLib music later: a translation of the INT 65h functions of `ADLIB.COM` (Ad Lib Inc. sound driver
  V1.51) that the game calls (0, 13h, 14h, 15h) on Nuked-OPL3, with `VALK12.MUS`.
* Timing: the host runs the handlers at the PIT rates from sample time (platform §1).

## 6. Open questions

* The `ADLIB.COM` implementation of INT 65h functions 0, 13h, 14h, 15h.
* The Tandy music output (`1b37:016f`: (divisor × 1800h × 2) >> 16 rounded, halved to ≤ 3FFh, out
  C0h/C1h, attenuation `vol ^ 0Fh`) and the Tandy effects' sound when that device is taken up.

## 7. Corrections to the first version of this spec

* The music stream pointer is `DS:F288` (far); `DS:F5D8` is the CMS driver's tick routine
  (renamed `cms_driver_tick`, was `music_stream`).
* `1ace:005c` silences any device (renamed `music_silence`, was `cms_silence`); `1b37:0002` resets
  the speaker driver and tests the Tandy ROM (renamed `speaker_music_reset`, was `tandy_detect`).
* `cms_detect` also finds a Sound Blaster DSP and an OPL2 (result bits, §3.3).
* The `.MUS` format (§4.3): note off has no data bytes; the two-byte delta drops a bit.
* The AdLib instrument calls in `music_start` are `adlib_call(voice, timbre off, seg)` (INT 65h,
  SI = 15h) with timbres from the EXE (DS:0822/0856/088A, game_flow); `ADLIB.BIN` is loaded by
  `sound_detect` (§3.2).
* `VALKPC.MUS` is the speaker's, `VALK3V.MUS` the Tandy's and the CMS's (§4.1).
