"""The CGA (mode 4) routines of the renderer and of the view copies (render3d.md §1.2, hud.md §6) on
the CGA machine, and the Hercules game that runs the same code (hercules_mode DS:0076 = 1; GB.EXE
never reads it).

States: the original run from main into a mission with GUNBOAT.CFG choosing mode 4 (Hercules: 0Ch)
on this machine (cga_game: mission_states, test_render's bases and moved variants, test_hud's
station states, in a cache of their own); test_render.capture snapshots where the original's frame
enters a routine (water_marks_cga and spotlight_beam_cga jump back to their entry for each row, so
their later "calls" are the states of their later rows). Targeted randomisation covers what the
frames do not reach: columns at the clip limits, first pixels 0-3 of a byte, even and odd rows,
rows off the view, the page being the screen (B800h), a RAM page or DGROUP itself (overlapping the
routines' own tables). The dispatching routines (terrain_setup, fill_triangle, edge_setup,
draw_group_b, spotlight_beam, spotlights, blit_place, blit_record, terrain_frame, object_frame, the
far view copies) run on the CGA states too. All memory and the card state are compared."""
import atexit
import contextlib
import pathlib
import shutil
import struct
import tempfile

from gbdiff import DGROUP, DS_BASE, GAME_DIR, Mismatch, lin, put8, put16, randomize
from test_modes import machine
import test_render
from test_render import BIG, capture, check_captured, moved

CFG_MODE = {'cga': 0x04, 'hercules': 0x0C}
_DIRS = {}

SPOTLIGHT_WIDTHS = 0x709A       # CS:709A, 19 rows of 10 width bytes
VIEW_ROWS = 0xD74D              # view_row_table


def get8(m, off):
    return m[DS_BASE + off]


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def game_dir(name):
    """A temporary copy of the game folder whose GUNBOAT.CFG chooses the machine's mode (no joystick,
    two pages)."""
    if name not in _DIRS:
        tmp = pathlib.Path(tempfile.mkdtemp(prefix='gbdiff_%s_' % name))
        for f in GAME_DIR.iterdir():
            if f.is_file() and f.name.upper() != 'GUNBOAT.CFG':
                shutil.copyfile(f, tmp / f.name)
        (tmp / 'GUNBOAT.CFG').write_bytes(bytes([CFG_MODE[name], 0, 0, 0, 0, 0]))
        atexit.register(shutil.rmtree, tmp, True)
        _DIRS[name] = tmp
    return _DIRS[name]


@contextlib.contextmanager
def cga_game(h):
    """Inside, the mission states the other test modules build (mission_states, test_render.base and
    moved, test_hud.station_state) are this machine's game: the original reads a GUNBOAT.CFG that
    chooses CGA (or Hercules), and the states are cached apart from the harness's other states."""
    saved_dir = h.dos.dir
    had = '_mission_states' in h.__dict__
    saved = h.__dict__.get('_mission_states')
    h.__dict__['_mission_states'] = h.__dict__.setdefault('_cga_mission_states', {})
    h.dos.dir = game_dir(h.machine)
    try:
        yield
    finally:
        h.dos.dir = saved_dir
        if had:
            h.__dict__['_mission_states'] = saved
        else:
            del h.__dict__['_mission_states']


def page_seg(m, page):
    return get16(m, 0xD9B6 + 2 * page)


def cga_base(h, i):
    """Mission state i of test_render.BASES in this machine's game (inside cga_game)."""
    m = test_render.base(h, i)
    if get8(m, 0xEED2) != 4 or get16(m, 0xD8F8) != 0x46E8:
        raise Mismatch('not a CGA state: mode %X, span routine %04X' % (get16(m, 0xEED2), get16(m, 0xD8F8)))
    return m


def shake_pages(m, rng):
    """Random bytes on the screen (B800h, 32 KB: the Hercules page too) and pages 1 and 2."""
    randomize(m, 0xB8000, 0x8000, rng)
    for page in (1, 2):
        randomize(m, lin(page_seg(m, page), 0), 0x4000, rng)


def pick_page(m, rng, dgroup=True):
    """The page a routine draws on: the drawing page (1), the screen, page 2, or DGROUP itself."""
    return rng.choice([page_seg(m, 1), page_seg(m, 1), 0xB800, page_seg(m, 2)] + ([DGROUP] if dgroup else []))


def captured_and_mutated(h, rng, scale, target, picks, mutate, outputs=(), variants=4, mutated=12, need=1, extra=0):
    """check_captured on this machine's states: as captured, then varied by mutate; then `extra` cases
    of the base states with random registers varied by mutate."""
    with cga_game(h):
        n = check_captured(h, rng, scale, target, picks, outputs=outputs, variants=variants, need=need)
        n += check_captured(h, rng, scale, target, picks[:2], outputs=outputs, variants=mutated, mutate=mutate,
                            need=need)
        for i in range(extra * scale):
            m = cga_base(h, i)
            regs = {r: rng.randrange(0x10000) for r in ('ax', 'bx', 'cx', 'dx', 'si', 'di')}
            regs['es'] = get16(m, 0xD9B8)
            mutate(m, regs, rng)
            h.check(target, m, regs=regs, outputs=outputs, label='random %d' % i)
            n += 1
    return n


# ---------------------------------------------------------------- the renderer's twins

@machine('cga')
def test_blit_rows_cga(h, rng, scale):
    def mutate(m, regs, rng):
        """First rows from the view's top to past its bottom (80h ends it; an AL that wraps), columns
        clipped left (D875) or anywhere (every first pixel 0-3, the half column D873), widths and
        repeat counts including 0 (256), transparent and opaque sources, other pages."""
        shake_pages(m, rng)
        row = rng.choice([0x10, 0x11, 0x4E, 0x4F, 0x50, 0xCF, 0xD0, 0xFF, rng.randrange(0x10, 0x50),
                          rng.randrange(0x10, 0x50), rng.randrange(256)])
        regs['ax'] = rng.randrange(256) << 8 | row
        put8(m, 0xB7F7, rng.randrange(256))
        put16(m, 0xD873, rng.choice([0, 1, 0, 1, rng.randrange(4), rng.randrange(0x10000)]))
        put16(m, 0xD875, rng.choice([0, 0, 0, rng.randrange(1, 0x30), rng.randrange(0x10000)]))
        put8(m, 0xD862, rng.choice([1, 2, 3, 4, 5, rng.randrange(1, 0x60), rng.randrange(1, 0x60), 0]))
        put8(m, 0xD86F, rng.choice([1, 1, 2, 3, rng.randrange(256)]))
        put8(m, 0xB7F9, rng.choice([1, 2, rng.randrange(1, 0x50), rng.randrange(1, 0x50), 0]))
        put8(m, 0xB7F8, rng.randrange(256))
        seg = get16(m, 0xD887)
        if rng.random() < 0.5:                              # a record of zeros and pixels 1-0FFh
            at = lin(seg, regs['si'])
            for k in range(0x400):
                if at + k < len(m):
                    m[at + k] = rng.choice([0, 0, rng.randrange(1, 4), rng.randrange(256)])
        if rng.random() < 0.2:
            put16(m, 0xD887, rng.choice([DGROUP, 0xB800]))
        regs['si'] = rng.choice([regs['si'], rng.randrange(0x10000)])
        put16(m, 0xD9B8, pick_page(m, rng))
        if get16(m, 0xD9B8) == DGROUP:                      # (keep the rows away from the test's stack)
            put16(m, 0xD873, get16(m, 0xD873) & 1)
    n = captured_and_mutated(h, rng, scale, 'blit_rows_cga', [0, 1, 3, 8, 21, 55], mutate, mutated=24)
    with cga_game(h):
        for i in range(300 * scale):                        # the same variations on the bases, more often
            m = cga_base(h, i)
            regs = {'si': rng.randrange(0x10000), 'ax': 0, 'bx': rng.randrange(0x10000),
                    'cx': rng.randrange(0x10000), 'dx': rng.randrange(0x10000), 'di': rng.randrange(0x10000)}
            mutate(m, regs, rng)
            if rng.random() < 0.3 and get16(m, 0xD9B8) != DGROUP:   # a start byte in the other bank than the row
                put16(m, 0xD873, rng.randrange(0x10000))
                put16(m, 0xD875, 0)
            h.check('blit_rows_cga', m, regs=regs, label='case %d' % i)
            n += 1
    return n


@machine('cga')
def test_spotlight_beam_cga(h, rng, scale):
    n = 0
    with cga_game(h):
        # the beams of night states with the lights on (and, per row, the states of the later rows)
        for i in range(12 * scale):
            m = moved(h, rng, i, rebuild=False, lights=True)
            put8(m, 0xD193, rng.randrange(0x40, 0x80))
            for k, (regs, words, snap) in enumerate(capture(h, m, 'spotlight_beam_cga', [0, 3, 9, 14, 25])):
                h.check('spotlight_beam_cga', snap, regs=regs, label='lights %d, call %d' % (i, k), max_insns=BIG)
                n += 1
        # rows, widths and columns anywhere: clipped left and right, all inside, off the view
        for i in range(250 * scale):
            m = cga_base(h, i)
            shake_pages(m, rng)
            put8(m, 0xB7E2, rng.choice([0, 0x10, 0x2D, 0x3A, 0x3F, 0x40, 0x41, rng.randrange(0x40), rng.randrange(256)]))
            put8(m, 0xB7E3, rng.choice([1, 10, 10, rng.randrange(1, 12), 0]))
            bx = rng.choice([SPOTLIGHT_WIDTHS + rng.randrange(0xBE), SPOTLIGHT_WIDTHS + 10 * rng.randrange(19),
                             rng.randrange(0x10000)])
            dx = rng.choice([rng.randrange(-0x100, 0x100) & 0xFFFF, rng.randrange(-0x100, 0x100) & 0xFFFF,
                             rng.choice([0, 0x20, 0x100, 0xFF00, 0xFFE0]), rng.randrange(0x10000)])
            h.check('spotlight_beam_cga', m, regs={'bx': bx, 'dx': dx, 'es': pick_page(m, rng),
                                                   'ax': rng.randrange(0x10000), 'cx': rng.randrange(0x10000)},
                    label='case %d' % i)
            n += 1
    return n


@machine('cga')
def test_sky_water_cga(h, rng, scale):
    def mutate(m, regs, rng):
        """Every horizon row and sky top (none above it: BL = CL; the wrap when CL > BL), every colour,
        the flash counter on and off, other pages."""
        shake_pages(m, rng)
        horizon = rng.choice([rng.randrange(0x3E), 0, 0x3D])
        put8(m, 0xD953, horizon)
        cl = rng.choice([rng.randrange(horizon + 1), horizon, 0])
        if rng.random() < 0.05:
            cl = rng.randrange(horizon, 0x40)
        regs['bx'] = rng.randrange(256) << 8 | horizon
        regs['cx'] = rng.randrange(256) << 8 | cl
        regs['ax'] = rng.randrange(0x10000)
        put8(m, 0xD94F, rng.randrange(256))
        put8(m, 0xD950, rng.randrange(256))
        put8(m, 0xD9B5, rng.choice([0, 0, 1, rng.randrange(256)]))
        regs['es'] = pick_page(m, rng)
    return captured_and_mutated(h, rng, scale, 'sky_water_cga', [0], mutate, outputs=['di'], variants=6, mutated=40,
                                extra=300)


@machine('cga')
def test_water_marks_cga(h, rng, scale):
    def mutate(m, regs, rng):
        """Every row of the view (the end at row pair 40h), mark ages and columns anywhere, row counts
        at the size limits, a BH that is not 0 (the offset table read further on), other pages."""
        randomize(m, DS_BASE + 0xD90D, 0x40, rng)
        shake_pages(m, rng)
        es = pick_page(m, rng)
        regs['es'] = es
        rows = [get16(m, VIEW_ROWS + 2 * r) for r in range(0x40)]
        regs['di'] = rng.choice(rows + [rng.randrange(0x4000), rng.choice([0x13BA, 0x13DC, 0x13DD, 0x33BA, 0x33DC, 0x33DD])]
                                + ([rng.randrange(0x10000)] if es != DGROUP else []))
        regs['cx'] = rng.randrange(0x10000) & 0xFF00 | rng.choice([0x20, 0x17, 0x16, 0x0F, 0x0E, 1, 2, rng.randrange(1, 0x21), 0])
        regs['bx'] = rng.choice([regs['bx'], rng.randrange(0x20), rng.randrange(0x20), rng.randrange(0x10000)])
        if es == DGROUP:        # (offsets from anywhere could reach the test's stack at the top of DGROUP)
            regs['bx'] &= 0x1F
        regs['ax'] = rng.randrange(0x10000)
        regs['dx'] = rng.randrange(0x10000)
    return captured_and_mutated(h, rng, scale, 'water_marks_cga', [0, 1, 7, 13, 20, 26, 31], mutate, variants=4,
                                mutated=40, extra=400)


def span_edges(m, rng, first_rows):
    put8(m, 0xD8FC, rng.choice(first_rows))
    put16(m, 0xD954, rng.randrange(0x10000))


def cga_rows(rng):
    return [rng.randrange(0x40, 0x80), rng.randrange(0x40, 0x80), rng.randrange(0x40, 0x80), 0x3F, 0x40, 0x7F,
            0x80, 0xBF, 0xC0, 0xFF, rng.randrange(0x100)]


@machine('cga')
def test_span_cga_a(h, rng, scale):
    def mutate(m, regs, rng):
        """Edges at the column boundaries: 0FFh/100h (the right clip), 17Fh/180h (the wrapped left
        part), 1FFh, every first and last pixel in a byte; steps of 0 and of one column; first rows
        above, in and below the view (the rows C0h-FFh are skipped, 80h-BFh end it)."""
        shake_pages(m, rng)
        span_edges(m, rng, cga_rows(rng))
        put8(m, 0xB7E2, rng.choice([1, 2, 3, 0x20, 0x40, 0, rng.randrange(256)]))
        for off in (0xD956, 0xD958):
            column = rng.choice([0, 1, 2, 3, 4, 5, 0xFC, 0xFD, 0xFE, 0xFF, 0x100, 0x101, 0x17E, 0x17F, 0x180,
                                 0x181, 0x1FD, 0x1FF, rng.randrange(0x200), rng.randrange(0x200)])
            put16(m, off, column << 7 | rng.randrange(0x80))
        if rng.random() < 0.3:                              # narrow spans: first and last pixel in one byte
            put16(m, 0xD958, (get16(m, 0xD956) + (rng.randrange(0, 5) << 7)) & 0xFFFF)
        for off in (0xD95A, 0xD95C):
            put16(m, off, rng.choice([0, 0x80, 0xFF80, rng.randrange(-0x400, 0x400) & 0xFFFF, rng.randrange(0x10000)]))
        regs['es'] = pick_page(m, rng)
    return captured_and_mutated(h, rng, scale, 'span_cga_a', [0, 4, 16, 64, 128], mutate, mutated=40,
                                extra=400)


@machine('cga')
def test_span_cga_b(h, rng, scale):
    n = 0
    with cga_game(h):
        n += check_captured(h, rng, scale, 'span_cga_b', [0, 3], variants=6, need=0)
        for i in range(300 * scale):                        # lines at the column boundaries
            m = cga_base(h, i)
            shake_pages(m, rng)
            span_edges(m, rng, cga_rows(rng))
            put8(m, 0xB7E2, rng.choice([1, 2, 3, 0x20, rng.randrange(256)]))
            column = rng.choice([0, 1, 2, 3, 4, 0xFE, 0xFF, 0x100, 0x13F, 0x140, 0x17E, 0x17F, 0x180, 0x1FF,
                                 rng.randrange(0x200)])
            put16(m, 0xD956, ((column << 7) - 0x40 + rng.randrange(0x80)) & 0xFFFF)
            put16(m, 0xD95A, rng.choice([0, 0, 0x80, 0xFF80, 0x100, 0x7F, 0xFF81, rng.randrange(-0x400, 0x400) & 0xFFFF,
                                         rng.randrange(-0x2000, 0x2000) & 0xFFFF]))
            h.check('span_cga_b', m, regs={'es': pick_page(m, rng)}, label='line %d column %X' % (i, column))
            n += 1
    return n


# ---------------------------------------------------------------- the dispatching routines on CGA states

@machine('cga')
def test_cga_dispatch(h, rng, scale):
    """The routines that call the CGA twins, as the frame calls them and varied: terrain_setup (sky,
    water, marks), fill_triangle and edge_setup (the span routines through D8F8/D8FA), draw_group_b,
    blit_place and blit_record, spotlight_beam and spotlights, and the whole terrain and object
    passes."""
    n = 0
    with cga_game(h):
        n += check_captured(h, rng, scale, 'terrain_setup', [0], variants=6)
        n += check_captured(h, rng, scale, 'terrain_setup', [0], variants=12, mutate=test_render.terrain_mutate)
        n += check_captured(h, rng, scale, 'fill_triangle', [0, 3, 9, 27, 81, 150], variants=3)
        n += check_captured(h, rng, scale, 'draw_group_b', [0], variants=6)
        n += check_captured(h, rng, scale, 'draw_primitive', [0, 7, 30, 99], variants=3)
        n += check_captured(h, rng, scale, 'blit_place', test_render.PICKS[:9], variants=4)
        n += check_captured(h, rng, scale, 'blit_record', [0, 5, 17], variants=6, mutate=test_render.blit_mutate)
        n += check_captured(h, rng, scale, 'terrain_frame', [0], variants=6)
        n += check_captured(h, rng, scale, 'object_frame', [0], variants=8)
        for i in range(60 * scale):                         # lines through edge_setup
            m = cga_base(h, i)
            put16(m, 0xD954, rng.randrange(0x10000))
            first = rng.randrange(0x70)
            regs = {'ax': (first + rng.randrange(0x40)) << 8 | first, 'cx': rng.randrange(0x10000),
                    'dx': rng.randrange(0x10000), 'es': get16(m, 0xD9B8)}
            if rng.random() < 0.5:
                regs['dx'] = (regs['cx'] + rng.randrange(-0x2000, 0x2000)) & 0xFFFF
            h.check('edge_setup', m, regs=regs, label='line %d' % i)
            n += 1
        for i in range(40 * scale):                         # the lights
            m = moved(h, rng, i, rebuild=False, lights=True)
            put16(m, 0x0086, rng.choice([1, 2, 3, 4, 1]))
            put8(m, 0xD193, rng.randrange(0x40, 0x80))
            put8(m, 0xD965, rng.randrange(0x40))
            h.check('spotlights', m, label='lights %d' % i)
            n += 1
        for i in range(60 * scale):
            m = cga_base(h, i)
            put8(m, 0xD193, rng.randrange(256))
            put16(m, 0x0086, rng.choice([1, 2, 3, 4, rng.randrange(0x10000)]))
            h.check('spotlight_beam', m, regs={'ax': rng.randrange(0x10000), 'bx': rng.randrange(0x10000)},
                    label='beam %d' % i)
            n += 1
    return n


@machine('cga')
def test_cga_dispatch_modes(h, rng, scale):
    """The dispatch points on the other mode values that reach the CGA twins in the original: sky and
    water for every mode below 0Dh but 9, the marks, the beams and the view copies below 9, the row
    copier for 4 only (the spans go through the pointers video_mode_setup set)."""
    n = 0
    with cga_game(h):
        for i in range(40 * scale):
            m = cga_base(h, i)
            mode = rng.choice([0, 1, 3, 4, 5, 6, 8, 0x0A, 0x0B, 0x0C])
            put16(m, 0xEED2, rng.randrange(256) << 8 | mode)
            put8(m, 0xD6BF, 1 if mode > 9 else 0)            # the marks drawn (0Ah-0Ch: Tandy's marks)
            h.check('terrain_setup', m, label='mode %X' % mode)
            n += 1
            mode = rng.choice([0, 1, 3, 4, 5, 6, 8])
            put16(m, 0xEED2, rng.randrange(256) << 8 | mode)
            h.check('spotlight_beam', m, regs={'ax': rng.randrange(0x10000), 'bx': rng.randrange(0x10000)},
                    label='beam mode %X' % mode)
            n += 1
    return n


# ---------------------------------------------------------------- the view copies

def presenting(h, key):
    """The memory when view_present is entered at the station the key selects (test_hud.presenting),
    in this machine's game (inside cga_game)."""
    import test_hud
    return test_hud.presenting(h, key)


def view_copies(h, rng, scale, keys):
    n = 0
    with cga_game(h):
        for key in keys:
            base = presenting(h, key)
            for k in range(1, 9):
                for src, dst in [(1, get16(base, 0x0074)), (1, 0), (2, 0), (0, 1), (1, 1)][:5 if key == '' else 2]:
                    m = bytearray(base)
                    shake_pages(m, rng)
                    mode = rng.choice([4, 4, 4, 0, 3, 8])   # every mode below 9 runs the CGA twin
                    put16(m, 0xEED2, rng.randrange(256) << 8 | mode)
                    h.check('view_copy_%d' % k, m, stack_args=[src, dst], outputs=['si'],
                            label='station key %r pages %d -> %d mode %X' % (key, src, dst, mode))
                    n += 1
    return n


@machine('cga')
def test_view_copies_cga(h, rng, scale):
    n = view_copies(h, rng, scale, ('', 'c', 'n', 'b', 'v'))
    # the CGA routines by themselves (the harness runs them with DS = DGROUP): ES = the screen, a RAM
    # page, DGROUP itself (overlapping copies)
    with cga_game(h):
        base = presenting(h, '')
    for name in ['view_copy_%d_cga' % k for k in range(1, 9)] + ['view_copy_head_cga']:
        for es in (0xB800, page_seg(base, 1), page_seg(base, 2), DGROUP):
            for _ in range(2 * scale):
                m = bytearray(base)
                randomize(m, DS_BASE, 0xF640, rng)
                shake_pages(m, rng)
                h.check(name, m, regs={'es': es, 'ax': rng.randrange(0x10000), 'cx': rng.randrange(0x10000),
                                       'dx': rng.randrange(0x10000), 'bx': rng.randrange(0x10000)},
                        outputs=['di', 'si', 'cx'] if name == 'view_copy_head_cga' else ['si'], label='ES %04X' % es)
                n += 1
    return n


# ---------------------------------------------------------------- Hercules: the same routines

@machine('hercules')
def test_hercules_frame(h, rng, scale):
    """The Hercules game (GUNBOAT.CFG mode 0Ch: CGA mode 4 in memory, hercules_setup's CRTC, DS:0076 =
    1): the terrain and object passes, the spans, the sprite rows and the view copies run the CGA
    twins on its states."""
    n = 0
    with cga_game(h):
        m = cga_base(h, 0)
        if get8(m, 0x0076) != 1:
            raise Mismatch('not a Hercules state: DS:0076 = %02X' % get8(m, 0x0076))
        n += check_captured(h, rng, scale, 'terrain_frame', [0], variants=4)
        n += check_captured(h, rng, scale, 'object_frame', [0], variants=4)
        n += check_captured(h, rng, scale, 'blit_rows_cga', [0, 5], variants=2)
        n += check_captured(h, rng, scale, 'span_cga_a', [0, 16], variants=2)
    n += view_copies(h, rng, scale, ('', 'n'))
    return n


TESTS = [test_blit_rows_cga, test_spotlight_beam_cga, test_sky_water_cga, test_water_marks_cga, test_span_cga_a,
         test_span_cga_b, test_cga_dispatch, test_cga_dispatch_modes, test_view_copies_cga, test_hercules_frame]
