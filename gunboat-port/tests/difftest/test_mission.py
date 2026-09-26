"""The mission segment 05bd (world.md; hud.md §6): view_present, the present of the 3D stations.

States: present_state below runs the original from main into a mission (mission_states) and stops it
when it enters view_present for the hits-th time, at the station a key selects: the pilot looking
left, ahead or right, the bow, the midship and the stern gun, for every weapon fit, by day and by
night, after a few frames or after many. With fire=True the main switch and the station's gun mounts
are switched on by their F-keys and Enter is held from the start (held_keys = 10h), so the gun fires
whenever it can: the flash counters, their latches and the barrels' recoil frames are the game's own.
Both callers are among the stops: view_restore's call (1, view_page) right after a station change
(the 3-frame states of the looks and guns) and mission_run's (1, 0) in its loop (the others).

Around those states each case sets what view_present reads, with edge values where it branches: the
sky tops (D965/D966: the clamp at 21h, the copy limits 6Bh/6Ch/77h, the 8-bit wrap from C0h), the
flash counters and latches, the world pass counter's parity, the gun bearings, the weapon fits,
the colours and the captured sprites, the station and look direction (unknown ones do nothing), the
source and destination pages, and random bytes in the screen and page 1 so that every copied byte
is seen. view_present reads neither the window cracks, the explosion flash (D9B5) nor the screen
shake (B7F2); some cases randomize them anyway. Every check compares all memory (both pages
included). AX is not compared: view_present returns what its last callee leaves there, and both
callers ignore it."""
from unicorn import UC_HOOK_CODE

import mission_states
from gbdiff import DS_BASE, Mismatch, lin, put8, put16, randomize, seg_of
from test_hud import CHASE, LOOK, STATION, STATION_WAIT_POLL, VIEW_PAGE, get8, get16, shake_pages
from test_sound import sound

BIG = 200_000_000
FIRE_KEY = 0x10          # held_keys (DS:DA43) bit of Enter
F1, F2 = 0x81, 0x82
Z = [0] * 8              # keys arrive one per poll: room for the station screens' own polls

STATION_KEYS = {'pilot': '', 'pilot_left': 'z', 'pilot_right': 'c', 'bow': 'v', 'midship': 'n', 'stern': 'b'}

# the captured sprite areas view_present draws from (hud_symbols.csv gun_sprite_*), as (DS, size):
# E9E2..EA83, ECA9..ECAD, EE94..EE98, EEA6..EECF, F0F0..F0F7, F2A4..F345, F3A2..F3AD, F5AE..F5B9,
# F5DC..F611 (the gun colours at ECAE, EEA0..EEA1 and F106..F10A lie between them)
SPRITE_AREAS = [(0xE9E2, 0xA2), (0xECA9, 5), (0xEE94, 5), (0xEEA6, 0x2A), (0xF0F0, 8), (0xF2A4, 0xA2), (0xF3A2, 0x0C),
                (0xF5AE, 0x0C), (0xF5DC, 0x36)]
COLOURS = [0xECAE, 0xECAF, 0xEEA1, 0xF106, 0xF107, 0xF108, 0xF109, 0xF10A]
SKY_TOP, SKY_TOP_PREV = 0xD965, 0xD966
FLASHES = [0xB839, 0xB83A, 0xB83B, 0xB83C]          # midship, bow barrels, stern
LATCHES = [0xF10B, 0xF132, 0xEA86, 0xECA8]          # stern, midship; the frame before's
HEADINGS = 0xB81E                                   # hull, bow, midship, stern
WEAPONS = {'bow': 0xB804, 'stern': 0xB806, 'midship': 0xB807}
WORLD_PASS = 0xD70C


def present_state(h, where, hits=3, fire=False, **kw):
    """The memory when the original enters view_present for the hits-th time at the station `where`
    (STATION_KEYS), in the mission mission_states starts with **kw (default: region 0, mission 3,
    rank 5). fire: see the module docstring (the key script then needs about 40 frames). Cached per
    harness."""
    args = dict(practice=None, region=0, mission=3, rank=5, station=1, weapons=(0, 0, 1), engines=1, sea=1,
                night=False, targets=0)
    args.update(kw)
    key = STATION_KEYS[where]
    if fire:
        keys = [0] * 20 + [F1] + Z + ([ord(key)] + Z + [F1] + Z + [F2] + Z if key not in ('', 'z', 'c') else [])
        if key in ('z', 'c'):
            keys += [ord(key)]
    else:
        keys = [0] * 5 + ([ord(key)] if key else [])
    cache_key = ('present_state', where, hits, fire, tuple(sorted(args.items())))
    cache = h.__dict__.setdefault('_mission_states', {})
    if cache_key not in cache:
        if mission_states.MISSION_POLL == '05bd:0000':
            mission_states.MISSION_POLL = mission_states._find_mission_poll()
        m = mission_states._base(h, **args)
        if fire:
            put8(m, 0xDA43, FIRE_KEY)
        seg, off, _ = h.sym.func('view_present')
        count = [0]

        def on_enter(uc, address, size, _):
            count[0] += 1
            if count[0] == hits:
                h.orig.fail('stop')
        hook = h.orig.uc.hook_add(UC_HOOK_CODE, on_enter, None, lin(seg_of(seg), off), lin(seg_of(seg), off))
        with sound(h):
            h.set_tick((mission_states.POLLS + [mission_states.MISSION_POLL, STATION_WAIT_POLL], 'both', [0] + keys))
            try:
                h.orig.set_memory(m)
                file_seg, foff, far = h.sym.func('mission_run')
                h.orig.call(file_seg, foff, far, {}, (), max_insns=4_000_000_000)
                raise Mismatch('mission_run returned before view_present call %d' % hits)
            except Mismatch as e:
                if str(e) != 'original: stop':
                    raise
            finally:
                h.orig.uc.hook_del(hook)
                h.set_tick(None)
        cache[cache_key] = bytes(h.orig.memory())
    return bytearray(cache[cache_key])


# (label, where, hits, fire, mission_states arguments): every station and look direction, every weapon
# fit, day and night, a few frames and many, firing. Night comes from the mission's start time
# (time_of_day sets daylight DS:B7FC during the set-up; mission_states' night argument is overwritten
# there): region 0 missions 1, 4, 5, region 1 missions 1, 2, 4, 7, region 2 missions 1, 3, 5, 7 and the
# practice missions start at night.
STATES = [
    ('pilot ahead', 'pilot', 3, False, {}),
    ('pilot left', 'pilot_left', 3, False, {}),
    ('pilot right', 'pilot_right', 3, False, {}),
    ('pilot ahead, night', 'pilot', 12, False, dict(mission=4, rank=9)),
    ('pilot left, region 1, night', 'pilot_left', 20, False, dict(region=1, mission=2, rank=9)),
    ('pilot right, region 2, main switch on', 'pilot_right', 45, True, dict(region=2, mission=4, rank=9)),
    ('bow 0', 'bow', 3, False, dict(weapons=(0, 0, 1))),
    ('bow 1', 'bow', 3, False, dict(weapons=(1, 0, 1))),
    ('bow 0 firing', 'bow', 40, True, dict(weapons=(0, 0, 1))),
    ('bow 0 firing, later', 'bow', 47, True, dict(weapons=(0, 0, 1))),
    ('bow 1 firing, night', 'bow', 44, True, dict(weapons=(1, 0, 1), mission=5, rank=9)),
    ('bow, practice 1 (night)', 'bow', 4, False, dict(practice=1)),
    ('midship 0', 'midship', 3, False, dict(weapons=(0, 0, 0))),
    ('midship 1', 'midship', 3, False, dict(weapons=(0, 0, 1))),
    ('midship 2', 'midship', 3, False, dict(weapons=(0, 0, 2))),
    ('midship 2 firing', 'midship', 40, True, dict(weapons=(0, 0, 2))),
    ('midship 1 firing, night', 'midship', 44, True, dict(weapons=(1, 1, 1), region=1, mission=4, rank=9)),
    ('midship 0 firing', 'midship', 52, True, dict(weapons=(0, 0, 0))),
    ('stern 0', 'stern', 3, False, dict(weapons=(0, 0, 1))),
    ('stern 1', 'stern', 3, False, dict(weapons=(0, 1, 1))),
    ('stern 0 firing', 'stern', 44, True, dict(weapons=(0, 0, 1))),
    ('stern 0 firing, later', 'stern', 52, True, dict(weapons=(0, 0, 1))),
    ('stern 1 firing, night', 'stern', 45, True, dict(weapons=(1, 1, 2), region=2, mission=3, rank=9)),
    ('stern, practice 2 (night)', 'stern', 6, False, dict(practice=2)),
]


def shake(m, rng):
    """Targeted randomization of what view_present reads (see the module docstring)."""
    for off in (SKY_TOP, SKY_TOP_PREV):
        put8(m, off, rng.choice([get8(m, off), 0, 0x20, 0x21, 0x22, 0x2B, 0x2C, 0x2D, 0x37, 0x38, 0x39, 0x7F, 0xBF,
                                 0xC0, 0xFF, rng.randrange(0x40), rng.randrange(256)]))
    for off in FLASHES + LATCHES:
        if rng.random() < 0.6:
            put8(m, off, rng.choice([0, 0, 1, 2, rng.randrange(256)]))
    put8(m, WORLD_PASS, rng.randrange(256))
    if rng.random() < 0.5:
        for i in range(4):
            put8(m, HEADINGS + i, rng.randrange(256))
    if rng.random() < 0.25:
        put8(m, WEAPONS['bow'], rng.choice([0, 1, rng.randrange(256)]))
        put8(m, WEAPONS['midship'], rng.choice([0, 1, 2, rng.randrange(256)]))
        put8(m, WEAPONS['stern'], rng.choice([0, 1, rng.randrange(256)]))
    if rng.random() < 0.1:
        put16(m, STATION, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xFFFF, 0x0102, 0x0103]))
    if rng.random() < 0.1:
        put16(m, LOOK, rng.choice([0, 1, 2, 3, 0xFFFF, 0x0100, 0x0101]))
    if rng.random() < 0.2:
        for off in COLOURS:
            put8(m, off, rng.randrange(256))
    if rng.random() < 0.3:
        for off, size in SPRITE_AREAS:
            randomize(m, DS_BASE + off, size, rng)
    if rng.random() < 0.2:   # not read by view_present: the cracks, the chase view, the flash, the shake
        randomize(m, DS_BASE + 0x0B49, 5, rng)
        put8(m, CHASE, rng.choice([0, 1]))
        put8(m, 0xD9B5, rng.randrange(4))
        put8(m, 0xB7F2, rng.randrange(9))
    shake_pages(m, rng)


def pages(m, rng):
    """(source, destination): the callers' (1, 0) and (1, view_page) mostly, other pages sometimes."""
    return rng.choice([(1, 0)] * 6 + [(1, get16(m, VIEW_PAGE))] * 2 + [(1, 1), (0, 1), (2, 0), (0, 0)])


def describe(m, src, dst):
    return 'station %X look %X weapons %d/%d/%d sky %02X/%02X flash %s latch %s D70C %02X pages %d->%d' % (
        get16(m, STATION), get16(m, LOOK), get8(m, WEAPONS['bow']), get8(m, WEAPONS['midship']),
        get8(m, WEAPONS['stern']), get8(m, SKY_TOP), get8(m, SKY_TOP_PREV),
        bytes(m[DS_BASE + 0xB839:DS_BASE + 0xB83D]).hex(),
        ''.join('%02X' % get8(m, off) for off in LATCHES), get8(m, WORLD_PASS), src, dst)


def test_view_present(h, rng, scale):
    n = 0
    for label, where, hits, fire, kw in STATES:
        base = present_state(h, where, hits, fire, **kw)
        for i in range(12 * scale):
            m = bytearray(base)
            src, dst = 1, 0
            if i:
                shake(m, rng)
                src, dst = pages(m, rng)
            h.check('view_present', m, stack_args=[src, dst], outputs=['si'], regs={'si': 0x5A5A}, max_insns=BIG,
                    label='%s, case %d: %s' % (label, i, describe(m, src, dst)))
            n += 1
    return n


def test_view_present_branches(h, rng, scale):
    """Every branch by construction, on one state per station: the sky tops across the limits, the
    flash counters and latches in every combination the code distinguishes, both parities of the
    world pass counter, every weapon fit and unknown ones."""
    n = 0
    guns = {'bow': (0, 0, 1), 'midship': (0, 0, 2), 'stern': (0, 1, 1)}     # firing, the belt fits
    states = {where: present_state(h, where, 40 if where in guns else 3, where in guns,
                                   weapons=guns.get(where, (0, 0, 1)))
              for where in STATION_KEYS}
    skies = [(0x00, 0x00), (0x37, 0x40), (0x38, 0x10), (0x39, 0x39), (0x2B, 0x2B), (0x2C, 0x2C), (0x2D, 0x50),
             (0x21, 0x22), (0x22, 0x21), (0xBF, 0xFF), (0xC0, 0xC0), (0xFF, 0xFE), (0x80, 0x80)]
    for where, base in states.items():
        for sky, prev in skies:
            m = bytearray(base)
            put8(m, SKY_TOP, sky)
            put8(m, SKY_TOP_PREV, prev)
            shake_pages(m, rng)
            h.check('view_present', m, stack_args=[1, 0], outputs=['si'], regs={'si': 0x5A5A}, max_insns=BIG,
                    label='%s sky %02X/%02X' % (where, sky, prev))
            n += 1
    fits = {'bow': [0, 1, 2, 0xFF], 'midship': [0, 1, 2, 3, 0x80], 'stern': [0, 1, 2, 0xFF]}
    for where in ('bow', 'midship', 'stern'):
        for weapon in fits[where]:
            for latch in (0, 1, 2):
                for prev in (0, 1):
                    for world_pass in (0x10, 0x11):
                        m = bytearray(states[where])
                        put8(m, WEAPONS[where], weapon)
                        put8(m, WORLD_PASS, world_pass)
                        if where == 'bow':
                            put8(m, 0xB83A, latch)
                            put8(m, 0xB83B, prev * 2)
                        elif where == 'midship':
                            put8(m, 0xB839, rng.choice([0, latch]))
                            put8(m, 0xF132, latch)
                            put8(m, 0xECA8, prev)
                        else:
                            put8(m, 0xF10B, latch)
                            put8(m, 0xEA86, prev)
                        h.check('view_present', m, stack_args=[1, 0], outputs=['si'], regs={'si': 0x5A5A}, max_insns=BIG,
                                label='%s weapon %d latch %d prev %d pass %02X' % (
                                    where, weapon, latch, prev, world_pass))
                        n += 1
    for station in (0, 5, 6, 7, 8, 9, 0x0101, 0xFFFF):
        m = bytearray(states['pilot'])
        put16(m, STATION, station)
        h.check('view_present', m, stack_args=[1, 0], outputs=['si'], regs={'si': 0x5A5A}, label='station %X' % station)
        n += 1
    for look in (3, 0x0100, 0xFFFF):
        m = bytearray(states['pilot'])
        put16(m, LOOK, look)
        h.check('view_present', m, stack_args=[1, 0], outputs=['si'], regs={'si': 0x5A5A}, label='look %X' % look)
        n += 1
    return n


def test_view_present_frames(h, rng, scale):
    """Successive presents of one frame: the second call sees the first one's sky top and screen."""
    n = 0
    for where in STATION_KEYS:
        m = present_state(h, where, 3)
        for k in range(3):
            h.check('view_present', m, stack_args=[1, 0], outputs=['si'], regs={'si': 0x5A5A}, max_insns=BIG, label='%s, call %d' % (where, k))
            m = bytearray(h.orig.memory())
            put8(m, SKY_TOP, rng.randrange(0x40))
            n += 1
    return n


TESTS = [test_view_present, test_view_present_branches, test_view_present_frames]
