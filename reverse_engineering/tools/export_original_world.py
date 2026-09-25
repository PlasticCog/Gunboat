"""Export original Gunboat tile geometry, using traced DOS renderer structures."""
import json
import struct
from pathlib import Path
from gunboat_formats import ROOT, read_datac, asset_path, filename_key

OUT = ROOT / 'reverse_engineering/out/world_export'


def daytime_color(control, vertex_index):
    """0x11826 remap with the daytime VGA table at DS:B420 (11,9,14,10)."""
    color = control & 0x7f
    alternating = 31 if vertex_index % 2 == 0 else 0
    if color == 14:
        color = 14
    elif color == 10:
        color = 10
    elif color == 2:
        color = 10 ^ (alternating & 24)
    elif color == 6:
        color = 6 ^ ((alternating & 16) ^ 4)
    return color & 63


def decode_tiles(data):
    # Entries 0..33; entry 0 is the empty tile, 17 and 18 share an offset.
    offsets = struct.unpack_from('<34H', data)
    assert offsets[1] == 68
    tiles = []
    for tile_id, offset in enumerate(offsets):
        a, b, objects, collisions = data[offset:offset + 4]
        n = a + b
        end = offset + 4 + n * 4 + objects * 4 + collisions * 3
        assert end <= len(data), (tile_id, end)
        arrays = [list(data[offset + 4 + j*n:offset + 4 + (j+1)*n]) for j in range(4)]
        vertices = list(zip(arrays[2], arrays[1], arrays[3]))
        faces, lines = [], []
        for start, count in [(0, a), (a, b)]:
            for i in range(start, start + count):
                control = arrays[0][i]
                color, mode = control & 63, control >> 6
                if not color:
                    continue
                # 0x1080A: mode 1 is a line. Otherwise vertex i-mode/2,
                # i+1, i+2. DOS mode 3 starts at an odd byte; not inferred.
                if mode == 1:
                    assert i+1 < start+count
                    lines.append([i, i+1, color, daytime_color(control, i)])
                elif mode in (0, 2):
                    indices = [i - (mode // 2), i+1, i+2]
                    assert min(indices) >= start and max(indices) < start+count, (tile_id, i)
                    faces.append(indices + [color, daytime_color(control, i)])
                else:
                    raise ValueError(f'Unsupported polygon control {control:#x}')
        tiles.append(dict(id=tile_id, offset=offset, vertices=vertices, faces=faces,
                          lines=lines, vertexGroups=[a,b], objectCount=objects,
                          collisionCount=collisions))
    return tiles


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    records = read_datac()
    assert all(filename_key(r.candidate_name.encode()) == (r.meta0,r.meta1) for r in records)
    tiles = decode_tiles(asset_path(records[83]).read_bytes())
    palette_bytes = asset_path(records[47]).read_bytes()[:96]
    # The DOSBox screenshots use RGB565 output. Reproduce that observed
    # quantization of the original six-bit DAC values, not an invented palette.
    def rgb565(r,g,b):
        return [((r>>1)<<3)|((r>>1)>>2), (g<<2)|(g>>4), ((b>>1)<<3)|((b>>1)>>2)]
    palette = [rgb565(*palette_bytes[i:i+3]) for i in range(0,96,3)]
    (OUT/'original-tiles.json').write_text(json.dumps(tiles, separators=(',', ':')))
    entries = []
    for index in range(70,74):
        r = records[index]
        raw = asset_path(r).read_bytes()
        cells = list(raw[2:2+17*11])
        assert all((v & 63) < len(tiles) for v in cells)
        slug = f'original-map-{index-69}'
        dataset = dict(slug=slug, label=f'Original world {index-69} — {r.candidate_name}',
                       kind='original-tiles', width=17, height=11, cells=cells,
                       records=[index,83], source=f'{r.candidate_name} + TILE.BIN · original tile vertices and rotations',
                       bank=r.source_file, offset=r.offset, gridOffset=2,
                       dosPalette=palette,
                       notes='Original geometry and captured daytime VGA colors. Vertical scale remains provisional; mission objects are not yet rendered.')
        (OUT/f'{slug}.json').write_text(json.dumps(dataset,separators=(',', ':')))
        entries.append(dict(slug=slug,label=dataset['label']))
    (OUT/'manifest.json').write_text(json.dumps(dict(datasets=entries),indent=2))
    (OUT/'asset-names.json').write_text(json.dumps([dict(record=r.index,name=r.candidate_name,
        bank=r.source_file,offset=r.offset,length=r.length) for r in records],indent=2))
    print(f'Exported 4 original maps, {len(tiles)} tile entries, '
          f'{sum(len(t["faces"]) for t in tiles)} triangle commands. All 84 filename keys verified.')
    from export_original_objects import main as export_objects
    export_objects()


if __name__ == '__main__':
    main()
