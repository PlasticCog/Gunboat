# Ghidra validation — 2026-09-14

## Gameplay-capture follow-up

The user navigated the game and supplied native captures through `gb_021.png`.
The daytime scene's sky, water, shore and five terrain shades match record 47,
TACTCOLR.BIN, with RGB565 quantization of its six-bit DAC values. Evidence is in
`out/ghidra/palette-verification.json`. The current viewer now uses these colors
with an unlit material, preserving DOS colors instead of applying modern lighting.

The exporter applies the original daytime color substitutions and alternating
shades from 0x11826, using the VGA state table [11,9,14,10] at DS:B420.
The machine-code loader checks now also verify every decoded daytime color.
Both desktop and mobile browser checks passed after this change.

These are palette and structure validations. A matched-location and matched-camera
comparison is still required for vertical calibration and full geometric fidelity.
The initial automation limitations described below still apply; the user controls DOSBox.

## Completed

Ghidra 12.0.4 imported the unpacked image as x86 16-bit Real Mode at
physical address 0x10000, matching its existing relocations. DS was set to
0x2B73. The saved project is `ghidra/GunboatAudit.gpr`.

`tools/GunboatAudit.java` creates named functions and exports disassembly and
decompiler output to `out/ghidra/terrain-audit.txt`. The inspected routines
cover filename hashing, bank loading, cell addressing, tile vertex loading,
coordinate rotation, primitive drawing, collision entries, and projection.

The audit agrees with the existing decoder on:

- 17-byte map row stride and grid start at DATn.DAT offset 2.
- Six-bit tile indices and two-bit quarter-turn rotations.
- The four vertex arrays and unsigned height bytes.
- Two separately processed vertex groups.
- Triangle and line connectivity rules.

Ghidra's C-like output is incomplete for routines that return values in
multiple registers. In particular, rotation returns both AX and CX; a default
C return signature omits the second coordinate. Assembly remains authoritative.

## Independent execution checks

`python reverse_engineering/tools/verify_original_tile_code.py` runs the
original machine code in Unicorn, without replacing the tested instructions.

- 25,576 transform cases: every tile vertex, four rotations, two translations.
- 12,788 vertex-loader cases: both groups, every vertex and four rotations.
- Zero mismatches with the decoded coordinates and height bytes.

The results are saved in `out/ghidra/transform-verification.json`.

## Still unverified

These checks do not establish the exact Three.js vertical scale or DOS palette,
the playable bounds of the world rectangle, or mission-object placement.
The projection routine uses lookup tables, quantized distances and a height
term involving a five-bit shift. That does not by itself establish a metric
height conversion for a conventional perspective camera.

Color replacement at image offsets 0xAFBC–0xB016 and 0x11826–0x11864 depends
on game state and graphics mode. The viewer palette remains provisional.

## DOSBox comparison status

An isolated copy of the game is running with `out/dosbox-audit.conf`.
Native captures are saved in `out/dosbox_captures`. Available captures include
the copy-protection, publisher and title screens; no in-world comparison has
yet been completed.

Desktop automation capture failed with `SetIsBorderRequired failed: No such
interface supported (0x80004002)`. Input attempts repeatedly reported
`user input was detected in this window; call get_window_state before continuing`
even after a fresh text-only observation. Restarting DOSBox did not resolve it.
Manual game navigation and native Ctrl+F5 captures can continue the comparison.
