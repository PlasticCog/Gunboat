# Gunboat: the C++ / SDL3 port

The faithful port of `GB.EXE`, rebuilt one original function at a time on the original memory
layout (see `/CLAUDE.md` for the method and `PORTING.md` for how the code is organised). The
game itself does not run yet: this is the **core** (phase 3), on which phase 4 ports the
subsystems.

| Part | State |
| --- | --- |
| Memory model and loader (`src/mem.*`) | GB.EXE (EXEPACK) unpacked, relocated and checked in C++; identical to the Python tools' image |
| Host layer (`src/host.*`, `src/platform/vga.*`) | TD3's SDL3 host in C++: window, the three PIT rates GB.EXE uses, keyboard, gamepad, OPL2 + speaker audio, files |
| Symbols (`src/symbols.hpp`) | Generated from `reverse_engineering/symbols.csv`: 318 DGROUP globals, 19 code-segment tables, 409 functions |
| Ported functions | 4: `random`, `vec_scale`, `heading_vector`, `boat_move`, each matching the original on all memory |
| Differential tests (`tests/difftest/`) | The original code in Unicorn against the C++ on the same memory: 9,097 cases, 0 mismatches |
| Codex prototype (`legacy/`) | The earlier playable patrol mode, kept as a reference; `Run Gunboat.cmd` starts it |

## Build and test (Windows)

MSYS2 UCRT64 (GCC 15, CMake, Ninja); SDL3 3.4.16 is under `deps/`:

```powershell
powershell -File gunboat-port/Build.ps1        # configure, build, ctest
```

or by hand:

```text
cmake -S gunboat-port -B gunboat-port/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build gunboat-port/build
ctest --test-dir gunboat-port/build --output-on-failure
```

The tests: `load_exe` (`gunboat --check`), `host_timer` (`gunboat --host-test`, headless),
`difftest` (`tests/difftest/run_all.py`, needs Python 3 with `unicorn` and
`reverse_engineering/out/GB_unp.exe` from `unexepack.py`), and the prototype's `legacy_*`.
`GB_GAME_DIR` points the tests at the original files (default `../Original DOS version`).

`python gunboat-port/tests/difftest/run_all.py [--scale N] [--seed N] [-k name]` runs the
differential tests alone (it builds `gb_difftest` first) and writes `out/difftest-report.json`.

GCC's runtime is linked statically: `gunboat.exe` needs only `SDL3.dll` beside it.

## Credits

Host layer, VGA model, EXEPACK decoder and tools adapted from the Test Drive III port (MIT,
© 2026 Krzysztof Kania); Nuked-OPL3 (LGPL-2.1); SDL3 (zlib). Details in `THIRD_PARTY.md`.
