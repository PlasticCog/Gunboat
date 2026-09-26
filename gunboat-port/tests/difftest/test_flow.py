"""The game flow's keys, waits and screens (game_flow.md §1, §3).

Until the sound branch is merged the effects timer stays off (DS:DA46 = 0): sfx_play then does
nothing in the original, which the port's placeholder matches. Keys that quit (Ctrl+Q), pause (Esc)
or stop the music (S with the menu timer on) are left out until quit_to_dos and the music are
ported."""
import struct
import sys

from gbdiff import DS_BASE, RE, STACK_BOTTOM, put8, put16, randomize
from test_video import vga_state

sys.path.insert(0, str(RE / 'tools'))
from gbfile import extract  # noqa: E402

KEYPTR = 0xF000           # DS offset of the key word the tests pass
BIOS_POLL = ['15d4:0019']  # bios_wait_ticks polls INT 1Ah here: one BIOS tick per poll
DAT6 = 0x6E54
CREDITS = 0x6EB1


def flow_state(h, rng):
    """Mode 13h, random DGROUP apart from the data the functions must find intact."""
    m = vga_state(h, rng)
    keep = bytes(m[DS_BASE + 0xD000:DS_BASE + 0xE000])   # graphics library and text state
    randomize(m, DS_BASE + 0x9000, 0x4000, rng)
    m[DS_BASE + 0xD000:DS_BASE + 0xE000] = keep
    put8(m, 0xDA46, 0)                                   # effects timer off (see above)
    put16(m, 0x08BE, 0)                                  # menu timer off
    put16(m, 0x0072, 0)
    return m


def with_dat6(m):
    data = extract('DAT6.DAT')
    m[DS_BASE + DAT6:DS_BASE + DAT6 + len(data)] = data
    return m


def safe_key(rng):
    while True:
        k = rng.choice([0x0D, 0x20, 0x41, 0x44, 0x45, 0x65, 0x61, 0x7A, 0x91, 0x95, 0x99, 0x31,
                        rng.randrange(1, 256)])
        if k != 0x80 and (k & 0xDF) not in (ord('Q'), ord('S')):   # never 0: a key is there
            return k


def test_bios_wait_ticks(h, rng, scale):
    n = 0
    for count in [-1, 0, 1, 2, 5, 37]:
        for start in (0, 0x7FFE, 0xFFFE, rng.randrange(0x10000)):
            m = h.fresh_memory()
            struct.pack_into('<I', m, 0x46C, start | rng.randrange(0x18) << 16)
            h.check('bios_wait_ticks', m, stack_args=[count & 0xFFFF], outputs=['ax'], tick=(BIOS_POLL, 'bios'),
                    label='%d ticks from %04X' % (count, start))
            n += 1
    return n


def test_demo_next_key(h, rng, scale):
    n = 0
    for _ in range(400 * scale):
        m = flow_state(h, rng)
        put16(m, 0x0C68, rng.randrange(0, 0x66, 2))
        put8(m, 0x0C6A, rng.choice([0, 0, 1, rng.randrange(256)]))
        put8(m, 0x0C6B, rng.randrange(256))
        put8(m, 0xDA42, rng.choice([0, 0, 0, rng.randrange(256)]))
        h.check('demo_next_key', m, stack_args=[KEYPTR])
        n += 1
    return n


def test_input_read_key(h, rng, scale):
    n = 0
    for _ in range(400 * scale):
        m = flow_state(h, rng)
        put16(m, 0x0070, rng.choice([0, 0, 1]))                  # demo mode
        put16(m, 0x0C68, rng.randrange(0, 0x66, 2))
        put8(m, 0xDA42, rng.choice([0, safe_key(rng)]))
        put16(m, 0x007E, rng.choice([0, 1]))                     # muted
        put16(m, 0x0082, rng.choice([0, 1, 2, 3]))               # phase
        put16(m, 0xF394, 0)                                      # no joystick
        put8(m, 0xDA3A, rng.randrange(2))
        h.check('input_read_key', m, stack_args=[KEYPTR])
        n += 1
    return n


def test_wait_key(h, rng, scale):
    n = 0
    for _ in range(60 * scale):
        m = flow_state(h, rng)
        put16(m, 0x0070, 0)
        count = rng.choice([1, 2, 3, 8, 0])
        key = rng.choice([0, safe_key(rng)]) if count else safe_key(rng)
        put8(m, 0xDA42, key)
        h.check('wait_key', m, stack_args=[count], outputs=['ax'], tick=(BIOS_POLL, 'bios'),
                label='n %d key %02X' % (count, key))
        n += 1
    return n


def test_text_records(h, rng, scale):
    n = 0
    for _ in range(20 * scale):
        m = with_dat6(flow_state(h, rng))
        put8(m, 0xD9BC, rng.randrange(2))
        h.check('print_records', m, stack_args=[CREDITS, 0], outputs=['ax'])
        h.check('print_text', m, stack_args=[CREDITS, rng.choice([2, 3, 10])], outputs=['ax'])
        n += 2
    return n


def test_credits(h, rng, scale):
    n = 0
    for key in (0, 0, 0x0D, 0x44):
        m = with_dat6(flow_state(h, rng))
        put16(m, 0x0070, 0)
        put16(m, 0x08C2, 1)
        put8(m, 0xDA42, key)
        h.check('credits_text', m, stack_args=[CREDITS], outputs=['ax'], tick=(BIOS_POLL, 'bios'),
                label='key %02X' % key)
        n += 1
    return n


def test_screens(h, rng, scale):
    n = 0
    for mode in (0x13, 0x13, 0x0D):
        m = flow_state(h, rng)
        seg1 = struct.unpack_from('<H', m, DS_BASE + 0xDD33)[0]
        put16(m, 0xD9B6, 0xA000)
        put16(m, 0xD9B8, seg1)
        put16(m, 0xEED2, mode)
        m[DS_BASE + 0x08C4:DS_BASE + 0x08C4 + 225] = bytes(rng.randrange(64) for _ in range(225))
        if mode == 0x13:  # the EGA dissolve is parked (PORT)
            h.check('screen_present', m, tick=(['121b:0670'], 'tick_counter'), label='mode %X' % mode)
        for name, poll in (('pal_apply_vga', None), ('pal_black_vga', None), ('pal_fade_in_vga', '121b:0841'),
                           ('pal_fade_out_vga', '121b:07eb'), ('ega_pal_apply', None), ('ega_pal_init', None)):
            if name.startswith('ega') and mode != 0x13:
                continue  # the EGA/Tandy/CGA palettes are parked (PORT)
            h.check(name, m, tick=([poll], 'tick_counter') if poll else None, label='mode %X' % mode)
        h.check('kbd_flush_key', m)
        n += 8
    return n


TESTS = [test_bios_wait_ticks, test_demo_next_key, test_input_read_key, test_wait_key, test_text_records,
         test_credits, test_screens]


def test_menu_cursor_init(h, rng, scale):
    from test_pictures import base_memory
    n = 0
    for mode in (0x13, 0x13, 0x13):
        m = base_memory(h, rng)
        m[DS_BASE + 0xB000:DS_BASE + 0xC000] = bytes(0x1000)
        seg1 = struct.unpack_from('<H', m, DS_BASE + 0xDD33)[0]
        put16(m, 0xD9B6, 0xA000)
        put16(m, 0xD9B8, seg1)
        put16(m, 0xEED2, mode)
        data = (h.dos.dir / 'DATAC.DAT').read_bytes()
        m[DS_BASE + 0x027C:DS_BASE + 0x027C + len(data)] = data
        h.check('menu_cursor_init', m, label='mode %X' % mode)
        n += 1
    return n


TESTS.append(test_menu_cursor_init)


def test_config_load(h, rng, scale):
    """Start-up: GUNBOAT.CFG (the shipped one: VGA, no joystick) read, the mode set, page 1."""
    m = h.fresh_memory()
    h.check('config_load', m, label='GUNBOAT.CFG')
    return 1


TESTS.append(test_config_load)
