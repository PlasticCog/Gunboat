# Third-party code and provenance

## Test Drive III SDL3 port

Source: the user-supplied `../test-drive-3-sdl3` repository, revision
`2c5a58d9440a450ac639542965dbef0f18a696d0`.

Copyright (c) 2026 Krzysztof Kania. MIT license, reproduced in
[`vendor/td3/LICENSE`](vendor/td3/LICENSE).

Copied from `td3port/src`:

- `host.c`, `host.h`, `types.h`
- `platform/vga.c`, `platform/vga.h`

Local changes: Gunboat window/error captions; `GB_KEYS` / `GB_SNAPSHOT_DIR`
environment names; C++ declaration compatibility in `host.h`. `mem.h` is a
minimal new framebuffer adapter. It does not load or execute a TD3 memory image.

`src/assets.cpp` adapts the data-only EXEPACK method in TD3 `mem.c` and LZW
dictionary semantics in `platform/pic.c`. The Gunboat hashes, records, tile
commands, sprite banks and translated sprite routines come from this project's
Gunboat reverse engineering.

## Nuked OPL3

Unmodified `opl3.c` / `opl3.h`, copied with their license from the supplied port.
Copyright (C) 2013–2020 Nuke.YKT. GNU LGPL 2.1 or later. Source and license are
included in [`vendor/nuked-opl3`](vendor/nuked-opl3).

This component is part of the inherited SDL host's sound-chip support. It is not
a DOS/x86 emulator. Current Gunboat gameplay only uses the host's speaker sound;
the original game's OPL music is not ported yet. The complete source and CMake
build are included so the executable can be rebuilt with a modified library.

## SDL3

SDL 3.4.16, zlib license. The downloaded developer package and license are under
`deps/SDL3-3.4.16`; the runtime copy is `build/SDL3.dll`.

[Official release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16)
and [MinGW developer archive](https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-devel-3.4.16-mingw.tar.gz).

Archive SHA-256:
`c7ef65bd72eabac6e5b535411dbd8d5824d0aab24fd62ff8812666b336f18a9c`.

## Original Gunboat resources

Maps, sprites, pictures, palette data and DOS executable remain in the user's
existing `Original DOS version` directory. This project reads them locally;
third-party open-source licenses above do not relicense those resources.
