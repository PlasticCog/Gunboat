"""Scene check of the station screens that a DOSBox capture shows (CLAUDE.md, Verification 2), until
the port runs a mission itself: the port's map_screen (gb_difftest DLL) draws the tactical map on the
memory of a mission started by the original (tests/difftest/test_hud.station_state), and the screen
(page 0 through the port's DAC) is compared with the capture as scene_check.py compares frames: the
share of pixels that agree under a one-to-one pairing of colours. The capture also shows the markers
that mission_run draws on the map every frame (the boat and the objective), which map_screen does not.

usage: scene_hud.py [--capture PATH]     (default reverse_engineering/out/dosbox_captures/gb_082.png)
Writes gunboat-port/out/scenes/<capture>_vs_map_screen.png (capture left, port right)."""
import argparse
import ctypes
import pathlib
import sys

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'difftest'))
import gbdiff  # noqa: E402
from scene_check import CAPTURES, PORT, same_pixels, to_vga  # noqa: E402
from test_hud import screen_state  # noqa: E402

# the map screens of a mission per region (the practice missions use region 3)
MISSIONS = [dict(region=0, mission=3, rank=5), dict(region=1, mission=5, rank=9), dict(region=2, mission=4, rank=9),
            dict(practice=3)]


def port_screen(h, m):
    """Runs the port's map_screen on m. Returns the screen (A000:0000) as an image with one colour per
    palette index (for the pairing: the DAC entries the screen does not set are all black) and as
    an image through the port's DAC (to look at)."""
    h.port.set_memory(m)
    h.port.dll.gb_dac_write(bytes(768))
    h.port.call('map_screen', {})
    pixels = h.port.memory()[0xA0000:0xA0000 + 64000]
    dac = ctypes.create_string_buffer(768)
    h.port.dll.gb_dac_read(dac)
    images = []
    for pal in ([c for i in range(256) for c in (i, (i * 37) & 255, 255 - i)],
                [min(255, v * 255 // 63) for v in dac.raw]):
        im = Image.new('P', (320, 200))
        im.putpalette(pal)
        im.putdata(pixels)
        images.append(im.convert('RGB'))
    return images


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--capture', default=str(CAPTURES / 'gb_082.png'))
    a = ap.parse_args()
    ref = to_vga(a.capture)
    h = gbdiff.Harness()
    results = []
    for kw in MISSIONS:
        m = screen_state(h, 'map_screen', 'm', **kw)
        by_index, im = port_screen(h, m)
        score = same_pixels(ref, by_index)[0]
        results.append((score, kw, im))
        print('%-40s %6.2f%% of pixels agree (palette indices paired one-to-one with the capture colours)'
              % (kw, 100 * score))
    score, kw, im = max(results, key=lambda t: t[0])
    out = PORT / 'out' / 'scenes'
    out.mkdir(parents=True, exist_ok=True)
    side = Image.new('RGB', (640, 200))
    side.paste(ref, (0, 0))
    side.paste(im, (320, 0))
    side.save(out / ('%s_vs_map_screen.png' % pathlib.Path(a.capture).stem))
    print('best: %s, %.2f%%' % (kw, 100 * score))
    return 0


if __name__ == '__main__':
    sys.exit(main())
