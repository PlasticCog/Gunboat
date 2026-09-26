"""The Tandy (mode 9) twins of the renderer and of the view copies (render3d.md §8, hud.md §6.1), on
the Tandy machine.

States: the original's own, set up in mode 9. The game folder is copied to a temporary folder whose
GUNBOAT.CFG chooses Tandy (09h), so the original's config_load sets mode 9, allocates the Tandy
pages (page 0 the screen at B800h, pages 1 and 2 in RAM) and mission_setup's video_mode_setup makes
the Tandy row table (D74D) and span routines (D8F8/D8FA); then mission_states and
test_render.capture as for VGA: the original's game_frame, stopped where it enters each routine
(its cockpit drawn by the library in mode 9 on the original's side only). The mission states made
here are cached apart from the harness's others. Targeted randomisation on top covers every branch
(clipping, the two nibbles of a byte, the four banks, tables). All memory, the card state and the
listed registers are compared."""
import atexit
import contextlib
import pathlib
import shutil
import struct
import tempfile

from gbdiff import DS_BASE, GAME_DIR, lin, put8, put16, randomize
from mission_states import mission_state
from test_modes import machine
from test_render import BIG, PICKS, check_captured

TANDY_CFG = bytes([9, 0, 0, 0, 0, 0])          # GUNBOAT.CFG: mode 9, no joystick
PAGE_BYTES = 0x80A0                             # a Tandy page as the routines reach it (bank 3 + A0h)
SPRITE_WIDTHS = 0x709A                          # CS:709A spotlight_widths

_dir = None


def get8(m, off):
    return m[DS_BASE + off]


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def tandy_dir():
    """A temporary copy of the game folder with a GUNBOAT.CFG for Tandy (the real one is never
    written); removed at exit."""
    global _dir
    if _dir is None:
        _dir = pathlib.Path(tempfile.mkdtemp(prefix='gb_tandy_'))
        atexit.register(shutil.rmtree, str(_dir), True)
        for f in GAME_DIR.iterdir():
            if f.is_file():
                shutil.copyfile(f, _dir / f.name)
        (_dir / 'GUNBOAT.CFG').write_bytes(TANDY_CFG)
    return _dir


@contextlib.contextmanager
def tandy_game(h):
    """Mission states made inside are the original's in mode 9 (the module docstring)."""
    saved = h.__dict__.get('_mission_states')
    h._mission_states = h.__dict__.setdefault('_tandy_mission_states', {})
    h.set_game_dirs(tandy_dir())
    try:
        yield
    finally:
        h.set_game_dirs(GAME_DIR)
        if saved is None:
            h.__dict__.pop('_mission_states', None)
        else:
            h._mission_states = saved


def tandy_base(h, **kw):
    """A mission state in mode 9 (default: gunnery practice after two frames)."""
    with tandy_game(h):
        m = mission_state(h, **(kw or dict(practice=1, frames=2)))
    if get16(m, 0xEED2) & 0xFF != 9 or get16(m, 0xD8F8) != 0x5693:
        raise AssertionError('not a Tandy state: mode %04X, span routine %04X' % (get16(m, 0xEED2), get16(m, 0xD8F8)))
    return m


def captured(h, rng, scale, target, picks, outputs=(), variants=4, mutate=None, need=1):
    """test_render.check_captured on the states of the original set up in mode 9."""
    with tandy_game(h):
        return check_captured(h, rng, scale, target, picks, outputs=outputs, variants=variants, mutate=mutate,
                              need=need)


def page_segments(m):
    return [get16(m, 0xD9B6 + 2 * i) for i in range(3)]


def shake_page(m, seg, rng):
    randomize(m, lin(seg, 0), PAGE_BYTES, rng)


def draw_page(m, rng):
    """A random drawing page (page 1, the screen, or elsewhere in RAM) with random pixels; returns
    its segment (also in D9B8)."""
    seg = rng.choice([get16(m, 0xD9B8), get16(m, 0xD9B8), 0xB800, 0x5000])
    put16(m, 0xD9B8, seg)
    shake_page(m, seg, rng)
    return seg


def noise(rng, regs):
    """Random values in the registers a routine does not take."""
    out = {r: rng.randrange(0x10000) for r in ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp')}
    out.update(regs)
    return out


# ---------------------------------------------------------------- the renderer's twins

@machine('tandy')
def test_blit_rows_tandy(h, rng, scale):
    n = captured(h, rng, scale, 'blit_rows_tandy', PICKS[:9])
    base = tandy_base(h)
    for i in range(200 * scale):
        m = bytearray(base)
        draw_page(m, rng)
        record = rng.choice([get16(m, 0xD887), 0x6000, 0x7000])
        put16(m, 0xD887, record)
        si = rng.randrange(0xE000)
        m[lin(record, si):lin(record, si) + 0x1400] = bytes(
            rng.choice([0, 0, 0, rng.randrange(16), rng.randrange(256)]) for _ in range(0x1400))
        repeat = rng.randrange(0xE000)
        m[lin(record, repeat):lin(record, repeat) + 0x100] = bytes(
            rng.choice([1, 1, 1, 2, 3, 0]) for _ in range(0x100))
        put16(m, 0xD87B, repeat)
        put8(m, 0xD86F, rng.choice([1, 2, 3, 0]))
        put8(m, 0xB7F8, rng.randrange(1, 0x41))
        put8(m, 0xD862, rng.choice([rng.randrange(1, 0x41), 1, 2, 0]))
        put8(m, 0xB7F7, rng.choice([rng.randrange(256), rng.randrange(0x10, 0x98)]))
        put16(m, 0xD873, rng.choice([0, 1, 0, 1, rng.randrange(0x10000)]))
        put16(m, 0xD875, rng.choice([0, 0, 0, rng.randrange(1, 0x40), rng.randrange(0x10000)]))
        put8(m, 0xB7F9, rng.choice([rng.randrange(1, 0x41), 0x80, 0]))
        if rng.random() < 0.15:                          # the nibble tables D848..D84B
            randomize(m, DS_BASE + 0xD848, 4, rng)
        al = rng.choice([rng.randrange(0x10, 0x50), rng.randrange(0x40, 0x50), rng.randrange(256)])
        h.check('blit_rows_tandy', m, regs=noise(rng, {'ax': rng.randrange(256) << 8 | al, 'si': si}),
                label='case %d' % i)
        n += 1
    return n


@machine('tandy')
def test_spotlight_beam_tandy(h, rng, scale):
    n = captured(h, rng, scale, 'spotlight_beam_tandy', PICKS[:6], need=0)
    base = tandy_base(h)
    for i in range(250 * scale):
        m = bytearray(base)
        es = draw_page(m, rng)
        put8(m, 0xB7E2, rng.choice([0, 0x10, 0x2D, 0x3F, 0x40, rng.randrange(0x40), rng.randrange(256)]))
        put8(m, 0xB7E3, rng.choice([1, 10, rng.randrange(1, 12), 0]))
        bx = rng.choice([SPRITE_WIDTHS + rng.randrange(0, 0xBF), SPRITE_WIDTHS + 10 * rng.randrange(0x13),
                         rng.randrange(0x10000)])
        dx = rng.choice([rng.randrange(-0x100, 0x100) & 0xFFFF, rng.randrange(-0x40, 0x140) & 0xFFFF,
                         rng.randrange(0x10000)])
        if rng.random() < 0.15:                          # the end tables D84C..D84F
            randomize(m, DS_BASE + 0xD84C, 4, rng)
        h.check('spotlight_beam_tandy', m, regs=noise(rng, {'bx': bx, 'dx': dx, 'es': es}), label='case %d' % i)
        n += 1
    return n


@machine('tandy')
def test_sky_water_tandy(h, rng, scale):
    n = captured(h, rng, scale, 'sky_water_tandy', [0], outputs=['di'])
    base = tandy_base(h)
    for i in range(150 * scale):
        m = bytearray(base)
        es = draw_page(m, rng)
        bl = rng.choice([rng.randrange(0x3E), 0, 0x3D])
        cl = rng.choice([rng.randrange(bl + 1), bl, 0, rng.randrange(0x40)])
        put8(m, 0xD953, rng.choice([bl, bl, rng.randrange(0x3E), rng.randrange(256)]))
        put8(m, 0xD950, rng.randrange(256))
        put8(m, 0xD9B5, rng.choice([0, 0, rng.randrange(256)]))
        if rng.random() < 0.15:                          # the colour pairs D852
            randomize(m, DS_BASE + 0xD852, 16, rng)
        sky = rng.randrange(256)
        ax = rng.choice([sky << 8 | sky, rng.randrange(0x10000)])
        regs = noise(rng, {'es': es, 'ax': ax, 'bx': rng.randrange(256) << 8 | bl, 'cx': rng.randrange(256) << 8 | cl})
        h.check('sky_water_tandy', m, regs=regs, outputs=['di'], label='case %d' % i)
        n += 1
    return n


@machine('tandy')
def test_water_marks_tandy(h, rng, scale):
    n = captured(h, rng, scale, 'water_marks_tandy', [0])
    base = tandy_base(h)
    ages = [0, 0x0A, 0x0B, 0x10, 0x11, 0x16, 0x17, 0x1A, 0x1B, 0x1F, 0x20]
    for i in range(200 * scale):
        m = bytearray(base)
        es = draw_page(m, rng)
        di = rng.choice([get16(m, 0xD74D + 2 * rng.randrange(0x40)), get16(m, 0xD74D + 2 * rng.randrange(0x40)),
                         rng.randrange(0x10000)])
        bx = rng.choice([rng.randrange(0x20), rng.randrange(0x20), rng.randrange(0x10000)])
        cx = rng.choice([0x20, 0x20, rng.randrange(1, 0x100), rng.randrange(0x10000)])
        for k in range(0x20):
            put8(m, 0xD90D + k, rng.randrange(256))
            put8(m, 0xD92D + k, rng.choice(ages + [rng.randrange(0x20), rng.randrange(256)]))
        if rng.random() < 0.15:                          # the patterns D816 and pixels D850
            randomize(m, DS_BASE + 0xD816, 0x32, rng)
            randomize(m, DS_BASE + 0xD850, 2, rng)
        h.check('water_marks_tandy', m, regs=noise(rng, {'es': es, 'di': di, 'bx': bx, 'cx': cx}),
                label='case %d' % i)
        n += 1
    return n


def span_edges(m, rng):
    """Edges at the column boundaries: 0FFh/100h (the right clip), 17Fh/180h (the wrapped left
    part), 1FFh; steps of 0 and of one column; mostly from a row in the view (40h-7Fh)."""
    put8(m, 0xD8FC, rng.choice([rng.randrange(0x40, 0x80), rng.randrange(0x40, 0x80), rng.randrange(0x100),
                                0x3F, 0x40, 0x7F, 0x80, 0xBF, 0xC0]))
    put8(m, 0xB7E2, rng.choice([1, 2, 3, 0x20, 0x40, 0, rng.randrange(256)]))
    for off in (0xD956, 0xD958):
        column = rng.choice([0, 1, 2, 0xFE, 0xFF, 0x100, 0x101, 0x17E, 0x17F, 0x180, 0x181, 0x1FF,
                             rng.randrange(0x200), rng.randrange(0x200)])
        put16(m, off, column << 7 | rng.randrange(0x80))
    for off in (0xD95A, 0xD95C):
        put16(m, off, rng.choice([0, 0x80, 0xFF80, 0x100, rng.randrange(-0x400, 0x400) & 0xFFFF,
                                  rng.randrange(0x10000)]))
    put16(m, 0xD954, rng.randrange(0x10000))


@machine('tandy')
def test_span_tandy_a(h, rng, scale):
    n = captured(h, rng, scale, 'span_tandy_a', [0, 4, 16, 64, 128])
    base = tandy_base(h)
    for i in range(300 * scale):
        m = bytearray(base)
        es = draw_page(m, rng)
        span_edges(m, rng)
        if rng.random() < 0.1:
            randomize(m, DS_BASE + 0xD852, 16, rng)
        h.check('span_tandy_a', m, regs=noise(rng, {'es': es}), label='case %d' % i)
        n += 1
    return n


@machine('tandy')
def test_span_tandy_b(h, rng, scale):
    n = captured(h, rng, scale, 'span_tandy_b', [0, 3], need=0)
    base = tandy_base(h)
    for i in range(300 * scale):
        m = bytearray(base)
        es = draw_page(m, rng)
        span_edges(m, rng)
        column = rng.choice([0, 1, 0xFE, 0xFF, 0x100, 0x13F, 0x140, 0x17E, 0x17F, 0x180, 0x1FF, rng.randrange(0x200)])
        put16(m, 0xD956, ((column << 7) - 0x40 + rng.randrange(0x80)) & 0xFFFF)
        put16(m, 0xD95A, rng.choice([0, 0x80, 0xFF80, 0x100, 0x7F, 0xFF00, rng.randrange(-0x2000, 0x2000) & 0xFFFF,
                                     rng.randrange(0x10000)]))
        h.check('span_tandy_b', m, regs=noise(rng, {'es': es}), label='case %d column %X' % (i, column))
        n += 1
    return n


# ---------------------------------------------------------------- the dispatch points

@machine('tandy')
def test_tandy_dispatch(h, rng, scale):
    """The callers that choose the Tandy routines, in mode 9 (and the other modes that reach them):
    terrain_setup (sky_water_tandy at mode 9, water_marks_tandy), fill_triangle and edge_setup
    ([D8F8]/[D8FA]), spotlight_beam and blit_place (Tandy for every mode but 13h, 0Dh and 4), and
    the passes that run them all: terrain_frame and object_frame."""
    n = captured(h, rng, scale, 'terrain_setup', [0])

    def detail(m, regs, rng):
        put8(m, 0xD6BF, rng.choice([0, 0, 1]))
        put8(m, 0xD9B5, rng.choice([0, 0, 1, 2, rng.randrange(256)]))
        put8(m, 0xD965, rng.randrange(0x40))
        put8(m, 0xD193, rng.randrange(256))
    n += captured(h, rng, scale, 'terrain_setup', [0], variants=6, mutate=detail)
    n += captured(h, rng, scale, 'fill_triangle', [0, 3, 9, 27, 81])
    n += captured(h, rng, scale, 'edge_setup', [0, 1, 2], need=0)
    base = tandy_base(h)
    for i in range(60 * scale):
        m = bytearray(base)
        es = draw_page(m, rng)
        put16(m, 0xD954, rng.randrange(0x10000))
        first = rng.randrange(0x70)
        regs = {'ax': (first + rng.randrange(0x40)) << 8 | first, 'cx': rng.randrange(0x10000),
                'dx': rng.randrange(0x10000), 'es': es}
        if rng.random() < 0.5:
            regs['dx'] = (regs['cx'] + rng.randrange(-0x2000, 0x2000)) & 0xFFFF
        h.check('edge_setup', m, regs=regs, label='line %d' % i)
        m = bytearray(base)
        es = draw_page(m, rng)
        put16(m, 0xD95E, (regs['cx'] + rng.randrange(-0x6000, 0x6000)) & 0xFFFF)
        put8(m, 0xD964, rng.choice([0, 1, 2]))
        put16(m, 0xD954, rng.randrange(0x10000))
        regs['es'] = es
        h.check('fill_triangle', m, regs=regs, label='triangle %d' % i)
        n += 2

    def other_mode(m, regs, rng):
        put16(m, 0xEED2, rng.choice([9, 9, 8, 0x0A, 0x0B, 0x0C, 0x0E, 0x12, 0x14, 0xFF, 0]))
    n += captured(h, rng, scale, 'blit_place', PICKS[:7], variants=4, mutate=other_mode)

    def beam_mode(m, regs, rng):
        put16(m, 0xEED2, rng.randrange(256) << 8 | rng.choice([9, 9, 0x0A, 0x0B, 0x0C]))
    n += captured(h, rng, scale, 'spotlight_beam', [0, 1, 2], variants=8, mutate=beam_mode, need=0)
    for i in range(80 * scale):
        m = bytearray(base)
        draw_page(m, rng)
        put8(m, 0xD193, rng.randrange(256))
        put16(m, 0x0086, rng.choice([1, 2, 3, 4, rng.randrange(0x10000)]))
        put16(m, 0xEED2, rng.choice([9, 9, 0x0A, 0x0B, 0x0C]))
        h.check('spotlight_beam', m, regs=noise(rng, {'ax': rng.randrange(0x10000), 'bx': rng.randrange(0x10000)}),
                label='beam %d' % i)
        n += 1
    with tandy_game(h):
        n += check_captured(h, rng, scale, 'terrain_frame', [0], variants=4)
        n += check_captured(h, rng, scale, 'object_frame', [0], variants=4)
    return n


# ---------------------------------------------------------------- the view copies

@machine('tandy')
def test_view_copies_tandy(h, rng, scale):
    """view_copy_1..8 in mode 9 (and 0Ah-0Ch, which also take the Tandy routines) between the Tandy
    pages with random pixels; then each Tandy routine and view_copy_head_tandy by themselves (the
    harness runs them with DS = DGROUP) on the screen, the RAM pages and DGROUP itself."""
    n = 0
    for kw in (dict(practice=1, frames=2), dict(region=0, mission=3, frames=5)):
        base = tandy_base(h, **kw)
        for k in range(1, 9):
            for src, dst in [(1, 0), (1, 2), (2, 0), (0, 1), (1, 1), (2, 2)]:
                for _ in range(scale):
                    m = bytearray(base)
                    for seg in page_segments(m):
                        shake_page(m, seg, rng)
                    put16(m, 0xEED2, rng.randrange(256) << 8 | rng.choice([9, 9, 9, 0x0A, 0x0B, 0x0C]))
                    h.check('view_copy_%d' % k, m, stack_args=[src, dst], regs=noise(rng, {}), outputs=['si'],
                            label='%s pages %d -> %d' % (kw, src, dst))
                    n += 1
    base = tandy_base(h)
    for name in ['view_copy_%d_tandy' % k for k in range(1, 9)] + ['view_copy_head_tandy']:
        for es in page_segments(base) + [0x2B73]:
            for _ in range(2 * scale):
                m = bytearray(base)
                randomize(m, DS_BASE, 0xF640, rng)
                if es != 0x2B73:
                    shake_page(m, es, rng)
                h.check(name, m, regs=noise(rng, {'es': es}),
                        outputs=['di', 'si', 'cx'] if name == 'view_copy_head_tandy' else ['si'],
                        label='ES %04X' % es)
                n += 1
    return n


TESTS = [test_blit_rows_tandy, test_spotlight_beam_tandy, test_sky_water_tandy, test_water_marks_tandy,
         test_span_tandy_a, test_span_tandy_b, test_tandy_dispatch, test_view_copies_tandy]
