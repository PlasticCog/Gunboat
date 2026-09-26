# Native Gunboat port milestone — 2026-09-25

The playable C++/SDL3 build is in [`../gunboat-port`](../gunboat-port/README.md).
Launch it with `Play Native Gunboat.cmd` in the project root. The browser viewer
remains a separate implementation for comparison.

## New recovered functionality

The supplied TD3 port established shared archive hash / LZW / EXEPACK behavior.
The new C++ loader unpacks the original `GB.EXE` without executing DOS code and
recreates all four worlds directly from the original archives.

The original sprite-cache routines are now translated into native data
operations. Relevant image offsets: `EC7B`, `F158`, `F503`, `F837`, `FA34`, and
the `EE01` blitter. Important details recovered during translation:

- Sprite A data is based at DS:6E54; B data at DS:53A8. B-relative row pointers
  can cross into A, so a native pointer lookup needs the combined address space
  with A starting at B-relative offset `1AAC`.
- The main descriptor is 8 bytes per kind. Composite parts use the table based
  at A's word at offset 6, indexed by the main descriptor's last byte.
- The directional mask table is at image `28FD7`. Scale 47/46 masks begin at
  image `EB3F`. Horizontal masks cycle through seven bytes. Vertical repetition
  consumes bytes 9, 8, then 7.
- Row flags `80h` select panorama rows and `40h` select static rows; ordinary
  rows combine front/depth spans according to directional masks. Width/depth
  and span order swap at quadrant boundaries.
- The composite horizontal shift uses the original fixed-point ratio and
  integer truncation. No additional vertical doubling is applied after row
  repetition.
- Cached fixture images are 256×64, cropped from DOS screen X=40..295,
  Y=64..127, with the baseline at local Y=56 and pivot X=120.

`gunboat-port/legacy/tests/probe_sprites.py` records the independent Python translation.
`gunboat-port/legacy/tests/verify_assets.py` compares the compiled C++ output directly
with execution of the original routines under the development-only Unicorn
oracle. None of that emulation is used by the native game.

## Verification result

Zero differences for 172,304 unpacked/relocated executable bytes, six compressed
picture streams, 10,036 world object records, 52,138 terrain triangles and 1,056
sprite type/view combinations across the four banks.

The native game has new patrol rules, movement, collision, rendering and cockpit
composition. These are explicitly provisional; asset equality is not a claim
of original simulation or whole-scene visual equality. See the native README's
accuracy table and next-iteration list before extending the port.
