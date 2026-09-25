# Original objects and sprites

## Placement records

DAT1.DAT–DAT4.DAT load at DS:B84C. Three parallel arrays contain object words,
world X and world Y at DS:B95D, DS:C12D and DS:C8FD. Their file offsets are
273, 2273 and 4273 respectively, each with room for 1000 unsigned 16-bit words.
The low byte of an object word is the object type; the high byte contains flags.
The facing lookup at image 0xF284 uses the low three high-byte bits.

DS:B959, file offset 269, marks where the tile-loader starts overwriting the
temporary scenery tail. Values are 199,199,199,191 in these four maps. Only
the initial nonzero entries before this boundary are exported as map objects.
The remaining saved buffer is not treated as extra authored world objects.

Each TILE.BIN record has four-byte scenery entries after its four vertex arrays:
type, local X, local Y, extra byte. Image 0x11783–0x117EF loads the entries,
applies the tile's original rotation and translation, and appends to the same
object arrays starting at DS:B959. The fourth byte is skipped by this loader.
The packed upper flags use the source entry pointer's low three bits, rather
than the tile rotation. The exporter preserves that rule for the aligned buffer.

The coordinate conversion used by the viewer is x/64-136 and 88-y/64, the same
translation and scale as its original terrain. No random scattering is used.

## Sprite graphics

These graphics are built from compact source records, rather than stored as
ordinary standalone PNG images. `probe_original_sprites.py` executes the original
sprite builder (image 0xEC7B) and VGA blitter (0xEE01) in Unicorn. That preserves
the game's command decoding, horizontal scaling, repeated rows, transparency,
and composite upper/lower sprite parts.

The emulated scratch cache's allocation limit is increased to prevent the
small first cache slot from truncating sprites. The sprite-building instructions
and graphics source bytes are unchanged. VGA mode 13h and the already verified
daytime palette are used. Zero pixels become transparent, matching the blitter.

The scene loader at 0x70F4 onward selects these graphics pairs:

| World data | Sprite source | Bank records |
|---|---|---|
| DAT1.DAT | DAT2A.DAT / DAT2B.DAT | 77 / 78 |
| DAT2.DAT | DAT3A.DAT / DAT3B.DAT | 79 / 80 |
| DAT3.DAT | DAT4A.DAT / DAT4B.DAT | 81 / 82 |
| DAT4.DAT | DAT1A.DAT / DAT1B.DAT | 75 / 76 |

Eight angles are rendered per used type into one atlas per world. Object type
57 is intentionally omitted from the visible sprite layer: the original
blitter explicitly returns without drawing it at 0xEE03. Its placement metadata
remains in the export. There are no other entirely blank used types across the
eight rendered angles.

## Viewer

Two instanced, camera-facing sprite batches draw map objects and tile scenery.
They share a nearest-filtered atlas. Controls separately show/hide those layers,
adjust sprite size, hide terrain for inspection, and focus an initial map object.
Focus reports source index, type, world coordinates, flags and data-file offset.

World 1 has 159 visible map objects and 1437 visible scenery sprites. The four
world totals are 1596, 2247, 1634 and 1798. Detailed counts are in
`out/object-export-report.json`.

## Verification and limits

`verify_original_objects.py` runs the original tile loader for every tile and
rotation and compares the resulting types, flags, and X/Y values. All 1664
object cases match. `out/object-placement-verification.json` records the result.

The Playwright check loads all four worlds, checks desktop/mobile rendering,
focuses a source-coordinate fixture, and verifies that hiding map objects
removes sprite-colored pixels from the rendered canvas. Extracted atlases were
visually inspected; no occupied pixels reach their top clipping boundary.

Placement coordinates and source graphics are decoded. Display size, exact
directional-frame convention, and the previously provisional terrain height
scale still need matched-camera DOS validation. Eight sampled views do not
reproduce every view available from the original builder. Sprites remain 2D
billboards, not reconstructed 3D meshes. The modern depth buffer can hide objects
behind terrain; the Terrain checkbox allows inspection without that occlusion.

These are initial map records and globally expanded tile scenery. Mission
selection, live spawning, movement, damage, destroyed variants and time-of-day
changes are not simulated, nor is this a live memory view of the running DOSBox.

Rebuild everything with `python reverse_engineering/tools/export_original_world.py`.
