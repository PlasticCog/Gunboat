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

`quit_to_dos` (`0000:021e`, Ctrl+Q, the vacation choice, the setup's "quit" option): music off,
`mem_free_all`, free pages 1 and 2, restore the BIOS video mode `F39C`, clear the text screen,
sound off, `kbd_restore`, `exit(0)`. `fatal_exit(code)` (`0000:0276`) does the same and prints
message `code` first: 1 "Insufficient memory for GUNBOAT.", 2 "Important file open failed in
GUNBOAT.", 3 "Roster update failed. Is your disk full?" (the others as TD3).

`archive_open` (`0000:0d74`): name hash (`name_hash`, ORIGINAL_WORLD_FORMAT.md) → DATAC record
→ opens `dataa.dat` / `datab.dat` (`DS:0066` with the bank letter), prompting "Insert Disk %c into
Drive A" and retrying on failure (**PORT:** fail with the platform error instead), seeks to the
record offset. `file_load_near` / `file_load_far` read a whole entry.

## 2. Start-up: `config_load` (0000:02fe) **verified**

```
F39C = current BIOS video mode (1469:0008, for restoring on exit)
default choice = DS:0092[gfx_detect()]; gfx_set_mode(F39C)
if "GUNBOAT.CFG" opens: read 3 words: EED2 (video mode), F394 (joystick), DS:0078
else (text mode, strings DS:012F..0219): list the modes, digits 1–6 choose (6 = quit_to_dos),
     Enter accepts; "Do you want to use a joystick?" defaults to Y unless the joystick probe
     (146a:000b, port 201h, 11 tries) times out; Y/N, Enter accepts → F394; EED2 = DS:008C[choice]
     (the file is not written; SETUP.EXE writes it)
if F394: joystick_calibrate (146e:000e)
EED2 == 0Ch: DS:0076 = 1, CGA mode 4 with 121b:0902; else DS:0076 = 0, gfx_set_mode(EED2)
page 1 allocated (failure → fatal 1); D9B8 = page 1 segment; D9B6 = page 0 segment
VGA (13h): DS:0078 = 1, DS:0074 = 0 (the view page is 0); otherwise DS:0074 = 2 and page 2 is
     allocated (D9BA)
```

The shipped `GUNBOAT.CFG` is `13 00 00 00 00 00`: VGA, no joystick. **PORT:** VGA only; the
joystick maps to an SDL gamepad (platform spec).

## 3. Title and main menu (`title_menu`, 00f2:000e) **verified** (flow), **likely** (menu keys)

Front-end text and data: `DAT6.DAT` is loaded at `DS:6E54` (§7.1). Pictures are LZ files decoded
with `08e1:01bd` into `DS:1094` and drawn with `1390:0000` (VGA `121b:08a8`); `00f2:0f24` sets
EGA palette entries (parked).

```
F110 = 0 (practice mode); D9BC = 0
file_load_near("DAT6.DAT", DS:6E54)
if demo mode or first run (F398):
    palette TITLCOLR.BIN (DS:08C4)
    first run: COPY.LZ (the copyright/credits screen), wait_key(16Ch)
    ACCO.LZ (Accolade logo), dissolve (screen_present)
    VGA: TITLE1A/B/C and FOOTIT1E into far buffers; others: TITLE1A only
TITLE3C/B/A; first run only:
    three moving bitmaps animated for 118h steps of 3, one step per tick (random() called while
    waiting), wait_key(40h); TITLE1 composed with TIT1COLR.BIN, dissolved in
    music_start()                                        00f2:1044  (§3.1)
    TITLE2A..D with TIT2COLR.BIN, wait_key(64h)
credits text (00f2:0d3e, DS:6EB1: "Designed by Tom Loughry", graphics, producers, music,
     "COPYRIGHT 1990 ACCOLADE, INC.")
TITLE3A/B/C with TIT3COLR.BIN: the menu background; menu_cursor_init (020d:064a, PENCIL.MPP)
menu loop (00f2:0a17..0d2d): input_read_key; arrows/keypad move between
     "REPORT FOR DUTY" / "GUNNERY PRACTICE" / "GRENADE PRACTICE" / "PILOT PRACTICE" (DS:6FB1..);
     the key table 00f2:0AF0 sets or combines the practice bits of F110 (0..3)
     demo key: region 3, DS:0080 = 1, F110 = 1, mission 1, demo mode DS:0070 = 1, demo script
          counter DS:0C68 = 8 (simulation §3.1)
     practice: region 3, mission by F110
music_stop() (00f2:119c); return the choice (0 = report for duty, 1..3 = practice)
```

### 3.1 Music (`music_start` 00f2:1044, `music_stop` 00f2:119c) **verified** (calls)

By the sound device: `VALKPC.MUS` (PC speaker), `VALK12.MUS`, `VALK3V.MUS` ("Ride of the
Valkyries"), loaded through the AdLib/CMS driver interface (`1ace`, `1af5`), with
`timer_install` (89.63 Hz, simulation §1.1). `music_stop` stops the CMS driver and restores the
timer. Details: sound spec.

## 4. HQ and the copy-protection quiz (`hq_quiz`, 020d:0008) **verified**

```
QUIZCOLR.BIN palette; HQ1/HQ2/HQ3.LZ drawn (the HQ office)
repeat: r = pit_random(random()); ship = r & 0Fh (−11 if > 10); question = (r & F0h) >> 4
        (−4 if ≥ 12); until the answer table DS:7C2D[question·37h + ship·5] is not FCh
print the question (DS:6FF4[…] + DS:6E54) and the ship class ("of River Patrol Boats (PBR)?" …)
HQARM.LZ (an arm/hand animation); the player types a number (input_read_key, blinking cursor)
right: "Right. Welcome to Headquarters." (DS:6E5F); disk-2 check; roster_load(); return 0
wrong: "WRONG! Go practice gunnery." (DS:6E81); return 1 (main starts gunnery practice)
```

**PORT:** skip the quiz and take the "right" branch (as the TD3 port does with its protection).
`B7F3 == 1` in the input loop re-asks; `B7F3` is also changed by `mission_setup` (−6) and at
mission end (+14h): a counter still to identify.

## 5. Roster (`GBROSTER.DAT`) **verified**

`roster_load` (`020d:0b28`): opens `GBROSTER.DAT`; accepted only if exactly **703 bytes** (a 704th
byte must not exist) and `byte[702] == (XOR of bytes 0..701) ^ 5Bh`; then copied to
**DS:B54E..B80C**. `roster_save` (`020d:0bdc`, "hi_write" in the TD3 match): recomputes the check
byte into `DS:B80C` and writes the 703 bytes (`wb+`); a short write is fatal 3.

| File offset | DS | Content |
|---|---|---|
| 0 | B54E | word: the last commander slot used (default 1) |
| 2 + 50k | B550 + 50k | 13 commander records, k = 0..12 |
| 652 | B7DA..B80B | live game settings saved with the roster (time compression `B7F1`, `B7F3`, `B7FC`, `B800..B80B`: fitted weapons, engines, engine state, fuel) |
| 702 | B80C | check byte |

Commander record (50 bytes); the working copy is `DS:B4F9..B52A` (same layout):

| Offset | Working copy | Content |
|---|---|---|
| 0..19 | B4F9 | Name, space-padded (matched on 17 characters) |
| 20 | B50D | Rank 1..13 (§7.3) |
| 21..22 | B50E/B50F | Medals earned (bits, §7.4) |
| 23..24 | B510 | Missions completed (BCD) |
| 25..48 | B512..B529 | 12 statistics (BCD words, §8) |
| 49 | B52A | (copy of score word 0; not used) |

The executable's default roster has one commander, "ACCOLADE".

## 6. Front end (`front_end`, 02d2:0008) **verified**

Set-up (every call): sub-buffers `F5CC = F5C6 + 125Ch`, `F63C = F280 + 10000`;
`DAT5.DAT` → DS:6E54 (identical to DAT6, §7.1); GENCOLR.BIN; GENB/GENC, FOLDER, SPEC1..6,
ADHEAD, SMALL, INSIG pictures; the office (`02d2:2c9e`, GENA); "I'll be with you in a moment.";
the Mare Island map MP5A/B; `0AB2 = B508 = B4DB = B50C = B50B = 7EF9 = 0`; `F26C = 1`.

Then a state machine on **DS:0084** (table `02d2:0718`) runs until `F26C` is cleared; the
mission is then played by `main` and `front_end` is called again, continuing at state 12.

| State | Handler | Screen and transitions |
|---|---|---|
| 0 | `0AAE = 1`; `02d2:076c`; `name_entry` 02d2:0bd8 | "Identify yourself, sailor:" (§6.1) → 1 new commander, 2 F1, 4 known commander |
| 1 | `roster_edit` 02d2:106c | "PBR COMMANDERS": # ADD # REPLACE # REDO; a new record by `02d2:123e` |
| 2, 3 | `personnel_files(0 / 1)` 02d2:1306 | "PERSONNEL FILE": rank, name, medals (`02d2:13cc` per commander); F1 returns |
| 4 | inline 02d2:03a6 | Region brief and conditions (§6.2) → 5 |
| 5 | `mission_select` 02d2:1b6e | §6.3; F3 → 6, → 7, Enter → 9 (or vacation) |
| 6, 8 | `assignment_map(5 / 9)` 02d2:1ec2 | Sector map with the objective (`02d2:209a`), returns to state 5 / 9 |
| 7, 10 | `pbr_specs(5 / 9)` 02d2:1712 | "PBR EQUIPMENT" spec sheets (`02d2:1826`), returns to 5 / 9 |
| 9 | `outfitting` 02d2:22aa | §6.4; F2 → 8, F4 → 10, done → 11 |
| 11 | inline 02d2:0616 | `02d2:2df4`; "Take the sector map and the assignment and be on your way. Watch carefully for ambushes…" → state 12, `F26C = 0` (play) |
| 12 | inline 02d2:06e0 | After the mission: `debrief` (§6.5), `roster_update`, `roster_save` → 0 |

### 6.1 Name entry (`name_entry`, 02d2:0bd8) **verified**

Up to 17 characters into `DS:F626` (letters, digits, space; Backspace; Enter with a name);
F1 → state 2. The name is compared with the 13 records (17 characters):

* found: rank and record bytes 21..48 → working copy; `B54C` = slot; "Welcome back! We need you:"
  with the rank title (`DS:7B1C + 10·rank`) and name. **Region**: rank < 5: Vietnam (no choice);
  5..8: "# Vietnam # Colombia"; ≥ 9: "# Vietnam # Colombia # Panama" (menu `020d:0824`) →
  `B503`; → state 4.
* not found: working copy cleared, rank 1; "Welcome to Vietnam. I've been waiting for you. We need
  you for some secret riverine missions." → state 1 (put the new commander on the roster).
* **Easter egg:** the name "TJL" (`DS:0B0B`, Tom Loughry's initials) prints "Hi Tom! Enjoy your
  game!" and sets **B803 = 1**: every enemy object becomes a "sleezy lawyer" (world §5).

### 6.2 Region brief and conditions (state 4) **verified**

```
region title and brief (DS:6E54 + word B311[region], B317[region]), e.g. "The Viet Cong are tricky…"
sea state B4FF = random() & 3: "The water surface is calm, and" / "a bit choppy" / "very choppy" /
     "rough" (DS:B4A0[sea]); it sets the wave bob (simulation camera_pitch_bob)
missions offered B501 = DS:7BB4[rank] (2, 4, 6, 8 for ranks 1–4, 5–8, 9–12; 8 for 13); the
     digit in "I have   missions for you" = DS:7BA7[rank]
     in Vietnam with rank ≥ 5, or Colombia with rank ≥ 9: all 8 ('8')
selected mission B505 = B501 − 1 (the newest)
load DATn.DAT → DS:B84C and the region's MPnA/B maps (for the assignment map)
```

Mission 0 of the list is not a mission (§6.3). So a new captain starts with mission 1 of Vietnam
only; each rank unlocks two more missions (§7.3).

### 6.3 Mission select (`mission_select`, 02d2:1b6e) **verified**

Arrows cycle `B505` over 0 .. `B501 − 1` (wrapping), redrawing the folder (`02d2:1d04`, which
also sets the day flag for the preview). Enter: mission 0 prints "HAVE A NICE VACATION" and calls
`quit_to_dos`; otherwise `B50C = 1` and **mission type `B509 = 8·region + mission`** → state 9. F3
→ state 6 (assignment map), the spec key → state 7.

### 6.4 Outfitting (`outfitting`, 02d2:22aa) **verified**

First visit (`B4DB == 0`): bow 0, stern 0, midship 1, engines 1. Four choices in turn
(`B4DA` = 0..3 → `B804` bow, `B805` engines, `B806` stern, `B807` midship; world §4 for the art and
simulation §6.1 for the weapons): the cursor moves in 8-pixel rows between `DS:AD97[i]` and
`DS:AD9B[i]`; Enter stores `(row − top) >> 3`. After the fourth → state 11. F2 → state 8
(assignment map), F4 → state 10 (specs).

### 6.5 Debrief (`debrief`, 02d2:273e) and roster update (`roster_update`, 02d2:2a6c) **verified**

```
debrief()
  objective_progress B544 = min(B544, 5); if hours B52E == 0: B52E = 1
  if friendly_hits B546 > 5: B545 = 0; B544 = 0
  0AAE = max(B544 − 1, 0)
  B545 == 0: "You shot my men!" (DS:AFE0); medals B50E = B50F = 0; rank B50D = 1   (demotion)
  B545 == 1: "You got killed." (DS:AF2E)
  else: result text DS:B472[B544]: "…a failure." / "…n't completed." ×2 / "…completed." /
        "…well done." / "…perfectly"
        if B544 >= 3 (success):
           missions completed B510 = bcd_add(B510, 1)
           if rank < DS:B2C3[type]: rank = that value; promotion screen (insignia, rank title
              DS:B456[rank], "Press ENTER to continue")
           m = DS:B293[type]; if m and not yet earned: medals |= m; medal screen (picture by
              DS:AD7F[8·region + mission])
roster_update()
  "Press ENTER…"; 02d2:1598 (stats page); for k in 0..11: stats[k] (B512+2k) = bcd_add(stats[k],
  score word k (B52A+2k)); roster record B54C ← working copy bytes 20..49
```

`bcd_add` (`02d2:2b72`, and `bcd_inc` `02d2:2b4a` = `bcd_add(x, 1)`) are 16-bit BCD additions
(`02d2:2bae`/`2bea` are helpers).

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

## 8. Mission score words and career statistics **verified** (mapping), **likely** (labels)

`mission_run` clears the 12 score words `B52A..B541`; the mission adds to them; `roster_update`
adds them to the commander's statistics `B512..B529` in the same order.

| Word | Score | Set by |
|---|---|---|
| 0 B52A | PBR lost | `boat_destroyed` (1) |
| 1 B52C | hurt or killed | +1 on the mission-ending messages (simulation §9.1) and Tab with a crippled boat |
| 2 B52E | hours of duty | +1 per game hour (simulation §9.2); at least 1 in the debrief |
| 3..11 B530..B540 | targets destroyed | `score_add(DS:5401[kind])`: value v adds to word 2+v |

Per-kind values (`DS:5401`, world §6.3), region 0: tanks/APC 2 (B532), boats 04–06 4 (B536),
guns/mortars/forts 07–09 3 (B534), infantry 0A–0C and 14 1 (B530), caches/huts 0D–0F 3 (B534),
dock 10 8 (B53E), bridge base 11 9 (B540), missile 12 6 (B53A), mine 13 7 (B53C). The personnel
file shows these as men, armored vehicles, boats, docks, mines, aircraft, missiles, bridges and
key targets; the exact label of each word is to be checked on the personnel screen (hud spec).

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
| 00f2:1044 | music_start | 3.1 |
| 00f2:119c | music_stop | 3.1 |
| 020d:0008 | hq_quiz | 4 |
| 020d:064a | menu_cursor_init | 3 |
| 020d:0824 | choice_menu | 6.1 |
| 020d:0b28 | roster_load | 5 |
| 020d:0bdc | roster_save | 5 |
| 02d2:0008 | front_end | 6 |
| 02d2:0bd8 | name_entry | 6.1 |
| 02d2:106c | roster_edit | 6 |
| 02d2:123e | roster_new_record | 6 |
| 02d2:1306 | personnel_files | 6 |
| 02d2:13cc | personnel_file_show | 6 |
| 02d2:1712 | pbr_specs | 6 |
| 02d2:1b6e | mission_select | 6.3 |
| 02d2:1d04 | mission_folder_draw | 6.3 |
| 02d2:1ec2 | assignment_map | 6 |
| 02d2:22aa | outfitting | 6.4 |
| 02d2:24ac | outfitting_draw | 6.4 |
| 02d2:273e | debrief | 6.5 |
| 02d2:2a6c | roster_update | 6.5 |
| 02d2:2b4a | bcd_inc | 6.5 |
| 02d2:2b72 | bcd_add | 6.5 |
| 02d2:2c9e | office_draw | 6 |

## 10. Open questions

* The title menu's key table (`00f2:0AF0`) and which key starts the demo (the code compares with
  44h, 'D').
* `B7F3`, `0AAE`, `0AB0/0AB2/0AB4`, `D9BC`, `F398` beyond "first run", `B507`, `B508`.
* Exact labels of statistics words 3–11 (the personnel screen's print order).
* `02d2:1626` has no callers (dead code?).
