# Porting guide

The rules are in `/CLAUDE.md` ("Porting rules", "Verification"); this file is the working guide:
where code goes, the memory API, and how to port and test one original function. Adapted from
the Test Drive III port's `td3port/PORTING.md`.

## Architecture

```text
src/main.cpp            arguments, mem_load_exe, host checks; later main (0000:0000)
src/mem.hpp/.cpp        mem[]: GB.EXE at 1000:0000, DGROUP 2B73h, VGA A000h; accessors; exact division;
                        the EXEPACK loader
src/host.hpp/.cpp       SDL3, the only file that includes it: window, PIT timer, retrace, XT keys,
                        gamepad, OPL2 + speaker, game files, fatal errors
src/symbols.hpp         generated from reverse_engineering/symbols.csv (never edit)
src/platform/           platform.md and video.md: vga (DAC/CRTC model), helpers (random ...), later
                        timers, keyboard ISR, files, LZW/pictures, text, graphics library
src/game/               the game: sim_*.cpp (simulation.md), later render_*, world_*, flow_*, hud_*
src/sound/              (later) sound.md
tests/difftest/         gbdiff.py (harness), bridge.cpp (the core as a DLL), host_stub.cpp, test_*.py
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

* **Same memory.** The harness builds one 1 MB + 64 KB memory image (the Python loader's, which
  `test_loader` checks against the C++ loader) and gives a copy to each side.
* **The original** runs in Unicorn: the function is called as the game calls it (far C functions
  with their stack arguments, near routines with registers; SS = DS = DGROUP, SP = FF00h) and runs
  until it returns to a sentinel address. `INT`, `IN` and `OUT` stop the run unless the test
  installs a handler (`h.orig.ints[0x21] = ...`), so a function never silently depends on
  hardware.
* **The port** runs through `gb_difftest.dll` on its own copy.
* **Compared:** every byte of memory except the original's stack below the call's SP (the port
  has no emulated stack), and the listed return registers. A mismatch names each differing range
  with its symbol and the before / original / port bytes.
* **Callees** run for real on both sides: the original executes its callees, the port calls its
  ports of them. A function whose callees are not ported yet waits for them, or the test stubs the
  callee identically on both sides.
* The harness was checked by planting six bugs in the port (a wrong clip limit, a missing sign
  flip, a wrong mask, a stray write, a short loop, a missing store); each was reported.

## Timing and host rules

* The host runs the active timer interrupt at the PIT rate the game programs (89.63 Hz menus,
  236.695 Hz missions, 18.2 Hz BIOS; `host_set_timer`). Every busy-wait loop of the original calls
  `host_pump()` once per iteration; port 3DAh polls become `host_wait_vretrace()`.
* Keys arrive as the XT byte stream for the ported INT 9 handler (`host_set_kbd_handler`); port
  201h reads become `host_joy_read`; OPL and speaker writes go to `host_opl_write`/`host_speaker`.
* In the test DLL, `host_pump()` runs exactly one timer interrupt and `host_fatal()` returns an
  error to the harness.
