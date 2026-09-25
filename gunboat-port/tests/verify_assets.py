"""Compare compiled C++ output with independent DOS execution / research fixtures.

Development-only dependencies: Python, Pillow, Unicorn. The native game uses none
of these. Run from any directory after building gunboat.exe.
"""
import csv
import json
from pathlib import Path
import subprocess
import sys

PORT = Path(__file__).resolve().parents[1]
ROOT = PORT.parent
sys.path.insert(0, str(ROOT / "reverse_engineering/tools"))
from gunboat_formats import read_datac, asset_path, td3_lzw_decode, UNPACKED_IMAGE
from probe_original_sprites import render


def rotate(x, y, angle):
    return ((x, y), (y, 128-x), (128-x, 128-y), (128-y, x))[angle]


def main():
    binary = PORT / "build/gunboat.exe"
    output = PORT / "out/native"
    subprocess.run([str(binary), "--game-dir", str(ROOT / "Original DOS version"),
                    "--dump-assets", str(output)], check=True)
    image = (output / "unpacked.bin").read_bytes()
    assert image == UNPACKED_IMAGE.read_bytes()[:len(image)], "EXEPACK image differs"
    records = read_datac()
    for name in ("BF1.LZ", "BF2.LZ", "BG1.LZ", "BG2.LZ", "MAP.LZ", "CLIP.LZ"):
        record = next(r for r in records if r.candidate_name == name)
        assert (output / (name + ".decoded")).read_bytes() == td3_lzw_decode(asset_path(record).read_bytes()), name
    tiles = json.loads((ROOT / "reverse_engineering/out/world_export/original-tiles.json").read_text())
    count = objects = triangles = lines = 0
    for world, bank in enumerate((77, 79, 81, 75), 1):
        data = json.loads((ROOT / f"reverse_engineering/out/world_export/original-map-{world}.json").read_text())
        with (output / f"world{world}-objects.csv").open() as stream:
            actual = list(csv.DictReader(stream))
        expected = data["objects"]
        assert len(actual) == len(expected)
        for a, b in zip(actual, expected):
            for key in ("kind", "heading", "index", "x", "y"):
                assert int(a[key]) == b[key], (world, key, a, b)
            assert bool(int(a["scenery"])) == (b["source"] == "tile")
            assert int(a["cell"]) == b.get("cell", -1)
        objects += len(actual)
        want_faces, want_lines = [], []
        for i, cell in enumerate(data["cells"]):
            tile = tiles[cell & 63]
            vertices = []
            for x, h, y in tile["vertices"]:
                rx, ry = rotate(x, y, cell >> 6)
                vertices.append(((i % 17 * 128 + rx - 1088)/8, h/32,
                                 (i // 17 * 128 + 128 - ry - 704)/8))
            for face in tile["faces"]:
                want_faces.append([face[4], *(c for v in face[:3] for c in vertices[v])])
            for line in tile["lines"]:
                want_lines.append([line[3], *(c for v in line[:2] for c in vertices[v])])
        for suffix, expected_rows in (("triangles", want_faces), ("lines", want_lines)):
            with (output / f"world{world}-{suffix}.csv").open() as stream:
                got = [[float(v) for v in row] for row in csv.reader(stream)]
            assert got == expected_rows, (world, suffix)
        triangles += len(want_faces)
        lines += len(want_lines)
        for path in sorted((output / f"world{world}").glob("*.raw")):
            kind, angle = map(int, path.stem.split("_"))
            reference = render(kind, bank=bank, scale=47, angle=angle, screen=True)
            cropped = b"".join(reference[y*320+40:y*320+296] for y in range(64, 128))
            assert path.read_bytes() == cropped, f"World {world} sprite {kind} angle {angle}"
            count += 1
        print(f"World {world}: objects, triangles, lines, and all sprite views match", flush=True)
    report = dict(unpacked_bytes=len(image), pictures=6, objects=objects,
                  triangles=triangles, lines=lines, sprite_views=count, mismatches=0)
    (PORT / "out/asset-verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
