"""Scene checks: the port's frames against DOSBox captures of the original (CLAUDE.md, Verification 2).

Runs gunboat.exe headless (SDL_VIDEO_DRIVER=dummy) with GB_SNAPSHOT_DIR, optionally scripted keys
(GB_KEYS), for a number of seconds; then, for each DOSBox capture given, finds the port snapshot
that matches it best. DOSBox captures are 640x400 (each VGA pixel doubled); they are reduced to
320x200 by taking every other pixel.

The DOSBox build that made the captures turns the 6-bit DAC values into 8-bit colours with its own,
non-linear table (e.g. 20 -> 82, 30 -> 121), so the RGB values cannot be compared. The score is the
share of pixels that agree under a one-to-one mapping of colours (the same picture drawn with the
same palette indices): each capture colour is paired with the port colour it most often meets, and
a pixel counts if its pair agrees and no two capture colours share a port colour. The largest
channel distance between paired colours is reported as a check that the pairs are the same colours.

usage: scene_check.py [--seconds N] [--keys SPEC] [--captures GLOB] [--no-run] [--out DIR]
  default captures: the title sequence, reverse_engineering/out/dosbox_captures/gb_00[0-3].png
Writes a side-by-side image per capture into --out (default gunboat-port/out/scenes).
"""
import argparse
import glob
import os
import pathlib
import shutil
import subprocess
import sys

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[3]
PORT = ROOT / 'gunboat-port'
CAPTURES = ROOT / 'reverse_engineering' / 'out' / 'dosbox_captures'


def to_vga(path):
    im = Image.open(path).convert('RGB')
    if im.size == (640, 400):
        im = im.resize((320, 200), Image.NEAREST)
    return im


def same_pixels(a, b):
    """(share of pixels agreeing under a one-to-one colour mapping, largest channel distance)"""
    pa, pb = a.load(), b.load()
    pairs = {}
    for y in range(200):
        for x in range(320):
            key = (pa[x, y], pb[x, y])
            pairs[key] = pairs.get(key, 0) + 1
    best = {}
    for (ca, cb), n in pairs.items():
        if n > best.get(ca, (None, 0))[1]:
            best[ca] = (cb, n)
    used, same, dist = {}, 0, 0
    for ca, (cb, n) in sorted(best.items(), key=lambda t: -t[1][1]):
        if cb in used:
            continue  # not one-to-one: the smaller group does not count
        used[cb] = ca
        same += n
        dist = max(dist, max(abs(u - v) for u, v in zip(ca, cb)))
    return same / 64000, dist


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seconds', type=float, default=40)
    ap.add_argument('--keys', default='')
    ap.add_argument('--captures', default=str(CAPTURES / 'gb_00[0-3].png'))
    ap.add_argument('--no-run', action='store_true', help='compare the snapshots of the last run only')
    ap.add_argument('--out', default=str(PORT / 'out' / 'scenes'))
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    snaps = out / 'snapshots'
    if not a.no_run:
        shutil.rmtree(snaps, ignore_errors=True)
        snaps.mkdir(parents=True)
        env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy', GB_SNAPSHOT_DIR=str(snaps))
        if a.keys:
            env['GB_KEYS'] = a.keys
        exe = PORT / 'build' / 'gunboat.exe'
        try:
            subprocess.run([str(exe), '--game-dir', str(ROOT / 'Game'),
                        '--no-launcher', '--original', '--window'], env=env, timeout=a.seconds)
        except subprocess.TimeoutExpired:
            pass
    shots = sorted(snaps.glob('snap*.bmp'))
    if not shots:
        sys.exit('no snapshots in %s' % snaps)
    port_frames = [(p, to_vga(p)) for p in shots]
    worst = 1.0
    for cap in sorted(glob.glob(a.captures)):
        ref = to_vga(cap)
        best = max(((same_pixels(ref, im), p, im) for p, im in port_frames), key=lambda t: t[0][0])
        (score, dist), path, im = best
        worst = min(worst, score)
        side = Image.new('RGB', (640, 200))
        side.paste(ref, (0, 0))
        side.paste(im, (320, 0))
        side.save(out / ('%s_vs_port.png' % pathlib.Path(cap).stem))
        print('%-12s best %-14s %6.2f%% of pixels agree (colours paired one-to-one, largest channel '
              'difference %d)' % (pathlib.Path(cap).name, path.name, 100 * score, dist))
    return 0 if worst == 1.0 else 1


if __name__ == '__main__':
    sys.exit(main())
