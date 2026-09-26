"""Scene checks of the mission (CLAUDE.md, Verification 2): the port run headless into pilot practice,
its frames against the DOSBox captures of the original (reverse_engineering/out/dosbox_captures).

The captures were taken while playing, so no port frame can be the same moment: the check pairs the
colours one-to-one (scene_check.same_pixels), takes the best port frame for each capture, and reports
where the differing pixels are: the message line (rows 0-11), the 3D view (12-87: water marks,
shimmer, objects that moved) and the cockpit (88-199: the clock digits and gauges that moved). A
difference elsewhere in the cockpit (the panel art, the lamps, the switches) is a fault.

usage: scene_mission.py [--no-run] [--out DIR]
Writes the side-by-side and difference images into --out (default gunboat-port/out/scenes)."""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys

from PIL import Image

from scene_check import CAPTURES, PORT, ROOT, same_pixels, to_vga

# (name, capture, keys after the menu, seconds, moving pixels allowed anywhere): pilot practice is
# the menu's bottom-right item; on the map (M) the boat marker blinks at the boat's position, which
# differs between the DOSBox session and the port's run (a cross of at most 40 pixels).
SCENES = [
    ('pilot', 'gb_004.png', '26:e050,27:e04d,29:1c', 50, 0),
    ('map', 'gb_082.png', '26:e050,27:e04d,29:1c,40:32', 50, 40),
]
INTRO_KEYS = '3:39,6:39,9:39,12:39,15:39,18:39,21:39'
# rows where differences are expected between two moments of the same mission
ALLOWED = [(0, 11, 'message line'), (12, 87, '3D view'), (170, 190, 'clock and heading digits')]


def run(name, keys, seconds, out):
    snaps = out / ('snapshots_' + name)
    shutil.rmtree(snaps, ignore_errors=True)
    snaps.mkdir(parents=True)
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy', GB_SNAPSHOT_DIR=str(snaps),
               GB_KEYS=INTRO_KEYS + ',' + keys)
    try:
        subprocess.run([str(PORT / 'build' / 'gunboat.exe'), '--game-dir', str(ROOT / 'Original DOS version'),
                        '--no-launcher', '--original'],
                       env=env, timeout=seconds)
    except subprocess.TimeoutExpired:
        pass
    return sorted(snaps.glob('snap*.bmp'))


def compare(ref, im):
    """(differing pixel count by region, difference image) under the one-to-one colour pairing."""
    pa, pb = ref.load(), im.load()
    pairs = {}
    for y in range(200):
        for x in range(320):
            pairs[(pa[x, y], pb[x, y])] = pairs.get((pa[x, y], pb[x, y]), 0) + 1
    best = {}
    for (a, b), n in pairs.items():
        if n > best.get(a, (None, 0))[1]:
            best[a] = (b, n)
    diff = Image.new('RGB', (320, 200))
    d = diff.load()
    regions = {}
    for y in range(200):
        for x in range(320):
            if best[pa[x, y]][0] != pb[x, y]:
                d[x, y] = (255, 0, 0)
                where = next((label for y0, y1, label in ALLOWED if y0 <= y <= y1), 'cockpit (unexpected)')
                regions[where] = regions.get(where, 0) + 1
    return regions, diff


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--no-run', action='store_true')
    ap.add_argument('--out', default=str(PORT / 'out' / 'scenes'))
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    failed = False
    for name, capture, keys, seconds, moving in SCENES:
        shots = sorted((out / ('snapshots_' + name)).glob('snap*.bmp')) if a.no_run else run(name, keys, seconds, out)
        ref = to_vga(CAPTURES / capture)
        scored = [(same_pixels(ref, to_vga(p))[0], p) for p in shots]
        score, path = max(scored)
        im = to_vga(path)
        regions, diff = compare(ref, im)
        if name == 'map':  # the whole screen is the map: only the marker may differ
            regions = {'boat marker': sum(regions.values())} if sum(regions.values()) <= moving else regions
        side = Image.new('RGB', (960, 200))
        side.paste(ref, (0, 0))
        side.paste(im, (320, 0))
        side.paste(diff, (640, 0))
        side.save(out / ('%s_vs_port.png' % name))
        print('%-6s %-11s best %-13s %6.2f%% of pixels agree; differing: %s' % (
            name, capture, path.name, 100 * score, ', '.join('%s %d' % kv for kv in sorted(regions.items())) or 'none'))
        if regions.get('cockpit (unexpected)', 0) > 0:
            failed = True
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
