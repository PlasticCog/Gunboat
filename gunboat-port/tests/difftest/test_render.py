"""The 3D renderer (render3d.md): camera, terrain window and projection, terrain primitives and their
VGA spans, the visible-object list, the sprite cache and blitter, spotlights, flash and shake, and
gfx_set_display_offset (video.md).

Memory states: mission_states.mission_state (the original run from main into a mission, stopped
at game_frame), in several regions, stations, frames, by day and at night, and variants of them
(the boat moved elsewhere with a forced terrain rebuild, turned views, the chase view, the lights
on, flash and shake). Routines deep in the frame (register arguments) are tested on the state the
original reaches them in: `capture` runs the original's game_frame (or another caller) from such
a state and snapshots memory and registers each time it enters the routine; the check then runs
both sides from that snapshot. Targeted randomisation on top covers the branches the frames do
not reach. All memory, the DAC and the listed registers are compared."""
import struct

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_SP, UC_X86_REG_SS

from gbdiff import (DS_BASE, MEM_SIZE, Mismatch, REGS, STACK_BOTTOM, UC_REGS, lin, put8, put16, randomize,
                    seg_of)
from mission_states import mission_state
from test_sound import sound

BIG = 400_000_000
VGA_PAGE_ROWS = (0x5000, 0xA000)

# Mission states: a practice state before its first frame (the first frame rebuilds everything),
# states some frames in, day and night, the chase view (',' pressed), and the three campaign regions.
BASES = [
    dict(practice=1),
    dict(practice=1, frames=3),
    dict(practice=2),
    dict(practice=3, frames=2),
    dict(region=0, mission=3, frames=40),
    dict(region=0, mission=0, frames=5),
    dict(region=0, mission=5, frames=30, keys=[0, 0, 0, 0x2C]),          # night, chase view
    dict(region=1, mission=2, rank=5, station=1, night=True, frames=10),  # night
    dict(region=2, mission=4, rank=9, station=1, frames=8),
    dict(region=2, mission=6, rank=9, station=2, frames=3),
]


def get8(m, off):
    return m[DS_BASE + off]


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def random_dgroup(h, rng):
    m = h.fresh_memory()
    randomize(m, DS_BASE, STACK_BOTTOM, rng)
    return m


def base(h, i):
    return mission_state(h, **BASES[i % len(BASES)])


def window_scenery(h, m):
    """The scenery count of the terrain window a rebuild at the boat's position loads (the
    original's terrain_cells_update on a copy)."""
    t = bytearray(m)
    put16(t, 0xD9AD, 0xFFFF)
    h.orig.set_memory(t)
    file_seg, off, far = h.sym.func('terrain_cells_update')
    h.orig.call(file_seg, off, far, {}, ())
    return h.orig.memory()[DS_BASE + 0xD9A4]


def moved(h, rng, i, rebuild=True, chase=None, night=None, lights=False):
    """Base state i with the boat somewhere else on the map, a random hull and view heading and
    pitch; a forced terrain rebuild; optionally the chase view, night and the spotlights on. The
    new place's terrain window has scenery (see test_empty_window)."""
    m = base(h, i)
    for _ in range(50):
        put16(m, 0xC12D, rng.randrange(0x05, 0x40) << 8 | rng.randrange(256))   # object 0 X
        put16(m, 0xC8FD, rng.randrange(0x05, 0x28) << 8 | rng.randrange(256))   # object 0 Y
        if window_scenery(h, m):
            break
    put8(m, 0xB826, rng.randrange(256))
    put8(m, 0xB827, rng.randrange(256))
    for k in range(4):                                                       # hull and gun headings
        put8(m, 0xB81E + k, rng.randrange(256))
        put8(m, 0xB822 + k, rng.randrange(256))
    put8(m, 0xB828, rng.choice([0, 0x10, 0x30, 0xF0, rng.randrange(256)]))  # speed
    put8(m, 0xB82D, rng.randrange(0x70, 0x90))                               # pitch reference
    if rebuild:
        put16(m, 0xD9AD, 0xFFFF)
    if chase is not None:
        put8(m, 0xD96B, chase)
        if chase:
            put8(m, 0xD96C, rng.randrange(256))
            put8(m, 0xD96D, rng.randrange(0x28, 0x100))
            put8(m, 0xD70C, rng.choice([0, 8, rng.randrange(256)]))
    if night is not None:
        put8(m, 0xB7FC, 0 if night else 1)
    if lights:
        put8(m, 0xB7FC, 0)
        put8(m, 0xD96B, 0)
        put8(m, 0xD520, get8(m, 0xD520) & 0xFE)
        for sw in (0xD528, 0xD52D, 0xD530, 0xD525, 0xD529, 0xD52E):
            put8(m, sw, get8(m, sw) & 0xFE if rng.random() < 0.85 else get8(m, sw) | 1)
        for cond in (0xD50C, 0xD50E, 0xD50F):
            put8(m, cond, rng.choice([0, 1, 3, 2]) if rng.random() < 0.3 else 0)
        for gun in (0xB836, 0xB837, 0xB838):
            put8(m, gun, rng.randrange(0x80, 0x100))
    return m


def capture(h, m, target, picks, caller='game_frame', regs=None, stack_args=()):
    """Runs `caller` on the original from memory m and returns (registers, stack words, memory) at
    the calls of `target` numbered in picks (0 = the first). Also returns how often it was called
    up to the last pick (all calls if a pick was not reached)."""
    file_seg, off, _ = h.sym.func(target)
    at = lin(seg_of(file_seg), off)
    picks = set(picks)
    last = max(picks)
    out, count = [], [0]

    def hook(uc, address, size, _):
        k = count[0]
        count[0] += 1
        if k in picks:
            r = {n: uc.reg_read(UC_REGS[n]) for n in REGS}
            ss, sp = uc.reg_read(UC_X86_REG_SS), uc.reg_read(UC_X86_REG_SP)
            words = [struct.unpack('<H', uc.mem_read((ss << 4) + ((sp + 2 + 2 * i) & 0xFFFF), 2))[0]
                     for i in range(3)]
            out.append((r, words, bytearray(uc.mem_read(0, MEM_SIZE))))
            if k >= last:
                h.orig.fail('stop')
    hk = h.orig.uc.hook_add(UC_HOOK_CODE, hook, None, at, at)
    try:
        with sound(h):
            h.orig.set_memory(m)
            fs, fo, far = h.sym.func(caller)
            h.orig.call(fs, fo, far, regs or {}, stack_args, max_insns=BIG)
    except Mismatch as e:
        if str(e) != 'original: stop':
            raise
    finally:
        h.orig.uc.hook_del(hk)
    return out


def frame_states(h, rng, scale, variants=6):
    """Base states and moved variants of them (game_frame entry states)."""
    states = [(str(BASES[i]), base(h, i)) for i in range(len(BASES))]
    for v in range(variants * scale):
        i = rng.randrange(len(BASES))
        kind = v % 4
        m = moved(h, rng, i, chase=1 if kind == 1 else None, night=kind == 2 or None, lights=kind == 3)
        states.append(('moved %d from %d (%s)' % (v, i, ['plain', 'chase', 'night', 'lights'][kind]), m))
    return states


def check_captured(h, rng, scale, target, picks, outputs=(), caller='game_frame', variants=6, stack=0,
                   mutate=None, need=1):
    """Checks `target` on the snapshots taken where the original's frames reach it."""
    n = 0
    for label, m in frame_states(h, rng, scale, variants):
        for k, (regs, words, snap) in enumerate(capture(h, m, target, picks, caller)):
            if mutate:
                mutate(snap, regs, rng)
            h.check(target, snap, regs=regs, stack_args=words[:stack], outputs=outputs,
                    label='%s, call %d' % (label, k), max_insns=BIG)
            n += 1
    if n < need:
        raise Mismatch('%s: only %d captured cases' % (target, n))
    return n


PICKS = [0, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377]


# ---------------------------------------------------------------- kernels on random memory

EDGE16 = [0, 1, 2, 0x7F, 0x80, 0xFF, 0x100, 0x3FFF, 0x4000, 0x7FFF, 0x8000, 0x8001, 0xFFFE, 0xFFFF]


def test_atan(h, rng, scale):
    n = 0
    for i in range(2000 * scale):
        m = random_dgroup(h, rng)
        cx = rng.choice(EDGE16) if rng.random() < 0.3 else rng.randrange(0x10000)
        dx = rng.choice(EDGE16 + [cx, (-cx) & 0xFFFF]) if rng.random() < 0.3 else rng.randrange(0x10000)
        if rng.random() < 0.3:                          # small vectors, as the projection sees them
            cx, dx = rng.randrange(-0x800, 0x800) & 0xFFFF, rng.randrange(-0x800, 0x800) & 0xFFFF
        h.check('atan', m, regs={'cx': cx, 'dx': dx, 'bx': rng.randrange(0x10000)}, outputs=['ax', 'cx', 'dx', 'bx'],
                label='case %d' % i)
        n += 1
    return n


def test_polar_small(h, rng, scale):
    n = 0
    for angle in range(256):
        for dl in [0, 1, 0x28, 0x80, 0xFF] + [rng.randrange(256) for _ in range(scale)]:
            m = random_dgroup(h, rng)
            h.check('polar_small', m, regs={'bx': rng.randrange(256) << 8 | angle, 'dx': rng.randrange(256) << 8 | dl},
                    outputs=['cx', 'dx'], label='angle %02X distance %02X' % (angle, dl))
            n += 1
    return n


def test_route_rotate(h, rng, scale):
    n = 0
    for i in range(600 * scale):
        m = random_dgroup(h, rng)
        put8(m, 0xB7E2, rng.choice([0, 1, 2, 3, rng.randrange(256)]))
        h.check('route_rotate', m, regs={'ax': rng.randrange(0x10000), 'cx': rng.randrange(0x10000)},
                outputs=['ax', 'cx'], label='case %d' % i)
        n += 1
    return n


def test_shore_edge_test(h, rng, scale):
    n = 0
    for i in range(1500 * scale):
        m = random_dgroup(h, rng)
        h.check('shore_edge_test', m, regs={'cx': rng.randrange(0x10000), 'dx': rng.randrange(0x10000)},
                label='case %d' % i)
        n += 1
    return n


def test_colour_remap(h, rng, scale):
    """All modes (VGA leaves the data alone; CGA, EGA and Tandy remap), ranges empty or reversed
    (the loop runs once), the tables anywhere."""
    n = 0
    for mode in (0x13, 0x0D, 0x09, 0x04, 0x0E, 0x0C, 0x03):
        for i in range(30 * scale):
            m = random_dgroup(h, rng)
            put16(m, 0xEED2, rng.randrange(256) << 8 | mode)
            di = rng.randrange(0x1000, 0xE000)
            dx = rng.choice([di, di + 1, di - 5, di + rng.randrange(1, 0x800)]) & 0xFFFF
            h.check('colour_remap', m, regs={'di': di, 'dx': dx, 'si': rng.randrange(0x10000), 'es': 0x2B73},
                    label='mode %X case %d' % (mode, i))
            n += 1
    return n


def test_video_mode_setup(h, rng, scale):
    n = 0
    for mode in [0x13, 0x0D, 0x0E, 0x09, 0x0A, 0x0C, 0x04, 0x00, 0xFF]:
        for _ in range(5 * scale):
            m = random_dgroup(h, rng)
            put16(m, 0xEED2, rng.randrange(256) << 8 | mode)
            h.check('video_mode_setup', m, label='mode %X' % mode)
            n += 1
    return n


def test_small(h, rng, scale):
    """terrain_save_view, order_reset, sprite_slots_reset, sprite_scale_patterns on random memory."""
    n = 0
    for i in range(150 * scale):
        m = random_dgroup(h, rng)
        for name in ('terrain_save_view', 'order_reset', 'sprite_slots_reset'):
            h.check(name, m, label='case %d' % i)
            n += 1
        put8(m, 0xD864, rng.choice([0, 0x17, 0x18, 0x2F, 0x30, 0x47, 0x48, 0x5F, 0x60, 0xFF, rng.randrange(256)]))
        put8(m, 0xD865, rng.choice([0, 0, 1, 2, 3, rng.randrange(256)]))
        h.check('sprite_scale_patterns', m, label='size %02X' % get8(m, 0xD864))
        n += 1
    return n


def test_palette_flash(h, rng, scale):
    """VGA (DAC 8 through INT 10h 1012h) and the modes that do nothing; EGA (0Dh) and Tandy (9) are
    parked (ega_pal_register)."""
    n = 0
    for i in range(300 * scale):
        m = random_dgroup(h, rng)
        put16(m, 0xEED2, rng.choice([0x13, 0x13, 0x13, 0x04, 0x0C, 0x0E, 0xFF]))
        put16(m, 0x0086, rng.choice([1, 2, 3, 4, 5, 6, 9, rng.randrange(0x10000)]))
        put8(m, 0xD9B5, rng.choice([0, 1, 2, 3, 4, 7, rng.randrange(256)]))
        put8(m, 0xD6E5, rng.choice([0, 1, 2, 3, get8(m, 0xD9B5), rng.randrange(256)]))
        h.check('palette_flash', m, label='case %d' % i)
        n += 1
    return n


class CrtcProbe:
    """The original's view of the CRTC for gfx_set_display_offset: port 3DAh alternates the
    retrace bit on each read, OUT 3D4h (index, value) is recorded."""

    def __init__(self, h):
        self.h, self.regs = h, {}
        self.saved = h.orig.ins.get(0x3DA), h.orig.outs.get(0x3D4)
        self.phase = 0

    def __enter__(self):
        self.h.orig.ins[0x3DA] = self._in
        self.h.orig.outs[0x3D4] = self._out
        return self

    def __exit__(self, *exc):
        self.h.orig.ins[0x3DA], saved_out = self.saved
        if saved_out is None:
            self.h.orig.outs.pop(0x3D4, None)
        else:
            self.h.orig.outs[0x3D4] = saved_out

    def _in(self, uc):
        self.phase ^= 8
        return self.phase

    def _out(self, uc, value):
        self.regs[value & 0xFF] = (value >> 8) & 0xFF

    def start(self):
        return ((self.regs.get(0x0C, 0) << 8 | self.regs.get(0x0D, 0)) << 2) & 0xFFFF


def test_gfx_set_display_offset(h, rng, scale):
    """Mode 13h: the CRTC start (compared through the port's VGA model) and the BIOS page offset;
    the modes whose dispatch only returns 0."""
    n = 0
    for i in range(200 * scale):
        m = random_dgroup(h, rng)
        mode_x2 = rng.choice([0x26, 0x26, 0x26, 0x00, 0x02, 0x04, 0x06, 0x0E, 0x10, 0x14])
        put16(m, 0xDCF8, mode_x2)
        struct.pack_into('<H', m, 0x463, 0x3D4)          # the BIOS's CRTC port (colour)
        x = rng.choice([0, 1, 3, 4, 0x13F, 0xFFFF, rng.randrange(0x10000)])
        y = rng.choice([0, 1, 2, 8, 199, 0xFFFF, rng.randrange(0x10000)])
        h.port.call('probe_vga_start', {})
        before = h.port.call('probe_vga_start', {})['ax']
        with CrtcProbe(h) as crtc:
            h.check('gfx_set_display_offset', m, stack_args=[x, y], outputs=['ax'],
                    label='mode_x2 %X x %X y %X' % (mode_x2, x, y))
            want = crtc.start() if crtc.regs else before
        got = h.port.call('probe_vga_start', {})['ax']
        if want != got:
            raise Mismatch('gfx_set_display_offset x %X y %X: CRTC start original %04X, port %04X' % (x, y, want, got))
        n += 1
    return n


def test_screen_shake_step(h, rng, scale):
    """VGA only counts down; other game modes call gfx_set_display_offset (tested here with the
    library in a mode whose dispatch returns at once, and in mode 13h with the CRTC probe)."""
    n = 0
    for i in range(200 * scale):
        m = random_dgroup(h, rng)
        put8(m, 0xB7F2, rng.choice([0, 1, 2, 8, rng.randrange(256)]))
        game_mode = rng.choice([0x13, 0x13, 0x0D, 0x09, 0x04])
        put16(m, 0xEED2, game_mode)
        put16(m, 0xDCF8, rng.choice([0x00, 0x06, 0x26]))
        struct.pack_into('<H', m, 0x463, 0x3D4)
        with CrtcProbe(h):
            h.check('screen_shake_step', m, label='case %d' % i)
        n += 1
    return n


# ---------------------------------------------------------------- camera

def test_camera(h, rng, scale):
    """camera_position and chase_view_collision on mission states with the chase view on (camera
    retries on land, the give-up after 20h tries) and off; terrain_rect_test at points around
    the camera."""
    n = 0
    for i in range(80 * scale):
        chase = i % 4 != 0
        m = moved(h, rng, i, rebuild=False, chase=1 if chase else 0)
        if chase and rng.random() < 0.3:
            put8(m, 0xD96D, rng.choice([0xF0, 0xFF]))
        # the terrain window as the frame leaves it: run terrain_cells_update first (checked)
        h.check('terrain_cells_update', m, label='state %d' % i)
        m = bytearray(h.orig.memory())
        for name in ('camera_position', 'chase_view_collision'):
            h.check(name, m, outputs=['cx', 'dx'] if name == 'camera_position' else (), label='state %d' % i)
            n += 1
        for k in range(10):
            cx = (get16(m, 0xD96E) * 4 + rng.randrange(-0x1800, 0x1800)) & 0xFFFF
            dx = (get16(m, 0xD970) * 4 + rng.randrange(-0x1800, 0x1800)) & 0xFFFF
            put16(m, 0xD96E, (cx >> 2) if rng.random() < 0.5 else get16(m, 0xD96E))
            h.check('terrain_rect_test', m, regs={'cx': cx, 'dx': dx}, label='state %d point %d' % (i, k))
            n += 1
    return n


# ---------------------------------------------------------------- terrain window

def test_terrain_cells_update(h, rng, scale):
    """Every kind of cell crossing: a new cell, the same cell with and without overflow, the
    quadrant and hysteresis tests, from real mission memory (TILE.BIN, grid, structures)."""
    n = 0
    for i in range(60 * scale):
        m = moved(h, rng, i, rebuild=False)
        x, y = get16(m, 0xC12D), get16(m, 0xC8FD)
        cell = (((9 - ((((y >> 8) - 4) & 0xFF) >> 2)) & 0xFF) * 17 + ((((x >> 8) - 4) & 0xFF) >> 2) + 1) & 0xFFFF
        q = ((((y >> 8) - 4) >> 1) & 1) << 1 | ((((x >> 8) - 4) >> 1) & 1)
        mode = i % 5
        if mode == 0:
            put16(m, 0xD9AD, 0xFFFF)
        elif mode == 1:
            put16(m, 0xD9AD, cell)
            put8(m, 0xD9A6, 0)
        else:
            put16(m, 0xD9AD, cell)
            put8(m, 0xD9A6, 1)
            put8(m, 0xD9A5, rng.choice([q, q ^ 1, q ^ 2, q ^ 3, rng.randrange(256)]))
            put8(m, 0xC12D, rng.choice([0x80 + 0x20 * 4 + 1, 0xC0, 0x40, rng.randrange(256)]))  # hysteresis band
            put8(m, 0xC8FD, rng.choice([0xC0, 0x40, 0x80 + 0x90 * 4 & 0xFF, rng.randrange(256)]))
        if rng.random() < 0.2:
            put8(m, 0xD9A4, rng.randrange(0x50, 0x61))        # scenery near its limit (the rebuild resets it)
        h.check('terrain_cells_update', m, label='case %d mode %d' % (i, mode), max_insns=BIG)
        n += 1
    return n


def test_tile_load(h, rng, scale):
    """The calls of the first frame's rebuilds, and random cell bytes at random origins with the
    groups and the scenery near their limits (overflow, the 8-bit scenery sum)."""
    n = check_captured(h, rng, scale, 'tile_load', [0, 1, 4, 8], variants=4)

    def near_limits(m):
        put16(m, 0xD960, rng.choice([0, 0x100, 0x1F0, 0x1FF, 0x200, 0x230]))
        put16(m, 0xD962, rng.choice([0, 0x180, 0x1FE, 0x200]))
        put8(m, 0xD9A4, rng.choice([0, 0x10, 0x55, 0x5F, 0x60, 0x70]))
        put16(m, 0xD9A9, rng.randrange(0x10000))
        put16(m, 0xD9AB, rng.randrange(0x10000))
    for i in range(80 * scale):
        m = base(h, i)
        if get16(m, 0xD960) == 0 and get16(m, 0xD962) == 0 and i % 3:
            m = moved(h, rng, i, rebuild=True)
        near_limits(m)
        al = rng.randrange(256)
        h.check('tile_load', m, regs={'ax': rng.randrange(256) << 8 | al}, label='cell byte %02X' % al)
        n += 1
    return n


def test_vertex_load(h, rng, scale):
    n = check_captured(h, rng, scale, 'vertex_load', [0, 1, 2, 5, 9, 13], outputs=['si'], variants=4)

    def mutate(m, regs, rng):
        put16(m, regs['bx'], rng.choice([0, 0x1F0, 0x1FF, 0x200, 0x250, rng.randrange(0x200)]))
        regs['cx'] = rng.choice([0, 1, regs['cx'], rng.randrange(256)])
        for off in (0xD9B0, 0xD9B1, 0xD9AF, 0xD951, 0xD952):
            put8(m, off, rng.randrange(256))
    n += check_captured(h, rng, scale, 'vertex_load', [0, 3, 6], outputs=['si'], variants=4, mutate=mutate)
    return n


# ---------------------------------------------------------------- terrain frame

def terrain_mutate(m, regs, rng):
    """The sky, water and horizon inputs of terrain_setup."""
    put8(m, 0xD193, rng.randrange(256))
    put8(m, 0xB82D, rng.choice([0x80, rng.randrange(0x60, 0xA0), rng.randrange(256)]))
    put8(m, 0xD965, rng.randrange(0x50))
    put8(m, 0xD9B5, rng.choice([0, 0, 1, 2, 5]))
    put8(m, 0xD94E, rng.randrange(256))
    put8(m, 0xD6BF, rng.choice([0, 0, 1]))
    put8(m, 0xD94D, rng.randrange(256))
    randomize(m, DS_BASE + 0xD90D, 0x40, rng)
    for k in range(0x20):
        put8(m, 0xD92D + k, rng.randrange(0x21))


def test_terrain_setup(h, rng, scale):
    n = check_captured(h, rng, scale, 'terrain_setup', [0], variants=6)
    n += check_captured(h, rng, scale, 'terrain_setup', [0], variants=10, mutate=terrain_mutate)
    return n


def test_sky_water_vga(h, rng, scale):
    n = check_captured(h, rng, scale, 'sky_water_vga', [0], outputs=['di'], variants=4)

    def mutate(m, regs, rng):
        horizon = rng.randrange(0x3E)
        put8(m, 0xD953, horizon)
        regs['bx'] = rng.randrange(256) << 8 | horizon
        regs['cx'] = rng.randrange(256) << 8 | rng.randrange(horizon + 1)
        regs['ax'] = rng.randrange(0x10000)
        put8(m, 0xD9B5, rng.choice([0, 1]))
    n += check_captured(h, rng, scale, 'sky_water_vga', [0], outputs=['di'], variants=10, mutate=mutate)
    return n


def test_water_marks_vga(h, rng, scale):
    n = check_captured(h, rng, scale, 'water_marks_vga', [0, 1, 7, 13, 20, 26, 31], variants=4)

    def mutate(m, regs, rng):
        randomize(m, DS_BASE + 0xD90D, 0x40, rng)
        regs['di'] = 0x5028 + 0x140 * rng.randrange(0x40)
        regs['cx'] = rng.choice([0x20, 0x17, 0x16, 0x0F, 0x0E, 1, rng.randrange(1, 0x21)])
        regs['bx'] = rng.choice([regs['bx'], rng.randrange(0x20), rng.randrange(256)])
    n += check_captured(h, rng, scale, 'water_marks_vga', [0], variants=12, mutate=mutate)
    return n


def test_project(h, rng, scale):
    n = check_captured(h, rng, scale, 'project', [0, 1], variants=12)

    def mutate(m, regs, rng):
        """A camera close to the vertices (candidates), reversing, heights of zero."""
        first, end = regs['si'], regs['ax']
        if end > first:
            v = rng.randrange(first, end)
            put16(m, 0xD972, (get16(m, 0x1C96 + 2 * v) + rng.randrange(-0x40, 0x40)) & 0xFFFF)
            put16(m, 0xD974, (get16(m, 0x2496 + 2 * v) + rng.randrange(-0x40, 0x40)) & 0xFFFF)
            for _ in range(20):
                put16(m, 0x1496 + 2 * rng.randrange(first, end), rng.choice([0, 0, 1, 0x40, 0xFF]))
        put8(m, 0xB828, rng.choice([0x10, 0xF0, 0x80, 0]))
        put16(m, 0xD190, rng.randrange(0x10000))
        put16(m, 0xD901, rng.choice([0xFFFF, 0xFFFF, 4]))
        put8(m, 0xD953, rng.randrange(0x3E))
    n += check_captured(h, rng, scale, 'project', [0, 1], variants=24, mutate=mutate)
    return n


def test_order_sort(h, rng, scale):
    n = check_captured(h, rng, scale, 'order_sort', [0], variants=6)

    def mutate(m, regs, rng):
        count = rng.choice([0, 1, 2, 3, 4, 0x20, 0x1FF, 0x200, get16(m, 0xD960)])
        put16(m, 0xD960, count)
        if rng.random() < 0.5:
            randomize(m, DS_BASE + 0x4096, 0x400, rng)                 # stale keys
            randomize(m, DS_BASE + 0x4496, 0x800, rng)                 # scales
            for k in range(0x200):
                put16(m, 0x3C96 + 2 * k, rng.randrange(0x200))
            for k in range(0x200):
                put8(m, 0x1096 + k, rng.choice([0, 0x05, 0x45, 0x85, 0xC5, rng.randrange(256)]))
    n += check_captured(h, rng, scale, 'order_sort', [0], variants=12, mutate=mutate)
    return n


def test_draw_group_b(h, rng, scale):
    return check_captured(h, rng, scale, 'draw_group_b', [0], variants=8)


def test_draw_primitive(h, rng, scale):
    n = check_captured(h, rng, scale, 'draw_primitive', PICKS, variants=4)

    def mutate(m, regs, rng):
        """New bearings and rows for the primitive's vertices: equal rows, wraps, off-view rows."""
        v = regs['bx']
        for k in range(-2, 3):
            o = (2 * (v + k)) & 0xFFFF
            if rng.random() < 0.7:
                put16(m, 0x2C96 + o, rng.choice([rng.randrange(0x10000), rng.randrange(0x3000, 0xB000),
                                                 0x8000, 0x7FFF, 0xC000]))
            if rng.random() < 0.7:
                put16(m, 0x3496 + o, rng.choice([rng.randrange(0x40), rng.randrange(0x60), 0x20, 0x21,
                                                 rng.randrange(0x10000)]))
        regs['dx'] = rng.choice([0, 0, 2, 1, 3]) if rng.random() < 0.3 else regs['dx']
    n += check_captured(h, rng, scale, 'draw_primitive', [0, 7, 30, 99], variants=12, mutate=mutate)
    return n


def test_fill_triangle(h, rng, scale):
    n = check_captured(h, rng, scale, 'fill_triangle', [0, 3, 9, 27, 81, 150], variants=4)

    def mutate(m, regs, rng):
        first = rng.randrange(0x60)
        regs['ax'] = (first + rng.randrange(0x30)) << 8 | first
        regs['cx'] = rng.randrange(0x10000)
        regs['dx'] = (regs['cx'] + rng.randrange(-0x6000, 0x6000)) & 0xFFFF
        put16(m, 0xD95E, (regs['cx'] + rng.randrange(-0x6000, 0x6000)) & 0xFFFF)
        put8(m, 0xD964, rng.choice([0, 1, 2]))
        put16(m, 0xD954, rng.randrange(0x10000))
    n += check_captured(h, rng, scale, 'fill_triangle', [0, 20], variants=12, mutate=mutate)
    return n


def test_span_vga_a(h, rng, scale):
    n = check_captured(h, rng, scale, 'span_vga_a', [0, 4, 16, 64, 128], variants=4)

    def mutate(m, regs, rng):
        """Edges at the column boundaries: 0FFh/100h (the right clip), 17Fh/180h (the wrapped left
        part), 1FFh; steps of 0 and of one column; mostly from a row in the view (40h-7Fh)."""
        put8(m, 0xD8FC, rng.choice([rng.randrange(0x40, 0x80), rng.randrange(0x40, 0x80), rng.randrange(0x100)]))
        put8(m, 0xB7E2, rng.choice([1, 2, 0x20, 0x40, 0, rng.randrange(256)]))
        for off in (0xD956, 0xD958):
            column = rng.choice([0, 1, 0xFE, 0xFF, 0x100, 0x101, 0x17E, 0x17F, 0x180, 0x181, 0x1FF,
                                 rng.randrange(0x200)])
            put16(m, off, column << 7 | rng.randrange(0x80))
        for off in (0xD95A, 0xD95C):
            put16(m, off, rng.choice([0, 0x80, 0xFF80, rng.randrange(-0x400, 0x400) & 0xFFFF, rng.randrange(0x10000)]))
        put16(m, 0xD954, rng.randrange(0x10000))
    n += check_captured(h, rng, scale, 'span_vga_a', [0, 1, 2, 3], variants=40, mutate=mutate)
    return n


def test_edge_setup(h, rng, scale):
    """Lines (none occur in the shipped worlds' group B, but the frames draw a few): captured, and
    random lines through edge_setup and span_vga_b."""
    n = check_captured(h, rng, scale, 'edge_setup', [0, 1, 2, 3], variants=6, need=0)
    n += check_captured(h, rng, scale, 'span_vga_b', [0, 3], variants=6, need=0)
    for i in range(150 * scale):
        m = base(h, i)
        put16(m, 0xD954, rng.randrange(0x10000))
        first = rng.randrange(0x70)
        regs = {'ax': (first + rng.randrange(0x40)) << 8 | first, 'cx': rng.randrange(0x10000),
                'dx': rng.randrange(0x10000), 'es': get16(m, 0xD9B8)}
        if rng.random() < 0.5:
            regs['dx'] = (regs['cx'] + rng.randrange(-0x2000, 0x2000)) & 0xFFFF
        h.check('edge_setup', m, regs=regs, label='case %d' % i)
        n += 1
    for i in range(150 * scale):                        # the line routine at the column boundaries
        m = base(h, i)
        put16(m, 0xD954, rng.randrange(0x10000))
        put8(m, 0xD8FC, rng.choice([rng.randrange(0x40, 0x80), rng.randrange(0x40, 0x80), rng.randrange(0x100)]))
        put8(m, 0xB7E2, rng.choice([1, 2, 3, 0x20, rng.randrange(256)]))
        column = rng.choice([0, 1, 0xFE, 0xFF, 0x100, 0x13F, 0x140, 0x17E, 0x17F, 0x180, 0x1FF, rng.randrange(0x200)])
        put16(m, 0xD956, ((column << 7) - 0x40 + rng.randrange(0x80)) & 0xFFFF)
        put16(m, 0xD95A, rng.choice([0, 0x80, 0xFF80, 0x100, rng.randrange(-0x2000, 0x2000) & 0xFFFF]))
        h.check('span_vga_b', m, regs={'es': get16(m, 0xD9B8)}, label='line %d column %X' % (i, column))
        n += 1
    return n


def test_shore_contact_test(h, rng, scale):
    n = 0
    for i in range(300 * scale):
        m = base(h, i)
        si = rng.choice([0xFFFF, 0, 2, 4, 0x400, 0x3FE, 2 * rng.randrange(0x400)])
        if rng.random() < 0.5:
            for k in range(-2, 3):
                o = (si + 2 * k) & 0xFFFF
                put16(m, 0x2C96 + o, rng.randrange(0x10000))
                put8(m, 0x1096 + (o >> 1), rng.choice([0, 0x06, 0x0E, 0x86, 0x4E, rng.randrange(256)]))
        put8(m, 0xD8FF, rng.choice([0, 1]))
        h.check('shore_contact_test', m, regs={'si': si}, label='si %04X' % si)
        n += 1
    return n


# ---------------------------------------------------------------- the visible-object list

def test_visible_list_rebuild(h, rng, scale):
    n = check_captured(h, rng, scale, 'visible_list_rebuild', [0], variants=6, need=0)
    for i in range(60 * scale):
        m = base(h, i)
        if get16(m, 0xD960) == 0:
            continue
        put16(m, 0xD96E, rng.randrange(0x0500, 0x4000))
        put16(m, 0xD970, rng.randrange(0x0500, 0x2800))
        put8(m, 0xD9B2, rng.randrange(256))
        put16(m, 0xD8BD, rng.randrange(0x10000))
        put8(m, 0xD6BF, rng.choice([0, 1]))
        put16(m, 0xD8BF, rng.choice([0, 0x40, 0xB5, rng.randrange(0xB6)]))
        put8(m, 0xD9A4, rng.randrange(1, 0x61))
        randomize(m, DS_BASE + 0x5189, 0xB5, rng)
        randomize(m, DS_BASE + 0xD8D1, 0x18, rng)
        h.check('visible_list_rebuild', m, label='case %d' % i)
        n += 1
    return n


def test_visible_project(h, rng, scale):
    n = 0
    for i in range(60 * scale):
        m = base(h, i)
        if get16(m, 0xB83D) == 0:
            continue
        if i % 2:
            put16(m, 0xD972, (get16(m, 0xC12D) * 4 + rng.randrange(-0x2000, 0x2000)) & 0xFFFF)
            put16(m, 0xD974, (get16(m, 0xC8FD) * 4 + rng.randrange(-0x2000, 0x2000)) & 0xFFFF)
            put8(m, 0xD191, rng.randrange(256))
            put8(m, 0xD192, rng.randrange(256))
        h.check('visible_project', m, label='case %d' % i, max_insns=BIG)
        n += 1
    return n


def list_state(h, rng, i):
    m = base(h, i)
    count = get16(m, 0xB83D)
    if rng.random() < 0.5 or count < 3:
        count = rng.choice([3, 4, 0x20, 0x24, 0xB5, rng.randrange(3, 0xB6)])
        put16(m, 0xB83D, count)
    for k in range(0xB5):
        if rng.random() < 0.5:
            put16(m, 0x4C96 + 2 * k, rng.choice([rng.randrange(0x10000), rng.randrange(0x400), 0x10, 0x7FFF]))
    put8(m, 0xD96B, rng.choice([0, 0, 1]))
    return m


def test_list_sorts(h, rng, scale):
    """list_bubble, list_quicksort, list_quicksort_range, list_bubble_range and list_swap on lists
    with real and random distances (equal keys, sorted, reversed). The lists have 3 entries or more
    and the ranges two or more: list_quicksort is only called on a rebuilt list (35 entries or
    more), and a range of fewer than two entries makes the original's recursion run over all of
    DGROUP and its stack (not tested)."""
    n = 0
    for i in range(80 * scale):
        m = list_state(h, rng, i)
        if i % 5 == 0:                                      # already sorted or reversed or all equal
            count = get16(m, 0xB83D)
            keys = sorted(rng.randrange(0x10000) for _ in range(count))
            if i % 10 == 0:
                keys.reverse()
            for k, v in enumerate(keys):
                put16(m, 0x4C96 + 2 * k, v if i % 15 else 0x100)
        h.check('list_bubble', m, label='case %d' % i, max_insns=BIG)
        h.check('list_quicksort', m, label='case %d' % i, max_insns=BIG)
        count = get16(m, 0xB83D)
        first = 2 * rng.randrange(0, count - 1)             # a range of two entries or more, as the
        last = 2 * rng.randrange(first // 2 + 1, count)      # sorts' own calls (shorter ones run wild)
        h.check('list_quicksort_range', m, regs={'ax': 2}, stack_args=[last, first],
                label='case %d %X..%X' % (i, first, last), max_insns=BIG)
        h.check('list_bubble_range', m, regs={'ax': 2}, stack_args=[last, first],
                label='case %d %X..%X' % (i, first, last), max_insns=BIG)
        h.check('list_swap', m, regs={'si': first, 'di': last}, label='case %d' % i)
        n += 5
    n += check_captured(h, rng, scale, 'list_quicksort_range', [0, 1, 3, 7, 11], variants=4, stack=2, need=0)
    n += check_captured(h, rng, scale, 'list_bubble_range', [0, 2, 5, 9], variants=4, stack=2, need=0)
    return n


def test_empty_window(h, rng, scale):
    """A terrain window without scenery (Mare Island's bay, grid cells 137 and 154): the
    original's visible_list_rebuild runs its LOOP 65536 times over DGROUP and its stack and does
    not come back; the port stops with a fatal error (PORT)."""
    n = 0
    for x, y in [(0x0580, 0x0B80), (0x0740, 0x0520), (0x06FF, 0x0900)]:
        m = base(h, 0)
        put16(m, 0xC12D, x)
        put16(m, 0xC8FD, y)
        if window_scenery(h, m) != 0:
            raise Mismatch('X %04X Y %04X: the window has scenery' % (x, y))
        m = bytearray(h.orig.memory())                   # after the rebuild: D9A4 = 0
        file_seg, off, far = h.sym.func('visible_list_rebuild')
        h.orig.set_memory(m)
        try:
            h.orig.call(file_seg, off, far, {}, (), max_insns=5_000_000)
            raise Mismatch('X %04X Y %04X: the original returned' % (x, y))
        except Mismatch as e:
            if 'the original returned' in str(e):
                raise
        h.port.set_memory(m)
        try:
            h.port.call('visible_list_rebuild', {})
            raise Mismatch('X %04X Y %04X: the port returned' % (x, y))
        except Mismatch as e:
            if 'no scenery' not in str(e):
                raise
        n += 1
    return n


# ---------------------------------------------------------------- sprite cache slots

def test_sprite_lod(h, rng, scale):
    n = check_captured(h, rng, scale, 'sprite_lod_update', [0], variants=6)
    n += check_captured(h, rng, scale, 'sprite_lod_entry', [0, 5, 20, 60], variants=4)
    for i in range(60 * scale):
        m = base(h, i)
        if get16(m, 0xB83D) == 0:        # before the first frame; the game rebuilds the list first
            continue
        put8(m, 0xD6C0, rng.choice([0xFF, 0x16, rng.randrange(256)]))
        put8(m, 0xD96B, rng.choice([0, 1]))
        if rng.random() < 0.5:
            randomize(m, DS_BASE + 0xD8D1, 0x18, rng)
            randomize(m, DS_BASE + 0x5189, 0xB5, rng)
        for k in range(get16(m, 0xB83D)):
            if rng.random() < 0.3:
                put16(m, 0x4C96 + 2 * k, rng.choice([0x10, 0x11, 0x900, 0xA00, 0xE00, 0xF00, rng.randrange(0x8000)]))
        h.check('sprite_lod_update', m, label='case %d' % i, max_insns=BIG)
        n += 1
    return n


def test_sprite_slot_alloc(h, rng, scale):
    n = check_captured(h, rng, scale, 'sprite_slot_alloc', [0, 3, 10, 40], variants=4, need=0)
    for i in range(300 * scale):
        m = base(h, i)
        fill = rng.random()
        for k in range(0x18):
            put8(m, 0xD8D1 + k, 0xFF if rng.random() < fill else rng.randrange(256))
        level = rng.choice([0, 1, 2])
        h.check('sprite_slot_alloc', m, regs={'ax': level << 8 | rng.randrange(256), 'si': rng.randrange(1, 0xB6)},
                label='level %d fill %.2f' % (level, fill))
        n += 1
    return n


def test_sprite_cache_invalidate(h, rng, scale):
    n = 0
    for i in range(20 * scale):
        m = base(h, i)
        randomize(m, DS_BASE + 0xD8D1, 0x18, rng)
        h.check('sprite_cache_invalidate', m, label='case %d' % i)
        n += 1
    return n


# ---------------------------------------------------------------- sprite images

def test_sprite_view_angle(h, rng, scale):
    n = check_captured(h, rng, scale, 'sprite_view_angle', PICKS[:8], variants=4)

    def mutate(m, regs, rng):
        regs['dx'] = rng.choice([0, 1, 8, 0x100, rng.randrange(0x10000)])
        bx = regs['bx']
        put8(m, 0x50D4 + bx, rng.randrange(256))
    n += check_captured(h, rng, scale, 'sprite_view_angle', [0, 9], variants=10, mutate=mutate)
    return n


def sprite_mutate(m, regs, rng):
    """A new size (all zoom levels), view and slot for the image being built, forcing a rebuild."""
    put8(m, 0xD864, rng.choice([rng.randrange(0x18), rng.randrange(0x18, 0x30), rng.randrange(0x30, 0x48),
                                rng.randrange(0x48, 0x70)]))
    put8(m, 0xD865, 0)
    put8(m, 0xD86A, rng.randrange(256))
    put8(m, 0xD70C, rng.randrange(256))
    if rng.random() < 0.15:
        put8(m, 0xD868, rng.choice([0x30, 0x31, 0x39, 0x3A, 0x3B, 0x17]))   # animated, even sizes, rebuilt
    slot = rng.choice([rng.randrange(1, 0x99), rng.randrange(0x99, 0xB3), rng.randrange(0xB3, 0xB9)])
    put8(m, 0xD863, slot)


def test_sprite_cache_build(h, rng, scale):
    n = check_captured(h, rng, scale, 'sprite_cache_build', PICKS[:9], variants=4)
    n += check_captured(h, rng, scale, 'sprite_cache_build', [0, 4, 11], variants=12, mutate=sprite_mutate)
    return n


def build_states(h, rng, scale, count):
    """Snapshots at sprite_cache_build with a new size, for capturing the scalers inside it."""
    out = []
    for label, m in frame_states(h, rng, scale, 4):
        for regs, words, snap in capture(h, m, 'sprite_cache_build', [0, 6]):
            for _ in range(count):
                s = bytearray(snap)
                sprite_mutate(s, regs, rng)
                out.append((label, s))
    return out


def check_in_build(h, rng, scale, target, picks, count=2, need=1):
    """`target` (a scaler) captured inside sprite_cache_build runs on mutated sizes."""
    n = 0
    for label, s in build_states(h, rng, scale, count):
        for k, (regs, words, snap) in enumerate(capture(h, s, target, picks, caller='sprite_cache_build')):
            h.check(target, snap, regs=regs, outputs=['di'], label='%s call %d' % (label, k), max_insns=BIG)
            n += 1
    if n < need:
        raise Mismatch('%s: only %d captured cases' % (target, n))
    return n


def test_sprite_scale_rows(h, rng, scale):
    n = 0
    for name in ('sprite_scale_rows', 'sprite_scale_rows_up', 'sprite_scale_rows_up2'):
        n += check_in_build(h, rng, scale, name, [0, 1], count=6)
    return n


def test_sprite_rows(h, rng, scale):
    n = 0
    for name in ('sprite_row_box', 'sprite_row_box_up', 'sprite_row_box_up2', 'sprite_row_turn',
                 'sprite_row_turn_up', 'sprite_row_turn_up2', 'sprite_row_flat', 'sprite_row_flat_up',
                 'sprite_row_flat_up2'):
        n += check_in_build(h, rng, scale, name, [0, 1, 2, 3, 5, 7, 9, 12, 16], count=5)
    return n


def test_sprite_prepare(h, rng, scale):
    return check_captured(h, rng, scale, 'sprite_prepare', PICKS[:9], variants=6)


def blit_mutate(m, regs, rng):
    """Another place on the screen: columns clipped left and right, rows above the view (skipped)
    and below it."""
    bx = regs['bx']
    put8(m, 0x4E00 + bx, rng.choice([rng.randrange(256), 0, 0x10, 0x8C, 0x90, 0xF8]))
    put8(m, 0x4EB5 + bx, rng.randrange(256))
    put8(m, 0x4F6A + bx, rng.randrange(256))
    put8(m, 0xD193, rng.choice([rng.randrange(0x30, 0x60), rng.randrange(0x100)]))
    put8(m, 0xB82D, rng.choice([0x80, rng.randrange(256)]))


def test_blit_record(h, rng, scale):
    n = check_captured(h, rng, scale, 'blit_record', PICKS[:9], variants=4)
    n += check_captured(h, rng, scale, 'blit_record', [0, 5, 17], variants=12, mutate=blit_mutate)
    return n


def test_blit_place(h, rng, scale):
    return check_captured(h, rng, scale, 'blit_place', PICKS[:9], variants=6)


def test_blit_rows_vga(h, rng, scale):
    n = check_captured(h, rng, scale, 'blit_rows_vga', PICKS[:9], variants=6)

    def mutate(m, regs, rng):
        """The last rows and columns of the page (the first row past A000h ends it), left clips."""
        row = rng.choice([rng.randrange(0x10, 0x50), rng.randrange(0x40, 0x50)])
        regs['ax'] = row
        put8(m, 0xB7E2, row)
        put8(m, 0xB7F7, rng.choice([rng.randrange(256), rng.randrange(0xC0, 0x100)]))
        put16(m, 0xD875, rng.choice([0, 0, rng.randrange(1, 0x40)]))
        put8(m, 0xB7F9, rng.randrange(1, 0x40))
    n += check_captured(h, rng, scale, 'blit_rows_vga', [0, 7], variants=12, mutate=mutate)
    return n


# ---------------------------------------------------------------- spotlights

def test_spotlights(h, rng, scale):
    n = 0
    for i in range(80 * scale):
        m = moved(h, rng, i, rebuild=False, lights=True)
        put16(m, 0x0086, rng.choice([1, 2, 3, 4, 1]))
        put8(m, 0xD193, rng.randrange(0x40, 0x80))
        put8(m, 0xD965, rng.randrange(0x40))
        if rng.random() < 0.2:
            put8(m, 0xD96B, 1)
        if rng.random() < 0.1:
            put8(m, 0xB7FC, 1)
        h.check('spotlights', m, label='case %d' % i)
        n += 1
    for i in range(150 * scale):
        m = base(h, i)
        put8(m, 0xD193, rng.randrange(256))
        put16(m, 0x0086, rng.choice([1, 2, 3, 4, rng.randrange(0x10000)]))
        h.check('spotlight_beam', m, regs={'ax': rng.randrange(0x10000), 'bx': rng.randrange(0x10000)},
                label='beam %d' % i)
        n += 1
    for i in range(100 * scale):
        m = base(h, i)
        put8(m, 0xB7E2, rng.choice([0, 0x10, 0x2D, 0x3F, 0x40, rng.randrange(0x40)]))
        put8(m, 0xB7E3, rng.choice([1, 10, rng.randrange(1, 12)]))
        bx = 0x709A + rng.randrange(0, 0xBF)
        dx = rng.choice([rng.randrange(-0x100, 0x100) & 0xFFFF, rng.randrange(0x10000)])
        h.check('spotlight_beam_vga', m, regs={'bx': bx, 'dx': dx, 'es': get16(m, 0xD9B8)}, label='vga %d' % i)
        n += 1
    return n


TESTS = [test_atan, test_polar_small, test_route_rotate, test_shore_edge_test, test_colour_remap,
         test_video_mode_setup, test_small, test_palette_flash, test_gfx_set_display_offset,
         test_screen_shake_step, test_camera, test_terrain_cells_update, test_tile_load, test_vertex_load,
         test_terrain_setup, test_sky_water_vga, test_water_marks_vga, test_project, test_order_sort,
         test_draw_group_b, test_draw_primitive, test_fill_triangle, test_span_vga_a, test_edge_setup,
         test_shore_contact_test, test_visible_list_rebuild, test_empty_window, test_visible_project, test_list_sorts,
         test_sprite_lod, test_sprite_slot_alloc, test_sprite_cache_invalidate, test_sprite_view_angle,
         test_sprite_cache_build, test_sprite_scale_rows, test_sprite_rows, test_sprite_prepare,
         test_blit_record, test_blit_place, test_blit_rows_vga, test_spotlights]
