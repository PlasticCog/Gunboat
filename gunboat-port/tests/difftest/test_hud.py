"""The cockpit and the station screens (hud.md; world.md §3): the panel lamps and switches, the pilot's
needles, levers, jet marker and radar, the view copies of view_present, the full-screen stations (map,
damage report, assignment), the gun sprites and frame, the window cracks, and the helpers they use
(gfx_line_to, gfx_fill_rect_clipped, crack_table_entry, far_to_near_copy, near_to_far_copy).

States: mission_states.mission_state (the original run from main into a mission, stopped at a
game_frame), and station_state below: the same run stopped when the original enters a given function
(a station screen, view_present, ...), with station keys. The variables each routine reads are then
randomized around those states: lamp and switch bytes, stations, look directions, needles, radar
contacts, cracks, pages. Every check compares all memory (both pages included) and the returned AX
of the assembly routines."""
import struct

from unicorn import UC_HOOK_CODE

import mission_states
from gbdiff import DS_BASE, Mismatch, lin, put8, put16, randomize, seg_of
from mission_states import mission_state
from test_sound import run_original, sound

BIG = 2_000_000_000
# the mission loop's wait for 3 ticks at a full-screen station (05bd:0450, mov ax,[08C0])
STATION_WAIT_POLL = '05bd:0450'

STATION, LOOK, CHASE, DRAW_PAGE, VIEW_PAGE = 0x0086, 0xF346, 0xD96B, 0x007A, 0x0074
LAMPS, SWITCHES = 0xD502, 0xD520


def get8(m, off):
    return m[DS_BASE + off]


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def page_seg(m, page):
    return get16(m, 0xD9B6 + 2 * page)


def station_state(h, stop, hits=1, keys=(), **kw):
    """The memory when the original enters the function `stop` (a symbols.csv name) for the hits-th
    time, in the mission mission_states starts with **kw; keys are delivered one per poll point (the
    fades, the mission loop's start and its wait at a full-screen station). Cached per harness."""
    key = ('station_state', stop, hits, tuple(keys), tuple(sorted(kw.items())))
    cache = h.__dict__.setdefault('_mission_states', {})
    if key not in cache:
        if mission_states.MISSION_POLL == '05bd:0000':
            mission_states.MISSION_POLL = mission_states._find_mission_poll()
        args = dict(practice=None, region=0, mission=1, rank=1, station=1, weapons=(0, 0, 1), engines=1, sea=1,
                    night=False, targets=0)
        args.update(kw)
        m = mission_states._base(h, **args)
        seg, off, _ = h.sym.func(stop)
        count = [0]

        def on_enter(uc, address, size, _):
            count[0] += 1
            if count[0] == hits:
                h.orig.fail('stop')
        hook = h.orig.uc.hook_add(UC_HOOK_CODE, on_enter, None, lin(seg_of(seg), off), lin(seg_of(seg), off))
        with sound(h):
            h.set_tick((mission_states.POLLS + [mission_states.MISSION_POLL, STATION_WAIT_POLL], 'both',
                        [0] + list(keys)))
            try:
                h.orig.set_memory(m)
                file_seg, foff, far = h.sym.func('mission_run')
                h.orig.call(file_seg, foff, far, {}, (), max_insns=4_000_000_000)
                raise Mismatch('mission_run returned before %s' % stop)
            except Mismatch as e:
                if str(e) != 'original: stop':
                    raise
            finally:
                h.orig.uc.hook_del(hook)
                h.set_tick(None)
        cache[key] = bytes(h.orig.memory())
    return bytearray(cache[key])


# ---------------------------------------------------------------- states

def pilot(h):
    return mission_state(h, region=0, mission=3, rank=5, station=1, frames=40)


def pilot_left(h):
    return mission_state(h, region=1, mission=2, rank=5, station=1, frames=30, keys=[0] * 10 + [ord('z')])


def pilot_right(h):
    return mission_state(h, region=0, mission=3, rank=5, station=1, frames=30, keys=[0] * 10 + [ord('c')])


def bow(h):
    return mission_state(h, practice=1)


def stern(h):
    return mission_state(h, practice=2, frames=5)


def midship(h):
    return mission_state(h, region=0, mission=3, rank=5, station=1, frames=30, keys=[0] * 10 + [ord('n')])


COCKPITS = [pilot, pilot_left, pilot_right, bow, stern, midship]


def any_ax(rng):
    return rng.randrange(0x10000)


def mode_id(rng, count):
    """AL for indicator_draw / switch_draw: a mode and a number (mostly valid, sometimes not, FFh)."""
    mode = rng.choice([0x00, 0x40, 0x80, 0xC0])
    n = rng.choice([rng.randrange(count)] * 6 + [count, 0x3F, rng.randrange(0x40)])
    return 0xFF if rng.random() < 0.03 else mode | n


def shake_panel(m, rng, positions=False):
    """Random lamp and switch bytes; sometimes random positions (column 0: not drawn) and graphics."""
    randomize(m, DS_BASE + LAMPS, 0x1E, rng)
    randomize(m, DS_BASE + SWITCHES, 0x11, rng)
    if positions:
        for i in range(0x1E):
            if rng.random() < 0.2:
                put8(m, 0xD532 + 2 * i, 0)
        for i in range(0x11):
            if rng.random() < 0.2:
                put8(m, 0xD58C + 2 * i, 0)
        # graphics 0..3 as in the boat records: beyond them the size tables give a height of 0, and
        # gfx_copy_rect copies 65536 rows (as the original would)
        for i in range(0x1E):
            put8(m, 0xD56D + i, rng.randrange(4))
        for i in range(0x11):
            put8(m, 0xD5AD + i, rng.randrange(4))


def shake_view(m, rng):
    """Station, chase view and draw page: mostly the state's own."""
    if rng.random() < 0.3:
        put16(m, STATION, rng.choice([0, 1, 2, 3, 4, 5, 7, 8, 0xFFFF]))
    put8(m, CHASE, 1 if rng.random() < 0.1 else 0)
    put16(m, DRAW_PAGE, rng.choice([0, 0, 1, 1, 0x100]))


# ---------------------------------------------------------------- the panel

def test_indicator_draw(h, rng, scale):
    n = 0
    for state in COCKPITS:
        base = state(h)
        for _ in range(60 * scale):
            m = bytearray(base)
            shake_panel(m, rng, positions=rng.random() < 0.3)
            shake_view(m, rng)
            ax = (rng.randrange(256) << 8) | mode_id(rng, 0x1E)
            h.check('indicator_draw', m, regs={'ax': ax}, outputs=['ax'], label='%s AX %04X' % (state.__name__, ax))
            n += 1
    return n


def test_switch_draw(h, rng, scale):
    n = 0
    for state in COCKPITS:
        base = state(h)
        for _ in range(40 * scale):
            m = bytearray(base)
            shake_panel(m, rng, positions=rng.random() < 0.3)
            shake_view(m, rng)
            ax = (rng.randrange(256) << 8) | mode_id(rng, 0x11)
            h.check('switch_draw', m, regs={'ax': ax}, outputs=['ax'], label='%s AX %04X' % (state.__name__, ax))
            n += 1
    return n


def test_panel_redraw(h, rng, scale):
    n = 0
    for state in COCKPITS:
        base = state(h)
        for i in range(8 * scale):
            m = bytearray(base)
            if i:
                shake_panel(m, rng, positions=rng.random() < 0.3)
                shake_view(m, rng)
            put8(m, 0xD5BE, rng.choice([1, 1, 2, 0, 0xFF, rng.randrange(256)]))
            for name in ('panel_redraw_all', 'panel_switches_redraw', 'panel_blink'):
                ax = any_ax(rng)
                h.check(name, m, regs={'ax': ax}, outputs=['ax'], label='%s AX %04X' % (state.__name__, ax))
                n += 1
    return n


def test_jet_indicator(h, rng, scale):
    n = 0
    for state in (pilot, pilot_left, pilot_right, bow):
        base = state(h)
        for _ in range(12 * scale):
            m = bytearray(base)
            if rng.random() < 0.8:
                put16(m, STATION, 1)
            put16(m, LOOK, rng.choice([0, 1, 2]))
            put8(m, 0xD632, rng.choice([0, 1, 2]))
            put16(m, DRAW_PAGE, rng.choice([0, 1, 0, 1, 0x100]))
            ax = any_ax(rng)
            h.check('jet_indicator_draw', m, regs={'ax': ax}, outputs=['ax'],
                    label='station %d look %d phase %d page %X' % (get16(m, STATION), get16(m, LOOK), get8(m, 0xD632),
                                                                   get16(m, DRAW_PAGE)))
            n += 1
    return n


# ---------------------------------------------------------------- lines (13d5, 15d9)

def clip_box(m, rng):
    """The clip box: the screen, or a random one (signed values, sometimes inverted)."""
    if rng.random() < 0.5:
        return
    x0, x1 = sorted(rng.randrange(-20, 340) for _ in range(2))
    y0, y1 = sorted(rng.randrange(-20, 220) for _ in range(2))
    # clip_x_max DD03, clip_x_min DD05, clip_y_max DD07, clip_y_min DD09
    put16(m, 0xDD03, x1)
    put16(m, 0xDD05, x0)
    put16(m, 0xDD07, y1)
    put16(m, 0xDD09, y0)


def signed(v):
    return v - 0x10000 if v & 0x8000 else v


def test_gfx_lines(h, rng, scale):
    n = 0
    base = pilot(h)
    for i in range(600 * scale):
        m = bytearray(base)
        clip_box(m, rng)
        put8(m, 0xDCF5, rng.randrange(256))                       # gfx_colour
        if i % 10 == 9:   # far ends: long lines, signed overflow of the deltas
            pts = [rng.choice([rng.randrange(0x10000), 0x7FFF, 0x8000, 0xFFFF, 0, 0x8100, 0x7E00]) for _ in range(4)]
            if i % 20 == 9:
                pts[3] = pts[1]   # horizontal: one clipped rectangle
        else:
            pts = [rng.randrange(-60, 380) & 0xFFFF, rng.randrange(-60, 260) & 0xFFFF,
                   rng.randrange(-60, 380) & 0xFFFF, rng.randrange(-60, 260) & 0xFFFF]
            if i % 7 == 1:
                pts[2] = pts[0]          # vertical
            elif i % 7 == 2:
                pts[3] = pts[1]          # horizontal
            elif i % 7 == 3:
                pts[2:] = pts[:2]        # a point
        put16(m, 0xDCFF, pts[0])
        put16(m, 0xDD01, pts[1])
        h.check('gfx_line_to', m, stack_args=pts[2:], outputs=['ax'], max_insns=BIG,
                label='(%04X, %04X) to (%04X, %04X)' % tuple(pts))
        n += 1
    for _ in range(400 * scale):
        m = bytearray(base)
        clip_box(m, rng)
        put8(m, 0xDCF5, rng.randrange(256))
        rect = [rng.randrange(-40, 360) & 0xFFFF, rng.randrange(-40, 360) & 0xFFFF,
                rng.randrange(-40, 240) & 0xFFFF, rng.randrange(-40, 240) & 0xFFFF]
        rect[2], rect[3] = sorted(rect[2:], key=signed)
        if rng.random() < 0.8:
            rect[0], rect[1] = sorted(rect[:2], key=signed)
        else:   # x0 > x1: 64K-byte rows (as the original); a few rows only
            rect[3] = (signed(rect[2]) + rng.randrange(3)) & 0xFFFF
        h.check('gfx_fill_rect_clipped', m, stack_args=rect, outputs=['ax'], max_insns=BIG, label=str(rect))
        n += 1
    return n


# ---------------------------------------------------------------- the pilot's gauges

def run_draw_page(h, m):
    """gfx_set_draw_page(draw_page) run by the original (the library's page as the game's)."""
    return run_original(h, m, 'gfx_set_draw_page', stack_args=[get16(m, DRAW_PAGE)])


def test_needle_draw(h, rng, scale):
    n = 0
    base = pilot(h)
    for i in range(300 * scale):
        m = bytearray(base)
        clip_box(m, rng)
        put16(m, 0xB7E0, rng.choice([rng.randrange(0x140), rng.randrange(0x140), rng.randrange(0x10000)]))
        put8(m, 0xB7E3, rng.randrange(256))
        put8(m, 0xB7E5, rng.randrange(256))
        put8(m, 0xB7E6, rng.randrange(256))
        put16(m, 0xB7DC, rng.choice([0xFFFF, 0x00FF, rng.randrange(0x10000), rng.randrange(0x10000)]))
        put8(m, 0xB7E9, rng.randrange(256))
        put8(m, 0xB7EA, rng.randrange(256))
        put16(m, DRAW_PAGE, rng.choice([0, 1]))
        m = run_draw_page(h, m)
        cx = rng.randrange(0x10000)
        h.check('needle_draw', m, regs={'cx': cx}, max_insns=BIG, label='CX %04X' % cx)
        n += 1
    return n


def needle_state(m, rng):
    """Random throttles, fuel and last drawn needles and levers (sometimes equal: nothing drawn)."""
    for off in (0xB816, 0xB817):
        put8(m, off, rng.choice([rng.randrange(0x68), rng.randrange(256), 0, 8, 0x67, 0x88, 0x90]))
    put8(m, 0xB818, rng.randrange(256))
    for off in (0xB80A, 0xB80C):
        put16(m, off, rng.choice([rng.randrange(0x10000), 0xC544, 0, 0xFFFF]))
    for off in (0xB80E, 0xB810, 0xB812, 0xB814):
        put16(m, off, rng.choice([0xFFFF, rng.randrange(0x10000), get16(m, off)]))
    for off in (0xB81A, 0xB81B):
        put8(m, off, rng.choice([0xFF, rng.randrange(256), get8(m, off)]))


def test_throttle_needles(h, rng, scale):
    n = 0
    for state in (pilot, pilot_left, pilot_right, bow):
        base = state(h)
        for i in range(25 * scale):
            m = bytearray(base)
            if i:
                needle_state(m, rng)
                if rng.random() < 0.3:
                    put16(m, LOOK, rng.choice([0, 1, 2]))
                put16(m, DRAW_PAGE, rng.choice([0, 0, 1, 0x100]))
            ax = any_ax(rng)
            h.check('throttle_needles', m, regs={'ax': ax}, outputs=['ax'], max_insns=BIG,
                    label='%s case %d AX %04X' % (state.__name__, i, ax))
            n += 1
            # again on the result: the needles and levers are where they were drawn
            m2 = run_original(h, m, 'throttle_needles', regs={'ax': ax})
            h.check('throttle_needles', m2, regs={'ax': ax}, outputs=['ax'], max_insns=BIG,
                    label='%s case %d, second call' % (state.__name__, i))
            n += 1
    return n


def test_jet_marker(h, rng, scale):
    n = 0
    for state in (pilot, pilot_left, pilot_right, bow):
        base = state(h)
        for i in range(25 * scale):
            m = bytearray(base)
            put8(m, 0xB818, rng.randrange(256))
            put8(m, 0xD6B9, rng.choice([get8(m, 0xB818) & 0x7F, 0x40, rng.randrange(256)]))
            put16(m, LOOK, rng.choice([0, 1, 2, 3]))
            if rng.random() < 0.2:
                put16(m, 0xEED2, 4)        # the CGA marker colour (the library stays in mode 13h)
            put16(m, DRAW_PAGE, rng.choice([0, 1]))
            m = run_draw_page(h, m)
            ax = any_ax(rng)
            cx = rng.randrange(0x10000)    # not used: gfx_set_colour(0) leaves CX = 0 before it is pushed
            h.check('jet_marker', m, regs={'ax': ax, 'cx': cx}, outputs=['ax'], max_insns=BIG,
                    label='%s B818 %02X D6B9 %02X look %d CX %04X' % (state.__name__, get8(m, 0xB818),
                                                                      get8(m, 0xD6B9), get16(m, LOOK), cx))
            n += 1
    return n


def test_radar(h, rng, scale):
    n = 0
    base = pilot_right(h)
    count = get16(base, 0xB83D)
    for i in range(60 * scale):
        m = bytearray(base)
        if i:
            put8(m, SWITCHES, rng.choice([0, 0, 0, 1]))
            put8(m, SWITCHES + 4, rng.choice([0, 0, 0, 1]))
            put8(m, SWITCHES + 3, rng.randrange(256))
            put8(m, 0xD50D, rng.randrange(256))
            put8(m, 0xD8BC, rng.choice([0, 0, 0, 1]))
            put8(m, 0xD6B8, rng.choice([rng.randrange(0x80), rng.randrange(256)]))
            put16(m, 0xB83D, rng.choice([count, count, rng.randrange(1, count + 1), 1, 0]))
            if rng.random() < 0.1:
                put16(m, LOOK, rng.choice([0, 1]))
            if rng.random() < 0.15:
                put16(m, 0xEED2, 4)
        ax = any_ax(rng)
        h.check('radar_scope', m, regs={'ax': ax}, outputs=['ax'], max_insns=BIG,
                label='case %d: sw %02X %02X range %02X radar %02X rebuild %d sweep %02X count %d' % (
                    i, get8(m, SWITCHES), get8(m, SWITCHES + 4), get8(m, SWITCHES + 3), get8(m, 0xD50D),
                    get8(m, 0xD8BC), get8(m, 0xD6B8), get16(m, 0xB83D)))
        n += 1
    for _ in range(1000 * scale):
        m = bytearray(base)
        put8(m, 0xB7E3, rng.randrange(256))
        put8(m, 0xB7E9, rng.randrange(256))
        put8(m, 0xB7E2, rng.randrange(4))
        put16(m, 0xEED2, rng.choice([0x13, 0x13, 4, 0x0D]))
        ax = rng.randrange(0x10000)
        cx = rng.choice([rng.randrange(0x4001), rng.randrange(0x10000)])
        h.check('radar_plot', m, regs={'ax': ax, 'cx': cx, 'bx': rng.randrange(0x10000)}, outputs=['ax', 'bx'],
                label='AX %04X CX %04X range %d' % (ax, cx, get8(m, 0xB7E2)))
        n += 1
    return n


# ---------------------------------------------------------------- the view copies

def presenting(h, key):
    """The memory when view_present is entered at the station the key selects (n midship, b stern,
    v bow, c/z pilot looking right/left, none: the pilot ahead): page 1 holds the new 3D view."""
    keys = [0] * 5 + ([ord(key)] if key else [])
    return station_state(h, 'view_present', hits=3, keys=keys, region=0, mission=3, rank=5)


def shake_pages(m, rng):
    """Random bytes in some rows of page 1 and of the screen, so that every copied byte is seen."""
    for page in (0, 1):
        seg = page_seg(m, page)
        for _ in range(20):
            at = rng.randrange(0, 64000 - 320)
            randomize(m, lin(seg, at), 320, rng)


def test_view_copies(h, rng, scale):
    n = 0
    for key in ('', 'c', 'n', 'b', 'v'):
        base = presenting(h, key)
        for k in range(1, 9):
            for src, dst in [(1, get16(base, VIEW_PAGE)), (1, 0), (0, 1), (1, 1), (2, 0)][:5 if key == '' else 2]:
                m = bytearray(base)
                shake_pages(m, rng)
                h.check('view_copy_%d' % k, m, stack_args=[src, dst], outputs=['si'],
                        label='station key %r pages %d -> %d' % (key, src, dst))
                n += 1
    # the VGA routines by themselves (the harness runs them with DS = DGROUP): ES = the screen, page 1,
    # DGROUP itself (overlapping copies)
    base = presenting(h, '')
    for name in ['view_copy_%d_vga' % k for k in range(1, 9)] + ['view_copy_head_vga']:
        for es in (0xA000, page_seg(base, 1), 0x2B73):
            m = bytearray(base)
            randomize(m, DS_BASE, 0xF640, rng)
            h.check(name, m, regs={'es': es, 'ax': rng.randrange(0x10000), 'cx': rng.randrange(0x10000),
                                   'dx': rng.randrange(0x10000), 'bx': rng.randrange(0x10000)},
                    outputs=['di', 'si', 'cx'] if name == 'view_copy_head_vga' else ['si'], label='ES %04X' % es)
            n += 1
    return n


# ---------------------------------------------------------------- the full-screen stations

def screen_state(h, stop, key, **kw):
    return station_state(h, stop, keys=[0] * 5 + [ord(key)], **kw)


# The screens' other video modes (EED2 = 4 CGA, 9 Tandy, 0Dh EGA) on the mode 13h library: their
# palette calls (ega_pal_entry, ega_pal_apply) do nothing there on both sides, and the pictures are
# drawn with picture_draw instead of picture_draw_vga; single_page_mode 0 adds the copy from the copy
# page (the mission states have 1). The text screens are not run in mode 0Dh: text_draw_char's EGA
# path is parked (platform/text.cpp).
SCREEN_VARIANTS = [(0x13, 1), (0x13, 0), (4, 1), (9, 0), (0x0D, 1)]
TEXT_SCREEN_VARIANTS = SCREEN_VARIANTS[:4]


def screen_variants(h, name, m, label, variants=SCREEN_VARIANTS):
    for mode, single in variants:
        mm = bytearray(m)
        put16(mm, 0xEED2, mode)
        put16(mm, 0x0078, single)
        h.check(name, mm, label='%s, mode %X single page %d' % (label, mode, single))
    return len(variants)


def test_map_screen(h, rng, scale):
    n = 0
    for region, mission, rank in ((0, 3, 5), (1, 5, 9), (2, 4, 9)):
        m = screen_state(h, 'map_screen', 'm', region=region, mission=mission, rank=rank)
        n += screen_variants(h, 'map_screen', m, 'region %d mission %d' % (region, mission))
    m = station_state(h, 'map_screen', keys=[0] * 5 + [ord('m')], practice=3)
    return n + screen_variants(h, 'map_screen', m, 'practice (region 3)')


def test_damage_report(h, rng, scale):
    n = 0
    base = screen_state(h, 'damage_report_screen', '/', region=0, mission=3, rank=5)
    for i in range(30 * scale):
        m = bytearray(base)
        if i:
            for off in range(0x1E):
                put8(m, LAMPS + off, (get8(m, LAMPS + off) & 0xFC) | rng.randrange(4))
            put8(m, 0xB7FE, rng.choice([0, 1, 2, 3, 0xFF, rng.randrange(256)]))
        h.check('damage_report_screen', m,
                label='case %d leak %d objective %02X' % (i, get8(m, 0xB7FE), get8(m, 0xD505)))
        n += 1
    return n + screen_variants(h, 'damage_report_screen', base, 'modes', TEXT_SCREEN_VARIANTS)


def test_assignment(h, rng, scale):
    n = 0
    for region, mission, rank in ((0, 1, 5), (1, 3, 9), (2, 6, 9)):
        base = screen_state(h, 'assignment_screen', '.', region=region, mission=mission, rank=rank)
        for r, mi in ((region, mission), (region, 1), (3, 1), (region, 0), (3, 2)):
            m = bytearray(base)
            put16(m, 0xB503, r)
            put16(m, 0xB505, mi)
            h.check('assignment_screen', m, label='region %d mission %d (text of %d/%d)' % (r, mi, region, mission))
            n += 1
        n += screen_variants(h, 'assignment_screen', base, 'region %d mission %d' % (region, mission),
                             TEXT_SCREEN_VARIANTS)
    return n


def test_station_screen_helpers(h, rng, scale):
    n = 0
    base = pilot(h)
    for mode in (0x13, 4, 9, 0x0D, 0, 0x0113):
        m = bytearray(base)
        put16(m, 0xEED2, mode)
        h.check('station_screen_colours', m, label='mode %X' % mode)
        n += 1
    for station in (1, 2, 3, 4, 5, 7, 8, 0x0A, 0x10A):
        for view_page in (0, 1):
            m = bytearray(base)
            put16(m, STATION, station)
            put16(m, VIEW_PAGE, view_page)
            h.check('screen_clear', m, label='station %X view page %d' % (station, view_page))
            n += 1
    for parts in (0, 1, 2, 3, 0x101, 0xFFFF):
        for chase in (0, 1):
            m = bytearray(base)
            put8(m, CHASE, chase)
            put16(m, DRAW_PAGE, rng.choice([0, 1]))
            h.check('gun_panel_copy', m, stack_args=[parts], label='parts %X chase %d' % (parts, chase))
            n += 1
    return n


# ---------------------------------------------------------------- the gun stations

def test_gun_sprites(h, rng, scale):
    n = 0
    states = [station_state(h, 'gun_sprites_capture', practice=1),
              screen_state(h, 'gun_sprites_capture', 'b', region=0, mission=3, rank=5),
              screen_state(h, 'gun_sprites_capture', 'n', region=0, mission=3, rank=5, weapons=(1, 1, 1))]
    for base in states:
        for i in range(6 * scale):
            m = bytearray(base)
            if i:
                put8(m, 0xEEA0, rng.choice([get8(m, 0xEEA0), rng.randrange(256)]))
                put8(m, 0xF10A, rng.choice([get8(m, 0xF10A), rng.randrange(256)]))
                put8(m, 0xF146, rng.choice([get8(m, 0xF146), rng.randrange(256)]))
            h.check('gun_sprites_capture', m, label='station %d case %d' % (get16(m, STATION), i))
            n += 1
    return n


def test_gun_frame(h, rng, scale):
    n = 0
    base = presenting(h, 'b')
    for bearing in range(256):
        m = bytearray(base)
        if bearing % 16 == 0:
            for off in (0xF107, 0xF109, 0xF10A):
                put8(m, off, rng.randrange(256))
            for off, size in ((0xF348, 45), (0xF21E, 45), (0xF375, 30), (0xF24B, 30), (0xF112, 32), (0xEA88, 32)):
                randomize(m, DS_BASE + off, size, rng)
        arg = bearing | (rng.randrange(256) << 8 if bearing % 5 == 0 else 0)
        h.check('gun_frame_draw', m, stack_args=[arg], label='bearing %04X' % arg)
        n += 1
    return n


# ---------------------------------------------------------------- the window cracks

def test_window_cracks(h, rng, scale):
    n = 0
    for state in (pilot, pilot_left, pilot_right, bow, stern, midship):
        base = state(h)
        for i in range(12 * scale):
            m = bytearray(base)
            randomize(m, DS_BASE + 0x0B49, 5, rng)
            if rng.random() < 0.3:
                put16(m, STATION, rng.choice([0, 1, 2, 3, 4, 5, 6, 0xFFFF, 0x8000]))
            if rng.random() < 0.3:
                put16(m, LOOK, rng.choice([0, 1, 2]))
            for off in (0xB804, 0xB806, 0xB807):
                put8(m, off, rng.choice([0, 1, 2]))
            put8(m, CHASE, 1 if rng.random() < 0.1 else 0)
            put16(m, DRAW_PAGE, rng.choice([0, 1]))
            h.check('window_cracks_draw', m, label='%s station %d look %d cracks %s' % (
                state.__name__, get16(m, STATION), get16(m, LOOK), bytes(m[DS_BASE + 0xB49:DS_BASE + 0xB4E]).hex()))
            n += 1
    m = h.fresh_memory()
    for index in list(range(0, 0x93)) + [rng.randrange(0x10000) for _ in range(40 * scale)] + [0xFF20, 0xFFFF]:
        h.check('crack_table_entry', m, stack_args=[index], outputs=['ax'], label='index %04X' % index)
        n += 1
    return n


# ---------------------------------------------------------------- the buffer copies (segment 0000)

def test_buffer_copies(h, rng, scale):
    n = 0
    base = pilot(h)
    world_b = (get16(base, 0xF5D4), get16(base, 0xF5D6))
    count = get16(base, 0xF396)
    # as view_restore and mission_load use them: the world B data to DS:53A8 and back
    h.check('far_to_near_copy', base, stack_args=[world_b[0], world_b[1], 0x53A8, count], label='world B -> 53A8')
    h.check('near_to_far_copy', base, stack_args=[0x53A8, world_b[0], world_b[1], count], label='53A8 -> world B')
    n += 2
    for i in range(40 * scale):
        m = bytearray(base)
        put16(m, 0xF13A, rng.randrange(0x10000))
        size = rng.choice([0, 1, 2, rng.randrange(0x100), rng.randrange(0x2000)])
        # far: the heap, the screen, page 1; near: below the stack (the original's frame is there) and
        # clear of the counter (a copy over it: below)
        src_seg = rng.choice([world_b[1], 0xA000, page_seg(m, 1)])
        src_off = rng.randrange(0x10000)
        dst = rng.randrange(0, 0xF13A - size)
        if size < 0x400 and rng.random() < 0.3:
            dst = rng.randrange(0xF13C, 0xF640 - size)
        h.check('far_to_near_copy', m, stack_args=[src_off, src_seg, dst, size], label='%04X:%04X -> %04X, %d' % (
            src_seg, src_off, dst, size))
        h.check('near_to_far_copy', m, stack_args=[dst, src_off, src_seg, size], label='%04X -> %04X:%04X, %d' % (
            dst, src_seg, src_off, size))
        n += 2
    # over the counter itself: the copy rewrites F13A's low byte (a value >= 3Fh ends the loop)
    for lo in (0xF0, 0xFF, 0x3F):
        m = bytearray(base)
        m[DS_BASE + 0x9000:DS_BASE + 0x9100] = bytes([lo]) * 0x100
        h.check('far_to_near_copy', m, stack_args=[0x9000, 0x2B73, 0xF130, 0x40], label='over F13A, bytes %02X' % lo)
        h.check('near_to_far_copy', m, stack_args=[0x9000, 0xF130, 0x2B73, 0x40], label='over F13A, bytes %02X' % lo)
        n += 2
    return n


TESTS = [test_indicator_draw, test_switch_draw, test_panel_redraw, test_jet_indicator, test_gfx_lines, test_needle_draw,
         test_throttle_needles, test_jet_marker, test_radar, test_view_copies, test_map_screen, test_damage_report,
         test_assignment, test_station_screen_helpers, test_gun_sprites, test_gun_frame, test_window_cracks,
         test_buffer_copies]
