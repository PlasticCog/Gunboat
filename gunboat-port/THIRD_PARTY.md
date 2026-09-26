# Third-party code and provenance

## Test Drive III SDL3 port

Source: the `test-drive-3-sdl3` repository (github.com/kylofon/test-drive-3-sdl3), revision
`2c5a58d9440a450ac639542965dbef0f18a696d0`. Copyright (c) 2026 Krzysztof Kania. MIT license,
reproduced in [`licenses/test-drive-3-sdl3-MIT.txt`](licenses/test-drive-3-sdl3-MIT.txt).

Converted to C++ for the port (each file says so in its header):

| Port file | From `td3port/src` | Changes |
| --- | --- | --- |
| `src/host.hpp`, `src/host.cpp` | `host.h`, `host.c` | C++ and `namespace gb`; the timer follows the PIT divisor the game sets (Gunboat uses three rates) instead of TD3's fixed rate; audio generated in chunks for long ticks; no overflow of the tick clock in long sessions; no mouse (GB.EXE has no INT 33h); no race frame pacing; Gunboat captions; `GB_KEYS` / `GB_SNAPSHOT_DIR` |
| `src/platform/vga.hpp`, `.cpp` | `platform/vga.h`, `vga.c` | C++ and `namespace gb` |
| `src/mem.hpp`, `src/mem.cpp` | `mem.h`, `mem.c` | Gunboat's layout (DGROUP 1B73h), accessors as C++ lvalue functions, the EXEPACK decoder in C++, Gunboat's build checks |

Adapted tools: `reverse_engineering/tools/unexepack.py`, `x86dis.py`, `gbindex.py`,
`gen_symbols.py`, `merge_symbols.py` and the Ghidra scripts (each says so in its header).

The Codex prototype keeps its unmodified C copies of `host.c`, `host.h`, `types.h`,
`platform/vga.*` (with Gunboat captions) and a small `mem.h` adapter in `legacy/vendor/td3`, with
the license. `legacy/src/assets.cpp` adapts the EXEPACK method of `mem.c` and the LZW dictionary
semantics of `platform/pic.c`.

## Nuked-OPL3

Unmodified `opl3.c` / `opl3.h` in [`vendor/nuked-opl3`](vendor/nuked-opl3), with its license.
Copyright (C) 2013–2020 Nuke.YKT. GNU LGPL 2.1 or later. Built as its own static library
(`nuked_opl3`) from the included source, so the executable can be relinked with a modified
library. It emulates the OPL2 sound chip for AdLib music; it is not a DOS or x86 emulator.

## SDL3

SDL 3.4.16, zlib license. The MinGW developer package and license are under
`deps/SDL3-3.4.16` (not in git); the runtime copy is `build/SDL3.dll`.
[Release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16),
[MinGW archive](https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-devel-3.4.16-mingw.tar.gz),
SHA-256 `c7ef65bd72eabac6e5b535411dbd8d5824d0aab24fd62ff8812666b336f18a9c`.

## Development-only tools

Unicorn (GPL-2.0) and Capstone (BSD) are used by the Python tests and research tools. They are
not linked into the game.

## Original Gunboat resources

`GB.EXE`, the data files, pictures, palettes and music stay in the user's own `Original DOS
version` folder. The port reads them at run time; nothing here relicenses or redistributes them.
