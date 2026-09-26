# Porting guide

The rules are in `/CLAUDE.md` ("Porting rules", "Verification"); this file is the working guide:
where code goes, the memory API, and how to port and test one original function. Adapted from
the Test Drive III port's `td3port/PORTING.md`.

## Architecture

```text
src/main.cpp            arguments, mem_load_exe, ADLIB.COM (--sound adlib), the machine set-up, then
                        game_main (main 0000:0000)
src/mem.hpp/.cpp        mem[]: GB.EXE at 1000:0000, DGROUP 2B73h, VGA A000h; accessors; exact division;
                        the EXEPACK loader
src/host.hpp/.cpp       SDL3, the only file that includes it: window, PIT timer, retrace, XT keys,
                        gamepad, OPL2 + speaker, game files, fatal errors
src/symbols.hpp         generated from reverse_engineering/symbols.csv (never edit)
src/platform/           platform.md and video.md: dos (DOS memory, files, the C runtime models),
                        bios (INT 10h / 1Ah model), gfx (graphics library, pictures), pal (palette,
                        RLE pictures, dissolve), text, lzw, kbd (INT 9), timer (INT 8, the host's
                        timer dispatch, BIOS waits), joystick, vga (DAC/CRTC model), helpers
src/game/               the game: flow_* (game_flow.md: main, files, keys, screens, title, music,
                        flow_hq the headquarters (020d), flow_office and flow_front the front end
                        (02d2); flow_util.hpp: idioms the flow code repeats), sim_* (simulation.md);
                        pending.cpp: placeholders for calls not ported yet
src/sound/              sound.md: effects and music; adlib_driver.cpp: the resident Ad Lib driver
                        ADLIB.COM (adlib_driver.md), not part of GB.EXE
src/render/             render3d.md: the 3D renderer (camera, terrain window and projection, terrain
                        primitives and VGA spans, visible-object list, sprite cache and blitter,
                        spotlights, flash); platform/gfx_display.cpp: gfx_set_display_offset
tests/difftest/         gbdiff.py (harness), dosmodel.py / biosmodel.py (the machine for the original),
                        bridge*.cpp (the core as a DLL), host_stub.cpp, test_*.py
legacy/                 the Codex prototype (reference only)
```

CMake builds `gbcore` (everything in `src/` except `main.cpp` and `host.cpp`, no SDL), then
`gunboat` (+ host + SDL3) and `gb_difftest` (+ bridge + stub host). A new `.cpp` under `src/`
is picked up automatically.

## Memory model (`src/mem.hpp`)

| Accessor | For |
| --- | --- |
| `ds_u8/s8/u16/s16/u32/s32(DS_x)` | DGROUP globals, as lvalues: `ds_u16(DS_x) += 2` |
| `seg_u8/s8/u16/s16(CSSEG_x, CS_x + i)` | tables and variables in code segments, by file segment; also code bytes read as data |
| `mem_u8/.../u32(seg, off)` | any real-mode address: heap blocks, VGA, the BIOS data area |
| `FarPtr`, `ds_far`, `ds_far_set`, `far_u8`, `far_add` | 16:16 pointers as stored (offset first); far, not huge, arithmetic |
| `div32_16`, `idiv32_16`, `div16_8`, `idiv16_8` | every DIV/IDIV; a divide error ends with R6003 as the runtime does |

Below GB.EXE, `mem[]` holds the resident Ad Lib driver when the machine has one (`--sound adlib`,
the default when the game folder has `ADLIB.COM`): the user's ADLIB.COM with its PSP at
`ADLIB_PSP` = 0B00h (CS; its data segment `ADLIB_DS` = 0D5Ch), where DOS could have loaded a TSR
run before the game. `adlib_load` copies the file there (never shipped) and `adlib_install` leaves
the state its installation leaves (spec `adlib_driver.md` §6–7): INT 65h and INT 8 hooked, the OPL2
set up. Its routines are C++ functions `adl_*` on that memory, like GB.EXE's.

All game state stays in `mem[]` at its original address. Names come from `symbols.hpp`
(`DS_`, `CS_`/`CSSEG_`, `FN_`); an unnamed address is written raw with a comment, and gets a name
in the spec's symbol file as soon as its meaning is known. A local of the original whose
address is passed to another function will need space on an emulated stack in DGROUP; add that
helper when the first such function is ported.

## Porting one function

1. **Spec.** Its pseudocode is in the subsystem spec. If the spec only summarises it, write the
   pseudocode from the disassembly first (`python reverse_engineering/tools/fn.py SSSS:OOOO -d`),
   as simulation.md §5.1 does for `boat_move`.
2. **Names.** Add missing names to `reverse_engineering/spec/<owner>_symbols.csv`, then
   `python reverse_engineering/tools/merge_symbols.py`, `symbols.py` and `gen_symbols.py`.
3. **Code.** One C++ function per original function, named as in `symbols.csv`, in the file of
   its subsystem, with a leading comment `// 0919:7ee8 boat_move (simulation.md §5.1): ...`.
   Assembly routines take the registers they read as parameters and return the registers their
   callers use (check the callers: `fn.py ADDR -x`, then read the code after each call). A
   helper that the original repeats inline may be a local function in an anonymous namespace.
   Mark deviations `// PORT: why` and doubts `// TODO(verify): what`.
4. **Bridge.** One line in `tests/difftest/bridge.cpp`: the symbol name, the registers in, the
   registers out.
5. **Test.** Add a function to a `tests/difftest/test_*.py` and to its `TESTS`: build memory
   (`h.fresh_memory()`, randomize what the function may read: the whole DGROUP is a good start),
   put edge values where the code branches, and call `h.check(name, m, regs=..., outputs=[...])`.
6. **Run** `python gunboat-port/tests/difftest/run_all.py -k name`, then all tests, then commit.

## The differential test harness (`tests/difftest/gbdiff.py`)

* **Same memory.** Each case starts from one 1 MB + 64 KB image: the Python loader's GB.EXE
  (`test_loader` checks it against the C++ loader), the DOS heap and the BIOS data area as at
  start-up (`h.fresh_memory()`), plus whatever the test sets. Both sides get a copy.
* **The original** runs in Unicorn, called as the game calls it: a far or near C function with its
  stack arguments, an assembly routine with registers, an interrupt handler with FLAGS/CS/IP (the
  convention is read from the code: RETF, RET or IRET). SS = DS = DGROUP, SP = FF00h; it runs
  until it returns to a sentinel address.
* **The machine models** (`dosmodel.py`, `biosmodel.py`) give the original the same machine the
  port has: INT 21h (files on the real game folder, memory on the MCB chain), INT 10h and INT 1Ah
  on the BIOS data area, a VGA DAC, the ports the code touches (3DAh reads "in retrace", 61h, the
  graphics registers). Anything else (`INT`, `IN`, `OUT`) stops the run with an error, so a
  function never silently depends on hardware. The C runtime functions the port replaces with
  models (`crt_fopen`, `crt_fmalloc`, ...) are replaced on the Unicorn side too, by
  `Original.stub()` running the Python mirror of the port's model.
* **Compared** after each call: every byte of memory except the original's stack below the call's
  SP; the listed return registers; every DAC write in order (a fade's steps, not only its end);
  the open DOS files and their positions; and whatever `h.extensions` add (the sound model's
  speaker events). A mismatch names each differing range with its symbol and the before /
  original / port bytes.
* **Time**: `tick=(poll_points, kind[, keys])`. The original gets one timer tick each time it
  executes a poll point (the instruction where it re-reads the tick counter or the BIOS clock),
  the port one per `host_pump()`. `kind` is `tick_counter` (DS:08C0 + 1), `bios` (the BIOS clock)
  or `both`; `keys` (a byte list) delivers one key per tick into isr_key_code. The game's own timer
  programming (`host_set_timer`) is recorded but does not drive the test. A key script must fit
  the screens it drives: a key that arrives during a timed wait (`wait_key(n)`, a tick mark) is
  taken by that wait (`test_front.py` spaces its keys by 80 ticks for that reason).
* **Hardware values and files**: `h.set_pit2(v)` sets what IN 42h reads on both sides (the quiz's
  question); `h.set_game_dirs(orig_dir, port_dir)` gives each side its own game folder, so a test
  that writes a file (`roster_save`) runs on temporary copies and compares the written files.
  **Tests never write the real game folder.**
* **Code outside GB.EXE** (`test_adlib.py`): `h.add_function(name, cs, off, conv, ds, sp)` makes a
  routine of another program in memory (ADLIB.COM) callable by `h.check(name, ...)`, with its own
  DS = SS and SP; `h.ignore` lists linear ranges that are not compared (that program's stacks and
  the SS:SP it saves), `h.sym.add_region` names its memory in reports.
* **The OPL2 and INT 65h** (`soundmodel.py`): OUT 388h/389h are logged as (register, value) and
  compared with the port's `host_opl_write` calls; IN 388h reads 06h; INT 65h goes through the
  interrupt vector as on the CPU (to ADLIB.COM's handler when a test installed it).
* **Program exit**: `h.check(..., exit_code=0)` expects both sides to end the program with
  `exit(0)` (`quit_to_dos`, e.g. the vacation choice) and compares the memory at that point.
* **Snapshots of a running original**: `Original.stub(seg, off, fn)` returns its hook; a test can
  stop the original when it reaches a function (`fn` calls `h.orig.fail(...)`) and use the memory
  there as the state for other tests (`test_front.front_state`: the front end after its set-up),
  then remove the hook with `h.orig.uc.hook_del`.
* **Callees** run for real on both sides. A port that calls a function not ported yet goes through
  `src/game/pending.cpp`: a placeholder that is exact in the tested states (and says so), or a
  fatal "not ported yet" error if it could be reached otherwise.
* **Bridge entries** live in `tests/difftest/bridge_<subsystem>.cpp` (`BRIDGE(name) { ... }`, see
  `bridge.hpp`); a test calls `h.check(name, m, regs=..., stack_args=[...], outputs=[...])`.
* The tests were checked by planting bugs (a wrong clip limit, a missing sign flip, a wrong mask, a
  stray write, a short loop, a missing store, an unsigned character, a size off by one, a fade
  rounding): each is reported. The fade rounding was only caught once DAC writes were logged. The
  AdLib tests caught a wrong pitch limit, a division loop one step short, a missing counter wrap and
  an ignored register input (through the OPL write log and all memory).

## Timing and host rules

* The host calls `timer_interrupt()` at the PIT rate the game programs (89.63 Hz menus, 236.695 Hz
  effects, 18.2 Hz BIOS); it runs the handler that the INT 8 vector in mem[] points at, so a game
  handler chaining to the vector it saved works as on the machine. The keyboard likewise goes to
  kbd_isr only while INT 9 points at it (`kbd_byte`).
* Every busy-wait loop of the original calls `host_pump()` once per iteration, in the same place
  relative to its test as the original's poll (see the title sprite loop); port 3DAh polls become
  `host_wait_vretrace()`.
* A timer vector may also point at the resident Ad Lib driver's INT 8 handler (the game's handlers
  chain to it when it was installed): `run_int8_handler` runs `adl_clock_isr`, which chains to the
  BIOS.
* Keys arrive as the XT byte stream (`host_set_kbd_handler`); port 201h reads become
  `host_joy_read`; OPL and speaker writes go to `host_opl_write`/`host_speaker`.
* In the test DLL, `host_pump()` runs the test's tick, `host_fatal()` and `host_exit()` return an
  error to the harness.
