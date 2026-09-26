"""Checks of the enhanced presentation (src/enhanced/): the port run headless (SDL_VIDEO_DRIVER=dummy)
into practice missions with every enhancement on.

For every frame of a 3D station the frame hook captures, the check (GB_VIEW_CHECK=1) draws the view
again at the original's size with the enhanced renderer and compares it with the original's own
pixels (the gun sprites drawn over the view left out), and proves that the capture left the game's
memory exactly as it found it. The present statistics (GB_PRESENT_STATS=1) show the game's frame rate
and the presents with the view. Passes when:
  * no capture changed the game's memory,
  * the re-drawn view matches the original on at least MIN_EQUAL of its pixels (the rest: sub-pixel
    edges of the terrain and the original's bit-pattern sprite scaling, which a smooth scale cannot
    reproduce pixel for pixel),
  * the game keeps its 15 frames per second and the display presents about 60 with the view (smooth
    motion).
The window snapshots (hdNNNN.bmp) and the view comparisons (viewNNN_orig/1x/4x.bmp) are kept in
--out for a look.

usage: scene_enhanced.py [--out DIR]"""
import argparse
import os
import pathlib
import re
import subprocess
import sys

from scene_check import PORT, ROOT

INTRO_KEYS = '3:39,6:39,9:39,12:39,15:39,18:39,21:39'
# (name, keys after the menu, seconds): pilot practice (the menu's bottom-right item) looking left,
# right and ahead, then the chase view; gunnery practice (bottom-left) at the bow station.
SCENES = [
    ('pilot', '26:e050,27:e04d,29:1c,30:48p,33:48r,33.5:2c,37:2e,41:2d,45:33', 50),
    ('gunnery', '26:e050,29:1c,33:2f,37:4bp,40:4br,41:39', 46),
]
MIN_EQUAL = 93.0


def run(name, keys, seconds, out):
    d = out / ('enhanced_' + name)
    d.mkdir(parents=True, exist_ok=True)
    for f in d.glob('*.bmp'):
        f.unlink()
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy', GB_SNAPSHOT_DIR=str(d),
               GB_VIEW_CHECK='1', GB_VIEW_CHECK_DIR=str(d), GB_PRESENT_STATS='1', GB_KEYS=INTRO_KEYS + ',' + keys)
    try:
        p = subprocess.run([str(PORT / 'build' / 'gunboat.exe'), '--game-dir', str(ROOT / 'Original DOS version'),
                            '--no-launcher', '--enhanced'], env=env, timeout=seconds, capture_output=True, text=True)
        text = p.stdout
    except subprocess.TimeoutExpired as e:
        text = e.stdout.decode() if isinstance(e.stdout, bytes) else (e.stdout or '')
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=str(PORT / 'out' / 'scenes'))
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    failed = False
    for name, keys, seconds in SCENES:
        text = run(name, keys, seconds, out)
        checks = re.findall(r'view check: (\d+) frames, (\d+) pixels, ([\d.]+)% equal \(worst frame ([\d.]+)%\), '
                            r'memory changed by (\d+) captures', text)
        presents = re.findall(r'present: \d+x\d+, ([\d.]+)/s \((\d+) with the view, (\d+) between frames\), '
                              r'([\d.]+) ms each; game ([\d.]+) frames/s', text)
        if not checks or not presents:
            print(f'{name}: FAILED, no statistics (did the mission start?)')
            failed = True
            continue
        frames, pixels, equal, worst, changed = checks[-1]
        steady = [p for p in presents if int(p[1]) > 0 and float(p[4]) > 12]
        rate = min((float(p[0]) for p in steady), default=0.0)
        game = [float(p[4]) for p in steady]
        ok = (int(changed) == 0 and float(equal) >= MIN_EQUAL and steady and rate >= 50 and
              all(13.5 <= g <= 16.5 for g in game))
        failed |= not ok
        print(f'{name}: {"ok" if ok else "FAILED"}: {frames} frames checked, {float(equal):.2f}% of {pixels} view '
              f'pixels equal to the original (worst frame {float(worst):.2f}%), memory changed by {changed} '
              f'captures; {len(steady)} periods of 5 s: at least {rate:.1f} presents/s, game '
              f'{min(game, default=0):.1f}-{max(game, default=0):.1f} frames/s')
    print(f'snapshots in {out}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
