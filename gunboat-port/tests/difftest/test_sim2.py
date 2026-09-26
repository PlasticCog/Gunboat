"""The simulation's middle layer (simulation.md §3-§9, package S2a): key_dispatch and the key handlers,
the held controls, the message line, the crew gunners and reloads, the mission clock (with its
busy-wait), the passengers' stop, the homing missile, engines and propulsion, damage and the boat's
loss, the enemies' effects, spotting, firing and movement, their shots, and the projectiles' impacts.

States: the mission states of mission_states.py and test_sim.py (practice and campaign missions at
their first frames), and combat states: the original run from main into a mission and on for
hundreds of frames with keys delivered at the mission loop's passes (main switch, engines, a gun
station, "faster" four times, fire at will), so that the boat moves among alerted enemies that
shoot, get hit and burn. From those states the tests randomize what each function reads, with edge
values where the code branches. Every check compares all memory; the registers listed per test are
those the callers use (AX and SI where game_frame passes them on)."""
import struct

from unicorn import UC_HOOK_CODE

import mission_states
from gbdiff import DS_BASE, Mismatch, lin, put8, put16, seg_of
from mission_states import mission_state
from test_sound import sound
import test_sim

KEY_F4 = 0x84          # pending (route_point / route_advance): only tested where it cannot be reached


def get8(m, off):
    return m[DS_BASE + (off & 0xFFFF)]


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + (off & 0xFFFF))[0]


def rbyte(rng, edges):
    return rng.choice(edges) if rng.random() < 0.6 else rng.randrange(256)


# ---------------------------------------------------------------- states

def run_mission(h, frames, keys, **kw):
    """The memory when the original enters game_frame for the (frames + 1)-th time, with keys
    {pass: key code} delivered into isr_key_code at the mission loop's tick read of that pass (the
    set-up's fades get none). Cached per harness."""
    key = ('run_mission', frames, tuple(sorted(keys.items())), tuple(sorted(kw.items())))
    cache = h.__dict__.setdefault('_mission_states', {})
    if key not in cache:
        if mission_states.MISSION_POLL == '05bd:0000':
            mission_states.MISSION_POLL = mission_states._find_mission_poll()
        args = dict(practice=None, region=0, mission=1, rank=1, station=1, weapons=(0, 0, 1), engines=1, sea=1,
                    night=False, targets=0)
        args.update(kw)
        m = mission_states._base(h, **args)
        count, passes = [0], [0]
        frame_at = lin(seg_of(mission_states.GAME_FRAME[0]), mission_states.GAME_FRAME[1])
        seg, off = (int(x, 16) for x in mission_states.MISSION_POLL.split(':'))
        pass_at = lin(seg_of(seg), off)

        def on_frame(uc, address, size, _):
            if count[0] == frames:
                h.orig.fail('stop')
            count[0] += 1

        def on_pass(uc, address, size, _):
            k = keys.get(passes[0], 0)
            if k:
                uc.mem_write(DS_BASE + 0xDA42, bytes([k]))
            passes[0] += 1
        hooks = [h.orig.uc.hook_add(UC_HOOK_CODE, on_frame, None, frame_at, frame_at),
                 h.orig.uc.hook_add(UC_HOOK_CODE, on_pass, None, pass_at, pass_at)]
        with sound(h):
            h.set_tick((mission_states.POLLS + [mission_states.MISSION_POLL, mission_states.STATION_WAIT_POLL], 'both'))
            try:
                h.orig.set_memory(m)
                file_seg, foff, far = h.sym.func('mission_run')
                h.orig.call(file_seg, foff, far, {}, (), max_insns=4_000_000_000)
                raise Mismatch('mission_run returned before game_frame call %d' % (frames + 1))
            except Mismatch as e:
                if str(e) != 'original: stop':
                    raise
            finally:
                for hk in hooks:
                    h.orig.uc.hook_del(hk)
                h.set_tick(None)
        cache[key] = bytes(h.orig.memory())
    return bytearray(cache[key])


# Main switch (F1), both engines (F2), the midship gun (N: the crew mans bow and stern), "faster"
# four times (F8: the captain follows the river), fire at will (F10).
GO = {0: 0x81, 2: 0x82, 70: ord('n'), 72: 0x88, 74: 0x88, 76: 0x88, 78: 0x88, 80: 0x8A}
COMBAT = [dict(frames=300, region=3, mission=4, rank=5, weapons=(1, 0, 1)),
          dict(frames=700, region=3, mission=4, rank=5, weapons=(1, 0, 1)),
          dict(frames=1100, region=3, mission=4, rank=5, weapons=(0, 1, 2)),
          dict(frames=500, region=0, mission=6, rank=7, weapons=(1, 1, 1), night=True)]


def combat(h, rng, which=None):
    kw = dict(COMBAT[rng.randrange(len(COMBAT)) if which is None else which])
    frames = kw.pop('frames')
    return run_mission(h, frames, GO, **kw)


def any_state(h, rng):
    """A mission state or a combat state."""
    if rng.random() < 0.5:
        return combat(h, rng)
    return test_sim.state(h, rng)


def visible_state(h, rng):
    """A state with a visible list: a combat state or a campaign mission after some frames."""
    if rng.random() < 0.6:
        return combat(h, rng)
    return test_sim.state(h, rng, rng.choice(test_sim.visible_states(h)))


def shake_crew(m, rng):
    """Crew conditions: mostly intact (3), sometimes wounded (0, 1) or dead (2)."""
    for off in (0xD512, 0xD513, 0xD514, 0xD515):
        put8(m, off, get8(m, off) & 0xFC | rng.choice([3, 3, 3, 0, 1, 2]))


def shake_keys_state(m, rng):
    """What the key handlers read."""
    put16(m, 0x0086, rng.choice([1, 1, 2, 3, 4, 0, 5, 7, 8]) if rng.random() < 0.95 else rng.randrange(0x10000))
    put8(m, 0xD96B, rng.choice([0, 0, 0, 1]))
    put16(m, 0xF110, rng.choice([0, 0, 0, 1, 2, 3]) if rng.random() < 0.95 else rng.randrange(0x10000))
    put16(m, 0x0070, rng.choice([0, 0, 0, 1]))
    put16(m, 0xF346, rng.choice([0, 1, 2]) if rng.random() < 0.9 else rng.randrange(0x10000))
    shake_crew(m, rng)
    put16(m, 0xB82E, rng.choice([0, 1, 0x100, 0xFFFF]))
    put8(m, 0xB800, rng.choice([0, 0, 1, 2]))
    put8(m, 0xB802, rng.choice([0, 2, 0x24, 0x12, rng.randrange(256)]))
    put8(m, 0xB7F1, rng.choice([0, 1, 2, 2, 3, rng.randrange(256)]))
    put8(m, 0xB7FF, rng.choice([0, 5, 0x0F, 0x10, 0x11]))
    for off in (0xD506, 0xD507, 0xD508, 0xD509):
        put8(m, off, get8(m, off) & 0xFC | rng.choice([3, 3, 2, 1, 0]))
    for off in (0xB80A, 0xB80C):
        put16(m, off, rng.choice([0, 1, 0xC544, rng.randrange(0x10000)]))
    for off in (0xD520, 0xD521, 0xD522, 0xD525, 0xD526, 0xD529, 0xD52A, 0xD52E, 0xD52F, 0xD52B, 0xD52C):
        if rng.random() < 0.5:
            put8(m, off, get8(m, off) ^ 1)
        if rng.random() < 0.1:
            put8(m, off, rng.randrange(256))
    for off in (0xD503, 0xD504):
        put8(m, off, get8(m, off) & 0xF8 | rng.randrange(8))
    for off in (0xB808, 0xB809):
        put8(m, off, rng.choice([0, 1, 1, 0x7F, 0x80, 0x81, 0x7D, 3, 2, rng.randrange(256)]))
    put8(m, 0xD680, rbyte(rng, [0, 7, 8, 9, 0x16, 0x17, 0x2C, 0x3B, 0x4A, 0x66, 0x67, 0xF0, 0xFF]))
    put8(m, 0xB81C, rng.choice([0x3B, 0x67, 0x1D, rng.randrange(256)]))
    put8(m, 0xB81D, rng.choice([0x3B, 0x67, 0x33, rng.randrange(256)]))
    put8(m, 0xB828, rng.choice([0, 0, 5, 0x30, 0xF0]))
    put8(m, 0xD6BF, rng.choice([0, 1]))
    put16(m, 0xB52C, rng.randrange(0x10000))
    put8(m, 0xDA43, rng.choice([0, 0, 1, 2, 4, 8, 0x10, 0x11, 0x15, 0x1A, 0x1F, rng.randrange(256)]))
    put8(m, 0xB7F0, rng.choice([0, 0, 0, 1, 0x10, rng.randrange(256)]))
    if rng.random() < 0.2:
        put16(m, 0xB83F, rng.choice([0, 1, 1, 5, get16(m, 0xB83D), 0xFFFF, 0x8000]))
    # a world-A byte that key_plus and enemy_update toggle
    put16(m, 0x6E54, rng.choice([get16(m, 0x6E54), rng.randrange(0x100)]))


# ---------------------------------------------------------------- keys

def test_key_dispatch(h, rng, scale):
    """Every key code at the 3D and full-screen stations (F4 only where it goes to controls_poll)."""
    n = 0
    codes = list(range(256)) * 2 + [rng.choice([0x81, 0x82, 0x83, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x2B, 0x2D, 0x2C,
                                                0x09, 0x44, 0x5A, 0x58, 0x43, 0x56, 0x4E, 0x42, 0x2F, 0x2E, 0x4D])
                                     for _ in range(1200 * scale)]
    for i, code in enumerate(codes):
        m = visible_state(h, rng) if i % 3 == 0 else any_state(h, rng)
        shake_keys_state(m, rng)
        if code == KEY_F4 and get16(m, 0x0086) < 5:
            put16(m, 0x0086, rng.choice([5, 7, 8, 9]))
        put8(m, 0xEE9C, code)
        h.check('key_dispatch', m, regs={'ax': rng.randrange(0x10000), 'si': rng.randrange(0x10000)}, outputs=['si'],
                label='case %d code %02X station %04X' % (i, code, get16(m, 0x0086)))
        n += 1
    return n


HANDLERS = ['key_default', 'key_f10_fire_at_will', 'key_f9_identify', 'key_minus_control_rate', 'key_f1_panel',
            'key_f3_panel', 'key_f2_panel', 'key_f5_branch_left', 'key_f6_branch_right', 'key_f8_faster',
            'key_f7_slower', 'pilot_command_reply', 'key_m_map', 'key_period_assignment', 'key_z_pilot_left',
            'key_x_pilot_ahead', 'key_c_pilot_right', 'station_dead_reply', 'key_v_bow', 'key_n_midship',
            'key_b_stern', 'key_slash_damage_report', 'key_plus_time_compression', 'key_tab_return_to_base',
            'key_comma_chase_view', 'key_d_detail']


def test_key_handlers(h, rng, scale):
    """Each handler directly (including station values the tables do not reach), and
    key_pilot_station with every look direction."""
    n = 0
    for name in HANDLERS + ['key_pilot_station']:
        for i in range(120 * scale):
            m = visible_state(h, rng) if name == 'key_f9_identify' else any_state(h, rng)
            shake_keys_state(m, rng)
            regs = {'ax': rng.randrange(0x10000), 'bx': rng.choice([0, 1, 2, rng.randrange(0x10000)]),
                    'cx': rng.randrange(0x10000), 'si': rng.randrange(0x10000)}
            h.check(name, m, regs=regs, outputs=['si'], label='case %d station %04X' % (i, get16(m, 0x0086)))
            n += 1
    return n


def test_key_identify(h, rng, scale):
    """F9 on real visible lists: every look direction, the first entry, kinds 39h / 3Ch and the
    friendly and scenery kinds next to the identifiable ones."""
    n = 0
    for i in range(600 * scale):
        m = visible_state(h, rng)
        count = get16(m, 0xB83D)
        put16(m, 0x0086, rng.choice([1, 1, 2, 3, 4, 0x101]))
        put16(m, 0xF346, rng.choice([0, 1, 2, 3]))
        put16(m, 0xD611, rng.randrange(0x10000))
        if rng.random() < 0.3:
            put16(m, 0xB83F, rng.choice([0, 1, 2, count - 1, count, 0xFFFF, 0xFFFE]))
        for _ in range(rng.randrange(6)):
            e = rng.randrange(count)
            obj = get16(m, 0x523E + 2 * e)
            put8(m, 0xB95D + obj, rng.choice([0x39, 0x3C, 0x3A, 0x27, 0x28, 0x38, 0x01, 0x17, 0x00,
                                               rng.randrange(0x40)]))
            if rng.random() < 0.5:
                put8(m, 0x4E00 + e, (0x44 + rng.randrange(-12, 12)) & 0xFF)
            if rng.random() < 0.2:
                put8(m, 0x4C97 + 2 * e, rng.choice([0, 1]))
        h.check('key_f9_identify', m, regs={'si': rng.randrange(0x10000)}, outputs=['si'], label='case %d' % i)
        n += 1
    return n


def test_panel_switch_toggle(h, rng, scale):
    """Every entry of the switch table (and BX past it, odd and wrapping), with every switch and lamp
    state; engine_switch for both engines with every lamp value and countdown."""
    n = 0
    for i in range(1200 * scale):
        m = any_state(h, rng)
        shake_keys_state(m, rng)
        test_hud_panel(m, rng)
        bx = rng.randrange(0, 0x20, 2)
        if rng.random() < 0.2:
            bx = rng.choice([rng.randrange(0x40), rng.randrange(0x10000)])
        h.check('panel_switch_toggle', m, regs={'bx': bx, 'ax': rng.randrange(0x10000), 'si': rng.randrange(0x10000)},
                outputs=['ax', 'si'], label='case %d bx %04X' % (i, bx))
        n += 1
    for i in range(800 * scale):
        m = any_state(h, rng)
        shake_keys_state(m, rng)
        test_hud_panel(m, rng)
        si = rng.choice([0, 1]) if rng.random() < 0.95 else rng.randrange(0x40)
        h.check('engine_switch', m, regs={'si': si, 'ax': rng.randrange(0x10000)}, outputs=['ax', 'si'],
                label='case %d si %04X' % (i, si))
        n += 1
    return n


def test_hud_panel(m, rng):
    """Lamp and switch bytes as the panel keeps them, sometimes random."""
    for off in range(0xD502, 0xD520):
        if rng.random() < 0.15:
            put8(m, off, rng.randrange(256))
    for off in range(0xD520, 0xD531):
        if rng.random() < 0.15:
            put8(m, off, rng.randrange(256))


# ---------------------------------------------------------------- controls

def test_controls_poll(h, rng, scale):
    n = 0
    for i in range(2500 * scale):
        m = any_state(h, rng)
        shake_keys_state(m, rng)
        test_sim.timers(m, rng)
        put8(m, 0xD96D, rbyte(rng, [0x20, 0x27, 0x28, 0x29, 0x30, 0xF8, 0xF7, 0xFF]))
        put8(m, 0xD96C, rng.randrange(256))
        put8(m, 0xB818, rbyte(rng, [0, 1, 7, 8, 0x3F, 0x40, 0x41, 0x44, 0x7C, 0x7D, 0x80, 0xC0, 0xFD]))
        put8(m, 0xD632, rng.choice([0, 1, 2, 3, 0x80, 0xFF]))
        for off in (0xB816, 0xB817):
            put8(m, off, rbyte(rng, [0, 8, 9, 0x3B, 0x67, 0xFF]))
        put8(m, 0xD70C, rng.randrange(256))
        h.check('controls_poll', m, regs={'ax': rng.randrange(0x10000), 'si': rng.randrange(0x10000)},
                label='case %d station %04X keys %02X|%02X' % (i, get16(m, 0x0086), get8(m, 0xDA43), get8(m, 0xB7F0)))
        n += 1
    return n


def test_pilot_throttle_controls(h, rng, scale):
    """Every jet angle around the detent and the limits, each step table entry, left and right."""
    n = 0
    for i in range(2500 * scale):
        m = any_state(h, rng)
        shake_keys_state(m, rng)
        put8(m, 0xB818, rbyte(rng, [0, 1, 2, 3, 4, 7, 8, 9, 0x38, 0x3C, 0x3F, 0x40, 0x41, 0x44, 0x48, 0x75, 0x79, 0x7C,
                                    0x7D, 0x7E, 0x7F, 0x80, 0x81, 0xBF, 0xC0, 0xC1, 0xFC, 0xFD, 0xFF]))
        put8(m, 0xD632, rng.choice([0, 1, 2, 3, 0x7F, 0x80, 0xFE, 0xFF]))
        if rng.random() < 0.2:
            for k in range(4):
                put8(m, 0xD62E + k, rng.randrange(256))
        for k in range(16):
            put8(m, 0xD70E + k, rng.choice([0, 0x20, 0x21, 0x1F, rng.randrange(256)]))
        for off in (0xB816, 0xB817):
            put8(m, off, rbyte(rng, [0, 7, 8, 9, 0x3A, 0x3B, 0x3C, 0x67, 0xFF]))
        cl = rng.choice([4, 8, 0x0C, 5, 9, 6, 0x0A, 1, 2, 0x14, 0x18, rng.randrange(256)])
        bx = rng.choice([0, 1, 2, 3]) if rng.random() < 0.9 else rng.randrange(0x10000)
        h.check('pilot_throttle_controls', m, regs={'cx': rng.randrange(0x100) << 8 | cl, 'bx': bx,
                                                    'ax': rng.randrange(0x10000), 'si': rng.randrange(0x10000)},
                outputs=['si'], label='case %d cl %02X bx %04X jet %02X' % (i, cl, bx, get8(m, 0xB818)))
        n += 1
    return n


# ---------------------------------------------------------------- messages

def test_message_sequencer(h, rng, scale):
    n = 0
    for i in range(800 * scale):
        m = any_state(h, rng)
        put8(m, 0xB801, rng.choice([0, 0, 0, 1, 2, 0xFF]))
        put8(m, 0xB800, rng.choice([0, 1, 1, 2, 3, 0xFF]))
        put8(m, 0xB802, rng.choice([0, 0, 2, 5, 0x12, 0x24, rng.randrange(0x38)]))
        put16(m, 0xD60F, rng.choice([0, 0, rng.randrange(1000) * 2]))
        put8(m, 0xD513, rng.choice([3, 2]))
        h.check('message_sequencer', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000)},
                outputs=['si'], label='case %d' % i)
        n += 1
    return n


def test_heading_readout(h, rng, scale):
    """Every heading and fraction class: the compass letters, the degrees (DIV 5B00h) and the
    hundreds carry."""
    n = 0
    for i in range(2000 * scale):
        m = test_sim.state(h, rng)
        put8(m, 0xD9C1, rng.randrange(0x24))
        put8(m, 0xD9C2, rng.randrange(0xC0))
        ax = rng.randrange(0x10000) if i % 4 else (rng.randrange(256) << 8 | rng.choice([0, 0xFF, 0x7F, 0x80, 0xFE]))
        h.check('heading_readout', m, regs={'ax': ax}, label='case %d ax %04X' % (i, ax))
        n += 1
    return n


def test_message_line_draw(h, rng, scale):
    n = 0
    for i in range(1500 * scale):
        m = any_state(h, rng)
        put16(m, 0x0086, rng.choice([0, 1, 1, 2, 3, 4, 5, 7, 0x101, 0x201]))
        put8(m, 0xD96B, rng.choice([0, 0, 0, 1]))
        put16(m, 0xF346, rng.choice([0, 1, 2, 0x101, 3]))
        put8(m, 0xB804, rng.choice([0, 1]))
        # (EED2 stays 13h: the CGA colour choice for mode 4 comes with CGA text drawing, parked)
        put8(m, 0xB54A, rng.randrange(60))
        put8(m, 0xB54B, rng.choice([0, 6, 0x12, 0x19, 0x23]))
        put8(m, 0xD649, rng.choice([get8(m, 0xB54A), 0xFF, rng.randrange(60)]))
        put8(m, 0xB81E, rng.randrange(256))
        put8(m, 0xD191, rng.randrange(256))
        put8(m, 0xD192, rng.randrange(256))
        if rng.random() < 0.3:
            ah = get8(m, 0xD192)
            al = get8(m, 0xB81E) if get16(m, 0x0086) == 1 else get8(m, 0xD191)
            put16(m, 0xD647, ah << 8 | al)
        else:
            put16(m, 0xD647, rng.choice([0xFFFF, rng.randrange(0x10000)]))
        h.check('message_line_draw', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000)},
                outputs=['si'], label='case %d station %04X' % (i, get16(m, 0x0086)))
        n += 1
    return n


# ---------------------------------------------------------------- crew gunners and reloads

def aim_at_entry(m, rng, gun_heading, gun_fraction, gun_elevation):
    """Turn a near visible entry (one of the last ones, the list is sorted far to near) into a
    target for a gun: a hostile kind, its bearing near the gun's, its elevation near the gun's."""
    count = get16(m, 0xB83D)
    if count == 0:
        return
    e = count - 1 - rng.randrange(min(count, 12))
    obj = get16(m, 0x523E + 2 * e)
    put8(m, 0xB95D + obj, rng.choice([1, 5, 0x0A, 0x0D, 0x14, 0x17, 0x10, 0x11, 0x18, 0x12]))
    put8(m, 0x4C97 + 2 * e, rng.choice([1, 5, 0x12, 0x13]))
    view = get8(m, 0xD191)
    a = rng.choice([0x14, 0x14, 0x13, 0x15, 0x10, 0x18, 0x0F, 0x19, 0, 0x28, 0x29, rng.randrange(256)])
    put8(m, 0x4E00 + e, (gun_heading + 0x38 + a - view) & 0xFF)
    if a == 0x14 and rng.random() < 0.7:
        put8(m, 0x4EB5 + e, (gun_fraction - get8(m, 0xD192) + rng.choice([0, 0, 1, 7, 8, 0xF8])) & 0xFF)
    put8(m, 0x4F6A + e, (-gun_elevation - 0x0E + rng.choice([0, 0, 1, 3, 4, 7, 8, -1, -4, -8, 0x40])) & 0xFF)
    for k in range(e + 1, count):
        if rng.random() < 0.3:
            put8(m, 0x4E00 + k, rng.randrange(256))


def gunner_memory(h, rng):
    m = visible_state(h, rng)
    shake_crew(m, rng)
    put16(m, 0xB82E, rng.choice([1, 1, 1, 0, 0x100]))
    put8(m, 0xD96B, rng.choice([0, 0, 0, 0, 1]))
    put16(m, 0x0086, rng.choice([1, 2, 3, 4, 1]))
    put16(m, 0x0070, rng.choice([0, 0, 0, 1]))
    put8(m, 0xB807, rng.choice([0, 1, 2, 1]))
    put8(m, 0x0088, rng.randrange(256))
    for off in (0xB81F, 0xB820, 0xB821):
        if rng.random() < 0.5:
            put8(m, off, (get8(m, 0xB81E) + rng.choice([0, 0x10, 0x40, 0x80, 0xC0, 0xE0, 0x20, 0x60, 0xA1])) & 0xFF)
    for off in (0xB836, 0xB837, 0xB838):
        put8(m, off, rbyte(rng, [0x60, 0x64, 0x68, 0x69, 0x80, 0xC0, 0xFF]))
    for off in (0xD67D, 0xD67E, 0xD67F):
        put8(m, off, rng.randrange(256))
    test_sim.timers(m, rng)
    for off in (0xD525, 0xD526, 0xD529, 0xD52A, 0xD52E, 0xD52F):
        put8(m, off, get8(m, off) & 0xFE if rng.random() < 0.8 else get8(m, off) | 1)
    put8(m, 0xB830, rng.choice([8, 8, 7]))
    put8(m, 0xB831, rng.choice([0x30, 0x30, 0x2F]))
    if rng.random() < 0.7:
        gun = rng.randrange(3)
        heading = get8(m, (0xB81F, 0xB821, 0xB820)[gun])
        fraction = get8(m, (0xB823, 0xB825, 0xB824)[gun])
        elevation = get8(m, (0xB837, 0xB838, 0xB836)[gun])
        aim_at_entry(m, rng, heading, fraction, elevation)
    return m


def test_crew_gunners(h, rng, scale):
    """The three gunners on combat states, with targets set up in front of each gun: AX and SI as
    game_frame passes them on."""
    n = 0
    for i in range(1500 * scale):
        m = gunner_memory(h, rng)
        regs = {'ax': rng.randrange(0x10000), 'si': rng.randrange(0x10000), 'cx': rng.randrange(0x10000),
                'dx': rng.randrange(0x10000)}
        h.check('crew_gunners', m, regs=regs, outputs=['ax', 'si'], label='case %d' % i)
        n += 1
    return n


def test_gunners(h, rng, scale):
    n = 0
    for name in ('gunner_bow', 'gunner_stern', 'gunner_midship'):
        for i in range(700 * scale):
            m = gunner_memory(h, rng)
            regs = {'ax': rng.randrange(0x10000), 'si': rng.randrange(0x10000), 'cx': rng.randrange(0x10000),
                    'dx': rng.randrange(0x10000)}
            h.check(name, m, regs=regs, outputs=['ax', 'si'], label='case %d' % i)
            n += 1
    return n


def test_gunner_aim(h, rng, scale):
    """gunner_aim entered with each gun described as its callers do, targets in front of it, the
    sweep against the arc limits, and an empty or wild visible list."""
    n = 0
    guns = [(0xB81F, 0xB823, 0, 0xB837, 0xD67D), (0xB821, 0xB825, 1, 0xB838, 0xD67E),
            (0xB820, 0xB824, 2, 0xB836, 0xD67F)]
    for i in range(2500 * scale):
        m = gunner_memory(h, rng)
        heading, fraction, gun, elevation, sweep = rng.choice(guns)
        put16(m, 0xB7DE, heading)
        put8(m, 0xB7E3, get8(m, fraction))
        put8(m, 0xB7E2, gun if rng.random() < 0.95 else rng.randrange(256))
        put8(m, 0xB7E9, get8(m, elevation))
        put16(m, 0xB7DC, sweep)
        aim_at_entry(m, rng, get8(m, heading), get8(m, fraction), get8(m, elevation))
        if rng.random() < 0.03:
            put16(m, 0xB83D, rng.choice([0, 1]))
        regs = {'ax': rng.randrange(0x10000), 'si': rng.randrange(0x10000), 'cx': rng.randrange(0x10000),
                'dx': rng.randrange(0x10000)}
        h.check('gunner_aim', m, regs=regs, outputs=['ax', 'si'], label='case %d gun %d' % (i, gun))
        n += 1
    for i in range(600 * scale):
        m = gunner_memory(h, rng)
        put8(m, 0xB7E2, rng.choice([0, 1, 2, 3, 0xFF]))
        bl = rng.choice([1, 2, 4, 8, 0, 3, 0x0C, rng.randrange(256)])
        cl = rng.choice([0, 1, 2]) if rng.random() < 0.9 else rng.randrange(256)
        h.check('gunner_key', m, regs={'bx': rng.randrange(0x100) << 8 | bl, 'cx': rng.randrange(0x100) << 8 | cl,
                                       'ax': rng.randrange(0x10000)},
                outputs=['ax'], label='case %d keys %02X rate %02X' % (i, bl, cl))
        n += 1
    return n


def test_reload_tick(h, rng, scale):
    n = 0
    for i in range(600 * scale):
        m = any_state(h, rng)
        test_hud_panel(m, rng)
        put8(m, 0xB830, rng.choice([8, 7, 1, 2, 0, 9, rng.randrange(256)]))
        put8(m, 0xB831, rng.choice([0x30, 0x2F, 1, 2, 0, 0x31, rng.randrange(256)]))
        put16(m, 0x0086, rng.choice([1, 2, 3, 4, 5]))
        put8(m, 0xD96B, rng.choice([0, 0, 1]))
        h.check('reload_tick', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000)}, outputs=['si'],
                label='case %d' % i)
        n += 1
    return n


# ---------------------------------------------------------------- mission clock

CLOCK_POLLS = ['0919:1d9e', '0919:1da4']


def test_mission_clock_tick(h, rng, scale):
    """The clock's carries into seconds, minutes (time of day) and hours (BCD, 24 -> 0), the score
    word, the deadline, and the easter egg's busy-wait of 300h ticks (one tick per poll on both
    sides)."""
    n = 0
    for i in range(1500 * scale):
        m = any_state(h, rng)
        put8(m, 0xD69F, rng.choice([0x0E, 0x0E, 0x0D, 0, 0x0F, 0xFF]))
        put8(m, 0xB549, rng.choice([0x3B, 0x3B, 0x3A, 0, 0x3C, 0xFF]))
        put8(m, 0xB54A, rng.choice([0x3B, 0x3B, 0x3A, 0x04, 0x36, 0x39, 0, 0x3C, 0xFF, rng.randrange(60)]))
        put8(m, 0xB54B, rng.choice([0x23, 0x05, 0x06, 0x19, 0x09, 0x12, 0x29, rng.randrange(256)]))
        put16(m, 0xB52E, rng.choice([0, 0x99, 0x9999, rng.randrange(0x10000)]))
        egg = rng.random() < 0.1
        put16(m, 0xB516, (rng.randrange(256) << 8 | 0x24) if egg else rng.choice([0, 0x23, 0x25, 0x124]))
        if rng.random() < 0.5:
            minutes = (get8(m, 0xB54A) + 1) % 0x3C
            hours = get8(m, 0xB54B)
            put8(m, 0xB547, minutes)
            put8(m, 0xB548, hours if minutes else rng.choice([hours, (hours + 1) & 0xFF]))
        else:
            put8(m, 0xB547, rng.choice([0, rng.randrange(60)]))
            put8(m, 0xB548, rng.choice([0, rng.randrange(256)]))
        put8(m, 0xB800, rng.choice([0, 1, 2]))
        put16(m, 0x08C0, rng.randrange(0x10000))
        h.check('mission_clock_tick', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000)},
                outputs=['si'], tick=(CLOCK_POLLS, 'tick_counter'), max_insns=20_000_000,
                label='case %d %02X:%02X:%02X.%02X' % (i, get8(m, 0xB54B), get8(m, 0xB54A), get8(m, 0xB549),
                                                    get8(m, 0xD69F)))
        n += 1
    return n


# ---------------------------------------------------------------- engines and propulsion

def engine_memory(h, rng):
    m = any_state(h, rng)
    for off in (0xB808, 0xB809):
        put8(m, off, rbyte(rng, [0, 1, 1, 1, 2, 3, 4, 5, 0x7F, 0x7D, 0x80, 0x81, 0xFE, 0xFF, 0xF0, 0x8F]))
    for off in (0xB816, 0xB817):
        put8(m, off, rbyte(rng, [0, 8, 9, 0x0A, 0x0B, 0x0C, 0x10, 0x3B, 0x44, 0x67, 0xFF]))
    for off in (0xB80A, 0xB80C):
        put16(m, off, rng.choice([0, 1, 2, 0x10, 0x11, 0x20, 0x21, 0x0E, 0x0F, 0xC544, rng.randrange(0x10000)]))
    for off in (0xD506, 0xD507, 0xD50A, 0xD50B):
        put8(m, off, get8(m, off) & 0xFC | rng.choice([3, 3, 2, 1, 0]))
    for off in (0xD521, 0xD522):
        put8(m, off, rng.choice([0x10, 0x11, 0x15, rng.randrange(256)]))
    put8(m, 0xB818, rng.randrange(256))
    if rng.random() < 0.2:
        put8(m, 0xD6B6, rng.randrange(256))
        put8(m, 0xD6B7, rng.randrange(256))
    put8(m, 0x008B, rng.choice([0, 0x20, 0x40, 0xE0, rng.randrange(256)]))
    put16(m, 0x0086, rng.choice([1, 1, 2, 3, 4, 0x101]))
    put8(m, 0xD6BF, rng.choice([0, 0, 1]))
    put8(m, 0xD6BA, rng.choice([1, 1, 2, 0, 0x11]))
    put8(m, 0xB828, rbyte(rng, [0, 1, 0x7F, 0x80, 0x81, 0xFF, 0xF8]))
    put8(m, 0xD6B4, rng.choice([0, 0, 1]))
    for k in range(1, 36):
        if rng.random() < 0.1:
            put8(m, 0xB95D + 2 * k, rng.randrange(256))
    return m


def test_engine_thrust(h, rng, scale):
    n = 0
    for i in range(3000 * scale):
        m = engine_memory(h, rng)
        si = rng.choice([0, 1])
        h.check('engine_thrust', m, regs={'si': si, 'ax': rng.randrange(0x10000), 'cx': rng.randrange(0x10000),
                                          'dx': rng.randrange(0x10000)},
                outputs=['cx', 'dx', 'si'], label='case %d engine %d state %02X throttle %02X jet %02X' % (
                    i, si, get8(m, 0xB808 + si), get8(m, 0xB816 + si), get8(m, 0xB818)))
        n += 1
    return n


def test_propulsion(h, rng, scale):
    """Random engine states, and both engines running near full throttle with the jet turned hard
    (the turning step clamped to 37h)."""
    n = 0
    for i in range(2000 * scale):
        m = engine_memory(h, rng)
        if i % 5 == 0:
            for k in (0, 1):
                put8(m, 0xB808 + k, 1)
                put8(m, 0xB816 + k, rng.randrange(0x80, 0x90))    # the summed turn reaches the clamp
                put8(m, 0xD50A + k, get8(m, 0xD50A + k) | 3)
            put8(m, 0xB818, rng.choice([0, 2, 5, 8, 0x78, 0x7C, 0x80, 0x10, 0xF0, 0xFD, rng.randrange(256)]))
        h.check('propulsion', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000)}, outputs=['si'],
                label='case %d' % i)
        n += 1
    return n


# ---------------------------------------------------------------- damage

def damage_memory(h, rng):
    m = any_state(h, rng)
    test_hud_panel(m, rng)
    for off in range(0xD502, 0xD520):
        if rng.random() < 0.5:
            put8(m, off, get8(m, off) & 0xFC | rng.choice([3, 3, 1, 0, 2]))
    put16(m, 0xF110, rng.choice([0, 0, 0, 1, 0x100]))
    put16(m, 0xB505, rng.choice([2, 3, 5, 0, 1, 0x101]))
    put16(m, 0x0086, rng.choice([1, 2, 3, 4, 7]))
    put8(m, 0xD96B, rng.choice([0, 0, 0, 1]))
    for off in (0xB81C, 0xB81D, 0xB816, 0xB817):
        put8(m, off, rng.choice([0x3B, 0x67, 0x0C, 0x0D, 0x1D, 6, rng.randrange(256)]))
    put8(m, 0xB7FE, rng.choice([0, 1, 2, 3, 0x80, 0xFF]))
    put8(m, 0x008A, rng.randrange(256))
    put16(m, 0x0088, rng.randrange(0x10000))
    put16(m, 0x008A, rng.randrange(0x10000))
    put8(m, 0xB800, rng.choice([0, 1, 2]))
    return m


def test_boat_hit(h, rng, scale):
    """Every weapon class through the RNG draw; then every component directly, each condition,
    practice and campaign, every station, the captain, the gunners and the engines."""
    n = 0
    for i in range(1500 * scale):
        m = damage_memory(h, rng)
        al = rng.choice(range(8)) if rng.random() < 0.9 else rng.randrange(256)
        h.check('boat_hit', m, regs={'ax': rng.randrange(0x100) << 8 | al, 'si': rng.randrange(0x10000)},
                outputs=['si'], label='case %d class %02X' % (i, al))
        n += 1
    for i in range(2500 * scale):
        m = damage_memory(h, rng)
        al = i % 16 if rng.random() < 0.95 else rng.randrange(0x14)
        cl = rng.choice([0x0A, 0x0C, 0x0A])
        h.check('boat_hit_component', m, regs={'ax': rng.randrange(0x100) << 8 | al, 'cx': cl,
                                               'si': rng.randrange(0x10000)},
                outputs=['si'], label='case %d component %02X' % (i, al))
        n += 1
    return n


def test_damage_panel_refresh(h, rng, scale):
    n = 0
    for i in range(400 * scale):
        m = damage_memory(h, rng)
        put16(m, 0x0086, rng.choice([2, 3, 4, 1]))
        al = rng.choice([0x0A, 0x0C, 0x0D, 0x0B, rng.randrange(256)])
        put8(m, 0xD50C + rng.randrange(4), rng.randrange(4))
        h.check('damage_panel_refresh', m, regs={'ax': rng.randrange(0x100) << 8 | al},
                label='case %d al %02X' % (i, al))
        n += 1
    return n


def test_sinking_update(h, rng, scale):
    n = 0
    for i in range(1000 * scale):
        m = damage_memory(h, rng)
        put8(m, 0xD510, rng.choice([0, 1, 2, 3, 0x43, 0x42]))
        put8(m, 0xB7FE, rng.choice([0, 1, 2, 3, 0x7F, 0x80, 0x81, 0xFF]))
        put8(m, 0xB7FF, rng.choice([0, 0x0E, 0x0F, 0x10, 0xF0, 0xFF]))
        put8(m, 0xD70C, rng.choice([0x5B, 0x5B, 0x5A, 0]))
        h.check('sinking_update', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000)},
                outputs=['si'], label='case %d' % i)
        n += 1
    return n


def test_window_hit(h, rng, scale):
    n = 0
    for i in range(500 * scale):
        m = damage_memory(h, rng)
        put16(m, 0x0086, rng.choice([1, 2, 3, 4]))
        put8(m, 0x0089, rng.randrange(256))
        for k in range(5):
            put8(m, 0x0B49 + k, rng.choice([0, 0xFF, rng.randrange(256)]))
        h.check('window_hit', m, label='case %d' % i)
        n += 1
    return n


def test_boat_destroyed(h, rng, scale):
    n = 0
    for i in range(300 * scale):
        m = damage_memory(h, rng)
        for k in range(1, 36):
            if rng.random() < 0.5:
                put8(m, 0xB95D + 2 * k, rng.randrange(256))
        h.check('boat_destroyed', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000)},
                outputs=['si'], label='case %d' % i)
        n += 1
    return n


# ---------------------------------------------------------------- passengers, missile, enemies

def test_mission_stop(h, rng, scale):
    """Insertions and extractions: the boat arriving next to the row's first object (within the
    80h box or not), the five passengers appearing, walking, arriving and the objective done."""
    n = 0
    for i in range(1500 * scale):
        m = any_state(h, rng)
        put16(m, 0xB509, rng.choice([2, 4, 0x0A, 2, 4, 0x0A, 3, 0x102, 0x104]))
        mission = rng.randrange(8)
        put16(m, 0xB505, mission)
        row = 0xB909 + 10 * mission
        first = rng.randrange(36, 900) * 2
        for k in range(5):
            put16(m, row + 2 * k, first + 2 * k if rng.random() < 0.8 else rng.randrange(1000) * 2)
        bx, by = get16(m, 0xC12D), get16(m, 0xC8FD)
        if rng.random() < 0.8:
            put16(m, 0xC12D + first, (bx + rng.choice([0, 5, -5, 0x7F, -0x7F, 0x80, -0x80, 0x100])) & 0xFFFF)
            put16(m, 0xC8FD + first, (by + rng.choice([0, 5, -5, 0x7F, -0x7F, 0x80, -0x80, 0x100])) & 0xFFFF)
        locked = rng.random() < 0.6
        put8(m, 0xD6B4, 1 if locked else 0)
        put8(m, 0xD505, get8(m, 0xD505) & 0xFC | rng.choice([2, 2, 3, 1]))
        put8(m, 0xD6B5, rng.choice([1, 2, 5, 0]))
        if locked:
            for k in range(1, 6):
                kind = rng.choice([0, 0x24, 0x25, 0x25, 0x24])
                put16(m, 0xB95D + 2 * k, (rng.randrange(1, 6) << 8 | kind) if kind else 0)
                target = (bx, by) if kind == 0x24 else (get16(m, 0xC12D + get16(m, row + 2 * (k - 1))),
                                                        get16(m, 0xC8FD + get16(m, row + 2 * (k - 1))))
                put16(m, 0xC12D + 2 * k, (target[0] + rng.randrange(-24, 25)) & 0xFFFF)
                put16(m, 0xC8FD + 2 * k, (target[1] + rng.randrange(-24, 25)) & 0xFFFF)
        h.check('mission_stop', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000)}, outputs=['si'],
                label='case %d type %04X locked %d' % (i, get16(m, 0xB509), locked))
        n += 1
    return n


def missile_memory(h, rng):
    """A missile in flight from a listed source (or an unlisted one, or a wreck), toward the boat."""
    m = visible_state(h, rng)
    count = get16(m, 0xB83D)
    e = rng.randrange(count)
    src = get16(m, 0x523E + 2 * e)
    if rng.random() < 0.1:
        src = rng.randrange(1000) * 2
    put16(m, 0xD6A6, src)
    if rng.random() < 0.1:
        put8(m, 0xB95D + src, rng.choice([0x18, 0x31]))
    put16(m, 0xD6B2, rng.choice([2 * e, 2 * e, rng.randrange(count) * 2, rng.randrange(0x10000) & 0xFFFE]))
    slot = rng.randrange(1, 36) * 2
    put16(m, 0xD6A0, slot)
    put16(m, 0xB95D + slot, 0x0012)
    put8(m, 0xD6A8, rng.choice([1, 1, 2, 0x41, 0x42, 0x40, 0]))
    put8(m, 0xD70C, rng.randrange(256))
    put8(m, 0xD6AD, rng.choice([5, 5, 1, 0, 0x10]))
    bx, by = get16(m, 0xC12D), get16(m, 0xC8FD)
    tx, ty = (bx + rng.choice([0, 2, -3])) & 0xFFFF, (by + rng.choice([0, 1, -2])) & 0xFFFF
    put16(m, 0xD6A9, tx)
    put16(m, 0xD6AB, ty)
    d = rng.choice([0, 1, 3, 5, 6, 20, 200])
    put16(m, 0xC12D + slot, (tx + rng.randrange(-d, d + 1)) & 0xFFFF)
    put16(m, 0xC8FD + slot, (ty + rng.randrange(-d, d + 1)) & 0xFFFF)
    if rng.random() < 0.3:
        put16(m, 0xC12D + slot, bx)
        put16(m, 0xC8FD + slot, by)
        put16(m, 0xD6A9, bx)
        put16(m, 0xD6AB, by)
    put8(m, 0x0089, rng.randrange(256))
    put16(m, 0xB503, rng.choice([0, 1, 2, 3]))
    return m


def test_missile_update(h, rng, scale):
    n = 0
    for i in range(1500 * scale):
        m = missile_memory(h, rng)
        damage = damage_memory(h, rng)
        for off in range(0xD502, 0xD520):                    # the boat's damage state from another state
            put8(m, off, get8(damage, off))
        h.check('missile_update', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000),
                                           'cx': rng.randrange(0x10000)},
                outputs=['si'], label='case %d age %02X' % (i, get8(m, 0xD6A8)))
        n += 1
    return n


def enemy_memory(h, rng):
    m = visible_state(h, rng)
    count = get16(m, 0xB83D)
    for e in range(count):
        obj = get16(m, 0x523E + 2 * e)
        kind = get8(m, 0xB95D + obj)
        if 0 < kind < 0x19 and rng.random() < 0.5:
            put8(m, 0xB95E + obj, rng.choice([0xC0, 0x80, 0x40, 0, 0xC8, 0xD0, 0xE0, rng.randrange(256)]))
        if rng.random() < 0.05:
            put8(m, 0xB95D + obj, rng.choice([0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x47, 0x48, 0x4A, 0x4B, 0x4C, 0x51,
                                               0x52, 0x53, 0x28, 0x29, 0x33, 0x34, 0x35, 0x14, 0x17, 0x12]))
            put8(m, 0xB95E + obj, rng.choice([0, 1, 2, rng.randrange(256)]))
        if rng.random() < 0.05:
            put8(m, 0x4C97 + 2 * e, rng.choice([0, 2, 5, 6, 0x0B, 0x0C, 0x17, 0x18]))
    put8(m, 0xD70C, rng.randrange(256))
    put16(m, 0x0088, rng.randrange(0x10000))
    put16(m, 0x008A, rng.randrange(0x10000))
    put16(m, 0xB503, rng.choice([0, 1, 2, 3]))
    put8(m, 0xB7FC, rng.choice([0, 1]))
    put8(m, 0xD905, rng.choice([0, 0, 1]))
    put8(m, 0xDA39, rng.choice([0, 0, 1]))
    put8(m, 0xB7F1, rng.choice([0, 1, 2, 0, rng.randrange(256)]))
    put8(m, 0xD8BC, rng.choice([0] * 9 + [1]))
    put8(m, 0xD6A8, rng.choice([0, 0, 0, 3]))
    put16(m, 0xD6A6, rng.choice([0, get16(m, 0xD6A6), get16(m, 0x523E + 2 * rng.randrange(count))]))
    for k in range(16):
        put8(m, 0xD71F + k, rng.choice([0, 0, 0, 0, 1, 5]))
    for off in (0xB816, 0xB817):
        put8(m, off, rng.choice([8, 0x3B, 0x44, 0x67]))
    for k in range(1, 36):
        if rng.random() < 0.2:
            put8(m, 0xB95D + 2 * k, rng.randrange(1, 256))
    if rng.random() < 0.1:
        for k in range(0x28):                               # other behaviour and class bytes
            if rng.random() < 0.3:
                put8(m, 0x541A + 2 * k, rng.randrange(256))
                put8(m, 0x541B + 2 * k, rng.randrange(256))
    put8(m, 0xB800, rng.choice([0, 0, 1, 2]))
    return m


def test_enemy_update(h, rng, scale):
    """The enemies' pass on the combat states: effects, spotting, firing (shots, salvos, missiles),
    movement; every world-pass phase and the RNG bytes random."""
    n = 0
    for i in range(1500 * scale):
        m = enemy_memory(h, rng)
        h.check('enemy_update', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000),
                                         'cx': rng.randrange(0x10000)},
                outputs=['si'], label='case %d pass %02X' % (i, get8(m, 0xD70C)))
        n += 1
    return n


def test_enemy_missile(h, rng, scale):
    """A missile boat (kind 14h) or launcher (17h) near the boat, alerted and active, with the fire
    gates mostly open: the launch (free slot or not, a missile already flying, the random gate, the
    launcher's minimum distance) against the ordinary shot."""
    n = 0
    for i in range(400 * scale):
        m = enemy_memory(h, rng)
        count = get16(m, 0xB83D)
        e = count - 1 - rng.randrange(min(count, 30))
        obj = get16(m, 0x523E + 2 * e)
        if obj < 0x48:
            obj = rng.randrange(40, 900) * 2
            put16(m, 0x523E + 2 * e, obj)
        kind = rng.choice([0x14, 0x17])
        put8(m, 0xB95D + obj, kind)
        put8(m, 0xB95E + obj, rng.choice([0xC0, 0xC0, 0xC1, 0xC8]))
        put8(m, 0x4C97 + 2 * e, rng.choice([2, 4, 5, 6, 7, 8, 0x0A]))
        put8(m, 0x541A + 2 * kind, rng.choice([0x2C, 0x28, 0x2D, 0x24, 0x34, 0x28]))
        put8(m, 0x541B + 2 * kind, rng.choice([0xC0, 0x80, 0x40, 0xE3]))
        region = get16(m, 0xB503) & 0xFF
        if rng.random() < 0.9:
            put8(m, 0xB3F4 + region, 0)                     # the phase gate open
        if rng.random() < 0.9:
            put8(m, 0x008A, 0)                              # the fire-rate gate open
        put8(m, 0x0089, rng.choice([0, 0, 0, 1, 0x10, rng.randrange(256)]))
        put8(m, 0xD6A8, rng.choice([0, 0, 0, 2]))
        put16(m, 0xD6A6, rng.choice([0, 0, obj]))
        for k in range(e + 1, count):                       # nothing nearer covers it
            if rng.random() < 0.9:
                put8(m, 0x5189 + k, 0)
        full = rng.random() < 0.2
        for k in range(1, 36):
            put8(m, 0xB95D + 2 * k, rng.randrange(1, 256) if full or rng.random() < 0.5 else 0)
        put8(m, 0xD8BC, 0)
        h.check('enemy_update', m, regs={'si': rng.randrange(0x10000)}, outputs=['si'],
                label='case %d kind %02X entry %d' % (i, kind, e))
        n += 1
    return n


def test_enemy_frames(h, rng, scale):
    """enemy_update called again and again on one state (the effects and movement run on)."""
    n = 0
    for i in range(15 * scale):
        m = enemy_memory(h, rng)
        put8(m, 0xD8BC, 0)
        for f in range(30):
            h.check('enemy_update', m, regs={'si': 0x1234}, label='run %d pass %d' % (i, f))
            m = bytearray(h.orig.memory())
            n += 1
    return n


def test_schedule_shot(h, rng, scale):
    n = 0
    for i in range(1500 * scale):
        m = enemy_memory(h, rng)
        count = get16(m, 0xB83D)
        si = rng.randrange(count) * 2
        al = rng.choice([0x14, 0x15, 0x17, 0x04, 0x08, 0x09, 0x0C, 0x10, 0x1C, 0x00, 0x03])
        al |= rng.choice([0, 0x20, 0xE0])
        if rng.random() < 0.3:
            al = rng.randrange(256)
        for k in range(16):
            put8(m, 0xD71F + k, rng.choice([0, 0, 1]) if rng.random() < 0.7 else 0)
        if rng.random() < 0.2:
            for k in range(8):
                put8(m, 0xD71F + k, 1)
        put8(m, 0xB7E2, rng.randrange(0x0C))
        obj = get16(m, 0x523E + si)
        if rng.random() < 0.3:
            put8(m, 0xB95D + obj, rng.choice([0x0C, 0x0D, 0x0E, 0x0F, 0x10]))
        h.check('schedule_shot', m, regs={'ax': rng.randrange(0x100) << 8 | al, 'si': si}, outputs=['si'],
                label='case %d al %02X' % (i, al))
        n += 1
    return n


# ---------------------------------------------------------------- projectile impacts

def shot_at(m, rng, e):
    """A shot (bearing word, range byte) aimed at visible entry e, near enough to reach every limit
    test of hit_test."""
    view = get8(m, 0xD191) << 8 | get8(m, 0xD192)
    entry = get8(m, 0x4E00 + e) << 8 | get8(m, 0x4EB5 + e)
    if rng.random() < 0.85:
        shot = (entry + view - 0x4C00 + rng.randrange(-0x300, 0x300)) & 0xFFFF
        rng_ = (get8(m, 0x4F6A + e) + 2 - rng.randrange(-2, 0x20)) & 0xFF
    else:
        shot, rng_ = rng.randrange(0x10000), rng.randrange(256)
    return shot, rng_


def target_object(m, rng, e):
    """Make the object of entry e something to hit: a kind (hostile, missile, friendly, wreck, bridge,
    scenery, effect), its damage bits, class and score bytes, maybe an objective."""
    obj = get16(m, 0x523E + 2 * e)
    kind = rng.choice([0x01, 0x05, 0x0A, 0x0D, 0x10, 0x11, 0x12, 0x13, 0x14, 0x17, 0x18, 0x19, 0x20, 0x21, 0x24, 0x27,
                       0x28, 0x29, 0x2A, 0x2F, 0x30, 0x31, 0x32, 0x3A, 0x3E, 0x3F, 0x00, rng.randrange(0x40)])
    put8(m, 0xB95D + obj, kind)
    put8(m, 0xB95E + obj, rng.choice([0, 0x08, 0x10, 0x18, 0x28, 0x30, 0x38, 0xC0, 0xF8, rng.randrange(256)]))
    if rng.random() < 0.5:
        cls = rng.choice([0, 1, 7, 0x0F, 0x17, 0x1F, 0x08, 0x10, 0x18, 0x20, 0x44, 0x05, 0x06, 0x04, 0x02, 0x03,
                          rng.randrange(256)])
        put8(m, 0x541B + 2 * kind, cls)
    if rng.random() < 0.3:
        put8(m, 0x5401 + kind, rng.choice([0, 1, 2, 3, 4]))
    if rng.random() < 0.3:
        put16(m, 0xB841 + 2 * rng.randrange(5), obj)
        put8(m, 0xB544, rng.choice([0, 1, 2, 2, 3]))
    return obj


def impact_memory(h, rng):
    m = visible_state(h, rng)
    count = get16(m, 0xB83D)
    e = count - 1 - rng.randrange(min(count, 40)) if rng.random() < 0.7 else rng.randrange(count)
    obj = target_object(m, rng, e)
    shot, range_ = shot_at(m, rng, e)
    put8(m, 0x0089, rng.randrange(256))
    put16(m, 0xD6A0, rng.choice([0, 0x44, 0x42, rng.randrange(1, 36) * 2]))
    for k in range(1, 36):
        if rng.random() < 0.3:
            put8(m, 0xB95D + 2 * k, rng.randrange(1, 256))
    if rng.random() < 0.3:                                  # structures in the object's cell
        x, y = get16(m, 0xC12D + obj), get16(m, 0xC8FD + obj)
        put16(m, 0xD0CD, rng.randrange(1, 33))
        for k in range(32):
            put16(m, 0xD10F + 2 * k, x & 0xFC00 | rng.randrange(0x400))
            put16(m, 0xD14F + 2 * k, y & 0xFC00 | rng.randrange(0x400))
            put16(m, 0xD0CF + 2 * k, rng.randrange(256) << 8 | rng.choice([0x50, 0x5C, 0x5D, 0x5E, 0x60, 0x62]))
    put8(m, 0xB800, rng.choice([0, 0, 1, 2]))
    return m, e, shot, range_


def test_hit_objects(h, rng, scale):
    """Shots at every kind of object: skipped ones, wrecks, missiles, friendlies, class 0 and 1,
    damage below and at destruction with every wreck variant, fires, bridges, scores and objectives."""
    n = 0
    for i in range(3000 * scale):
        m, e, shot, range_ = impact_memory(h, rng)
        put16(m, 0xD749, shot)
        put8(m, 0xD74B, range_)
        put8(m, 0xB7EA, rng.choice([1, 2, 3, 4, 5]) if rng.random() < 0.95 else rng.randrange(256))
        h.check('hit_objects', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000)},
                label='case %d entry %d kind %02X' % (i, e, get8(m, 0xB95D + get16(m, 0x523E + 2 * e))))
        n += 1
    return n


def test_projectile_impact(h, rng, scale):
    n = 0
    for i in range(1500 * scale):
        m, e, shot, range_ = impact_memory(h, rng)
        slot = rng.randrange(32)
        rec = 0xD1DC + 8 * slot
        put8(m, rec, rng.choice([1, 2, 3, 4, 5, 0, 6]))
        put16(m, rec + 1, rng.randrange(0x10000))
        put16(m, rec + 3, rng.randrange(0x10000))
        put8(m, rec + 5, shot & 0xFF)
        put8(m, rec + 6, shot >> 8)
        put8(m, rec + 7, range_)
        for k in range(get16(m, 0xB83D)):
            if rng.random() < 0.2:
                put8(m, 0x4C97 + 2 * k, rng.choice([5, 6, 7, 8]))
        h.check('projectile_impact', m, regs={'bx': slot, 'si': rng.randrange(0x10000)}, outputs=['si'],
                label='case %d slot %d' % (i, slot))
        n += 1
    return n


def test_projectile_tick(h, rng, scale):
    n = 0
    for i in range(800 * scale):
        m, e, shot, range_ = impact_memory(h, rng)
        put8(m, 0xD8BC, rng.choice([0] * 9 + [1]))
        for slot in range(32):
            put8(m, 0xD1BC + slot, rng.choice([0, 0, 0, 2, 5, 1]))
            rec = 0xD1DC + 8 * slot
            put8(m, rec, rng.choice([1, 2, 3, 4, 5]))
            if rng.random() < 0.5:
                put8(m, rec + 5, shot & 0xFF)
                put8(m, rec + 6, shot >> 8)
                put8(m, rec + 7, range_)
        h.check('projectile_tick', m, regs={'si': rng.randrange(0x10000), 'ax': rng.randrange(0x10000)},
                outputs=['si'], label='case %d' % i)
        n += 1
    return n


def test_mark_near_objects(h, rng, scale):
    n = 0
    for i in range(400 * scale):
        m, e, shot, range_ = impact_memory(h, rng)
        put16(m, 0xD749, shot)
        put8(m, 0xD74B, range_)
        put8(m, 0xB7EA, rng.choice([1, 2, 3, 4, 5]))
        if rng.random() < 0.05:
            put16(m, 0xB83D, rng.choice([0, 1]))
        h.check('mark_near_objects', m, regs={'si': rng.randrange(0x10000)}, outputs=['si'], label='case %d' % i)
        n += 1
    return n


def test_mission_target_check(h, rng, scale):
    n = 0
    for i in range(600 * scale):
        m = any_state(h, rng)
        si = rng.randrange(1000) * 2
        for k in range(5):
            put16(m, 0xB841 + 2 * k, si if rng.random() < 0.3 else rng.randrange(1000) * 2)
        put8(m, 0xB544, rng.choice([0, 1, 2, 2, 3, 0xFF]))
        test_hud_panel(m, rng)
        put16(m, 0x007A, rng.choice([0, 1]))
        ax = rng.randrange(0x10000)
        reaches_three = si in [get16(m, 0xB841 + 2 * k) for k in range(5)] and get8(m, 0xB544) == 2
        # AX: AL is the message; AH is AH in, except after the lamp and sound (sfx_play's AH)
        h.check('mission_target_check', m, regs={'si': si, 'ax': ax}, outputs=[] if reaches_three else ['ax'],
                label='case %d' % i)
        n += 1
    return n


TESTS = [test_key_dispatch, test_key_handlers, test_key_identify, test_panel_switch_toggle, test_controls_poll,
         test_pilot_throttle_controls, test_message_sequencer, test_heading_readout, test_message_line_draw,
         test_crew_gunners, test_gunners, test_gunner_aim, test_reload_tick, test_mission_clock_tick,
         test_engine_thrust, test_propulsion, test_boat_hit, test_damage_panel_refresh, test_sinking_update,
         test_window_hit, test_boat_destroyed, test_mission_stop, test_missile_update, test_enemy_update,
         test_enemy_missile, test_enemy_frames, test_schedule_shot, test_hit_objects, test_projectile_impact,
         test_projectile_tick, test_mark_near_objects, test_mission_target_check]
