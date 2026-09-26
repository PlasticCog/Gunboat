# Game flow: start-up, title, HQ, roster, front end, debrief (GB.EXE `0000`, `00f2`, `020d`, `02d2`)

Target: `reverse_engineering/out/GB_unp.exe`, DGROUP `1B73`. Addresses as in `RE_GUIDE.md`.
Symbols: `spec/game_flow_symbols.csv`. Confidence tags as in `simulation.md`. Most routines here
are compiled C; `tools/fn.py NAME -k` lists each call with its constant arguments and strings,
which is the quickest way to read them. Ghidra's decompilation of the two state machines
(`mission_run`, `front_end`) is wrong: it follows their jump tables into other code.

The shared text routines (`print_records`, `print_text`, `print_chars`, `wait_key`,
`screen_present`) are the same code as Test Drive III's; their specification is in
`test-drive-3-sdl3/port/spec/game_flow.md` §4.6. Text records are `{col, row, text}` with a byte
≥ 80h ending the text: 80h = next record, AAh = end.

## 1. `main` (0000:0000) and the program phases **verified**

`DS:0082` is the phase: FFh configuration, 0 title, 1 HQ, 2 front end, 3 mission (the pause
message and some screens test it).

```
main()
  file_load_near("DATAC.DAT", DS:027C)                 archive directory (platform spec)
  phase = FFh; F398 = 1 (first run); config_load()       0000:02fe   §2
  phase = 0; kbd_install(); mem_alloc_all()
title:
  choice = title_menu()                                  00f2:000e   §3   (F13A)
  if choice == 0: goto campaign                          "REPORT FOR DUTY"
practice:                                                (also after a failed quiz, §4)
  F398 = 0; phase = 3; station = 2 (choice 1, gunnery), 4 (choice 2, grenade), else 1 (pilot)
  copy page 2
  until "DATAB.DAT" opens: print "Insert Disk 2 and press any key." (DS:7835), wait_key   PORT: skip
  mission_run(); copy page 1; 12ed:0063
  until "DATAA.DAT" opens: clear, print "Insert Disk 1 …" (DS:B4DC), wait_key             PORT: skip
  phase = 0; goto title
campaign:
  phase = 1; failed = hq_quiz()                          020d:0008   §4
  if failed: region 3, F110 = 1, mission 1, type 19h, no weapons (B804/B806/B807 = 0),
             upgraded engines B805 = 1, sea state B4FF = 1; goto practice (choice 1: bow)
  loop forever:                                          (quitting is "HAVE A NICE VACATION", §6.6)
     phase = 2; front_end()                              02d2:0008   §6
     phase = 3; station = 1; copy page 2; mission_run(); copy page 1
```

`DS:0072` (disk_prompt) is 1 while a disk prompt waits; `input_read_key` then reads the keyboard
even in the demo. The disk-2 prompt (`DS:7835`, colours 0Fh/4) does not clear; the disk-1 prompt
(`DS:B4DC`) first clears pixel rows 0..0Bh with colour 0. They appear only when `fopen` fails;
otherwise `wait_key(1)` returns at once (in the port the files are always there).

`quit_to_dos` (`0000:021e`, Ctrl+Q, the vacation choice, the setup's "quit" option): music off,
`mem_free_all`, free pages 1 and 2 (2 only when `DS:0078` = 0), restore the BIOS video mode
`F39C`, clear the text screen, sound off, `kbd_restore` unless in phase FFh, `exit(0)`.
`fatal_exit(code)` (`0000:0276`) does the same, but frees the pages only when code != 1, calls
`kbd_restore` unconditionally, and prints message `code` after the mode restore: 1
"Insufficient memory for GUNBOAT.", 2 "Important file open failed in GUNBOAT.", 3 "Roster update
failed. Is your disk full?".

`archive_open` (`0000:0d74`): name hash (`name_hash`, ORIGINAL_WORLD_FORMAT.md) → DATAC record
→ opens `dataa.dat` / `datab.dat` (`DS:0066` with the bank letter), prompting "Insert Disk %c into
Drive A" and retrying on failure (**PORT:** fail with the platform error instead), seeks to the
record offset. `file_load_near` / `file_load_far` read a whole entry.

## 2. Start-up: `config_load` (0000:02fe) **verified**

```
F39C = current BIOS video mode (1469:0008, for restoring on exit)
default choice = DS:0092[gfx_detect()]; gfx_set_mode(F39C)
if "GUNBOAT.CFG" opens: fread 3 words: EED2 (video mode), F394 (joystick), DS:0078
else (text mode, strings DS:012F..0219): list the modes, digits 1–6 choose (6 = quit_to_dos),
     Enter accepts; "Do you want to use a joystick?" defaults to Y, and N if any of 11 probes of
     axis 1 (146a:000b) returns FFFFh; Y/N, Enter accepts → F394; EED2 = DS:008C[choice]
     (the file is not written; SETUP.EXE writes it)
     DS:008C = 13 0D 04 0C 09 00 (VGA, EGA, CGA, Hercules, Tandy, exit);
     DS:0092[detect 0..13h] = 05 05 05 05 02 02 02 05 05 04 05 03 03 01 01 01 01 01 00 00
if F394: joystick_calibrate (146e:000e)
EED2 == 0Ch (Hercules): DS:0076 = 1 (byte), EED2 = 4, CGA mode 4, hercules_setup (121b:0902,
     the Hercules CRTC); else DS:0076 = 0, gfx_set_mode(EED2)
page 1 allocated (failure → fatal 1); D9B8 = page 1 segment; D9B6 = page 0 segment
VGA (13h): DS:0078 = 1, DS:0074 = 0 (the view page is 0); otherwise DS:0074 = 2 and page 2 is
     allocated (D9BA)
```

CGA (EED2 = 4, not Hercules) also sets `ega_pal_register(1, 0)` after the mode: the bright palette
1 on black (video.md §7.1). `DS:0076` (hercules_mode) is written here and read nowhere in GB.EXE
(no instruction reads DS:0076): the Hercules card shows the CGA picture because `hercules_setup`
programs its CRTC, nothing else of the game differs from CGA.

The shipped `GUNBOAT.CFG` is `13 00 00 00 00 00`: VGA, no joystick. **PORT:** the player's video
card chooses the mode (the launcher, `--video`); the joystick maps to an SDL gamepad (platform
spec); without the file the questions are answered with Enter (there is no text screen). The EGA,
CGA, Tandy and Hercules paths of `config_load` are tested on their machines
(`test_modes_game.test_config_load_cases_*`). `DS:F13A` (flow_scratch) is a scratch word: the default choice,
then the page allocation result.

## 3. Title and main menu (`title_menu`, 00f2:000e) **verified** (flow), **likely** (menu keys)

Front-end text and data: `DAT6.DAT` is loaded at `DS:6E54` (§7.1). Pictures are LZ files decoded
with `08e1:01bd` into `DS:1094` and drawn with `1390:0000` (VGA `121b:08a8`); `00f2:0f24` sets
EGA palette entries (parked).

```
engine_sound_on; pal_fade_out_vga; engine_sound_off; F110 = 0 (practice mode); D9BC = 0
file_load_near("DAT6.DAT", DS:6E54)
if demo (DS:0070 = 1) or first run (F398 = 1):                 the intro
    palette TITLCOLR.BIN (DS:08C4)
    first run: COPY.LZ (the copyright screen), faded in, wait_key(16Ch); demo: the screen black
    a demo sets F398 = 1 here, so it replays the whole first-run sequence
    ACCO.LZ (Accolade) on page 1, screen_present (the dissolve)
    VGA: TITLE1A/B/C/D preloaded into four far buffers; other modes FOOTIT1E and FOOTIT1F
else: TITLE3C/B/A preloaded, straight to the menu
first run only:
    one 16x4 sprite in three colour layers (0, 4, 0Ch in VGA), captured at (0, 41h) from the ACCO
    screen and redrawn at x = 0, 3, ..., 117h (94 positions), one per DS:08C0 tick; while it waits
    for a tick random() is called on every poll (00f2:0460 tests first, 03d0 calls random)
    wait_key(40h); TITLE1 with TIT1COLR.BIN, faded in (VGA: picture_draw_vga of the four parts)
    music_start (§3.1); DS:08C2 = 1; TITLE2A..D loaded, wait_key(64h), then drawn with
    TIT2COLR.BIN and faded in; text colours (0Fh, 0); TITLE3C/B/A loaded
    credits_text (00f2:0d3e, DS:6EB1): "Designed by Tom Loughry", graphics, producers, music,
         "COPYRIGHT 1990 ACCOLADE, INC." on text row 24, one group per 45 BIOS ticks, a key ends it
the menu: D9BC = 1 (transparent text); TITLE3 on page 1 with TIT3COLR.BIN (non-first runs start
    the effects timer here and do not fade out first); the right half TITLE3C with picture_draw;
    page 1 copied to the screen (gfx_copy_rect_from_copy_page); faded in; menu_cursor_init
    (020d:064a, PENCIL.MPP captured as colour masks); E9E0 = the pencil colour; F134 = 0 (the
    text colour; 2 in CGA)
menu loop (00f2:0a6c): a 2x2 grid "REPORT FOR DUTY" / "GUNNERY PRACTICE" / "GRENADE PRACTICE" /
    "PILOT PRACTICE" (DS:6FB1); bit 0 of F110 = column, bit 1 = row; the key table 00f2:0AF0 for
    91h..99h (keypad and joystick: 7 = 0, 8 up, 9 = 1, 4 left, 6 right, 1 = 2, 2 down, 3 = 3); a
    movement key is taken every second iteration (debounce); the selection blinks in 0Fh; one
    bios_wait_ticks(2) per iteration; ECB0 counts idle iterations
    Enter: B503 = 3; B505 = 1 gunnery, 2 grenade, else 0; B509 = 18h + B505; B804/B806/B807 = 0,
         B805 = 1; B4FF = 3 for pilot practice else 1
    'D', 'd', or 113h idle iterations (ECB0, ~30 s): the demo: as Enter plus DS:0080 = 1,
         F110 = 1, B505 = 1, DS:0070 = 1, demo script DS:0C68 = 8, 0C6A = 0C6B = 0
music_stop() if F398 = 1; engine_sound_off; D9BC = 0; return F110 & 7Fh (0 = report for duty,
    1..3 = practice)
```

### 3.1 Music (`music_start` 00f2:1044, `music_stop` 00f2:119c) **verified** (calls)

By the sound device (`sound_detect(0Fh, ...)`): 0 speaker -> `VALKPC.MUS`, 1 Tandy and 2 CMS ->
`VALK3V.MUS`, 4 AdLib and 8 MT-32 -> `VALK12.MUS` ("Ride of the Valkyries"). For AdLib, INT 65h
function 15h sets the instruments of voices 4 (DS:088A), 5-7 (DS:0856) and 8 (DS:0822).
`timer_install` (89.63 Hz) runs even when the sound is off (`DS:007E`), and `DS:08BE` = 1; the
file (`{u16 count; bytes}`) goes into the far buffer `F5BE` and `music_play` starts it looping.
`music_stop` acts only while `DS:08BE` = 1: `music_silence`, `timer_restore` (which also resets
the speaker music), `DS:08BE` = 0, and `engine_sound_on` puts the effects timer back.
Details: sound spec.

## 4. HQ and the copy-protection quiz (`hq_quiz`, 020d:0008) **verified**, **ported**

```
engine_sound_on; pal_fade_out_vga; engine_sound_off; tick-mark colour DS:09FE = 0
QUIZCOLR.BIN palette; HQ1/HQ2/HQ3.LZ drawn on page 1 at rows 34h, 69h, 9Dh; grey text area
     9Eh..C7h; text colours (0, 7); page 1 copied to the screen and faded in
repeat: r = pit_random(random())        (random() + the PIT channel-2 counter's low byte, IN 42h)
        question = r & 0Fh (-11 if >= 11)                 11 questions, texts DS:6E54 + w[6FF4 + 2q]
        class = (r & F0h) / 16 (-4 if >= 12)              12 ship classes, texts DS:6E54 + w[700A + 2c]
        answer = DS:7C2D + class*37h + question*5         5 bytes; FCh first = no answer: pick again
print the question (row 20) and the class (rows 21-22); text area saved to page 1
typing (DS:B7F3 = 1 while it lasts): '.', '0'..'9' (5 at most) into DS:F626 (20 spaces first),
     Backspace (the glyph stays drawn), Enter with at least one character; the cursor cell at
     column 16 + n of row 23 blinks in colours 6..9; one bios_wait_ticks(1) and one random() per
     iteration
check: for digit 1..5 (B7F3): expected = 0FFh - swap_nibbles(answer byte), compared with the typed
     character; after the loop B7F3 = 6
right: "Right. Welcome to Headquarters." (DS:6E5F); result 0
wrong: "WRONG! Go practice gunnery." (DS:6E81); HQARM.LZ (loaded before the typing) drawn at
     (70h, 79h); result 1 (main starts gunnery practice)
the disk-2 check: DATAB.DAT must open, else "Insert Disk 2 and press any key." (DS:7812)
roster_load(); wait_key(32h); return the result
```

**In this GB.EXE the answer check is disabled**: at `020d:0412` the compare is followed by `EB 08`
(an unconditional `jmp`) where the compiled check has `74 08` (`je`), so the "wrong" branch
(`count + 1`, `B7F3 += 0Eh`) is never taken and every answer is right. The port keeps that code
as it is, so no PORT deviation is needed. `B7F3` is the check loop's digit index (6 after a pass;
10h or more after a failure in a copy with the check); `mission_setup` subtracts 6 and the mission
end adds 14h.

## 5. Roster (`GBROSTER.DAT`) **verified**, **ported**

`roster_load` (`020d:0b28`): opens `GBROSTER.DAT` ("rb"); reads 703 bytes into DS:1094, then a
704th read must return 0 (the file is exactly 703 bytes); accepted if `byte[702] == (XOR of bytes
0..701) ^ 5Bh`, then copied to **DS:B54E..B80C**; otherwise the EXE's default roster stays.
`roster_save` (`020d:0bdc`, "hi_write" in the TD3 match): opens "wb+" (the result is **not**
checked), recomputes the check byte into `DS:B80C`, writes the 703 bytes; `fwrite` < 703 is fatal
error 3 ("Roster update failed. Is your disk full?").

| File offset | DS | Content |
|---|---|---|
| 0 | B54E | word: the **number of commanders** (1..13; the roster screen appends at this index) |
| 2 + 50k | B550 + 50k | 13 commander records, k = 0..12 |
| 652 | B7DA..B80B | live game settings saved with the roster (time compression `B7F1`, `B7F3`, `B7FC`, `B800..B80B`: fitted weapons, engines, engine state, fuel) |
| 702 | B80C | check byte |

The current commander's slot is `DS:B54C`, just before the saved range (not saved).

Commander record (50 bytes); the working copy is `DS:B4F9..B52A` (only B50D..B52A are used):

| Offset | Working copy | Content |
|---|---|---|
| 0..16 | | Name, space-padded (17 characters compared) |
| 17..19 | | never written by the front end (old bytes stay) |
| 20 | B50D | Rank 1..13 (§7.3) |
| 21..22 | B50E/B50F | Medals earned (bits, §7.4) |
| 23..24 | B510 | Missions completed (BCD) |
| 25..48 | B512..B529 | 12 statistics (BCD words, §8) |
| 49 | B52A | the low byte of score word 0 (copied back, not used) |

`roster_new_record` clears record bytes 21..49; `roster_update` copies working bytes 20..49 into
the record. The executable's default roster: count 1, "ACCOLADE", rank 5, medals 0003h, missions
0007, statistics 0001 0001 0008 0033 0009 0016 0037 0008 0011 0005 0001 0002.

## 6. Front end (`front_end`, 02d2:0008) **verified**, **ported**

Set-up (every call): sub-buffers `F5CC = F5C6 + 125Ch` (offset only), `F63C = F280 + 2710h`;
fade out; `DAT5.DAT` -> DS:6E54 (identical to DAT6, §7.1); `0AB2 = 0`, `F26C = 1`, `B508 = B4DB =
B50C = B50B = 0`, `7EF9 = 0`; GENCOLR.BIN; GENB, GENC; the office (`office_draw` 02d2:2c9e: GENA,
GENB, GENC on page 1, the speech area cleared, shown and faded in); "I'll be with you in a
moment."; FOLDER, SPEC1..3; ADHEAD.LZ drawn at (F0h, 43h) on the screen while SPEC4..6 and SMALL
load, then removed; INSIG.LZ drawn on page 1 at (0, 27h) as the **sprite sheet** (rows 0..0Fh rank
insignia, 16 px each; 10h..1Fh medals; 20h..27h and C0h..EFh the office animation frames; only
page 1 rows 28h..C7h are ever shown); MP5A/B (Mare Island, run counts DS:B440/B442); `0AB0 = 0`;
the key flushed; the effects timer stays on.

Then a state machine on **DS:0084** (jump table `02d2:0718`; a state above 12 does nothing) runs
until `F26C` is cleared; **after every state the officer's face is reset** (page 1 (0..1Fh,
20h..23h) to the screen at (100h, 24h)). The mission is then played by `main`, and `front_end` is
called again, continuing at state 12.

| State | Handler | Screen and transitions |
|---|---|---|
| 0 | `0AAE = 1`; `office_face_draw`; `name_entry` | "Identify yourself, sailor:" (§6.1) -> 1 new commander, 2 F1, 4 known commander |
| 1 | `roster_edit` 02d2:106c | "PBR COMMANDERS" folder "4D76": # ADD # REPLACE # REDO (§6.6) -> 4, 0 (REDO), 3 (F1) |
| 2, 3 | `personnel_files(0 / 1)` 02d2:1306 | "PERSONNEL FILE" folder "4E7n" (§6.6); F1 returns to 0 / 1 |
| 4 | inline 02d2:03a6 | Region brief and conditions (§6.2) -> 5 |
| 5 | `mission_select` 02d2:1b6e | §6.3: **F2 -> 6** (map), **F4 -> 7** (specs), Enter -> 9 (or the vacation) |
| 6, 8 | `assignment_map(5 / 9)` 02d2:1ec2 | The sector map (§6.7): **F3** returns to 5 / 9, F4 -> 7 / 10 |
| 7, 10 | `pbr_specs(5 / 9)` 02d2:1712 | "PBR EQUIPMENT" sheets (§6.7): **F3** returns to 5 / 9, F2 -> 6 / 8 |
| 9 | `outfitting` 02d2:22aa | §6.4; F2 -> 8, F4 -> 10, done -> 11 |
| 11 | inline 02d2:0616 | `office_restore`; if the mood `0AAE` is 0: "It's about time you decided! I have a lot to do, so get out of here before I have you keel-hauled."; then "Take the sector map and the assignment and be on your way. Watch carefully for ambushes..." -> state 12, `F26C = 0` (play) |
| 12 | inline 02d2:06e0 | After the mission: `debrief` (§6.5), `roster_update`, effects off, 4 BIOS ticks, `roster_save`, state 0, 4 ticks, effects on |

**The office** (screen rows 1Ch..33h; they overlap only the office picture): `office_idle`
(02d2:07d2) runs once per iteration of every front-end input loop: `r = random()`; two background
figures get a random 2-row frame (hold `0AB4`); the officer is reset to neutral and, by bits of
r, blinks (hold `0AB2` = 1) or shows another expression (hold 8); **every 32nd blink on an even r
lowers the mood `0AAE`** and redraws the face. `office_face_draw` (02d2:076c) draws face frame
`0AAE & 7` unless a folder is shown (`F27E`). The mood is 1 at state 0, 2 for a known commander,
the objective progress - 1 after a mission; at 0 the officer loses patience (state 11).
`speech_clear` (02d2:0994): rows 90h..BFh grey, C0h..C7h black. `folder_draw(c1, c2)`
(02d2:09da): FOLDER.LZ on page 1 and the folder code "4<c1>7<c2>" (DS:F13C) at row 7, column
31; `folder_present` (02d2:0bac) shows page 1 rows 28h..C7h and sets `F27E`; `office_restore`
(02d2:2df4) redraws GENB/GENC and the face. `wait_key_idle(n)` (02d2:2c52) is `wait_key` with
`office_idle`.

**The pencil menu** `choice_menu(timeout, items, deltas, y0, y1, key_fe, count, key_fd)`
(020d:0824): the pencil (`menu_cursor_move`/`menu_cursor_draw`, 020d:09a0/09cc) over {x, y} word
items; keys 91h..99h move by the signed deltas (95h does nothing); Enter draws the tick mark
(`menu_tick_mark` 020d:0a56: 9 pixels from DS:6E9F on page 1, one per BIOS tick, then
`wait_key(9)`) and returns the index; `key_fe`/`key_fd` return FEh/FDh. The region menu (items
DS:7ED3) starts on the **last** item. **DS:007C (`key_delay`) = 2 after a move** blocks moves and
the two keys (not Enter) for two iterations; nothing else counts it down, and `pbr_specs`,
`mission_select` and `assignment_map` ignore every key except (in mission_select) Enter while it
is non-zero: **Enter within one iteration of a menu start or a move leaves it set and locks those
screens** (a quirk the port keeps). The time-out never fires (the idle count is always 0 when
tested; every caller passes 0).

### 6.1 Name entry (`name_entry`, 02d2:0bd8) **verified**

Up to **17** characters into `DS:F626`: MSC `isalnum` (the `_ctype` table DS:E21C) or a space (not
first), lower case converted to upper; Backspace (the glyph stays drawn); Enter with a name; F1
(81h) -> state 2. The cursor cell blinks in colours 8..11. The name is compared with all 13
records (17 characters; `B50D` serves as the match flag):

* found: rank and record bytes 21..48 -> working copy B50E..B529; `B54C` = slot; mood 2; "Welcome
  back! We need you:" with the rank title (`DS:7B1C + 10*rank`) and name. `B503 = 0` (Vietnam);
  rank < 5: `wait_key_idle(3Ch)`; 5..8: "What region are you serving now?" "# Vietnam # Colombia";
  9 and up: "# Vietnam # Colombia # Panama" (`choice_menu`, starting on the last) -> `B503`;
  -> state 4.
* not found: B50E..B529 cleared, rank 1; "Welcome to Vietnam. I've been waiting for you. We need
  you for some secret riverine missions. How should I update my roster?" -> state 1.
* `B803 = 0` for every name. **Easter egg:** the name "TJL" (`DS:0B0B`, Tom Loughry's initials,
  compared with `strncmp` over 17 characters) prints "Hi Tom! Enjoy your game!" and sets
  **B803 = 1**: every enemy object becomes a "sleezy lawyer" (world §5).

### 6.2 Region brief and conditions (state 4) **verified**

```
folder "4S7n" (n = region + 1): the region title in red and the brief (DS:6E54 + w[B311 + 2*region]),
     "Press ENTER to continue"; a key
office_restore; sea state B4FF = random() & 3: "The water surface is calm, and" / "a bit choppy" /
     "very choppy" / "rough" (absolute pointers DS:B4A0[sea]); it sets the wave bob (simulation
     camera_pitch_bob)
missions offered B501 = DS:7BB4[rank] (2, 4, 6, 8 for ranks 1-4, 5-8, 9-12; 8 for 13); the
     digit in "I have   missions for you" = DS:7BA7[rank], written into the loaded text (DS:769C)
     in Vietnam with rank >= 5, or Colombia with rank >= 9: all 8 ('8')
the orders (DS:6E54 + w[B317 + 2*region]) and the sea text; selected mission B505 = B501 - 1 (the
     newest); the text area saved to page 1
effects off; DATn.DAT -> DS:B84C (mission_record); MPnA -> F5CC, run count EED0 = w[B444 + 4*region];
     MPnB -> F61A, F100 = w[B446 + 4*region]; effects on; a key -> state 5
```

Mission 0 of the list is not a mission (§6.3). So a new captain starts with mission 1 of Vietnam
only; each rank unlocks two more missions (§7.3).

### 6.3 Mission select (`mission_select`, 02d2:1b6e) **verified**

The folder "4A7n" (`mission_folder_draw` 02d2:1d04): the briefing (DS:6E54 + w[B2DB / B2EB /
B2FB + 2*mission] by region); the byte after its text is the **day flag `B7FC`** (DAY/NIGHT
MISSION in red); mission 1 adds "You are invulnerable for this PRACTICE mission."; the medal the
mission can earn (DS:AD7F[8*region + mission], from the sprite sheet); the pencil at "# Accept"
until one is accepted (then the mark '\', a crossed box). Arrows 91h/92h/93h/96h next,
94h/97h/98h/99h previous, wrapping over 0 .. `B501 - 1`. Enter: the tick mark; mission 0 prints
"HAVE A NICE VACATION", waits `wait_key_idle(64h)` and calls `quit_to_dos`; otherwise `B50C = 1`
and **mission type `B509 = 8*region + mission`**; the next iteration goes to state 9 (arrows can
still change the mission until then). F2 -> state 6, F4 -> state 7.

### 6.4 Outfitting (`outfitting`, 02d2:22aa) **verified**

First visit (`B4DB == 0`): bow 0, stern 0, item 0, midship 1, engines 1. The folder "4I7n"
(`outfitting_draw` 02d2:24ac): SMALL.LZ (the boat), DAY/NIGHT, "PBR OUTFIT" and the lines; the
choices made so far are marked with '\' at column 13, row 11 + 3i + choice. Four choices in turn
(`B4DA` = 0..3 -> `B804` bow: M2HB guns / Minigun, `B805` engines: 215HP / 450HP, `B806` stern:
M129 / M60D, `B807` midship: mortar / M2HB / M60D; world §4 for the art and simulation §6.1 for the
weapons): only **92h/96h move up and 94h/98h down**, 8 pixels, between `DS:AD97[i]` (5Bh 73h 8Bh
A3h) and `DS:AD9B[i]` (63h 7Bh 93h B3h), with a 2-iteration debounce (also for Enter); the pencil
starts on the second line for items 1 and 3. Enter stores `(y - top) >> 3` and draws the tick
mark. After the fourth -> state 11. F2 -> state 8 (assignment map), F4 -> state 10 (specs).

### 6.5 Debrief (`debrief`, 02d2:273e) and roster update (`roster_update`, 02d2:2a6c) **verified**

```
debrief()
  office_restore; "Press ENTER to continue" and "Your mission was"
  objective_progress B544 = min(B544, 5); if hours B52E == 0: B52E = bcd_add(B52E, 1)
  if friendly_hits B546 > 5: B545 = 0; B544 = 0
  mood 0AAE = B544 - 1 (0 if B544 = 0); office_face_draw
  B545 == 0: "You shot my men! YOU WILL BE COURT-MARTIALED!" (DS:AFE0); medals B50E = B50F = 0;
             rank B50D = 1 (demotion)
  B545 == 1: "You got killed." (DS:AF2E)
  else: result text by absolute pointer DS:B472[B544]: "...a failure." / "...n't completed." x2 /
        "...completed. They never knew what hit them." / "...well done. You almost wiped them all
        out." / "...perfectly done!"
        if B544 >= 3 (success):
           missions completed B510 = bcd_add(B510, 1)
           if DS:B2C3[type] > rank: rank = it; a key; the promotion screen (the insignia from the
              sprite sheet at (F0h, AFh), "CONGRATULATIONS! You are now a" + DS:B456[rank])
           m = DS:B293[type]; if m and not yet earned: medals |= m; a key; the medal screen (icon
              DS:AD7F[8*region + mission], "Your outstanding heroism earned you a medal!")
  a key
roster_update()
  "Press ENTER..."; "You lost ... PBRs in exchange for ... men ... armored vehicles ..." with the
  mission's scores (bcd_stats_print, list DS:B4C4); for k in 0..11: career stat k (B512+2k) =
  bcd_add(stat k, score word k (B52A+2k)); roster record B54C <- working copy bytes 20..49; a key
```

BCD helpers: `bcd_to_bin` (02d2:2bae, 4 digits, nibbles not checked), `bin_to_bcd` (02d2:2bea,
unsigned divisions), `bcd_add` (02d2:2b72: the binary sum wraps at 16 bits, then saturates at
9999), `bcd_inc` (02d2:2b4a: + 1, saturating at 9999).

### 6.6 Roster screen and personnel files **verified**

`roster_edit` (02d2:106c): "PBR COMMANDERS" and the list (rank title and name at rows 11..23);
the pencil menu ADD / REPLACE / REDO (items DS:7BED; F1 -> state 3). ADD appends at `B54E`
(`B54E + 1`), or overwrites the last slot when 13 exist. REPLACE (`B50B = 1`, marked '\') lists the
13 slots (items DS:7BF9, vertical deltas DS:7ECA); a slot beyond the end appends; F1 -> state 3.
`B50B` is reset only by the front end's set-up: after F1 and back the list comes at once.
`roster_new_record(slot)` (02d2:123e) writes the typed name and the rank, prints them, clears
record bytes 21..49 and waits `wait_key_idle(1Eh)`; it calls `gfx_set_colour(0, 7)` where
`text_set_colours(0, 7)` was meant (only the graphics colour changes). -> state 4.

`personnel_files(ret)` (02d2:1306) starts at the file last viewed (`DS:7EF9`, inside the DAT5
buffer); 91h/92h/93h/96h next, 94h/97h/98h/99h previous (wrapping over `B54E`), F1 -> `ret`.
`personnel_file_show(slot)` (02d2:13cc): rank title and name, the statistics
(`bcd_stats_print` 02d2:1598, list DS:B4A8 with cells DS:7EDF, 4 digits with leading zeros
blanked), the insignia and one 16-pixel medal icon per set bit (the loop shifts the medals word
arithmetically: with bit 15 set it would never end). **Viewing a file copies that commander's
record bytes 21..48 into the working copy B50E..B529 and the rank into B507**: a commander added
afterwards inherits the viewed medals and statistics (kept). `byte_stats_print` (02d2:1626) has
no callers.

### 6.7 Assignment map and spec sheets **verified**

`assignment_map(ret)` (02d2:1ec2) and `map_draw` (02d2:209a): folder "4B7n"; mission 0 shows Mare
Island (MP5A/B), others the region's map (MPnA/B); `EA84` = the mission drawn: between two real
missions page 1 is only shown again (the folder code keeps the first digit). With a mission, the
objective (DS:B3C0[8*region + mission] (x, y)) blinks with the pencil masks and the boat's start
(object 0 of the mission file: x = `C12D >> 6` + 18h, y from `C8FD >> 7`) is marked. Arrows change
the mission while none is accepted; F3 returns, F4 -> specs.

`pbr_specs(ret)` (02d2:1712) and `spec_sheet_draw` (02d2:1826): folder "4C7n" (n = page / 2 + 1),
pages 0..16 in `B508` (13 and 15 skipped), arrows as above; text pages 1, 3, 5, 7, 9, 10, 12, 14,
16 (absolute pointers DS:B47E: "PATROL BOAT, RIVER", Propulsion, Radar, M2HB, GE Minigun, M60D,
M129, M224 Mortar, "SPECIAL BOAT UNIT STANDING ORDERS"); picture pages 0, 2, 4, 6, 8, 11 = SPEC1..6
(run counts in `F284`) with their EGA/Tandy colours (`D9CB..D9CE`; `ega_pal_entry` does nothing
in VGA). F3 returns, F2 -> map.

## 7. Data

### 7.1 `DAT5.DAT` / `DAT6.DAT` (17,921 bytes, identical; → DS:6E54) **verified**

One file, on both floppies: all front-end texts and some tables, addressed as `DS:6E54 + offset`
(file offset = DS address − 6E54h). Contents in order: HQ quiz replies, credits (6EB3..), the
main menu items (6FB1..), the quiz questions and ship classes (7024..), name entry and roster
texts (73B6..), region choice and sea-state texts (745F..), disk prompts (7814, 7837), final
orders (7885..), personnel file texts (796F..), roster texts (7AAF..), **rank titles** (7B1C,
10 characters each), mission-offer digits (7BA7) and counts (7BB4), the region briefs
(7EFD.. "VIETNAM Region Brief: …"), and every **mission briefing** with its title, text and
"Promotion rank:" line (e.g. "Rescue POWs", "Raid Charlie Point", "Destroy Chemicals", "Crush the
Kingpins"), the "HAVE A NICE VACATION" line (9F3D) and the PBR equipment sheets (9FA7..). The
port reads them at run time; they are not transcribed here.

### 7.2 Mission numbering

`B509` (mission type) = `8·region + mission`: types 1–7 Vietnam, 9–15 Colombia, 17–23 Panama;
0, 8, 16 are the "vacation" slots; 19h is the gunnery practice after a failed quiz; the practice
missions use region 3. `B509` indexes the start-time table (world §5), the promotion ranks and the
medals below, and the passenger missions are types 2, 4 and 0Ah (simulation §5.2).

### 7.3 Ranks and promotion

| Rank | Title | | Rank | Title |
|---|---|---|---|---|
| 1, 2 | 3rd Petty (Officer) | | 7 | Jr. Lt. |
| 3 | 2nd Petty | | 8 | Lt. |
| 4 | 1st Petty | | 9, 10 | Lt. Comm. |
| 5, 6 | Ensign | | 11 | Commander |
| | | | 12 | Captain |
| | | | 13 | Admiral |

Promotion rank by mission type (`DS:B2C3`): Vietnam 1–7 → 2, 3, 3, 4, 4, 5, 5; Colombia 9–15 →
6, 7, 7, 8, 8, 9, 9; Panama 17–23 → 10, 11, 11, 12, 12, 13, 13. Region access: Colombia from rank
5, Panama from rank 9.

### 7.4 Medals

`DS:B293[type]` (word bit): type 5 → 1, 7 → 2, 13 → 4, 14 → 8, 15 → 10h, 19 → 20h, 20 → 40h,
21 → 80h, 22 → 100h, 23 → 200h. The personnel file draws one 16-pixel icon per set bit.

## 8. Mission score words and career statistics **verified**

`mission_run` clears the 12 score words `B52A..B541`; the mission adds to them; `roster_update`
adds them to the commander's statistics `B512..B529` in the same order.

| Word | Score | Set by |
|---|---|---|
| 0 B52A | hurt or killed | `boat_destroyed` (1) |
| 1 B52C | PBRs lost | +1 on the mission-ending messages (simulation §9.1) and Tab with a crippled boat |
| 2 B52E | hours of duty | +1 per game hour (simulation §9.2); at least 1 in the debrief |
| 3..11 B530..B540 | men, armored vehicles, key targets, boats, aircraft, missiles, mines, docks, bridges | `score_add(DS:5401[kind])`: value v adds to word 2+v |

Per-kind values (`DS:5401`, world §6.3), region 0: tanks/APC 2 (B532), boats 04–06 4 (B536),
guns/mortars/forts 07–09 3 (B534), infantry 0A–0C and 14 1 (B530), caches/huts 0D–0F 3 (B534),
dock 10 8 (B53E), bridge base 11 9 (B540), missile 12 6 (B53A), mine 13 7 (B53C). The labels are
those of the personnel file ("hurt or killed ... times" = career word 0 B512, "He lost ... PBRs" =
word 1 B514) and of the debrief ("You lost ... PBRs" = score word 1 B52C).

## 9. Function table

| Address | Name | § |
|---|---|---|
| 0000:0000 | main | 1 |
| 0000:021e | quit_to_dos | 1 |
| 0000:0276 | fatal_exit | 1 |
| 0000:02fe | config_load | 2 |
| 0000:0d74 | archive_open | 1 |
| 00f2:000e | title_menu | 3 |
| 00f2:0d3e | credits_text | 3 |
| 00f2:0e44 | print_chars | 6 |
| 00f2:1044 | music_start | 3.1 |
| 00f2:119c | music_stop | 3.1 |
| 020d:0008 | hq_quiz | 4 |
| 020d:064a | menu_cursor_init | 3 |
| 020d:0824 | choice_menu | 6 |
| 020d:09a0 | menu_cursor_move | 6 |
| 020d:09cc | menu_cursor_draw | 6 |
| 020d:0a56 | menu_tick_mark | 6 |
| 020d:0b28 | roster_load | 5 |
| 020d:0bdc | roster_save | 5 |
| 02d2:0008 | front_end | 6 |
| 02d2:076c | office_face_draw | 6 |
| 02d2:07d2 | office_idle | 6 |
| 02d2:0994 | speech_clear | 6 |
| 02d2:09da | folder_draw | 6 |
| 02d2:0bac | folder_present | 6 |
| 02d2:0bd8 | name_entry | 6.1 |
| 02d2:106c | roster_edit | 6.6 |
| 02d2:123e | roster_new_record | 6.6 |
| 02d2:1306 | personnel_files | 6.6 |
| 02d2:13cc | personnel_file_show | 6.6 |
| 02d2:1598 | bcd_stats_print | 6.6 (also `chase_view_screen` 05bd:07da) |
| 02d2:1626 | byte_stats_print | 6.6 (no callers) |
| 02d2:1712 | pbr_specs | 6.7 |
| 02d2:1826 | spec_sheet_draw | 6.7 |
| 02d2:1b6e | mission_select | 6.3 |
| 02d2:1d04 | mission_folder_draw | 6.3 |
| 02d2:1ec2 | assignment_map | 6.7 |
| 02d2:209a | map_draw | 6.7 |
| 02d2:22aa | outfitting | 6.4 |
| 02d2:24ac | outfitting_draw | 6.4 |
| 02d2:273e | debrief | 6.5 |
| 02d2:2a6c | roster_update | 6.5 |
| 02d2:2b4a | bcd_inc | 6.5 |
| 02d2:2b72 | bcd_add | 6.5 |
| 02d2:2bae | bcd_to_bin | 6.5 |
| 02d2:2bea | bin_to_bcd | 6.5 |
| 02d2:2c52 | wait_key_idle | 6 |
| 02d2:2c9e | office_draw | 6 |
| 02d2:2df4 | office_restore | 6 |

## 10. Open questions

* Whether the `EB 08` at `020d:0414` (the disabled quiz check) is a crack or the shipped code.
* A reader of `B7F3` outside the indexed code.
* `roster_save` after a failed `fopen` (fwrite on a null stream, runtime 15ee:0524); both of the
  port's runtime models return 0 items, so fatal error 3.
* `D9BC`, and `F398` beyond "first run".
