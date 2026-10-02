"""Mission loading, the terrain pass and the crew pilot's routes (package W2b): mission_load and
mission_setup (world.md §4, §5), terrain_frame (render3d.md §3, simulation.md §10), route_point,
route_advance, route_find and crew_pilot_decide (simulation.md §4.5, §4.6).

States: the original run from main into a mission (mission_states.py). test_hud.station_state stops
it at mission_load's or mission_setup's entry (every region, the practice modes, weapon fits, the
missions of day and night); mission_state stops it at game_frame, and test_render.capture takes
snapshots at terrain_frame's and crew_pilot_decide's entries inside a frame (the stations 2-4, where
the crew pilot steers). On top: targeted randomisation of what each function reads (the world
files and TILE.BIN stay real), with edge values where the code branches, and the branches each test
reached are counted: a test fails if a branch it is meant to cover was not reached. Every check
compares all memory, the DAC writes (mission_load's fade), the open files, the speaker and timer
events, and the registers the callers use."""
import struct

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_AX, UC_X86_REG_SP, UC_X86_REG_SS

import mission_states
from gbdiff import DS_BASE, Mismatch, lin, put8, put16, seg_of
from mission_states import mission_state
from test_hud import station_state
from test_render import BIG, capture, frame_states
from test_sound import sound


def get8(m, off):
    return m[DS_BASE + off]


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def need(name, counts, minimum):
    """Fails unless every branch in counts was reached at least minimum[branch] times."""
    short = {k: counts.get(k, 0) for k, v in minimum.items() if counts.get(k, 0) < v}
    if short:
        raise Mismatch('%s: branches not reached often enough: %s (all: %s)' % (name, short, counts))


def bump(counts, key):
    counts[key] = counts.get(key, 0) + 1


# ---------------------------------------------------------------- the route network (TILE.BIN)

ROUTE_STATES = [
    dict(region=0, mission=3, rank=5, station=2, frames=5),
    dict(region=1, mission=2, rank=5, station=4, frames=20),
    dict(region=2, mission=4, rank=9, station=2, frames=5),
    dict(practice=1, frames=3),
    dict(practice=2, frames=3),
    dict(region=1, mission=5, rank=9, station=2, frames=20, night=True),
    dict(region=2, mission=2, rank=3, station=3, frames=30, weapons=(1, 1, 2)),
]


def route_state(h, rng, which=None):
    return mission_state(h, **ROUTE_STATES[rng.randrange(len(ROUTE_STATES)) if which is None else which])


def waypoints(m, cell):
    """The waypoints (link, x, y) of the tile in grid cell `cell`, read as route_point reads them
    (the link not yet turned), from TILE.BIN in memory."""
    seg, base = get16(m, 0xF282), get16(m, 0xF280)

    def at(o):
        return lin(seg, o & 0xFFFF)
    tile = get8(m, 0xB84E + cell) & 0x3F
    si = (struct.unpack_from('<H', m, at(base + 2 * tile))[0] + base) & 0xFFFF
    s = (m[at(si)] + m[at(si + 1)] + m[at(si + 2)]) & 0xFF
    n = m[at(si + 3)]
    p = si + 4 + 4 * s
    return [(m[at(p + 3 * i)], m[at(p + 3 * i + 1)], m[at(p + 3 * i + 2)]) for i in range(n)]


def link_kind(link):
    if link == 0xFF:
        return 'none'
    if link >= 0xC0:
        return 'back'
    if link >= 0x80:
        return 'fork'
    if link >= 0x40:
        return 'exit_b'     # leaves the tile when going backward
    if link:
        return 'exit_f'     # leaves the tile when going forward
    return 'plain'


def pick_waypoint(m, rng, kind=None):
    """(cell, index, link) of a random waypoint, of the link kind `kind` if given."""
    cells = [(c, w) for c in range(187) for w in [waypoints(m, c)] if w]
    for _ in range(400):
        c, w = rng.choice(cells)
        i = rng.randrange(len(w))
        if kind is None or link_kind(w[i][0]) == kind:
            return c, i, w[i][0]
    c, w = rng.choice(cells)
    return c, 0, w[0][0]


class DivideStop:
    """Stops the original before route_point's DIV DL (0919:875e) when the quotient does not fit (a
    cell of 4352 or more): the run ends there with R6003 on both sides, nothing else is compared.
    Except where route_advance scans the tile its route leaves to (its call at 0919:88CE): there the
    port turns the captain round instead (PORT, sim_routes.cpp: the route off the map's top edge),
    which is counted in `turned`."""
    AT = (0x0919, 0x875E)
    SCAN_RETURN = 0x88D1  # route_advance's scan of the next tile calls route_point from 0919:88CE

    def __init__(self, h):
        self.h, self.cases, self.turned = h, 0, 0
        self.scan = False

    def __enter__(self):
        o = self.h.orig

        def hook(uc, address, size, _):
            if uc.reg_read(UC_X86_REG_AX) // 0x11 > 0xFF:
                # route_point has pushed ES and BX: its return address is at SS:SP+4
                sp = uc.reg_read(UC_X86_REG_SP)
                ss = uc.reg_read(UC_X86_REG_SS)
                ret = struct.unpack('<H', uc.mem_read(ss * 16 + ((sp + 4) & 0xFFFF), 2))[0]
                self.scan = ret == self.SCAN_RETURN
                o.fail('divide error at 0919:875E')
        at = lin(seg_of(self.AT[0]), self.AT[1])
        self.hook = o.uc.hook_add(UC_HOOK_CODE, hook, None, at, at)
        return self

    def __exit__(self, *exc):
        self.h.orig.uc.hook_del(self.hook)

    def check(self, name, m, **kw):
        self.scan = False
        try:
            return self.h.check(name, m, **kw)
        except Mismatch as e:
            if not str(e).startswith('original: divide error'):
                raise
        self.h.port.set_memory(m)
        try:
            self.h.port.call(name, kw.get('regs') or {}, kw.get('stack_args', ()))
        except Mismatch as e:
            if 'R6003' in str(e) and not self.scan:
                self.cases += 1
                return None
            raise
        if self.scan:  # the route off the map: the port's captain turned round (PORT)
            self.turned += 1
            return None
        raise Mismatch('%s [%s]: the original divides by zero, the port does not' % (name, kw.get('label', '')))


def test_route_point(h, rng, scale):
    """Every cell of four worlds with real and past-the-end indices, cells past the grid (other
    mission bytes as tile numbers) and past 4352 (the divide error), random CH."""
    n = 0
    counts = {}
    with DivideStop(h) as div:
        for i in range(1500 * scale):
            m = route_state(h, rng)
            r = rng.random()
            if r < 0.75:
                bx = rng.randrange(187)
            elif r < 0.9:
                bx = rng.choice([0, 186, 187, rng.randrange(187, 0x400), 0x10FF])
            else:
                bx = rng.choice([0x1100, 0x1101, 0xFFFF, rng.randrange(0x1100, 0x10000)])
            count = len(waypoints(m, bx)) if bx < 187 else 0
            cl = rng.randrange(count + 2) if rng.random() < 0.8 else rng.choice([0, 0xFF, rng.randrange(256)])
            cx = rng.randrange(256) << 8 | cl
            put8(m, 0xB7E2, rng.randrange(256))
            want = div.check('route_point', m, regs={'cx': cx, 'bx': bx, 'es': rng.randrange(0x10000)},
                             outputs=['ax', 'cx', 'dx', 'si', 'bx', 'es'], label='case %d cell %d index %d' % (i, bx, cl))
            if want is None:
                bump(counts, 'divide')
            elif want['dx'] >> 8 == 0xFF:
                bump(counts, 'none')
            else:
                bump(counts, 'point')
                bump(counts, 'rot%d' % (get8(m, 0xB84E + bx) >> 6) if bx < 187 else 'rot?')
            n += 1
    need('route_point', counts, {'divide': 5, 'none': 50, 'point': 300, 'rot0': 20, 'rot1': 20, 'rot2': 20,
                                 'rot3': 20})
    return n


def test_route_advance(h, rng, scale):
    """The waypoints of every link kind (plain, ahead and back steps, forks, tile edges both ways,
    none), both directions, every branch command and fork memory D688, the message state, random
    CH; the link passed is the waypoint's (as the callers pass it) or another one."""
    n = 0
    counts = {}
    kinds = ['plain', 'back', 'fork', 'exit_f', 'exit_b', 'none']
    with DivideStop(h) as div:
        n = _route_advance_cases(h, rng, scale, counts, kinds, div)
    need('route_advance', counts, {'no_route': 20, 'crossed': 20, 'turned': 20, 'step_plain': 20,
                                   'step_plain_back': 20, 'step_back': 20, 'step_back_back': 20, 'step_fork': 20,
                                   'step_fork_back': 20, 'step_exit_f_back': 20, 'step_exit_b': 20})
    return n


def _route_advance_cases(h, rng, scale, counts, kinds, div):
    n = 0
    for i in range(1200 * scale):
        m = route_state(h, rng)
        cell, index, link = pick_waypoint(m, rng, rng.choice(kinds))
        if rng.random() < 0.8:          # as the callers do: the link route_point returns (turned)
            h.orig.set_memory(m)
            fs, fo, far = h.sym.func('route_point')
            link = h.orig.call(fs, fo, far, {'cx': index, 'bx': cell})['dx'] >> 8
        else:
            link = rng.choice([0, 1, 4, 0x3F, 0x40, 0x43, 0x7F, 0x80, 0x85, 0xBF, 0xC0, 0xFE, 0xFF, rng.randrange(256)])
        # D684 is only ever 0 or 1 (every writer); with another value a failed tile crossing would
        # recurse without end (D684 ^= 1 never reaches 0), on both sides
        put8(m, 0xD684, rng.choice([0, 1]))
        put8(m, 0xD685, rng.choice([0, 1, 2, rng.randrange(256)]))
        put8(m, 0xD688, rng.choice([0, 0x42, 0x82, 0xEF, 0xF0, 0xFE, 0xFF, rng.randrange(256)]))
        put8(m, 0xD686, index if rng.random() < 0.8 else rng.randrange(256))
        put16(m, 0xD689, cell if rng.random() < 0.8 else rng.randrange(187))
        put8(m, 0xB800, rng.choice([0, 1, 2, rng.randrange(256)]))
        cx = rng.randrange(256) << 8 | index
        dx = link << 8 | rng.randrange(256)
        want = div.check('route_advance', m, regs={'dx': dx, 'cx': cx, 'bx': cell}, outputs=['dx'],
                         label='case %d cell %d index %d link %02X dir %d' % (i, cell, index, link, get8(m, 0xD684)))
        n += 1
        if want is None:            # off the map: the next cell is past 4352 (R6003 on both sides)
            bump(counts, 'divide')
            continue
        mo = h.orig.memory()
        kind, backward = link_kind(link), get8(m, 0xD684) != 0
        if kind == 'none':
            bump(counts, 'no_route')
        elif (kind == 'exit_f' and not backward) or (kind == 'exit_b' and backward):
            step = get16(m, 0xD68F + 2 * ((link - 1) & 3))
            bump(counts, 'crossed' if get16(mo, 0xD689) == (cell + step) & 0xFFFF else 'turned')
        else:
            bump(counts, 'step_' + kind + ('_back' if backward else ''))
    return n


def test_route_find(h, rng, scale):
    """The boat anywhere on the four maps (cells without waypoints, waypoints behind, several
    ahead), any hull heading, in missions and in practice."""
    n = 0
    counts = {}
    for i in range(700 * scale):
        m = route_state(h, rng)
        if rng.random() < 0.7:
            cell, index, _ = pick_waypoint(m, rng)
            w = waypoints(m, cell)[index]
            row, col = divmod(cell, 17)
            x = (col << 10) + w[1] * 8 + rng.randrange(-0x100, 0x100)
            y = ((0x28 - 4 * row) << 8) + w[2] * 8 + rng.randrange(-0x100, 0x100)
        else:
            x, y = rng.randrange(0x4400), rng.randrange(0x2C00)
        if rng.random() < 0.05:
            x, y = rng.randrange(0x10000), rng.randrange(0x10000)
        put16(m, 0xC12D, x)
        put16(m, 0xC8FD, y)
        put8(m, 0xB81E, rng.randrange(256))
        put16(m, 0xF110, rng.choice([0, 0, 1, 2, 0x100]))
        for off in (0xD683, 0xD684, 0xD686, 0xD688):
            put8(m, off, rng.randrange(256))
        put8(m, 0xD681, 2)                               # the state the caller has
        with DivideStop(h) as div:                       # off the map: a cell past 4352 is R6003
            want = div.check('route_find', m, label='case %d X %04X Y %04X heading %02X' % (i, x, y, get8(m, 0xB81E)))
        n += 1
        if want is None:
            bump(counts, 'divide')
            continue
        mo = h.orig.memory()
        if get8(mo, 0xD681) == 1:
            # B7ED counts the candidates after the first from FFh: FFh one, 0 two, more
            bump(counts, {0xFF: 'one', 0: 'two'}.get(get8(mo, 0xB7ED), 'more'))
        else:
            bump(counts, 'none')
    need('route_find', counts, {'one': 25, 'two': 30, 'more': 30, 'none': 50})
    return n


# ---------------------------------------------------------------- the crew pilot

PILOT_STATES = [
    dict(region=0, mission=3, rank=5, station=2, frames=5),
    dict(region=1, mission=2, rank=5, station=4, frames=20),
    dict(region=2, mission=4, rank=9, station=2, frames=12),
    dict(practice=1, frames=3),
    dict(practice=2, frames=10),
    dict(region=1, mission=5, rank=9, station=3, frames=25, night=True),
]


def test_crew_pilot_decide(h, rng, scale):
    """Snapshots where the original's frames call it (stations 2-4), then random throttles and
    targets, states 0-3, the waypoint anywhere around the boat (the jet keys, rates and the arrival
    within 16 units that advances the route), hull and jet angles."""
    counts = {}
    with DivideStop(h) as div:
        n = _crew_pilot_cases(h, rng, scale, counts, div)
    need('crew_pilot_decide', counts, {'state0': 20, 'state1': 100, 'state2': 20, 'state3': 20, 'rate0': 20,
                                       'rate1': 20, 'rate2': 20, 'arrived': 20, 'keys0': 5, 'keys1': 5, 'keys2': 5,
                                       'keys4': 5, 'keys8': 5, 'keys5': 5, 'keys6': 5, 'keys9': 5, 'keys10': 5})
    return n


def _crew_pilot_cases(h, rng, scale, counts, div):
    n = 0
    for k in range(len(PILOT_STATES)):
        m = mission_state(h, **PILOT_STATES[k])
        for regs, _, snap in capture(h, m, 'crew_pilot_decide', [0]):
            h.check('crew_pilot_decide', snap, label='%s as captured' % PILOT_STATES[k])
            n += 1
            for i in range(120 * scale):
                s = bytearray(snap)
                put8(s, 0xB816, rng.choice([0, 8, 0x3B, rng.randrange(256)]))
                put8(s, 0xB817, rng.choice([0, 8, 0x3B, get8(s, 0xB816), rng.randrange(256)]))
                put8(s, 0xD680, rng.choice([0, 7, 8, 9, 0x3B, get8(s, 0xB816), rng.randrange(256)]))
                put8(s, 0xD681, rng.choice([0, 1, 1, 1, 2, 3, rng.randrange(256)]))
                put8(s, 0xB81E, rng.randrange(256))
                put8(s, 0xB818, rng.randrange(256))
                x, y = get16(s, 0xC12D), get16(s, 0xC8FD)
                if rng.random() < 0.5:           # a real waypoint, the boat near it or not
                    cell, index, _ = pick_waypoint(s, rng)
                    put8(s, 0xD686, index)
                    put16(s, 0xD689, cell)
                    w = waypoints(s, cell)[index]
                    row, col = divmod(cell, 17)
                    wx = (col << 10) + w[1] * 8
                    wy = ((0x28 - 4 * row) << 8) + w[2] * 8
                    put16(s, 0xD68B, wx)
                    put16(s, 0xD68D, wy)
                    if rng.random() < 0.6:
                        x = wx + rng.randrange(-0x14, 0x14)
                        y = wy + rng.randrange(-0x14, 0x14)
                    put8(s, 0xD684, rng.choice([0, 1]))
                    put8(s, 0xD688, rng.choice([0, 0xF0, rng.randrange(256)]))
                else:
                    put16(s, 0xD68B, (x + rng.randrange(-0x800, 0x800)) & 0xFFFF)
                    put16(s, 0xD68D, (y + rng.randrange(-0x800, 0x800)) & 0xFFFF)
                put16(s, 0xC12D, x & 0xFFFF)
                put16(s, 0xC8FD, y & 0xFFFF)
                if rng.random() < 0.4:           # the hull at a jet or rate boundary of the bearing
                    h.orig.set_memory(s)
                    fs, fo, far = h.sym.func('atan')
                    bearing = h.orig.call(fs, fo, far, {'cx': (get16(s, 0xD68B) - x) & 0xFFFF,
                                                        'dx': (get16(s, 0xD68D) - y) & 0xFFFF})['ax'] & 0xFF
                    rel = rng.choice([0x48, 0x49, 0x6F, 0x70, 0x80, 0x90, 0x91, 0xB7, 0xB8])
                    put8(s, 0xB81E, (bearing + 0x80 - rel) & 0xFF)
                    if rng.random() < 0.5:       # the jet already where the captain wants it, or next to it
                        angle = (((rel - 0x40) << 1) + 2) & 0xFC
                        put8(s, 0xB818, ((angle >> 1) + rng.choice([0, 0, 0x80, 1, -1])) & 0xFF)
                want = div.check('crew_pilot_decide', s, label='%s case %d state %d' % (
                    PILOT_STATES[k], i, get8(s, 0xD681)))
                n += 1
                if want is None:
                    bump(counts, 'divide')
                    continue
                mo = h.orig.memory()
                state = get8(s, 0xD681)
                bump(counts, 'state%d' % min(state, 3))
                bump(counts, 'keys%d' % get8(mo, 0xD682))
                if state == 1:
                    bump(counts, 'rate%d' % get8(mo, 0xD683))
                    if get8(mo, 0xD687) != get8(s, 0xD687) or get8(mo, 0xD686) != get8(s, 0xD686) \
                            or get8(mo, 0xD681) != 1:
                        bump(counts, 'arrived')
    return n


def bearing(h, m, x, y):
    """The original's atan of the vector from the boat to (x, y): the angle byte."""
    h.orig.set_memory(m)
    fs, fo, far = h.sym.func('atan')
    return h.orig.call(fs, fo, far, {'cx': (x - get16(m, 0xC12D)) & 0xFFFF,
                                     'dx': (y - get16(m, 0xC8FD)) & 0xFFFF})['ax'] & 0xFF


def test_route_walk(h, rng, scale):
    """The captain following the river: from a random place (route_find), the boat is put on each
    waypoint in turn, facing it, so that every call arrives and advances along the real network
    (forks by the branch command, which changes on the way, tile crossings, ends of routes and new
    searches). Each step is a check of all memory, continuing from the original's result. A walk
    that leaves the map through the north edge (a crossing from row 0: the cell becomes negative,
    route_point divides by 17 into more than 255) ends in R6003 on both sides, as in the game."""
    with DivideStop(h) as div:
        return _route_walks(h, rng, scale, div)


def _route_walks(h, rng, scale, div):
    n = 0
    counts = {}
    for walk in range(16 * scale):
        m = bytearray(route_state(h, rng, walk % len(ROUTE_STATES)))
        cell, index, _ = pick_waypoint(m, rng, 'plain')
        w = waypoints(m, cell)[index]
        row, col = divmod(cell, 17)
        put16(m, 0xC12D, (col << 10) + w[1] * 8 + rng.randrange(-0x80, 0x80))
        put16(m, 0xC8FD, ((0x28 - 4 * row) << 8) + w[2] * 8 + rng.randrange(-0x80, 0x80))
        put8(m, 0xB81E, rng.randrange(256))
        put8(m, 0xD681, 2)
        put8(m, 0xD685, rng.choice([0, 1, 2]))
        for step in range(50):
            before = bytes(m)
            want = div.check('crew_pilot_decide', m, label='walk %d step %d cell %d index %d state %d' % (
                walk, step, get16(m, 0xD689), get8(m, 0xD686), get8(m, 0xD681)))
            n += 1
            if want is None:
                bump(counts, 'off_the_map')
                break
            m = bytearray(h.orig.memory())
            state = get8(m, 0xD681)
            if get8(before, 0xD681) == 1:
                if state == 2:
                    bump(counts, 'end')
                elif get16(m, 0xD689) != get16(before, 0xD689):
                    bump(counts, 'crossing')
                elif get8(m, 0xD688) & 0x80 and get8(m, 0xD688) < 0xC0:
                    bump(counts, 'fork')
                else:
                    bump(counts, 'step')
            elif get8(before, 0xD681) == 2:
                bump(counts, 'found' if state == 1 else 'search')
            if state == 1:                               # arrive at the waypoint, facing it
                x, y = get16(m, 0xD68B), get16(m, 0xD68D)
                put8(m, 0xB81E, bearing(h, m, x, y))
                put16(m, 0xC12D, x)
                put16(m, 0xC8FD, y)
            else:                                        # drift until a search finds a waypoint
                put16(m, 0xC12D, (get16(m, 0xC12D) + rng.randrange(-0x200, 0x200)) & 0xFFFF)
                put16(m, 0xC8FD, (get16(m, 0xC8FD) + rng.randrange(-0x200, 0x200)) & 0xFFFF)
                put8(m, 0xB81E, rng.randrange(256))
            if rng.random() < 0.1:
                put8(m, 0xD685, rng.choice([0, 1, 2]))
    need('route_walk', counts, {'step': 200, 'crossing': 50, 'fork': 10, 'found': 10})
    return n


# ---------------------------------------------------------------- mission loading

# Where the front end or the title menu leaves the choices: every region, the practice modes, weapon
# fits (the cockpit art), engines, practice targets; missions of day and night (the mission type's
# start time: types 1, 2, 4, 5, 9, 10, 12 and 15 start at night or dawn).
LOAD_STATES = [
    dict(practice=1),
    dict(practice=2),
    dict(practice=3),
    dict(region=0, mission=1, rank=5, station=1),
    dict(region=0, mission=6, rank=7, station=2, weapons=(1, 1, 2), engines=0),
    dict(region=1, mission=2, rank=5, station=4, weapons=(0, 1, 1), targets=1),
    dict(region=2, mission=4, rank=9, station=2, weapons=(1, 0, 0)),
    dict(region=3, mission=1, rank=1, station=1, weapons=(0, 0, 2)),
]


def load_mutate(m, rng):
    """Another fit and mission on a mission_load entry state (mission_load reads them itself)."""
    put8(m, 0xB804, rng.choice([0, 1, 1, 2, rng.randrange(256)]))           # bow: 0 grenade launcher
    put8(m, 0xB806, rng.choice([0, 1, 1, 2, rng.randrange(256)]))           # stern: 1 the other gun
    put8(m, 0xB807, rng.choice([0, 1, 2, 3, rng.randrange(256)]))           # midship: 0, 1, others
    put8(m, 0xB805, rng.choice([0, 1]))
    put8(m, 0xB803, rng.choice([0, 0, 1]))
    put16(m, 0xB505, rng.randrange(8))
    put16(m, 0xB509, rng.choice([0, 1, 2, 4, 5, 9, 10, 12, 15, rng.randrange(16)]))
    put16(m, 0x0070, rng.choice([0, 0, 0, 1]))
    if rng.random() < 0.3:
        put16(m, 0xB503, rng.randrange(4))
        put16(m, 0xF110, 0)


def check_load(h, name, m, rng, label):
    dac = bytes(rng.randrange(64) for _ in range(768))
    with sound(h):
        return h.check(name, m, tick=(mission_states.POLLS, 'both', []), max_insns=BIG, label=label, dac=dac)


def test_mission_load(h, rng, scale):
    """mission_load at its entry in each LOAD_STATES mission, as reached and with other fits,
    missions, regions and the demo flag: all memory (the world data, TILE.BIN, the art buffers, the
    palette, page 1), the fade's DAC writes, the open files, the effects timer."""
    n = 0
    counts = {}
    for k, kw in enumerate(LOAD_STATES):
        m = station_state(h, 'mission_load', **kw)
        check_load(h, 'mission_load', m, rng, '%s as reached' % kw)
        n += 1
        for i in range(6 * scale):
            s = bytearray(m)
            load_mutate(s, rng)
            check_load(h, 'mission_load', s, rng, '%s case %d region %d weapons %d %d %d type %d' % (
                kw, i, get16(s, 0xB503), get8(s, 0xB804), get8(s, 0xB806), get8(s, 0xB807), get16(s, 0xB509)))
            n += 1
            mo = h.orig.memory()
            bump(counts, 'night' if get8(mo, 0xB7FC) == 0 else 'day')
            bump(counts, 'region%d' % get16(s, 0xB503))
    need('mission_load', counts, {'night': 3, 'day': 3, 'region0': 2, 'region1': 2, 'region2': 2, 'region3': 2})
    return n


def setup_mutate(m, rng):
    """What mission_setup reads, around a real state: practice modes (the low byte counts), the
    demo flag, practice targets with real and small object counts, engines, region, mission and
    mission type (also past the tables), the video mode (the remap in CGA, EGA and Tandy), the
    sprite cache pointers, the RNG, the world data's remap ranges."""
    put16(m, 0xF110, rng.choice([0, 0, 1, 2, 3, 4, 0x100, 0x101, 0xFF]))
    put16(m, 0x0070, rng.choice([0, 0, 1, 0x100]))
    put8(m, 0xB803, rng.choice([0, 0, 1, 0xFF]))
    if rng.random() < 0.2:
        put16(m, 0xB959, rng.choice([0x10, 0x24, 0x25, 0x26, 0xC7]))
    put8(m, 0xB805, rng.choice([0, 1, 0xFF]))
    put16(m, 0xB503, rng.choice([0, 1, 2, 3, 3, 0x100, rng.randrange(8)]))
    put16(m, 0xB505, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, rng.randrange(0x34), 0x105]))
    put16(m, 0xB509, rng.choice(list(range(16)) + [0x18, 0x19, 0x1A, rng.randrange(0x40)]))
    put16(m, 0xB7F3, rng.randrange(0x10000))
    put16(m, 0xEED2, rng.choice([0x13, 0x13, 0x13, 0x0D, 0x09, 0x04, 0x0E, 0x113]))
    for off in (0xF0DA, 0xF0DC, 0xF0E0, 0xF0E2):
        put16(m, off, rng.randrange(0x10000))
    put16(m, 0x0088, rng.randrange(0x10000))
    put16(m, 0x008A, rng.randrange(0x10000))
    put8(m, 0xB7FC, rng.choice([0, 1, 0xFF]))
    put8(m, 0xB7FD, rng.randrange(3))                      # twilight stage
    if rng.random() < 0.2:                                 # other remap ranges (DGROUP offsets)
        a = rng.randrange(0x100, 0x6000)
        put16(m, 0x6E56, a)
        put16(m, 0x6E58, a + rng.choice([0, 1, rng.randrange(0x400)]))
        b = rng.randrange(0x10, 0x1000)
        put16(m, 0x53A8, b)
        put16(m, 0x53AA, b + rng.choice([0, 1, rng.randrange(0x400)]))


def test_mission_setup(h, rng, scale):
    """mission_setup at its entry (mission_load's call) in each LOAD_STATES mission, as reached and
    with setup_mutate: all memory (the start state, the sprite segments and row tables, the clock and
    the scene colours, the remapped world data in CGA, EGA and Tandy)."""
    n = 0
    counts = {}
    for kw in LOAD_STATES:
        m = station_state(h, 'mission_setup', **kw)
        h.check('mission_setup', m, label='%s as reached' % kw)
        n += 1
        for i in range(60 * scale):
            s = bytearray(m)
            setup_mutate(s, rng)
            h.check('mission_setup', s, label='%s case %d' % (kw, i))
            n += 1
            bump(counts, 'practice%d' % min(get8(s, 0xF110), 3))
            bump(counts, 'mode%02X' % get8(s, 0xEED2))
            if get8(s, 0xB803):
                bump(counts, 'targets')
    need('mission_setup', counts, {'practice0': 50, 'practice1': 20, 'practice2': 20, 'practice3': 20,
                                   'mode13': 50, 'mode0D': 10, 'mode09': 10, 'mode04': 10, 'targets': 50})
    return n


# ---------------------------------------------------------------- the terrain pass

def contact_mutate(m, rng):
    """A camera next to a terrain vertex at water level (a shore-contact candidate when it is in the
    bow or stern window of the hull heading), the hull heading and the speed's sign, the previous
    contact, the station and region (the Mare Island loss), the RNG byte and the mission number
    (the damage gate) and the waterjet and hull conditions."""
    a, b = get16(m, 0xD960), get16(m, 0xD962)
    groups = [g for g in ((0, a), (0x200, b)) if g[1]]
    if groups:
        first, count = rng.choice(groups)
        v = first + rng.randrange(min(count, 0x200))
        put16(m, 0xD972, (get16(m, 0x1C96 + 2 * v) + rng.randrange(-0x30, 0x30)) & 0xFFFF)
        put16(m, 0xD974, (get16(m, 0x2496 + 2 * v) + rng.randrange(-0x30, 0x30)) & 0xFFFF)
        if rng.random() < 0.5:
            put16(m, 0x1496 + 2 * v, 0)
    put8(m, 0xB81E, rng.randrange(256))
    put8(m, 0xB828, rng.choice([0, 0x10, 0x30, 0xF0, 0x80, rng.randrange(256)]))
    put8(m, 0xD8FF, rng.choice([0, 0, 1, 0x0E, 6]))
    put8(m, 0xD8BC, rng.choice([0, 1]))
    put8(m, 0xD96B, 1 if rng.random() < 0.1 else 0)
    put16(m, 0x0086, rng.choice([1, 1, 2, 3, 4, 0x101]))
    put16(m, 0xB503, rng.choice([0, 1, 2, 3, 3, 0x103]))
    if rng.random() < 0.3:                                 # the Mare Island pilot (byte tests)
        put16(m, 0x0086, rng.choice([1, 0x101]))
        put16(m, 0xB503, rng.choice([3, 0x103]))
    put8(m, 0x0088, rng.choice([0, 1, 0x10, 0x11, 0xF0, 0xF1, rng.randrange(256)]))
    put16(m, 0xB505, rng.choice([0, 1, 2, 3, 7, 0x100]))
    if rng.random() < 0.5:                                 # the damage gate open
        put8(m, 0x0088, rng.choice([0, 1, 0x10, 0x11, 0xF0, 0xF1]))
        put16(m, 0xB505, rng.choice([2, 3, 7]))
    for off in (0xD50A, 0xD50B, 0xD510):
        put8(m, off, rng.choice([0, 1, 2, 3, 0, 1, 2, 3, 0x40, 0x43, 0xFD, rng.randrange(256)]))
    put8(m, 0xB800, rng.choice([0, 0, 1, 2]))


STILL = 0x1235   # in D901 before a case: only a skipped projection leaves it (candidates are even)


def terrain_case(h, snap, counts, label):
    put16(snap, 0xD901, STILL)
    h.check('terrain_frame', snap, label=label, max_insns=BIG)
    mo = h.orig.memory()
    if get16(mo, 0xD901) == STILL:
        bump(counts, 'still')
        return
    bump(counts, 'projected')
    c = get8(mo, 0xD8FF)
    if get8(snap, 0xD96B):
        bump(counts, 'chase')
    elif c and c != get8(mo, 0xD900):
        bump(counts, 'contact')
        if get8(mo, 0xD8FD) and not get8(snap, 0xD8FD):
            bump(counts, 'lost')
        for off, name in ((0xD50A, 'jet'), (0xD50B, 'jet'), (0xD510, 'hull')):
            if get8(mo, off) != get8(snap, off):
                bump(counts, name)
                bump(counts, 'condition%d' % (get8(snap, off) & 3))
    elif c:
        bump(counts, 'same_contact')


def test_terrain_frame(h, rng, scale):
    """terrain_frame where the original's frames call it: the mission states and moved variants
    (chase view, night, lights) of test_render; then the same frame again (the projection skipped
    when nothing moved); then shore contacts: the camera next to a waterline vertex with random
    headings, speeds, stations, regions, RNG, missions and conditions (the speed reversal, the Mare
    Island loss, waterjet and hull damage with their messages)."""
    n = 0
    counts = {}
    states = frame_states(h, rng, scale, variants=12)
    for label, m in states:
        for regs, _, snap in capture(h, m, 'terrain_frame', [0]):
            terrain_case(h, snap, counts, '%s as captured' % label)
            again = bytearray(h.orig.memory())             # the same view once more: nothing moved
            put8(again, 0xD8BC, 0)
            terrain_case(h, again, counts, '%s again' % label)
            n += 2
            for i in range(40 * scale):
                s = bytearray(snap)
                contact_mutate(s, rng)
                terrain_case(h, s, counts, '%s contact case %d' % (label, i))
                n += 1
    need('terrain_frame', counts, {'still': 10, 'projected': 300, 'chase': 20, 'contact': 100, 'lost': 5,
                                   'jet': 5, 'hull': 5, 'same_contact': 10, 'condition0': 3,
                                   'condition1': 3, 'condition3': 3})
    return n


TESTS = [test_route_point, test_route_advance, test_route_find, test_crew_pilot_decide, test_route_walk,
         test_mission_setup, test_mission_load, test_terrain_frame]
