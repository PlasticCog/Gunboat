# Gunboat DOS Reverse Engineering Notes

> **2026-09-25:** Start with [RE_GUIDE.md](RE_GUIDE.md) (executable map, addresses, symbols)
> and `/CLAUDE.md` (porting rules). This file is the chronological research log. Sections
> about the Three.js lab, the asset-explorer era and Ghidra describe work now in `/archive/`;
> their file paths are historical.

## Playable build — September 15, 2026 (archived)

The Three.js patrol prototype is in `/archive/threejs-lab/` with its
`PLAYABLE_BUILD.md`.

## Latest: original world geometry decoded (2026-09-14)

Objects and sprites: [OBJECT_FORMAT.md](OBJECT_FORMAT.md) documents initial
map objects, tile scenery, original sprite extraction and the viewer's object layers.

Follow-up: [GHIDRA_VALIDATION.md](GHIDRA_VALIDATION.md) documents the saved
Ghidra project and 38,364 successful machine-code execution checks. Live DOSBox
world-view validation is still pending; colors and vertical scale remain provisional.

See [ORIGINAL_WORLD_FORMAT.md](ORIGINAL_WORLD_FORMAT.md) for the new traced
filename lookup and world/tile structures. All 84 bank records now have matched
original filenames. Record 83 is TILE.BIN, while MAP.LZ is record 51.
The Three.js viewer now loads original DAT1.DAT–DAT4.DAT grids and tile geometry.
Earlier candidate names and the record-83 heightmap hypothesis below are
historical and superseded. Use `tools/export_original_world.py` to export.

## Current Source Set

The DOS release in `Original DOS version` contains:

- `GB.EXE` - main game executable, DOS MZ, EXEPACK-style packed.
- `SETUP.EXE` - graphics/joystick setup, also packed.
- `INSTALL.EXE` - installer, also packed.
- `DATAA.DAT`, `DATAB.DAT` - packed asset/data banks.
- `DATAC.DAT` - asset index for `DATAA.DAT` and `DATAB.DAT`.
- `GUNBOAT.CFG` - 6-byte setup file. Current bytes are `13 00 00 00 00 00`; `0x13` matches VGA/MCGA mode 13h.
- `GBROSTER.DAT` - roster/save file, currently empty.
- `VALK*.MUS`, `ADLIB.*`, `CMS.DRV`, `FOOTIT1F.LZ` - music/audio/asset support files.

Manual-derived gameplay notes are summarized in:

```text
reverse_engineering\MANUAL_NOTES.md
```

A Steel Thunder DOS reference set has also been added:

```text
Original DOS version\SteelThunder
reverse_engineering\STEEL_THUNDER_REFERENCE.md
```

This is useful comparative material for Accolade/Tom Loughry-era engine behavior and loose asset formats. It is separate from the unrelated modern open-source Thunder Engine repository discussed below.

## Main Executable

`GB.EXE` is a DOS MZ executable with a packed entry near the end of the file:

- Packed entry: `CS:IP = 2187:0010`
- Packed stack: `SS:SP = 2AEC:0080`
- MZ header size: 512 bytes
- Relocation count: 0

The packer stub expands the real image, applies relocations, restores the original stack, then jumps through `CS:[0]`.

After emulating the packer:

- Real entry: `CS:IP = 15EE:0016`
- Real stack: `SS:SP = 2AD7:08C8`
- Generated analysis files:
  - `reverse_engineering/out/gunboat_unpacked_image.bin`
  - `reverse_engineering/out/gunboat_unpacked_rough.exe`

Repeat with:

```powershell
python .\reverse_engineering\tools\unpack_gb_exepack.py
```

## Compiler/Runtime

The unpacked binary contains:

- `MS Run-Time Library - Copyright (c) 1988, Microsoft Corp`
- Microsoft C runtime error strings such as `R6000`, `R6001`, `R6002`, `R6003`, `R6009`

So the game was likely built with a late-1980s Microsoft C toolchain.

## Data Banks

`DATAC.DAT` is 1190 bytes: 85 records of 14 bytes, where the final record is all zeroes.

Observed record layout:

```c
struct AssetIndexRecord {
    uint16_t meta0;
    uint16_t meta1;
    uint16_t file_key;  // low byte is 'a' or 'b'
    uint32_t offset;
    uint32_t length;
};
```

The index tiles the data banks exactly:

- Records `00..24` reference `DATAA.DAT`
- Records `25..83` reference `DATAB.DAT`
- Record `84` is the zero sentinel

Repeat index parsing with:

```powershell
python .\reverse_engineering\tools\parse_datac.py
```

Extract chunks with:

```powershell
python .\reverse_engineering\tools\parse_datac.py --extract
```

## Embedded Asset Names

The unpacked executable references original asset names, including:

- `DAT6.DAT`, `DAT5.DAT`, `DAT1.DAT` through `DAT4.DAT`, `DAT10.DAT`, `DAT11.DAT`
- `TITLE*.LZ`, `FOOTIT*.LZ`, `HQ*.LZ`, `SPEC*.LZ`, `MP*A.LZ`, `MP*B.LZ`
- `BD*.LZ`, `BG*.LZ`, `BF*.LZ`, `B6*.LZ`, `BGR*.LZ`, `BR*.LZ`, `BM*.LZ`
- `TILE.BIN`, `TACTCOLR.BIN`, `LIGHTS.LZ`, `MAP.LZ`
- `VALKPC.MUS`, `VALK12.MUS`, `VALK3V.MUS`

These names suggest the shipped `DATAA/DATAB` files are combined banks replacing many development-time disk files.

## Gameplay/Systems Visible From Strings

Major systems already visible in the unpacked image:

- Front end / HQ flow
- Personnel roster
- Manual-based quiz/copy protection
- Regions: Vietnam, Colombia, Panama Canal
- Practice modes: gunnery, grenade, pilot
- Equipment/outfitting/spec screens
- Tactical map and mission assignment flow
- PBR damage model: hull, captain, engineman, gunner's mate, seaman, engines, waterjets, fuel tanks
- Weapons/events: rapid-fire guns, grenades, mortar, missiles, salvos, mines, aircraft
- Mission status and scoring categories
- Time compression and detail level toggles

## Rebuild Direction

For a new version, treat this as behavioral research rather than a direct port:

- Preserve high-level concepts: PBR crew roles, river navigation, ambushes, mission briefings, damage control, outfitting.
- Rebuild systems and assets cleanly in a modern codebase.
- Use the original only to infer mechanics, pacing, data structures, mission shapes, and UI flow.
- Avoid copying original art, text, music, or code into the new game unless there is a separate rights decision.

## Thunder Engine Reference

Thunder Engine has been checked as a possible modern rebuild target:

```text
reverse_engineering\references\thunder
reverse_engineering\THUNDER_ENGINE_ASSESSMENT.md
```

It is not currently supported by evidence as a source port of Accolade's Steel Thunder/Gunboat engine. The cloned repo has no meaningful Gunboat, Accolade, Steel Thunder, Test Drive 3, or Tom Loughry references. It should be treated as a modern open-source C++ engine candidate only.

## Asset Explorer GUI

The first GUI workbench is available at:

```powershell
python .\reverse_engineering\tools\gunboat_asset_explorer.py
```

Or double-click:

```text
reverse_engineering\Launch Asset Explorer.bat
```

It currently provides:

- DATAC/DATAA/DATAB asset table browsing.
- Hex and ASCII string inspection for each chunk.
- Palette strip rendering for small palette-like chunks.
- Test Drive 3-style LZW+RLE rendering for high-entropy `.LZ` graphics.
- Adjustable raw indexed and packed-4bpp preview modes.
- Mouse-wheel zoom, left-drag pan, and double-click reset in the asset preview canvas.
- Optional strip combining is enabled by default. It handles named groups like `TITLE3A/B/C` and also contiguous same-shape records such as `39..42` and `43..46`, which appear to be full-screen map/tactical panels split into `320x54` strips. Strip order is selectable; `record order` is the default because the first `TITLE3` group appears to stack as `TITLE3C`, `TITLE3B`, `TITLE3A`.
- Candidate map/tile viewer with adjustable width, byte skip, and zoom.
- Extracted gameplay/logic report from the unpacked executable.
- Embedded original asset-name list from the binary.

Current caveats:

- Asset names are provisional. The executable's original file-name table is visible, but the exact bank-record-to-name mapping still needs deeper loader tracing.
- `.LZ` image compression now has a strong working decoder and row orientation. Palette assignment, exact dimensions for every record, and non-image/map records still need deeper loader tracing.
- Small 189/225-byte records are palette/control chunks, not TD3-compressed images. Forcing the TD3 decoder on them should be treated as an invalid-format test, not a codec failure.
- Debug palette rendering can make correctly decoded images look psychedelic. Record `09` decodes coherently to a `320x50` title/footer strip using only indices `0..31`; applying palette record `03` produces a much more plausible DOS title image than the synthetic indexed palette.
- Not every decoded image is `320` pixels wide. Record `13` has `15680` decoded pixels: `320x49` looks flattened, while `160x98` is the better first-pass dimension. The explorer now lists candidate dimensions and has a `Suggest` width button.
- False "interlacing" can appear when a `320`-wide strip is rendered at `160`; each true scanline wraps into two half-lines. The `TITLE2A-D` group now prefers `320` and combined-strip rendering chooses the widest common candidate width across the group.
- Tactical map terrain records `39..46` are not normal title-screen palette images and are not one full-screen raster. Each group of four `320 x 54` decoded records packs a `2 x 3` sheet of low-resolution `160 x 72` sector-map backgrounds. The explorer now has `Tactical map sector` and `Tactical map sheet` preview modes, with a `Sector` selector for choosing one of the six packed backgrounds. Values `0x10..0x1F` behave like flagged material values; `0x1A` in particular is best treated as a flagged terrain material rather than a direct palette index. The gray frame, place labels, cursor, and footer instructions visible in reference screenshots are separate UI/text layers still to be traced.
- The apparent tactical-map "interlace" is mostly material value `3`, a hard horizontal relief/hill hatch layer, plus one-pixel gaps in shore/water strokes. The explorer's tactical preview now hides the relief hatch and smooths line gaps by default, with `Relief hatch` and `Smooth lines` checkboxes so the raw material layer can still be inspected.
- Record `83` is the strongest current map candidate: it is 14,885 bytes and starts with `0x3A20`, which equals 14,880, suggesting a small header plus map-like payload.

## 3D Map Lab

A first-pass browser-based 3D terrain workbench is available at:

```powershell
python .\reverse_engineering\tools\export_gunboat_3d_map_assets.py
python -m http.server 8765 --bind 127.0.0.1 --directory .\reverse_engineering\web\gunboat_3d_map
```

Then open:

```text
http://127.0.0.1:8765/
```

Or double-click:

```text
reverse_engineering\Launch 3D Map Lab.bat
```

The lab uses Three.js with orbit/zoom controls and currently exposes three datasets:

- Tactical terrain strips `39..42`, decoded with the TD3 LZW+RLE image path, stacked into a `160 x 108` downsampled height/material grid.
- Tactical terrain strips `43..46`, decoded the same way.
- Record `83`, interpreted as a raw `96 x 155` probe after its 5-byte header. This is speculative structural data: the first word `0x3A20` equals the remaining payload byte count (`96 * 155`), but field meaning still needs loader tracing.

The strip datasets are not a confirmed original 3D world model. Their height is inferred from tactical-map material colors so the terrain can be inspected in 3D while we look for the engine's true map/tile tables. The Test Drive 3 viewer shows the likely target shape: a browser scene driven by a tile grid, height fields, object placement, and remap tables. For TD3, the confirmed map grid is `32 x 16` tile entries with tile id, rotation, and elevation; Gunboat has not yet exposed an equivalent confirmed table.

Verification:

```powershell
node .\reverse_engineering\tools\verify_3d_map_lab.mjs http://127.0.0.1:8765/
```

This Playwright check loads desktop and mobile viewports, switches the desktop dataset to record `83`, drags/zooms the camera, and reads a non-background WebGL pixel from the canvas.

## Compression Probe

The current compression workbench is:

```powershell
python .\reverse_engineering\tools\probe_compression.py --all --lzss
```

It writes:

```text
reverse_engineering\out\compression_probe\compression_probe.md
reverse_engineering\out\compression_probe\*.png
```

Confirmed from the original draw routine at `0xEE01`:

- A graphic record has a one-byte cache/control prefix.
- The visible primary dimensions are then stored at bytes `1` and `2`.
- Bytes `3..5` hold optional secondary/subimage dimensions or stride/count data.
- Bytes `6..29` are row-control data for the direct blitter.
- Pixel bytes for the direct VGA path begin at byte `30`.
- Zero pixels are treated as transparent by the VGA blitter.

That direct-blit structure is an already-expanded drawing record, not the raw `.LZ` compression stream. Most high-entropy DATAA/DATAB chunks render as noise when interpreted directly, because they must first pass through the loader-time decompressor.

The Test Drive 3 reverse-engineering project was the key lead:
https://github.com/s-macke/Test-Drive-3-Maps

Its documented VGA image path is:

```text
compressed bytes -> LZW decode -> RLE unpack -> indexed pixels
```

Using that pipeline against Gunboat records produces recognizable graphics. Like Test Drive 3, decoded rows are stored bottom-to-top; applying a vertical flip makes record `00`'s copy-protection screen readable.

- Record `01`: `4362` post-LZW bytes -> `64000` pixels, fitting `320x200`.
- Record `07`: `16920` post-LZW bytes -> `16000` pixels, fitting `320x50`.
- Record `09`: `12388` post-LZW bytes -> `16000` pixels, fitting `320x50`.
- Record `26`: `11374` post-LZW bytes -> `12800` pixels, fitting `320x40`.
- Record `56`: `19924` post-LZW bytes -> `35840` pixels, fitting `320x112`.

The earlier direct-RLE failure is still useful negative evidence: Gunboat is not simple RLE on the DAT chunk bytes. The RLE layer appears after the TD3-style LZW wrapper.

The probe also tries a matrix of common DOS-era 4K-window LZSS layouts and simple direct-RLE variants, including PackBits-style, PCX-style `0xC0` run coding, and byte-pair count/value forms. The TD3 LZW+RLE path is currently the only tested family producing clearly recognizable images.

The Asset Explorer now defaults high-entropy encoded chunks to `TD3 LZW+RLE`. Manual raw previews are still available for investigation, but visual noise there means the compressed byte stream is being viewed before decompression.

Promising next reverse-engineering targets:

- Find the code path that opens `DATAC.DAT`, `DATAA.DAT`, and `DATAB.DAT`, then follows the 14-byte DATAC records.
- Trace what happens after a bank chunk is read into memory and before `0xEE01` draws it.
- Match each `.LZ` record with its intended palette and display width/height from the executable tables.
- Determine whether map/control records use this image codec, a separate codec, or raw structured data.
