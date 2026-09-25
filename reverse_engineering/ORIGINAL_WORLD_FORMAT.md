# Original Gunboat world geometry — September 14, 2026

## Result

The Three.js viewer now reads the original DAT1.DAT–DAT4.DAT tile grids and
TILE.BIN vertex/primitive streams. It no longer displays a guessed 96 × 155
heightmap or combines tactical-map pictures into one world.

Run `python reverse_engineering/tools/export_original_world.py`, then use
`Launch 3D Map Lab.bat` to serve the viewer on localhost:8765.

## Filename lookup: all 84 records resolved

Unpacked image offsets 0xD74–0xE61 implement bank lookup. The two 16-bit
keys in DATAC.DAT are compared against the return value of 0xEF2.

- Key 0: sum(i * filename[i]) for i from 0 through length-2, modulo 65536.
- Key 1: sum(filename[i] * 257**i), modulo 65536.
- Helpers: 0xEAC (weighted sum), 0xE62 (reverse polynomial accumulation).
- Lookup advances in 14-byte records and obtains bank, offset, and length.

All 84 records match a filename found in the unpacked executable. The exported
`out/world_export/asset-names.json` (written by `tools/export_original_world.py`) contains the complete mapping.
The shared `read_datac()` now uses these matches.

Critical corrections to older notes:

| Record | Original filename | Meaning |
|---|---|---|
| 51 | MAP.LZ | Compressed picture; not record 83 |
| 70–73 | DAT1.DAT–DAT4.DAT | World/mission records, 6468 bytes each |
| 75–82 | DAT1A/B.DAT–DAT4A/B.DAT | Companion records; not yet decoded here |
| 83 | TILE.BIN | Shared vertex, drawing, object and collision data |

## World cells

The near-buffer loader at 0x7152 loads DATn.DAT to DS:B84C. Grid reads at
0x11658 and 0x11908 use DS:B84E + cell index, establishing file offset 2.
The cell addressing at 0x11599 and 0x118E4 uses a row stride of 17.
The viewer exports the 17 × 11 world rectangle corresponding to the coordinate
mapping `worldY = (10-row)*1024`; further data in the record is preserved on disk.
Full world bounds and boundary behavior still merit dynamic validation.

Each cell byte is `(rotation << 6) | tileId`:

- Low six bits: TILE.BIN table index.
- High two bits: rotation, confirmed at 0x1172B–0x1173C.
- Local coordinates rotate around the 128 × 128 tile extent at 0x118A1.
- Rotations: (x,y), (y,128-x), (128-x,128-y), (128-y,x).
- Coordinates become world coordinates via multiplication by 8 and translation.

## TILE.BIN

The first 68 bytes are 34 little-endian offsets, indexed directly by tile ID
at 0x1173C–0x1174D. Entry 0 points to an empty record at 14880; this value
was previously mistaken for a payload size. Entries 17 and 18 share an offset.

At each offset are four unsigned counts: A, B, objectCount, collisionCount.
With N=A+B, four N-byte arrays follow: drawing control/color, height, X, Y.
The original loader copies these at 0x117F5–0x118A0, processing groups A and B
separately. There are then objectCount four-byte entries and collisionCount
three-byte entries. Additional trailing bytes in some tiles are not interpreted.

Primitive decoding is traced to 0x1080A:

- Low six control bits are color; zero means no primitive.
- Mode 0 (high bits 00): triangle (i, i+1, i+2).
- Mode 1 (01): line (i, i+1).
- Mode 2 (10): triangle (i-1, i+1, i+2).
- Mode 3 is rejected by this exporter; none is encountered.

Exporter checks all primitive references against their vertex groups, all cell
IDs against the offset table, record extents against file size, and all filename
hashes. It exports 34 tile table entries and 2445 triangle commands, including
the aliased entries. World 1 renders 11,934 triangles.

## Display limits

Update: daytime colors are now checked against the user's DOSBox gameplay capture
`gb_021.png`, sourced from TACTCOLR.BIN with the original VGA color remapping.
The viewer uses unlit colors. The earlier provisional palette/lighting description
below records the first implementation; vertical scaling remains provisional.
See [GHIDRA_VALIDATION.md](GHIDRA_VALIDATION.md) for the verification results.

X/Y layout, rotations, vertex values and primitive connectivity come from the
original files. The viewer uses X/Y divided by 8 and heights divided by 32 as
an initial display scale. Exact DOS vertical calibration and color remapping
are not yet reproduced. The original renderer dynamically substitutes colors
at 0x11826–0x11864; the viewer uses an explicitly provisional palette.

The water-colored backing plane, lighting, and tiny per-face depth separation
are viewer aids. Mission objects, sprites, and collision markers are not yet
rendered. This is a terrain reconstruction, not a complete game renderer.

## Verification

`python reverse_engineering/tools/export_original_world.py`

`node reverse_engineering/tools/verify_3d_map_lab.mjs`

The browser check loads all four worlds on desktop, checks mobile rendering,
exercises orbit/zoom, and rejects console errors. Screenshots are saved in
`out/3d_map_lab/original-desktop.png` and `original-mobile.png`.

Next: calibrate heights and original palette; decode tile object entries and
DATnA/B companion data; validate maps against the running DOS game. Steel Thunder
remains useful comparative material, but these findings came directly from
Gunboat's loader and renderer.
