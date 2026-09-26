"""The EGA (mode 0Dh) paths of the renderer and of the view copies (render3d.md §1.4, hud.md §6.1), on
the EGA machine: blit_rows_ega, ega_gc_setup, spotlight_beam_ega, sky_water_ega, water_marks_ega,
span_ega_a/b, view_copy_1..8_ega and view_copy_head_ega, the far view_copy_1..8 on EGA pages, and
terrain_frame / object_frame whole (their EGA plane set-ups and the dispatch to the EGA twins).

States: the original run in EGA from main into a mission (GUNBOAT.CFG saying mode 0Dh, in a temporary
copy of the game folder for the original's side; test_render's BASES and moved variants), stopped at
game_frame; the memory and the card (planes, latches, registers) there. The routines are checked
where the original's frames reach them (a snapshot of registers, memory and card at each entry), and
on those snapshots with randomised inputs: the variables, the planes, the latches and the graphics
controller's registers (so that set/reset, bit mask, latches and write mode all matter). All memory
and the whole card state are compared (the A000 window as the planes)."""
import atexit
import pathlib
import shutil
import struct
import tempfile

from unicorn import UC_HOOK_CODE

import cardmodel
import test_render
from gbdiff import DS_BASE, GAME_DIR, Mismatch, REGS, UC_REGS, lin, put8, put16, randomize, seg_of
from mission_states import mission_state
from test_modes import machine
from test_render import BASES, BIG, PICKS
from test_sound import sound

EGA_PAGES = (0xA000, 0xA200, 0xA400)   # pages 0, 1, 2 in mode 0Dh (A000h + 200h per page)
ROW_TABLE = 0xD74D                     # view_row_table: 0A05h + 28h per view row
WIDTHS = 0x709A                        # CS:709A, the spotlight widths (10 per elevation step)

_EGA_DIR = []


def get8(m, off):
    return m[DS_BASE + off]


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def ega_dir():
    """A temporary copy of the game folder whose GUNBOAT.CFG selects mode 0Dh (removed at exit)."""
    if not _EGA_DIR:
        tmp = pathlib.Path(tempfile.mkdtemp(prefix='gbdiff_ega_'))
        for f in GAME_DIR.iterdir():
            if f.is_file() and f.name != 'GUNBOAT.CFG':
                shutil.copy(f, tmp / f.name)
        (tmp / 'GUNBOAT.CFG').write_bytes(bytes([0x0D, 0, 0, 0, 0, 0]))   # EGA, no joystick, two pages
        atexit.register(shutil.rmtree, tmp, True)
        _EGA_DIR.append(tmp)
    return _EGA_DIR[0]


def ega_mission(h, **kw):
    """(memory, card state) of mission_state(h, **kw) with the original run in EGA from a reset
    card. Cached per harness (the card with the memory)."""
    cache = h.__dict__.setdefault('_ega_states', {})
    key = tuple(sorted((k, tuple(v) if isinstance(v, list) else v) for k, v in kw.items()))
    if key not in cache:
        saved = h.__dict__.get('_mission_states')
        h._mission_states = {}                 # (mission_state's own cache does not keep the card)
        h.set_game_dirs(ega_dir(), GAME_DIR)
        try:
            h.card.s[:] = cardmodel.reset_state()
            m = mission_state(h, **kw)
            cache[key] = (bytes(m), bytes(h.card.s))
        finally:
            h.set_game_dirs(GAME_DIR)
            if saved is None:
                del h._mission_states
            else:
                h._mission_states = saved
    m, card = cache[key]
    if get8(m, 0xEED2) != 0x0D:
        raise Mismatch('the EGA mission state is not in mode 0Dh')
    return bytearray(m), card


def ega_frame_states(h, rng, scale, variants=6):
    """The BASES in EGA and moved variants of them (test_render.moved: another place, chase view,
    night, lights): (label, memory, card)."""
    out = [(str(BASES[i]), *ega_mission(h, **BASES[i])) for i in range(len(BASES))]
    for v in range(variants * scale):
        i = rng.randrange(len(BASES))
        kind = v % 4
        m, card = ega_mission(h, **BASES[i])
        saved = test_render.base
        test_render.base = lambda _h, _i: bytearray(m)
        try:
            mm = test_render.moved(h, rng, i, chase=1 if kind == 1 else None, night=kind == 2 or None,
                                   lights=kind == 3)
        finally:
            test_render.base = saved
        out.append(('moved %d from %d (%s)' % (v, i, ['plain', 'chase', 'night', 'lights'][kind]), mm, card))
    return out


def ega_capture(h, m, card, target, picks, caller='game_frame'):
    """(registers, memory, card) where the original's `caller`, run from m and card, enters `target`
    for the calls numbered in picks."""
    file_seg, off, _ = h.sym.func(target)
    at = lin(seg_of(file_seg), off)
    picks = set(picks)
    last = max(picks)
    out, count = [], [0]

    def hook(uc, address, size, _):
        k = count[0]
        count[0] += 1
        if k in picks:
            out.append(({n: uc.reg_read(UC_REGS[n]) for n in REGS}, bytearray(h.orig.memory()), bytes(h.card.s)))
            if k >= last:
                h.orig.fail('stop')
    hk = h.orig.uc.hook_add(UC_HOOK_CODE, hook, None, at, at)
    try:
        with sound(h):
            h.card.s[:] = card
            h.orig.set_memory(m)
            fs, fo, far = h.sym.func(caller)
            h.orig.call(fs, fo, far, {}, (), max_insns=BIG)
    except Mismatch as e:
        if str(e) != 'original: stop':
            raise
    finally:
        h.orig.uc.hook_del(hk)
    return out


def check(h, name, m, card, regs=None, outputs=(), stack_args=(), label=''):
    h.card_state = card
    try:
        h.check(name, m, regs=regs or {}, outputs=outputs, stack_args=stack_args, label=label, max_insns=BIG)
    finally:
        h.card_state = None


def shuffle_card(card, rng, planes=True, registers=True):
    """The card with random planes and latches and random graphics controller / sequencer registers
    (indices kept in range)."""
    s = bytearray(card)
    if planes:
        s[cardmodel.PLANES:cardmodel.PLANES + 0x40000] = rng.randbytes(0x40000)
        s[cardmodel.LATCH:cardmodel.LATCH + 4] = rng.randbytes(4)
    if registers:
        s[cardmodel.GC:cardmodel.GC + 9] = rng.randbytes(9)
        s[cardmodel.SEQ + 2] = rng.randrange(256)
        s[cardmodel.GC_INDEX] = rng.randrange(9)
        s[cardmodel.SEQ_INDEX] = rng.randrange(5)
    return bytes(s)


def draw_card(card, rng):
    """A card as the frame's plane set-up leaves it (map mask 0Fh, write mode 0, set/reset on every
    plane, function replace) most of the time, else random registers; random planes and latches."""
    s = bytearray(shuffle_card(card, rng))
    if rng.random() < 0.7:
        s[cardmodel.SEQ + 2] = 0x0F
        s[cardmodel.GC + 5] = rng.choice([0, 0, 0x10, 0x08])
        s[cardmodel.GC + 1] = 0x0F
        s[cardmodel.GC + 3] = 0
    return bytes(s)


def captured(h, rng, scale, target, picks, caller='game_frame', variants=6, need=1):
    n = 0
    for label, m, card in ega_frame_states(h, rng, scale, variants):
        for k, (regs, snap, c) in enumerate(ega_capture(h, m, card, target, picks, caller)):
            n += 1
            yield '%s, call %d' % (label, k), regs, snap, c
    if n < need:
        raise Mismatch('%s: only %d captured cases' % (target, n))


# ---------------------------------------------------------------- the renderer's EGA twins

@machine('ega')
def test_ega_gc_setup(h, rng, scale):
    m, card = ega_mission(h, practice=1)
    n = 0
    for k in range(40 * scale):
        check(h, 'ega_gc_setup', m, shuffle_card(card, rng, planes=k % 4 == 0), label='case %d' % k)
        n += 1
    return n


@machine('ega')
def test_sky_water_ega(h, rng, scale):
    """sky_water_ega where terrain_setup calls it (AX = sky, BL = horizon, CL = the rows already
    covered), then random horizons, tops (above and below the horizon, the 8-bit wraps), colours,
    the flash, planes and registers."""
    n = 0
    for label, regs, snap, card in captured(h, rng, scale, 'sky_water_ega', [0], variants=4):
        check(h, 'sky_water_ega', snap, card, regs=regs, outputs=['di'], label=label)
        n += 1
        for i in range(8 * scale):
            m = bytearray(snap)
            r = dict(regs)
            bl = rng.choice([0, 1, 0x1F, 0x3C, 0x3D, rng.randrange(0x3E), rng.randrange(0x3E), rng.randrange(256)])
            cl = rng.choice([0, bl, rng.randrange(bl + 1), rng.randrange(256)])
            r['bx'] = rng.randrange(0x100) << 8 | bl
            r['cx'] = rng.randrange(0x100) << 8 | cl
            r['ax'] = rng.randrange(0x10000)
            put8(m, 0xD953, rng.choice([bl, bl, rng.randrange(0x3F), 0x3E, 0x3F, 0x40, rng.randrange(256)]))
            put8(m, 0xD950, rng.randrange(256))
            put8(m, 0xD9B5, rng.choice([0, 0, 1, rng.randrange(256)]))
            check(h, 'sky_water_ega', m, draw_card(card, rng), regs=r, outputs=['di'], label='%s random %d' % (label, i))
            n += 1
    return n


@machine('ega')
def test_water_marks_ega(h, rng, scale):
    """water_marks_ega where terrain_setup calls it, then random marks (columns, ages across the size
    thresholds 0Bh/11h/17h/1Bh), first mark and BH, row counts, start rows (the 13DDh end) and
    colours on random planes."""
    n = 0
    for label, regs, snap, card in captured(h, rng, scale, 'water_marks_ega', [0], variants=4):
        check(h, 'water_marks_ega', snap, card, regs=regs, label=label)
        n += 1
        for i in range(10 * scale):
            m = bytearray(snap)
            r = dict(regs)
            for k in range(0x20):
                put8(m, 0xD90D + k, rng.randrange(256))
                put8(m, 0xD92D + k, rng.choice([0, 0x0A, 0x0B, 0x10, 0x11, 0x16, 0x17, 0x1A, 0x1B, 0x1F,
                                                rng.randrange(0x20), rng.randrange(256)]))
            r['ax'] = rng.randrange(0x10000)
            r['bx'] = (rng.choice([0, 0, 0, 1, rng.randrange(256)]) << 8) | rng.randrange(0x20)
            r['cx'] = rng.choice([0x20, 0x20, rng.randrange(1, 0x41), rng.randrange(0x10000)])
            r['dx'] = rng.randrange(0x10000)
            row = rng.choice([0x1E, 0x3F, 0x3E, rng.randrange(0x40)])
            r['di'] = rng.choice([get16(m, ROW_TABLE + 2 * row), get16(m, ROW_TABLE + 2 * row),
                                  0x13DC, 0x13DD, rng.randrange(0x1400)])
            check(h, 'water_marks_ega', m, draw_card(card, rng), regs=r, label='%s random %d' % (label, i))
            n += 1
    return n


def span_mutate(m, rng):
    """Random span state: first row (the view rows, rows past it, the ones that wrap onto the view),
    rows, edges and steps (whole view, the wrapped left part, off the view), colour."""
    put8(m, 0xD8FC, rng.choice([0x40, 0x5F, 0x7F, 0x80, 0xBF, 0xC0, 0xFF, rng.randrange(0x40, 0x80),
                                rng.randrange(0x30, 0x90), rng.randrange(256)]))
    put8(m, 0xB7E2, rng.choice([1, 2, 3, rng.randrange(1, 0x20), rng.randrange(1, 0x50)]))

    def bearing():
        return rng.choice([rng.randrange(0x10000), rng.randrange(0x8000), rng.randrange(0xC000, 0x10000),
                           rng.randrange(0x7F00, 0x8100), (rng.randrange(0x200) << 7) | rng.randrange(0x80)])
    put16(m, 0xD956, bearing())
    put16(m, 0xD958, bearing())
    for off in (0xD95A, 0xD95C):
        put16(m, off, rng.choice([0, 0x80, 0xFF80, rng.randrange(0x400), -rng.randrange(0x400),
                                  rng.randrange(0x10000)]) & 0xFFFF)
    c = rng.randrange(256)
    put16(m, 0xD954, c << 8 | c)


def span_test(h, rng, scale, name):
    n = 0
    for label, regs, snap, card in captured(h, rng, scale, name, [0, 4, 16, 64, 128], variants=4):
        check(h, name, snap, card, regs=regs, label=label)
        n += 1
    base, card = ega_mission(h, practice=1)
    es = get16(base, 0xD9B8)
    for i in range(300 * scale):
        m = bytearray(base)
        span_mutate(m, rng)
        check(h, name, m, draw_card(card, rng), regs={'es': rng.choice([es, es, es, 0xA000])}, label='random %d' % i)
        n += 1
    return n


@machine('ega')
def test_span_ega_a(h, rng, scale):
    """span_ega_a ([D8F8] in EGA) where the original's triangles reach it, then random spans."""
    return span_test(h, rng, scale, 'span_ega_a')


@machine('ega')
def test_span_ega_b(h, rng, scale):
    """span_ega_b ([D8FA] in EGA) where the original's lines reach it, then random lines."""
    return span_test(h, rng, scale, 'span_ega_b')


@machine('ega')
def test_spotlight_beam_ega(h, rng, scale):
    """spotlight_beam_ega where the original's lights reach it (night states with the lights on),
    then random beams: first rows and row counts (the 40h end), width rows of the table (and other
    code bytes), columns across both edges of the view."""
    n = 0
    for label, regs, snap, card in captured(h, rng, scale, 'spotlight_beam_ega', [0, 1, 2], variants=16, need=0):
        check(h, 'spotlight_beam_ega', snap, card, regs=regs, label=label)
        n += 1
    base, card = ega_mission(h, practice=1)
    for i in range(300 * scale):
        m = bytearray(base)
        put8(m, 0xB7E2, rng.choice([0, 0x10, 0x2D, 0x36, 0x3F, 0x40, 0x41, rng.randrange(0x40), rng.randrange(256)]))
        put8(m, 0xB7E3, rng.choice([10, 10, 1, 2, rng.randrange(256)]))
        bx = rng.choice([WIDTHS + 10 * rng.randrange(0x13), WIDTHS + 10 * rng.randrange(0x13), rng.randrange(0x10000)])
        dx = rng.choice([(rng.randrange(-0x80, 0x80) * 2) & 0xFFFF, rng.randrange(-0x40, 0x140) & 0xFFFF,
                         rng.randrange(0x10000)])
        check(h, 'spotlight_beam_ega', m, draw_card(card, rng), regs={'es': get16(m, 0xD9B8), 'bx': bx, 'dx': dx},
              label='random %d' % i)
        n += 1
    return n


@machine('ega')
def test_blit_rows_ega(h, rng, scale):
    """blit_rows_ega where the original's sprites reach it, then those snapshots with random rows,
    columns (bit positions), left clips, widths, repeat counts and pixels on random planes."""
    n = 0
    for label, regs, snap, card in captured(h, rng, scale, 'blit_rows_ega', PICKS[:9], variants=6):
        check(h, 'blit_rows_ega', snap, card, regs=regs, label=label)
        n += 1
        for i in range(3 * scale):
            m = bytearray(snap)
            r = dict(regs)
            r['ax'] = rng.randrange(256) << 8 | rng.choice([0, 0x10, 0x3F, 0x4F, 0x50, rng.randrange(0x50),
                                                           rng.randrange(256)])
            put8(m, 0xB7F7, rng.randrange(256))
            put16(m, 0xD873, rng.choice([0, 1, rng.randrange(8)]))
            put16(m, 0xD875, rng.choice([0, 0, rng.randrange(1, 0x28)]))
            put8(m, 0xD862, rng.choice([1, 7, 8, 9, rng.randrange(1, 0x40)]))
            put8(m, 0xB7F9, rng.choice([1, rng.randrange(1, 0x30)]))
            put8(m, 0xD86F, rng.choice([1, 2, rng.randrange(256)]))
            seg = get16(m, 0xD887)
            randomize(m, (seg << 4) + r['si'], 0x400, rng)
            check(h, 'blit_rows_ega', m, draw_card(card, rng), regs=r, label='%s random %d' % (label, i))
            n += 1
    return n


# ---------------------------------------------------------------- the frame routines

@machine('ega')
def test_terrain_frame_ega(h, rng, scale):
    """terrain_frame in EGA where game_frame calls it (the plane set-up, sky and water, marks, group B
    through span_ega_a/b), on the captured card and on random planes and registers."""
    n = 0
    for label, regs, snap, card in captured(h, rng, scale, 'terrain_frame', [0], variants=8):
        check(h, 'terrain_frame', snap, card, label=label)
        check(h, 'terrain_frame', snap, shuffle_card(card, rng), label=label + ' (random card)')
        n += 2
    return n


@machine('ega')
def test_object_frame_ega(h, rng, scale):
    """object_frame in EGA where game_frame calls it (the plane set-up, group A through the spans,
    the sprites through blit_rows_ega, the spotlights)."""
    n = 0
    for label, regs, snap, card in captured(h, rng, scale, 'object_frame', [0], variants=8):
        check(h, 'object_frame', snap, card, regs=regs, label=label)
        check(h, 'object_frame', snap, shuffle_card(card, rng), regs=regs, label=label + ' (random card)')
        n += 2
    return n


# ---------------------------------------------------------------- the view copies

@machine('ega')
def test_view_copies_ega(h, rng, scale):
    """view_copy_1..8 on the EGA pages (page_segments A000h/A200h/A400h): the dispatch and the EGA
    twins on random planes, latches and registers; then the twins by themselves (the harness runs
    them with DS = DGROUP: RAM read, the latches of the last read written) with ES = each page."""
    n = 0
    base, card = ega_mission(h, practice=1)
    for k in range(1, 9):
        for src, dst in [(1, 0), (1, 2), (2, 0), (0, 1), (1, 1)]:
            for _ in range(2 * scale):
                m = bytearray(base)
                check(h, 'view_copy_%d' % k, m, shuffle_card(card, rng), stack_args=[src, dst], outputs=['si'],
                      regs={'si': rng.randrange(0x10000)}, label='pages %d -> %d' % (src, dst))
                n += 1
    for name in ['view_copy_%d_ega' % k for k in range(1, 9)] + ['view_copy_head_ega']:
        for es in EGA_PAGES + (0x2B73,):
            m = bytearray(base)
            randomize(m, DS_BASE, 0xF640, rng)
            check(h, name, m, shuffle_card(card, rng), regs={'es': es, 'ax': rng.randrange(0x10000),
                                                             'cx': rng.randrange(0x10000),
                                                             'dx': rng.randrange(0x10000),
                                                             'bx': rng.randrange(0x10000)},
                  outputs=['di', 'si', 'cx'] if name == 'view_copy_head_ega' else ['si'], label='ES %04X' % es)
            n += 1
    return n


@machine('ega')
def test_game_frame_ega(h, rng, scale):
    """game_frame whole in EGA on the BASES and moved variants. It needs the graphics library's EGA
    paths (gfx_set_draw_page: page p at A000h + 200h p): on the video-ega branch alone 8 of the 10
    BASES pass and the two others differ only by gfx_draw_page / gfx_draw_seg and what the frame
    then draws on the wrong page. Not in TESTS until the library package is merged."""
    n = 0
    for label, m, card in ega_frame_states(h, rng, scale, variants=6):
        check(h, 'game_frame', m, card, label=label)
        n += 1
    return n


TESTS = [test_ega_gc_setup, test_sky_water_ega, test_water_marks_ega, test_span_ega_a, test_span_ega_b,
         test_spotlight_beam_ega, test_blit_rows_ega, test_terrain_frame_ega, test_object_frame_ega,
         test_view_copies_ega]
# After the graphics library's EGA paths are merged: TESTS.append(test_game_frame_ega)
