# The Ad Lib sound driver `ADLIB.COM` V1.51 (not part of GB.EXE)

Target: the user's `Game/ADLIB.COM`, 13,280 bytes, the Ad Lib Inc. sound driver
V1.51 of 1989 (its banner says so; FNV-1a 44561E68h; its texts are not quoted here). Gunboat does
not contain an AdLib driver: on an AdLib the player ran this TSR before `GB.EXE`, and the game's
music back end (sound.md §4.5) calls it through INT 65h.
Port: `gunboat-port/src/sound/adlib_driver.cpp`; tests `gunboat-port/tests/difftest/test_adlib.py`
(the original's code in Unicorn, sound.md §3.4). Tool: `python reverse_engineering/tools/adlib_dis.py
[--fn 1ee7 ...] [--data]` (capstone; names from this spec). The driver's routines are not in
`symbols.csv` (that list is GB.EXE's); they are named here and in the C++ as `ADLIB.COM oooo name`.
Confidence: everything in §2–§6 is **verified** by the differential tests (all memory and every OPL2
register write), except where a routine is marked *not ported*.

## 1. The program

* A .COM: loaded at `PSP:0100`; addresses here are **CS offsets** (file offset + 100h). Code
  0100–25B9; the data segment starts at the next paragraph: **DS = CS + 25Ch** (DS:0000 = CS:25C0 =
  file offset 24C0h). The file ends at DS:0F20.
* Compiled C (probably Lattice C 3: the start-up's error messages about the stack size, I/O
  redirection and stack overflow; longs in **AX:BX**, AX the high word; `lmul`/`ldiv` helpers)
  plus hand-written assembly for the interrupt handlers and the chip access. C functions are near,
  arguments on the stack (first argument at [BP+x] lowest), the caller cleans up, DS = SS = ES.
* Resident: CS:0000 up to DS:1F1A (the event buffer), 44Fh paragraphs (INT 27h with DX = 44E9h).
* The port puts the PSP at **0B00h** (CS = 0B00h, DS = 0D5Ch): below GB.EXE at 1000h, where DOS
  could have loaded it (a TSR loaded early; GB.EXE's environment and PSP would follow at 0F4Fh..0FF0h).
  The image is copied from the user's file at run time (`adlib_load`), never shipped.

## 2. Memory

### 2.1 Code-segment variables

| CS | Size | Name | Role |
|---|---|---|---|
| 020B / 020D | w, w | caller SS / SP | saved by the INT 65h handler (stack state; the port does not model it) |
| 020F / 0211 | w, w | driver SP / SS | the INT 65h stack: DS:0F20 and DS (set by `install_int65`) |
| 0213 | w[24] | fn_table | near handlers of INT 65h functions 0..17h (§3) |
| 0243 | w[24] | fn_argc | argument words each takes from ES:BX |
| 02D7 | w | version | 0151h: `adlib_driver_present` (GB.EXE) returns it |
| 02D9 | char[19] | signature | "SOUND-DRIVER-AD-LIB" (16h bytes before the INT 65h handler) |
| 02EC | 3 bytes | | 01 01 00 (unused by the reached code) |
| 0568 | w | clk_chain | 1 while the driver's PIT divisor is 0 (every interrupt is also a BIOS tick) |
| 056A | w | clk_divisor | the PIT channel 0 divisor the driver programmed |
| 056C | w | clk_accum | += divisor per interrupt; a carry means a BIOS tick is due |
| 056E | far | clk_old | the INT 8 vector before the driver (the BIOS's) |
| 0572 / 0574 | w, w | | the interrupted SS:SP while the sequencer tick runs (not modelled) |
| 0576 | w | clk_countdown | interrupts until the next sequencer tick |
| 0578 | dword | clk_count | interrupts counted; the high word is reset to 0 when it would reach 8000h |
| 057C | byte | clk_busy | the sequencer tick is running (re-entry guard) |

### 2.2 The data segment (DS offsets; initial values from the file)

| DS | Size | Name | Role |
|---|---|---|---|
| 0002 | w | dos_version | INT 21h 30h's AX (1 if AL = 0); DOS 5.00 in the port: 0005h |
| 0006 | w | psp_seg | the PSP |
| 0008 | w | args | the argument string "c[ tail]" on the start-up stack (0F18h without options) |
| 000A | w | stack_top | 0C20h + 300h = 0F20h |
| 000C | w | data_end | 0C20h (file) |
| 000E | w | free_bytes | PSP:0006 − resident_end (FEF0h − 34E0h = CA10h) |
| 0010 | w | heap_base | 0F20h: the event buffer |
| 0012 | w | resident_end | (DS − CS) × 16 + stack_top = 34E0h |
| 011C–031B | | | the timer handler's stack ('S' fill; SP = 031Ch) |
| 031E | 54 × 10 | event_queue | linked nodes (first word = next) |
| 053A / 0544 / 0546 | w | queue_count / head / free | |
| 0548 | 11 × 5 | voice_state | first byte of each = 1 after Init |
| 057F–0635 | | event slots | per-voice words of the sequencer's event types (§4.1 `event_slot_a/b`) |
| 063B | w | event_pool | the free list in the event buffer |
| 063D | 77 bytes | voice_flags | |
| 06B6 | w | clock_ticks | ticks_per_beat × tempo / 60 |
| 06B8 | w | tempo | at least 13h |
| 06BA | w | fn0A_value | set by function 0Ah, read by 0Bh |
| 06BC / 06BE | w | seq_active / seq_playing | the driver's own sequencer; 0 unless functions 3 / 0cd9 start it |
| 06C0 | w | act_voice | functions 0Ch / 0Dh |
| 06C2 / 06C4 | w | seq_start | |
| 06C6 | w | mode | 0 melodic, else percussion (function 6) |
| 06C8 | w[6] | event_types | 2, 3, 4, 5, 7, 6 |
| 06D4 / 06D6 | w | buffer / buffer_nodes | the event buffer and its 10-byte nodes (size / 10 = 199h) |
| 06D8 | w | ticks_per_beat | F0h (function 12h) |
| 06DA | | | the sequencer's two error texts (bad event type, list full) |
| 06F6 | w | genPort | 388h |
| 06F8 | byte | percBits | the rhythm key bits of register BDh |
| 06F9 | 5 bytes | percMasks | 10h 08h 04h 02h 01h (bass drum, snare, tom, cymbal, hi-hat) |
| 06FE | 9 bytes | voiceNote | the last pitch of each voice (after +30h) |
| 0705 / 0706 | byte | sd_pitch / tom_pitch | |
| 0709 | 11 bytes | voiceKeyOn | 20h or 0 |
| 0714 / 0774 | 96 bytes | noteDIV12 / noteMOD12 | octave and semitone of each pitch |
| 07D4 | 18 × 2 | slotRelVolume | per slot: numerator, denominator (1/1) |
| 07F8–07FB | bytes | amDepth, vibDepth, noteSel, percussion | |
| 07FC / 0818 | w[14] | pianoParamsOp0 / Op1 | 1,1,3,15,5,0,1,3,15,0,0,0,1,0 / 0,1,1,15,7,0,2,4,0,0,0,1,0,0 |
| 0834 | 18 × 14 | paramSlot | per slot: KSL, multi, feedback, attack, sustain, EG, decay, release, level, AM, VIB, KSR, FM, wave |
| 0930 | 9 × 2 | slotVoice | the two slots of each melodic voice (0,3 1,4 2,5 6,9 7,10 8,11 12,15 13,16 14,17) |
| 0942 | 5 × 2 | slotPerc | bass drum 12,15; snare 16; tom 14; cymbal 17; hi-hat 13 |
| 094C | 18 | offsetSlot | the operator register offset of each slot (0..5, 8..0Dh, 10h..15h) |
| 095E | 18 | carrierSlot | 0 0 0 1 1 1 × 3 |
| 0970 | 18 | voiceSlot | the voice of each slot |
| 0982 | 25 × 12 w | fNumTbl | F-numbers (x1) of 12 semitones for pitch-bend steps of 4/100 half-tone |
| 0BDA / 0BF0 | w[11] | halfToneOffset / fNumFreqPtr | per voice (0 / fNumTbl) |
| 0C06 / 0C08 | w | pitchRange / pitchRangeStep | 1 / 25 |
| 0C0A | w | modeWaveSel | 0: the wave select registers are written 0 |
| 0C20–0F1F | | | the start-up's stack, then the INT 65h stack ('P' fill in the file) |
| 0F20–1F19 | | event buffer | 199h nodes of 10 bytes (past the file's end: whatever memory held; zero in the port) |

## 3. INT 65h (`int65_handler` 02EF)

```c
push ds; save SS:SP in CS:020B/020D; SS:SP = CS:0211 / CS:020F       // the driver's stack
si += si;                                   // 16 bits: SI >= 8000h wraps (quirk)
if (si > 2Eh) { DS = CS; INT 21h 09h: the bad-function-number message (CS:034C); }
else { copy fn_argc[si] words from ES:BX to the new stack; DS = ES = SS; AX = fn_table[si](...) }
restore SS:SP; pop ds; iret                 // AX = the function's AX
```

The caller puts the function number in SI and ES:BX on its argument words. `driver_installed` (0298,
and GB.EXE's `adlib_driver_present`) finds the driver by the signature 16h bytes before the vector's
offset and returns the word 18h before it (0151h).

| SI | CS | Args | Role (from the code) | GB.EXE |
|---|---|---|---|---|
| 0 | 08DC | 0 | **Init** (§4.1) | `adlib_driver_init` |
| 1 | 0947 | 0 | all voices off; INT 8 and the PIT restored (`clock_remove` 060E) | |
| 2 | 09CA | 2 | sequencer | |
| 3 | 0C36 | 1 | start (`seq_active`, `seq_playing` = 1, the clock set) / stop | |
| 4 | 0C55 | 0 | AX = playing | |
| 5 | 0C75 | 0 | stop and flush the event queue | |
| 6 | 0991 | 1 | SetMode (§4.1) | (from Init) |
| 7 | 09C2 | 0 | AX = mode | |
| 8, 9, 0Eh, 0Fh, 10h, 11h | 0AE0, 0B7E, 0A10, 0A7B, 0B2F, 0BD4 | 4, 4, 5, 3, 4, 5 | timed sequencer events | |
| 0Ah / 0Bh | 0C23 / 0C2E | 1 / 0 | set / get `fn0A_value` | (from Init) |
| 0Ch / 0Dh | 096D / 0989 | 1 / 0 | set (if < 11) / get `act_voice` | |
| 12h | 092C | 1 | ticks_per_beat = the argument, doubled until >= 60 | |
| 13h | 1EE7 | 2 | **NoteOn**(voice, pitch) (§4.2) | `adlib_note_on` |
| 14h | 1FBF | 1 | **NoteOff**(voice) (§4.3) | `adlib_note_off` |
| 15h | 223A | 3 | **SetVoiceTimbre**(voice, params far) (§4.4) | `adlib_call` |
| 16h | 165A | 1 | SetPitchRange | |
| 17h | 167E | 1 | SetWaveSel | |

GB.EXE calls 0, 13h, 14h and 15h only. The port translates those four with everything they reach;
the others and the bad-function path are *not ported* (fatal: GB.EXE never reaches them).

## 4. The functions GB.EXE calls

Notation: `out(r, v)` = `SndOutput` (06CE): `OUT genPort, r` (low byte), 6 reads of genPort,
`OUT genPort+1, v` (low byte), 35 reads (the OPL2's address and data write delays; the values read
are discarded). All loops count with signed compares unless noted; bytes are zero-extended.

### 4.1 Init (function 0, `fn_init` 08DC) and its callees

```c
fn_init():      seq_playing = seq_active = 0; SoundWarmInit(); SetMode(0); fn0A_value = 0;
                init_event_queue(); init_event_pool(); act_voice = seq_start = 0;
                init_voice_state(); clear_voice_flags(); clear_event_ptrs(); set_tempo(90);
SoundWarmInit() 2100:  for v < 9: out(B0h+v, 0);  InitSlotVolume();          // num = den = 1
                for slot < 18: SetSlotParamValue(slot, 8, 3Fh); SetSlotParamValue(slot, 7, 0Fh);
                100 x inp(388h) summed into an uninitialised local (a delay; never read);
                amDepth = vibDepth = noteSel = percussion = 0;
                SetWaveSel(0); SetPitchRange(1); SetPercMode(0);
                SetGParam(&amDepth);   // quirk, below
                InitSlotParams(); InitFNumPtrs();
SetMode(m) 0991:   w = modeWaveSel; if (mode != m) SoundWarmInit(); mode = m; SetPercMode(m); SetWaveSel(w);
SetPercMode(m) 19B9: if (m) { SoundChut(6..8); SetFreq(8, 24, 0); tom_pitch = 24; sd_pitch = 31;
                SetFreq(7, 31, 0); }  percBits = 0; percussion = (m != 0); SndSAmVibRhythm(); InitSlotParams();
SetWaveSel(s) 167E: modeWaveSel = s; for slot < 18: out(E0h + offsetSlot[slot], 0);
SetPitchRange(r) 165A: r = min(r, 12) (unsigned); pitchRange = r; pitchRangeStep = 25 r;
SetGParam(p far) 2200: for i < 3: (&amDepth)[i] = low byte of p[i] (words); SndSetPrm(-1, 14 + i);
InitSlotParams 21BA: for slot < 18: SetSlotParam(slot, carrierSlot[slot] ? pianoParamsOp1 : pianoParamsOp0, 0);
InitFNumPtrs 186F:   for v < 11: fNumFreqPtr[v] = fNumTbl; halfToneOffset[v] = 0;
SoundChut(v) 20A6:   out(A0h+v, 0); out(B0h+v, 0);
init_event_queue 1491: node i (DS:031E + 10 i, i < 54) -> DS:0328 + 10 i; head = count = 0; free = DS:031E;
init_event_pool 070A:  pool = buffer; node i -> node i+1 for i < nodes - 1; the last one's link 0;
init_voice_state 0773: voice_state[5 i] = 1 (i < 11);  clear_voice_flags 074E: 77 bytes of 063D = 0;
clear_event_ptrs 079C: for t < 6, v < 11: a = event_slot_a(type[t], v); b = event_slot_b(type[t], v);
                *b = 0; *a = 0;
event_slot_a(t, v) 1212: t 2: 05D7+2v; 3: v ? 0 : 062F; 4: 05AB+2v; 5: 0603+2v; 7: 057F+2v; else v ? 0 : 0633
event_slot_b(t, v) 129E: t 2: 05ED+2v; 3: 0631;          4: 05C1+2v; 5: 0619+2v; 7: 0595+2v; else 0635
set_tempo(t) 083E: tempo = max(t, 13h) (unsigned); clock_ticks = low word of ldiv(lmul(ticks_per_beat, t), 60)
                (the unclamped t); if (seq_active) set_clock_rate(clock_ticks) (160F, not ported);
```

Quirks kept: **SetGParam(&amDepth)** reads three *words* from the byte variables amDepth..percussion
and on: amDepth = amDepth, vibDepth = noteSel (0), **noteSel = pianoParamsOp0[0] = 1**, so register
08h is written 40h (note select) after every warm start. **clear_event_ptrs** writes 0 to DS:0000 for
the types whose slot exists for voice 0 only (event_slot_a returns 0).

Init writes 491 OPL2 registers.

### 4.2 NoteOn (function 13h, `note_on` 1EE7)

GB.EXE passes (channel, MIDI note − 3Ch).

```c
keyOn = (percussion && voice >= 6 /*unsigned*/) ? 0 : 20h;  voiceKeyOn[voice] = keyOn;
pitch = (signed(pitch) + 30h < 0) ? 0 : pitch + 30h;      // JGE after ADD: the true signed sum
voiceNote[voice] = pitch;                                  // byte
if (keyOn) { if (voice <= 8 /*unsigned*/) SetFreq(voice, pitch, 20h); return; }
if (voice < 6) return;
if (voice == 6) SetFreq(6, pitch, 0);                      // bass drum
else if (voice == 8) { SetFreq(8, pitch, 0); sd_pitch = pitch + 7; SetFreq(7, sd_pitch, 0); }  // tom, snare
percBits |= percMasks[voice - 6]; SndSAmVibRhythm();
SetFreq(v, p, k) 2018: p += halfToneOffset[v]; p = clamp(p, 0, 95) (signed);
                fN = word at fNumFreqPtr[v] + 2 * noteMOD12[p];
                out(A0h+v, fN); out(B0h+v, k + 4 * noteDIV12[p] + (fN >> 8 & 3));   // added
```

### 4.3 NoteOff (function 14h, `note_off` 1FBF)

```c
voiceKeyOn[voice] = 0;
if (percussion && (s16)voice >= 6) { percBits &= ~percMasks[voice - 6]; SndSAmVibRhythm(); return; }
if ((s16)voice <= 8) SetFreq(voice, voiceNote[voice], 0);
```

(NoteOn compares unsigned, NoteOff signed.)

### 4.4 SetVoiceTimbre (function 15h, `set_voice_timbre` 223A)

`params` (far): operator 0's 13 words, operator 1's 13 words at +1Ah, and the wave selects at +34h and
+36h.

```c
w0 = params[+34h]; w1 = params[+36h];
if (!percussion || (s16)voice < 6) { SetSlotParam(slotVoice[voice][0], params, w0);
                                     SetSlotParam(slotVoice[voice][1], params + 1Ah, w1); }
else if ((s16)voice > 10) nothing;
else if (voice == 6) { SetSlotParam(slotPerc[0][0], params, w0); SetSlotParam(slotPerc[0][1], params + 1Ah, w1); }
else SetSlotParam(slotPerc[voice - 6][0], params, w0);
SetSlotParam(slot, p far, wave) 2319: paramSlot[slot][0..12] = low bytes of p[0..12];
                paramSlot[slot][13] = wave & 3; SndSetAllPrm(slot);
```

Quirk: GB.EXE's instruments are 34h bytes (ADLIB.BIN, the DGROUP timbres), so the two wave-select
words are the next instrument's first bytes (or what follows the last one). With `modeWaveSel` = 0
they only land in `paramSlot[][13]`.

### 4.5 The register writers (`SndSetPrm` 1B60 and the `SndS*` routines)

| Routine | Register | Value |
|---|---|---|
| `SndSAmVibRhythm` 1E4B | BDh | amDepth 80h, vibDepth 40h, percussion 20h, OR percBits |
| `SndSNoteSel` 1C9D | 08h | noteSel ? 40h : 0 |
| `SndSKslLevel` 1C1D | 40h + offsetSlot | KSL << 6 OR 63 − (den + 2 num (63 − level)) / 2den (16-bit MUL and DIV; den = 0 divides by zero) |
| `SndSFeedFm` 1CC0 | C0h + voiceSlot | modulator slots only: feedback << 1 OR (FM ? 0 : 1) |
| `SndSAttDecay` 1D18 | 60h + offsetSlot | attack << 4 OR decay & 0Fh |
| `SndSSusRelease` 1D59 | 80h + offsetSlot | sustain << 4 OR release & 0Fh |
| `SndSAVEK` 1D9A | 20h + offsetSlot | AM 80h + VIB 40h + EG 20h + KSR 10h + multi & 0Fh |
| `SndWaveSelect` 1EA3 | E0h + offsetSlot | modeWaveSel ? wave & 3 : 0 |

`SndSetPrm(slot, prm)`: prm 0/8 KslLevel, 1/5/9/10/11 AVEK, 2/12 FeedFm, 3/6 AttDecay, 4/7 SusRelease,
13 WaveSelect, 14/15/17 AmVibRhythm, 16 NoteSel, >= 18 (unsigned) nothing. `SndSetAllPrm(slot)`:
AmVibRhythm, NoteSel, then the slot's six. `SetSlotParamValue(slot, prm, v)` 1B36: paramSlot[slot][prm]
= v, SndSetPrm(slot, prm).

## 5. The timer (`clock_isr` 0621, INT 8)

```c
sum = clk_accum + clk_divisor; clk_accum = sum;             // carry c
if (clk_chain + c) PUSHF / CALL FAR clk_old (the BIOS) else OUT 20h,20h
if (++clk_count.lo == 0 && (s16)++clk_count.hi < 0) clk_count.hi = 0;
if (--clk_countdown || clk_busy) iret;
save regs and SS:SP; SS = DS = ES = CS:0211, SP = 031Ch
do { clk_busy++; ax = seq_tick(clk_count.lo, clk_count.hi); clk_busy--;
     if (-clk_countdown < ax (unsigned)) { clk_countdown += ax; break; } clk_countdown = 0; } while (1);
seq_tick 0CF8: if (!seq_active || !seq_playing) return FFFFh;  ... the sequencer (not ported)
```

Installed with the PIT at divisor 0 (`clock_install` 05D4: `pit_set_ch0` 0586 = OUT 43h,36h; OUT
40h,AL; OUT 40h,AH), `clk_chain` 1, divisor and accumulator 0, the old vector saved. With GB.EXE the
sequencer is never started, so every call chains to the BIOS, counts, and once every 65536 calls
runs the idle `seq_tick` (the countdown goes from 0 to FFFFh). GB.EXE's own timer handlers
(`menu_timer_isr` every 5th interrupt, `sfx_timer_isr` every 13th) chain to the vector they saved,
which is this handler when the driver is installed.

## 6. Installation (start-up 0100, `main` 0371)

```c
start-up: DS = SS = CS + 25Ch, SP = 0F20h; dos_version = INT 21h 30h (AL = 0 -> 1); psp_seg = ES;
          stack_top = heap_base = 300h + data_end; resident_end = 25C0h + stack_top;
          free_bytes = PSP:0006 - resident_end; args = "c" (+ " " + the command tail) on the stack;
          main(heap_base, free_bytes, args)
main:     options /B /P /W (hex): buffer size FFAh (at least 3E8h), port 388h, w 0; the banner;
          buffer > free_bytes: the buffer-too-large message, exit(1);
          install_int65() != 0: the already-installed message, exit(0);
          the installed message; driver_setup(heap_base, buffer, port, w);
          keep_resident(buffer): INT 27h, DX = buffer + resident_end + 0Fh
install_int65 0273: driver_installed() != 0 -> return it; CS:020F = stack_top, CS:0211 = SS;
          INT 65h = CS:02EF; return 0
driver_setup(buf, size, port, w) 08AA: buffer = buf; buffer_nodes = size / 10 (DIV);
          SoundColdInit(port, w); seq_playing = 0; clock_install();
SoundColdInit(port, w) 20CF: genPort = port; InitFNums(); out(01h, 20h); out(04h, E0h); SoundWarmInit();
InitFNums 17D6: for i < 25 (unsigned): SetFNum(fNumTbl + 24 i, 4 i, 100); InitFNumPtrs's loop;
          noteDIV12[12 o + n] = o, noteMOD12[12 o + n] = n (o < 8, n < 12)
SetFNum(f, num, den) 1756: v = CalcPremFNum(num, den); f[0] = (v.lo + 4) >> 3 (16 bits, SHR);
          for i = 1..11: v = ldiv(lmul(v, 106), 100); f[i] = (v.lo + 4) >> 3
CalcPremFNum(num, den) 16B5: d = (s16)(den * 100); n = (s16)(num * 6) (16-bit IMUL, sign-extended);
          v = ldiv(lmul(n + d, 52088), lmul(d, 25)); return ldiv(lmul(v << 14, 9), 1B503h)
```

`lmul` 2481: AX:BX × CX:DX, low 32 bits in AX:BX. `ldiv` 23D9: AX:BX / CX:DX signed; quotient AX:BX,
remainder CX:DX (its sign the dividend's); 0 and 0 for a zero divisor or dividend; the magnitudes by
shift-and-subtract: a divisor below 8000h by 32 exact steps, a larger one by 16 steps that take the
dividend's high word as the first remainder, **correct only when that word is below the divisor**
(true in the driver's own calls; the port reproduces the loop). `inp` 24AE: AX = IN port.

The installation writes 339 OPL2 registers. What stays resident for GB.EXE: INT 65h = CS:02EF, INT 8 =
CS:0621 (the BIOS's vector in CS:056E), the PIT at 18.2 Hz, the chip in melodic mode with the piano
parameters.

## 7. PORT decisions

* **Where**: PSP 0B00h (`ADLIB_PSP`), DS 0D5Ch (`ADLIB_DS`), in mem[] as the resident program; the
  file is read and checked (size and FNV-1a) by `adlib_load`, which also builds the PSP: INT 20h, the
  memory top A000h, the CP/M call with PSP:0006 = FEF0h, INT 21h/RETF at 50h, an empty command tail.
  No MCB or environment (the port's DOS arena starts above GB.EXE).
* **Installation**: `adlib_install` is `main`'s path without options, the start-up's effects written
  directly (DOS 5.00 as the version); the messages are not shown; the stack contents are not written.
  The error paths are not ported (fatal).
* **Chip**: `out(r, v)` = `host_opl_write(r, v)`; genPort is 388h; the status reads (delays, the
  warm start's sum) read 06h (an OPL2 with no timer flags) and change nothing.
* **Stacks**: the driver's two stacks and the saved SS:SP words (CS:020B–020E, CS:0572–0575) are not
  modelled; the tests do not compare them.
* **INT 65h**: GB.EXE's calls go to `int65(si, ES:BX)` (the port's INT 65h: the driver's handler when
  the vector points at CS:02EF, else fatal); the argument words are put where the original's stack
  had them (a `StackLocal` in DGROUP's stack area) and read from there. AX after the call is not
  modelled (GB.EXE ignores it).
* **INT 8**: `run_int8_handler` (timer.cpp) runs `clock_isr` when a vector points at CS:0621; the EOI
  path does nothing (no PIC model). `seq_tick` returns FFFFh when the sequencer is idle; the rest is
  not ported (fatal), as are functions 1–12h, 16h, 17h, `set_clock_rate` 160F and `clock_set_rate` 05B2.
* **Choice**: `gunboat --sound adlib|speaker`; default adlib when the game folder has ADLIB.COM.

## 8. Open questions

* The sequencer (functions 1–5, 8–11h) and the command-line options are documented only as far as
  GB.EXE needs them.
* The PSP layout and the DOS version are the port's choice; nothing GB.EXE does depends on them.
