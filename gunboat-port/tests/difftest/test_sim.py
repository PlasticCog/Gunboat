"""The simulation's leaves (simulation.md §3.4, §4, §5, §6.1, §7, §8, §9; sound.md §2.1 for
engine_sound_update): controls, heading steps, fire keys, projectiles, hit and sight tests,
messages and the readout digits, the time of day, the camera's pitch and wave bob.

Every case starts from a mission state that the original reached itself (mission_states.py:
practice missions at their first frame, campaign missions after 10-40 frames with their visible
lists and sprite caches), then randomizes what the function reads, with edge values where the code
branches. The pure arithmetic kernels (heading steps, elevation clamp) also run on a random DGROUP.
All memory is compared, and the registers the callers use."""
import struct

from gbdiff import DS_BASE, STACK_BOTTOM, put8, put16, randomize
from mission_states import mission_state

F10 = 0x8A

# Mission states: (kwargs for mission_state). Stations 1-4, day and night, several weapon fits.
STATES = [
    dict(practice=1),
    dict(practice=2),
    dict(practice=3),
    dict(region=0, mission=3, rank=5, station=1, frames=40),
    dict(region=1, mission=5, rank=9, station=2, frames=20, night=True),
    dict(region=2, mission=2, rank=3, station=3, frames=30, weapons=(1, 1, 2)),
    dict(region=3, mission=1, rank=1, station=4, frames=12, weapons=(1, 0, 0)),
    dict(region=0, mission=6, rank=7, station=2, frames=25, keys=[0, 0, F10]),
]


def get8(m, off):
    return m[DS_BASE + off]


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def state(h, rng, which=None):
    """A copy of one of the mission states (random, or STATES[which])."""
    kw = STATES[rng.randrange(len(STATES)) if which is None else which]
    return mission_state(h, **kw)


def random_dgroup(h, rng):
    m = h.fresh_memory()
    randomize(m, DS_BASE, STACK_BOTTOM, rng)
    return m


def rbyte(rng, edges):
    return rng.choice(edges) if rng.random() < 0.6 else rng.randrange(256)


# ---------------------------------------------------------------- heading steps and aiming

def test_heading_steps(h, rng, scale):
    """heading_step_plus / heading_step_minus: every fraction and heading, the step tables of the
    four control rates and the propulsion slot, and random BX (the table index is 16-bit)."""
    n = 0
    for name in ('heading_step_plus', 'heading_step_minus'):
        for i in range(1500 * scale):
            m = random_dgroup(h, rng) if i % 2 else state(h, rng)
            bx = rng.choice([0, 1, 2, 3, 4]) if rng.random() < 0.8 else rng.randrange(0x10000)
            if bx == 4 or rng.random() < 0.3:
                put8(m, 0xD629 + 4, rng.randrange(0x38) if rng.random() < 0.7 else rng.randrange(256))
            ax = rng.randrange(0x10000)
            if rng.random() < 0.5:
                ax = ax & 0xFF00 | rng.randrange(8)
            h.check(name, m, regs={'ax': ax, 'bx': bx}, outputs=['ax'], label='case %d bx %04X' % (i, bx))
            n += 1
    return n


def aim_memory(h, rng):
    m = state(h, rng)
    for off in (0xB81E, 0xB81F, 0xB820, 0xB821):             # headings
        put8(m, off, rng.randrange(256))
    for off in (0xB822, 0xB823, 0xB824, 0xB825):             # fractions
        put8(m, off, rng.randrange(8) if rng.random() < 0.8 else rng.randrange(256))
    for off in (0xB836, 0xB837, 0xB838):                     # elevations
        put8(m, off, rbyte(rng, [0x5F, 0x60, 0x61, 0x62, 0x64, 0x70, 0x80, 0xDF, 0xE0, 0xEF, 0xF0, 0xFB, 0xFC,
                                 0xFD, 0xFE, 0xFF]))
    put8(m, 0xD965, rng.randrange(256))
    put8(m, 0xD966, rng.randrange(256))
    return m


def near_arc(rng, m, gun, bow):
    """Put the gun's heading next to its blocked arc relative to the hull."""
    hull = get8(m, 0xB81E)
    edge = (hull + (0x60 if bow else 0xE0) + rng.choice([-3, -2, -1, 0, 1, 2, 0x41, 0x42, 0x43, 0x44])) & 0xFF
    put8(m, gun, edge)


def test_aim(h, rng, scale):
    n = 0
    guns = {'stern': (0xB821, False), 'midship': (0xB820, False), 'bow': (0xB81F, True)}
    for gun, (heading, bow) in guns.items():
        for name in ('aim_' + gun, 'aim_%s_left' % gun, 'aim_%s_right' % gun):
            for i in range(500 * scale):
                m = aim_memory(h, rng)
                if rng.random() < 0.5:
                    near_arc(rng, m, heading, bow)
                bx = rng.choice([0, 1, 2, 3]) if rng.random() < 0.9 else rng.randrange(0x10000)
                cl = rng.randrange(0x20) if rng.random() < 0.9 else rng.randrange(256)
                h.check(name, m, regs={'cx': rng.randrange(0x100) << 8 | cl, 'bx': bx, 'ax': rng.randrange(0x10000)},
                        outputs=['ax'], label='case %d cl %02X bx %04X' % (i, cl, bx))
                n += 1
    return n


def test_rotate_headings(h, rng, scale):
    n = 0
    for name in ('rotate_headings_minus', 'rotate_headings_plus'):
        for i in range(600 * scale):
            m = aim_memory(h, rng)
            put8(m, 0xD62D, rng.randrange(0x38) if rng.random() < 0.8 else rng.randrange(256))
            bx = 4 if rng.random() < 0.7 else rng.choice([0, 1, 2, 3, rng.randrange(0x10000)])
            h.check(name, m, regs={'bx': bx}, label='case %d bx %04X' % (i, bx))
            n += 1
    return n


# ---------------------------------------------------------------- throttles and speed

def test_throttle(h, rng, scale):
    n = 0
    edges = [0, 1, 7, 8, 9, 10, 0x3A, 0x3B, 0x3C, 0x66, 0x67, 0x68, 0xFF]
    for i in range(2000 * scale):
        m = state(h, rng)
        for off in (0xB816, 0xB817):
            put8(m, off, rbyte(rng, edges))
        for off in (0xB81C, 0xB81D):
            put8(m, off, rng.choice([0x3B, 0x67, rng.randrange(256)]))
        for off in (0xB808, 0xB809):
            put8(m, off, rng.choice([0, 1, 1, 1, 2, 0x80, 0x7F, rng.randrange(256)]))
        if rng.random() < 0.3:                               # equal throttles
            put8(m, 0xB817, get8(m, 0xB816))
        put8(m, 0xB818, rng.choice([0x40, 0xC0, 0x00, 0x7D, 0x80, rng.randrange(256)]))
        cl = rng.choice([1, 2, 3, 0, 0x11, 0x12, rng.randrange(256)])
        if i % 3 == 0:
            h.check('pilot_slow_down', m, label='case %d' % i)
        else:
            si = rng.choice([0, 1]) if rng.random() < 0.95 else rng.randrange(0x10000)
            h.check('throttle_step', m, regs={'si': si, 'cx': cl}, label='case %d si %04X cl %02X' % (i, si, cl))
        n += 1
    return n


def test_accelerate_speed(h, rng, scale):
    n = 0
    edges = [0, 1, 2, 0x7E, 0x7F, 0x80, 0x81, 0xFE, 0xFF]
    for i in range(2000 * scale):
        m = state(h, rng) if i % 2 else random_dgroup(h, rng)
        speed = rbyte(rng, edges)
        put8(m, 0xB828, speed)
        target = rbyte(rng, edges) if rng.random() < 0.7 else (speed + rng.choice([-1, 0, 1])) & 0xFF
        put8(m, 0xD6B4, rng.choice([0, 0, 0, 1, rng.randrange(256)]))
        put16(m, 0xD982, rng.choice([0, 5, 0xFFFB, 0x7FFE, 0x8002, rng.randrange(0x10000)]))
        h.check('accelerate_speed', m, regs={'cx': target << 8 | rng.randrange(256)},
                label='case %d speed %02X target %02X' % (i, speed, target))
        n += 1
    return n


def test_evade_incoming(h, rng, scale):
    n = 0
    for i in range(300 * scale):
        m = state(h, rng)
        for k in range(16):
            put8(m, 0xD70E + k, rng.choice([0, 0x20, 0x21, 0x1F, 0xE0, rng.randrange(256)]))
        h.check('evade_incoming', m, regs={'bx': rng.randrange(0x10000)}, outputs=['bx'], label='case %d' % i)
        n += 1
    return n


# ---------------------------------------------------------------- firing and projectiles

def fire_memory(h, rng):
    m = state(h, rng)
    for off in (0xD525, 0xD526, 0xD529, 0xD52A, 0xD52E, 0xD52F):   # mounts: bit 0 = off
        put8(m, off, rng.choice([0x20, 0x21, 0x40, 0x41, 0x08, 0x09]) if rng.random() < 0.3 else get8(m, off) & 0xFE)
    for off in (0xB804, 0xB806, 0xB807):                          # weapon fits
        put8(m, off, rng.choice([0, 0, 1, 1, 2, 3, rng.randrange(256)]))
    put8(m, 0xB830, rng.choice([8, 8, 7, 0, 9, rng.randrange(256)]))   # reload counters
    put8(m, 0xB831, rng.choice([0x30, 0x30, 0x2F, 0, 0x31, rng.randrange(256)]))
    put8(m, 0xB7F1, rng.choice([0, 0, 1, 2]))
    put8(m, 0xD70C, rng.randrange(256))
    put8(m, 0xD644, rng.randrange(256))
    for off in (0xB81F, 0xB820, 0xB821, 0xB823, 0xB824, 0xB825, 0xB836, 0xB837, 0xB838, 0xB82C, 0xB82D):
        put8(m, off, rng.randrange(256))
    timers(m, rng)
    put8(m, 0xDA46, rng.choice([0, 1, 1]))                        # effects timer on (sfx_play plays)
    return m


def timers(m, rng):
    """Projectile countdowns: free, busy or all busy."""
    mode = rng.randrange(4)
    for k in range(32):
        if mode == 0:
            v = 0
        elif mode == 1:
            v = rng.randrange(1, 256)
        else:
            v = 0 if rng.random() < 0.2 else rng.randrange(256)
        put8(m, 0xD1BC + k, v)


def test_fire(h, rng, scale):
    n = 0
    for name in ('fire_station4', 'fire_station3', 'fire_bow'):
        for i in range(700 * scale):
            h.check(name, fire_memory(h, rng), regs={'ax': rng.randrange(0x10000)}, outputs=['ax'], label='case %d' % i)
            n += 1
    return n


def test_projectile_launch(h, rng, scale):
    n = 0
    for i in range(1500 * scale):
        m = fire_memory(h, rng)
        put8(m, 0xB7F8, rng.randrange(256))
        bx = rng.choice([1, 2, 3, 4, 5]) if rng.random() < 0.9 else rng.randrange(0x100)
        cx = rng.randrange(0x10000)
        h.check('projectile_launch', m, regs={'bx': bx, 'cx': cx, 'si': rng.randrange(0x10000)}, outputs=['si', 'ax'],
                label='case %d weapon %d cx %04X' % (i, bx, cx))
        n += 1
    return n


def test_projectile_alloc(h, rng, scale):
    n = 0
    for i in range(400 * scale):
        m = state(h, rng)
        timers(m, rng)
        h.check('projectile_alloc', m, outputs=['bx', 'cx'], label='case %d' % i)
        n += 1
    return n


def test_projectile_aim(h, rng, scale):
    """Every elevation class (flight time 1..18, ranges 0, 1 and the division), every heading
    quadrant and interpolation fraction, the boat anywhere."""
    n = 0
    for i in range(3000 * scale):
        m = state(h, rng) if i % 4 else random_dgroup(h, rng)
        put16(m, 0xC12D, rng.randrange(0x10000))
        put16(m, 0xC8FD, rng.randrange(0x10000))
        slot = rng.randrange(32)
        al = rng.choice([0, 0x6F, 0x70, 0x77, 0x78, 0xEB, 0xEC, 0xED, 0xEE, 0xFF]) if rng.random() < 0.3 else rng.randrange(256)
        ax = rng.choice([1, 2, 3, 4, 5, rng.randrange(256)]) << 8 | al
        cx = rng.randrange(0x10000)
        h.check('projectile_aim', m, regs={'ax': ax, 'bx': slot, 'cx': cx, 'si': slot * 8}, outputs=['ax'],
                label='case %d ax %04X cx %04X slot %d' % (i, ax, cx, slot))
        n += 1
    return n


def test_elevation_add_clamped(h, rng, scale):
    n = 0
    edges = [0, 1, 0x7F, 0x80, 0x81, 0xFE, 0xFF]
    for i in range(2000 * scale):
        m = random_dgroup(h, rng)
        put8(m, 0xB7F8, rbyte(rng, edges))
        ax = rng.randrange(0x100) << 8 | rbyte(rng, edges)
        h.check('elevation_add_clamped', m, regs={'ax': ax}, outputs=['ax'], label='case %d ax %04X' % (i, ax))
        n += 1
    return n


def test_muzzle_flash_tick(h, rng, scale):
    n = 0
    for i in range(200 * scale):
        m = state(h, rng)
        for off in (0xB839, 0xB83A, 0xB83B, 0xB83C):
            put8(m, off, rng.choice([0, 1, 2, rng.randrange(256)]))
        h.check('muzzle_flash_tick', m, regs={'si': 0x1234, 'di': 0x5678}, outputs=['si', 'di'], label='case %d' % i)
        n += 1
    return n


# ---------------------------------------------------------------- objects, sight and hits

def visible_states(h):
    """The mission states that have a visible list (campaign missions after some frames)."""
    return [i for i, kw in enumerate(STATES) if kw.get('frames')]


def test_line_of_sight(h, rng, scale):
    """Every visible entry of the campaign states, as the renderer left them, then with bearings
    moved next to the covering limits, other kinds and zero sprites."""
    n = 0
    for which in visible_states(h):
        base = state(h, rng, which)
        count = get16(base, 0xB83D)
        for e in range(count):
            h.check('line_of_sight', base, regs={'si': 2 * e, 'cx': rng.randrange(0x10000)}, outputs=['cx'],
                    label='state %d entry %d' % (which, e))
            n += 1
    for i in range(1500 * scale):
        m = state(h, rng, rng.choice(visible_states(h)))
        count = get16(m, 0xB83D)
        e = rng.randrange(count) if count else 0
        for k in range(e + 1, min(count, e + 12)):
            if rng.random() < 0.5:
                put8(m, 0x4E00 + k, (get8(m, 0x4E00 + e) + rng.randrange(-40, 41)) & 0xFF)
            if rng.random() < 0.1:
                put8(m, 0x5189 + k, rng.choice([0, 0x68, 0x69, 0x6A]))
            if rng.random() < 0.1:
                obj = get16(m, 0x523E + 2 * k)
                put8(m, 0xB95D + obj, rng.choice([0x10, 0x35, 0x12, 0x01]))
        if rng.random() < 0.05:
            put16(m, 0xB83D, rng.randrange(0xB6))
        h.check('line_of_sight', m, regs={'si': 2 * e, 'cx': rng.randrange(0x10000)}, outputs=['cx'],
                label='case %d entry %d' % (i, e))
        n += 1
    return n


def test_hit_test(h, rng, scale):
    """Shots aimed at a visible entry (bearing and range near the entry, to reach every limit test)
    and random shots."""
    n = 0
    for i in range(3000 * scale):
        m = state(h, rng, rng.choice(visible_states(h)))
        count = get16(m, 0xB83D)
        e = rng.randrange(count)
        view = get8(m, 0xD191) << 8 | get8(m, 0xD192)
        if rng.random() < 0.3:
            view = rng.randrange(0x10000)
            put8(m, 0xD191, view >> 8)
            put8(m, 0xD192, view & 0xFF)
        entry = get8(m, 0x4E00 + e) << 8 | get8(m, 0x4EB5 + e)
        if rng.random() < 0.8:
            shot = (entry + view - 0x4C00 + rng.randrange(-0x600, 0x600)) & 0xFFFF
            rng_ = (get8(m, 0x4F6A + e) + 2 - rng.randrange(-4, 0x30)) & 0xFF
        else:
            shot, rng_ = rng.randrange(0x10000), rng.randrange(256)
        put16(m, 0xD749, shot)
        put8(m, 0xD74B, rng_)
        if rng.random() < 0.05:
            put8(m, 0x5189 + e, rng.choice([0, 1, 0x68, 0x69, 0xB7]))
        ax = rng.randrange(0x10000)
        h.check('hit_test', m, regs={'bx': e, 'ax': ax, 'cx': rng.randrange(0x10000)}, outputs=['ax', 'bx'],
                label='case %d entry %d shot %04X range %02X' % (i, e, shot, rng_))
        n += 1
    return n


def test_terrain_structure_break(h, rng, scale):
    n = 0
    for i in range(1500 * scale):
        m = state(h, rng)
        si = rng.randrange(1000) * 2
        x, y = get16(m, 0xC12D + si), get16(m, 0xC8FD + si)
        count = rng.choice([0, 1, 2, 5, 16, 32, rng.randrange(33)])
        put16(m, 0xD0CD, count)
        for k in range(32):
            same = rng.random() < 0.6
            sx = (x & 0xFC00 | rng.randrange(0x400)) if same else rng.randrange(0x10000)
            sy = (y & 0xFC00 | rng.randrange(0x400)) if same or rng.random() < 0.3 else rng.randrange(0x10000)
            put16(m, 0xD10F + 2 * k, sx)
            put16(m, 0xD14F + 2 * k, sy)
            put16(m, 0xD0CF + 2 * k, rng.randrange(256) << 8 | rng.choice(
                [0x50, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, rng.randrange(256)]))
        ah = rng.choice([0x10, 0x11, 0x20, 0x21, 0x10, 0x20, 0x12, 0x30, rng.randrange(256)])
        ax = ah << 8 | rng.randrange(256)
        bx = rng.randrange(0x10000)
        h.check('terrain_structure_break', m, regs={'ax': ax, 'si': si, 'bx': bx}, outputs=['ax', 'bx', 'si'],
                label='case %d kind %02X count %d' % (i, ah, count))
        n += 1
    return n


def test_free_temp_object(h, rng, scale):
    n = 0
    for i in range(500 * scale):
        m = state(h, rng)
        full = rng.random() < 0.3
        for k in range(1, 36):
            if full or rng.random() < 0.8:
                put8(m, 0xB95D + 2 * k, rng.randrange(1, 256))
            else:
                put8(m, 0xB95D + 2 * k, 0)
        ax = rng.randrange(0x10000)
        h.check('free_temp_object', m, regs={'ax': ax}, outputs=['ax', 'bx'], label='case %d' % i)
        n += 1
    return n


def test_spawn_enemy_wake(h, rng, scale):
    n = 0
    for i in range(500 * scale):
        m = state(h, rng)
        for k in range(1, 36):
            if rng.random() < 0.7:
                put8(m, 0xB95D + 2 * k, rng.randrange(1, 256))
        bx = rng.randrange(1000) * 2
        si = rng.randrange(0xB6) * 2
        regs = {'bx': bx, 'si': si, 'ax': rng.randrange(0x10000), 'cx': rng.randrange(0x10000)}
        h.check('spawn_enemy_wake', m, regs=regs, outputs=['ax', 'bx', 'cx', 'si'], label='case %d' % i)
        n += 1
    return n


def test_move_toward(h, rng, scale):
    n = 0
    for i in range(2000 * scale):
        m = state(h, rng)
        bx = rng.randrange(1000) * 2
        x, y = get16(m, 0xC12D + bx), get16(m, 0xC8FD + bx)
        if rng.random() < 0.7:
            cx = (x + rng.randrange(-40, 41)) & 0xFFFF
            dx = (y + rng.randrange(-40, 41)) & 0xFFFF
            if rng.random() < 0.2:
                dx = (y + (cx - x)) & 0xFFFF            # equal distances
        else:
            cx, dx = rng.randrange(0x10000), rng.randrange(0x10000)
        for off in (0xD6A2, 0xD6A4):
            put16(m, off, rng.choice([0, 1, 2, 3, 5, 0xFFFB, 0xFFFE, 0x8000, rng.randrange(0x10000)]))
        h.check('move_toward', m, regs={'bx': bx, 'cx': cx, 'dx': dx}, outputs=['bx'],
                label='case %d obj %04X to %04X,%04X' % (i, bx, cx, dx))
        n += 1
    return n


def test_score_add(h, rng, scale):
    n = 0
    for i in range(300 * scale):
        m = state(h, rng)
        dl = rng.choice([0, 2, 4, 6, 8, 10, 12, 14, rng.randrange(256)])
        put16(m, 0xB52E + dl, rng.choice([0, 9, 0x99, 0x9999, rng.randrange(0x10000)]))
        regs = {'dx': rng.randrange(0x100) << 8 | dl, 'bx': rng.randrange(0x10000), 'cx': rng.randrange(0x10000)}
        h.check('score_add', m, regs=regs, outputs=['bx', 'cx', 'dx'], label='case %d dl %02X' % (i, dl))
        n += 1
    return n


# ---------------------------------------------------------------- messages and readouts

def test_show_message(h, rng, scale):
    """Every message of the table with each attribute's branches: the line locked (state 2), the bow
    gunner dead or alive, each component name, practice and campaign missions, identified objects
    (none, kind 3Ch) and the mission end with its score word; also through the page-0 and far
    entries."""
    n = 0
    for i in range(700 * scale):
        m = state(h, rng)
        put8(m, 0xB800, rng.choice([0, 1, 2, 0, 1]))
        put8(m, 0xD513, rng.choice([0, 1, 2, 3, rng.randrange(256)]))
        put8(m, 0xD194, rng.randrange(16))
        put16(m, 0xF110, rng.choice([0, 1, 2, 3, 0x100]))
        put16(m, 0xB505, rng.choice([0, 1, 2, 7, 8, 9, rng.randrange(0x10000)]))
        obj = rng.choice([0, 0, rng.randrange(1000) * 2])
        put16(m, 0xD60F, obj)
        if obj and rng.random() < 0.3:
            put8(m, 0xB95D + obj, 0x3C)
        put16(m, 0xB52C, rng.randrange(0x10000))
        msg = i % 0x38 if i < 0x38 * 4 else rng.randrange(0x38)
        which = rng.randrange(3)
        if which == 0:
            h.check('show_message', m, regs={'ax': rng.randrange(0x100) << 8 | msg}, label='case %d msg %02X' % (i, msg))
        elif which == 1:
            h.check('show_message_page0', m, regs={'ax': rng.randrange(0x100) << 8 | msg},
                    label='case %d msg %02X' % (i, msg))
        else:
            h.check('show_message_far', m, stack_args=[rng.randrange(0x100) << 8 | msg],
                    label='case %d msg %02X' % (i, msg))
        n += 1
    return n


def test_readout_digits(h, rng, scale):
    """The clock and heading digits: every AL, both carry values (print_3digits with CF entered at
    0919:1729 inside the heading readout), counts CL and digit offsets B7E3."""
    h.sym.funcs['print_3digits_carry'] = (0x0919, 0x1729, False)
    n = 0
    for i in range(600 * scale):
        m = state(h, rng)
        put8(m, 0xD9C1, rng.randrange(0x24))       # text column (text_col)
        put8(m, 0xD9C2, rng.randrange(0xC0))       # text row in pixels (text_y)
        put8(m, 0xB7E3, rng.choice([0, 1, 2, 3, rng.randrange(256)]) if rng.random() < 0.2 else rng.choice([0, 1]))
        al = rng.randrange(256)
        ax = rng.randrange(0x100) << 8 | al
        k = i % 6
        if k == 0:
            h.check('print_colon', m, label='case %d' % i)
        elif k == 1:
            h.check('print_bcd_2digits', m, regs={'ax': ax}, label='case %d al %02X' % (i, al))
        elif k == 2:
            h.check('print_3digits', m, regs={'ax': ax}, label='case %d al %02X' % (i, al))
        elif k == 3:
            v = rng.randrange(0x200) if rng.random() < 0.8 else rng.choice([0x100, 0x167, 0x1C7, 0x1C8, 0x1FF])
            h.check('print_3digits_carry', m, regs={'ax': v << 7}, label='case %d value %d' % (i, v))
        elif k == 4:
            h.check('print_digits_hundreds', m, regs={'ax': ax}, label='case %d al %02X' % (i, al))
        else:
            cl = rng.choice([0, 0, 1, 2, rng.randrange(256)])
            h.check('print_digits_tens', m, regs={'ax': ax, 'cx': cl}, label='case %d al %02X cl %02X' % (i, al, cl))
        n += 1
    return n


def test_end_mission(h, rng, scale):
    n = 0
    for i in range(20):
        h.check('end_mission', state(h, rng), label='case %d' % i)
        n += 1
    return n


# ---------------------------------------------------------------- clock, camera, engine noise

def test_time_of_day(h, rng, scale):
    """Every hour (BCD, with invalid ones) and minute around the dawn and dusk steps, the day flags
    before (so that some calls change nothing) and every video mode."""
    n = 0
    for i in range(2500 * scale):
        m = state(h, rng)
        hours = rng.choice([0x05, 0x06, 0x07, 0x18, 0x19, 0x20, 0x00, 0x12, 0x23, 0x0A, 0x1A]) if rng.random() < 0.8 \
            else rng.randrange(256)
        minutes = rng.choice([0, 3, 4, 6, 7, 0x36, 0x37, 0x39, 0x3A, 0x3B]) if rng.random() < 0.7 else rng.randrange(256)
        put8(m, 0xB54B, hours)
        put8(m, 0xB54A, minutes)
        put8(m, 0xB7FC, rng.choice([0, 1, rng.randrange(256)]))
        put8(m, 0xB7FD, rng.choice([0, 1, 2, rng.randrange(256)]))
        put16(m, 0xEED2, rng.choice([0x13, 0x13, 0x0D, 0x09, 0x04, rng.randrange(0x10000)]))
        h.check('time_of_day', m, label='case %d %02X:%02X' % (i, hours, minutes))
        n += 1
    return n


def test_camera_pitch_bob(h, rng, scale):
    n = 0
    for i in range(3000 * scale):
        m = state(h, rng)
        put16(m, 0xD982, rng.choice([0, 1, 3, 4, 5, 0x17, 0x18, 0x1C, 0x1D, 0xFFFF, 0xFFFC, 0xFFFB, 0xFFE8, 0xFFE4,
                                     0xFFE3, 0x7FFF, 0x8000, rng.randrange(0x10000)]))
        put8(m, 0xB82A, rbyte(rng, [0, 1, 0x7F, 0x80, 0x81, 0xFF, 0xFE]))
        put16(m, 0xB82B, rng.choice([0, 0x80, 0xFF80, 0xFFFF, 0x7F00, rng.randrange(0x10000)]))
        put8(m, 0xD967, rng.choice([1, 1, 2, 0, rng.randrange(256)]))
        put8(m, 0xD968, rng.randrange(256))
        put8(m, 0xD969, rng.randrange(256) if rng.random() < 0.1 else rng.randrange(4))
        put16(m, 0x0086, rng.choice([1, 2, 3, 4]) if rng.random() < 0.8 else rng.randrange(0x10000))
        for off in (0xB836, 0xB837, 0xB838, 0xB819, 0x008A):
            put8(m, off, rng.randrange(256))
        put8(m, 0xB819, rng.choice([0, 1, 5, 9, 0x0F, 0x10, rng.randrange(256)]))
        put16(m, 0xB4FF, rng.choice([0, 1, 2, 3]) if rng.random() < 0.95 else rng.randrange(0x10000))
        if i % 10 == 0:                                     # a new swing whose period comes out 0 (-> 1)
            put8(m, 0xD967, 1)
            put8(m, 0xD969, 1)
            sea = get16(m, 0xB4FF)
            mask = get16(m, (0xD97A + 2 * sea) & 0xFFFF)
            x = ((get8(m, 0x008A) & mask) + 0x1D) & 0xFF
            put8(m, 0xB819, x * 171 & 0xFF)                 # 3 * 171 = 1 (mod 256)
        h.check('camera_pitch_bob', m, label='case %d' % i)
        n += 1
    return n


def test_engine_sound_update(h, rng, scale):
    n = 0
    for i in range(800 * scale):
        m = state(h, rng)
        put16(m, 0x0080, rng.choice([0, 0, 0, 1, 0x100]))
        for off in (0xB816, 0xB817):
            put8(m, off, rbyte(rng, [0, 1, 2, 3, 4, 8, 0x20, 0x3B, 0x67, 0x80, 0xFF]))
        put16(m, 0xDAF0, rng.choice([0, 0, 1, 2]))
        put8(m, 0xDA46, rng.choice([0, 1, 1]))
        h.check('engine_sound_update', m, label='case %d throttles %02X %02X' % (i, get8(m, 0xB816), get8(m, 0xB817)))
        n += 1
    return n


TESTS = [test_heading_steps, test_aim, test_rotate_headings, test_throttle, test_accelerate_speed,
         test_evade_incoming, test_fire, test_projectile_launch, test_projectile_alloc, test_projectile_aim,
         test_elevation_add_clamped, test_muzzle_flash_tick, test_line_of_sight, test_hit_test,
         test_terrain_structure_break, test_free_temp_object, test_spawn_enemy_wake, test_move_toward,
         test_score_add, test_show_message, test_readout_digits, test_end_mission, test_time_of_day,
         test_camera_pitch_bob, test_engine_sound_update]
