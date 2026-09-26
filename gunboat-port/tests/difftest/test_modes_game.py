"""The palette routines of the graphics library and the game side of the video modes other than VGA
(video.md §1-§4), each on the machines it has a path for: ega_pal_set, ega_pal_register,
gfx_set_ega_palette, gfx_set_pal_reg, gfx_set_display_offset, ega_pal_entry / ega_pal_apply /
ega_pal_init, palette_flash, screen_shake_step, text_draw_char (the EGA planar path) and
dissolve_page1_to_0. The library is put in its mode by the original's gfx_set_mode (test_modes);
all memory and the whole card state are compared, and the DAC on the VGA machine."""
import struct

import cardmodel
from gbdiff import DS_BASE, put8, put16, randomize, seg_of
from test_modes import after_original, machine, mode_state

SCRATCH = 0xF000                 # DS offset of a table the tests build (BSS, below the stack)
PALETTE_TABLES = (0x0924, 0x80)  # ega_palette, ega_patterns, cga_patterns (the palette file after the DAC part)

# The library modes each machine is tested in: the game's (EGA 0Dh, CGA 4, Tandy 9, Hercules 4 on
# the CGA mode) and the other modes of the same card.
LIB_MODES = {'ega': [0x0D, 0x0D, 0x0E, 0x0F, 0x10], 'cga': [4, 4, 5, 6], 'tandy': [9, 9, 8, 0x0A],
             'hercules': [4, 4, 0x0B, 0x0C], 'vga': [0x13, 0x13, 3]}
# The game's video mode (EED2) on each machine.
GAME_MODE = {'ega': 0x0D, 'cga': 4, 'tandy': 9, 'hercules': 4, 'vga': 0x13}


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def on_machines(*names):
    """One test function per machine (run_all.py picks the harness by the `machine` attribute)."""
    def make(body):
        tests = []
        for name in names:
            def test(h, rng, scale, _body=body):
                return _body(h, rng, scale)
            test.__name__ = 'test_%s_%s' % (body.__name__, name)
            tests.append(machine(name)(test))
        return tests
    return make


def game_state(h, rng, lib_mode=None, game_mode=None):
    """The library in lib_mode (default the machine's game mode), the game's video_mode set, the
    palette file's tables random."""
    lib_mode = GAME_MODE[h.machine] if lib_mode is None else lib_mode
    m = mode_state(h, rng, lib_mode)
    randomize(m, DS_BASE + PALETTE_TABLES[0], PALETTE_TABLES[1], rng)
    put16(m, 0xEED2, GAME_MODE[h.machine] if game_mode is None else game_mode)
    return m


def random_planes(h, rng):
    """The card state (after the mode set) with random EGA planes and latches."""
    s = bytearray(h.card_state)
    s[cardmodel.PLANES:cardmodel.PLANES + 0x40000] = rng.randbytes(0x40000)
    s[cardmodel.LATCH:cardmodel.LATCH + 4] = rng.randbytes(4)
    h.card_state = bytes(s)


# ---------------------------------------------------------------- the library's palette routines

def ega_pal_set(h, rng, scale):
    n = 0
    for mode in LIB_MODES[h.machine]:
        for k in range(20 * scale):
            m = mode_state(h, rng, mode)
            randomize(m, DS_BASE + 0xDDC1, 0x40, rng)
            index = rng.choice([k & 0x1F, rng.randrange(0x20), rng.randrange(0x10000)])
            value = rng.choice([0x200, 0x255, 0x2AA, 0x2FF, rng.randrange(0x10000), rng.randrange(0x100)])
            h.check('ega_pal_set', m, stack_args=[index, value], outputs=['ax'],
                    label='mode %X index %X value %04X' % (mode, index, value))
            n += 1
    return n


def ega_pal_set_modes(h, rng, scale):
    """Every entry of the dispatch table (the library's mode word set directly)."""
    n = 0
    for x2 in range(0, 0x28, 2):
        for _ in range(4 * scale):
            m = h.fresh_memory()
            randomize(m, DS_BASE + 0xDDC1, 0x40, rng)
            put16(m, 0xDCF8, x2)
            index, value = rng.randrange(0x10000), rng.randrange(0x10000)
            h.check('ega_pal_set', m, stack_args=[index, value], outputs=['ax'], label='mode %X' % (x2 >> 1))
            n += 1
    return n


def ega_pal_register(h, rng, scale):
    n = 0
    for mode in LIB_MODES[h.machine]:
        for k in range(30 * scale):
            m = mode_state(h, rng, mode)
            if k % 3 == 2:  # the colour sets and the levels as any memory
                randomize(m, DS_BASE + 0xE0D1, 0x16, rng)
            m[0x465] = rng.randrange(256)
            reg = rng.choice([k % 6, rng.randrange(0x10), 8, rng.randrange(0x10000)])
            value = rng.choice([0, rng.randrange(0x40), rng.randrange(0x10000)])
            h.check('ega_pal_register', m, stack_args=[reg, value], outputs=['ax'],
                    label='mode %X reg %X value %04X' % (mode, reg, value))
            n += 1
    return n


def gfx_set_ega_palette(h, rng, scale):
    n = 0
    for mode in LIB_MODES[h.machine]:
        for k in range(20 * scale):
            m = mode_state(h, rng, mode)
            table = rng.choice([SCRATCH, 0x0924])
            randomize(m, DS_BASE + table, 0x20, rng)
            if k % 2:
                randomize(m, DS_BASE + 0xE0E3, 4, rng)
            h.check('gfx_set_ega_palette', m, stack_args=[table], outputs=['ax'], label='mode %X case %d' % (mode, k))
            n += 1
    return n


def gfx_set_pal_reg(h, rng, scale):
    """Dead code, called as its jump table expects: 1584:0008 (157d:0078)."""
    h.add_function('gfx_set_pal_reg', seg_of(0x1584), 0x0008, True)
    n = 0
    modes = {'ega': [0x0D, 0x0E, 0x0F, 0x10], 'tandy': [8, 9, 0x0A], 'cga': [4, 6], 'hercules': [4, 0x0C],
             'vga': [0x13, 3]}[h.machine]
    for mode in modes:
        for k in range(24 * scale):
            m = mode_state(h, rng, mode)
            index = rng.choice([rng.randrange(0x10), -rng.randrange(1, 0x10), rng.randrange(0x10000)]) & 0xFFFF
            comps = [rng.choice([rng.randrange(4), rng.randrange(0x40), rng.randrange(0x10000)]) for _ in range(3)]
            h.check('gfx_set_pal_reg', m, stack_args=[index] + comps, outputs=['ax'],
                    label='mode %X index %04X rgb %s' % (mode, index, comps))
            n += 1
    return n


def display_offset(h, rng, scale):
    n = 0
    modes = {'ega': [0x0D, 0x0D, 0x0E, 0x10], 'cga': [4, 5, 6], 'tandy': [9, 9, 8, 0x0A],
             'hercules': [4, 4, 0x0B, 0x0C]}[h.machine]
    for mode in modes:
        for k in range(30 * scale):
            m = mode_state(h, rng, mode)
            x = rng.choice([0, 1, 2, 3, 7, 8, rng.randrange(0x140), rng.randrange(0x10000)])
            y = rng.choice([0, 1, 2, 3, 4, 5, rng.randrange(0xC8), rng.randrange(0x10000)])
            if k % 4 == 3:  # the visible page and the mode's sizes as other states leave them
                put8(m, 0xDCFD, rng.randrange(256))
                put16(m, 0xDF45, rng.randrange(0x10000))
                put16(m, 0xDF1B, rng.randrange(0x10000))
            h.check('gfx_set_display_offset', m, stack_args=[x, y], outputs=['ax'],
                    label='mode %X x %X y %X' % (mode, x, y))
            n += 1
    return n


# ---------------------------------------------------------------- the game's palette code

def game_palette(h, rng, scale):
    """ega_pal_entry, ega_pal_apply and ega_pal_init in the machine's game mode (and in modes where
    they do nothing), twice in a row for ega_pal_init (the Tandy conversion runs on its own output)."""
    n = 0
    for k in range(24 * scale):
        game_mode = GAME_MODE[h.machine] if k % 6 else rng.choice([0x13, 0x0C, 9, 0x0D, 4, rng.randrange(0x10000)])
        m = game_state(h, rng, game_mode=game_mode)
        label = 'game mode %X case %d' % (game_mode, k)
        h.check('ega_pal_entry', m, stack_args=[rng.randrange(0x40), rng.randrange(0x10000)], label=label)
        h.check('ega_pal_apply', m, label=label)
        h.check('ega_pal_init', m, label=label)
        m = after_original(h, m, 'ega_pal_init')
        h.check('ega_pal_init', m, label=label + ' (again)')
        n += 4
    return n


def palette_flash(h, rng, scale):
    n = 0
    for k in range(60 * scale):
        game_mode = GAME_MODE[h.machine] if k % 5 else rng.choice([0x13, 0x0D, 9, 4, 0x0C, 0x0E])
        m = game_state(h, rng, game_mode=game_mode)
        put16(m, 0x0086, rng.choice([1, 2, 3, 4, 5, 9, rng.randrange(0x10000)]))
        put8(m, 0xD9B5, rng.choice([0, 1, 2, 3, 4, 7, rng.randrange(256)]))
        put8(m, 0xD6E5, rng.choice([0, 1, 2, 3, rng.randrange(256)]))
        randomize(m, DS_BASE + 0xD6E6, 4 + 9, rng)
        if k % 3 == 0:  # the colours of the palette file (EGA rgbRGB)
            for i in range(4):
                m[DS_BASE + 0xD6E6 + i] = rng.randrange(0x40)
        if game_mode != 0x13:
            h.check('palette_flash', m, label='game mode %X case %d' % (game_mode, k))
            n += 1
    return n


def screen_shake(h, rng, scale):
    n = 0
    for k in range(40 * scale):
        m = game_state(h, rng)
        put8(m, 0xB7F2, rng.choice([0, 1, 2, 3, 8, rng.randrange(256)]))
        h.check('screen_shake_step', m, label='case %d' % k)
        n += 1
    return n


# ---------------------------------------------------------------- text and the dissolve

def text_ega(h, rng, scale):
    """text_draw_char's EGA path (121b:049c): the character into page 0, 1 or 2 of the card's memory."""
    n = 0
    for k in range(60 * scale):
        m = game_state(h, rng)
        random_planes(h, rng)
        put16(m, 0xD9B6, 0xA000)
        put16(m, 0xD9B8, 0xA200)
        put16(m, 0xD9BA, 0xA400)
        put16(m, 0x007A, rng.choice([0, 1, 2]))
        put8(m, 0xD9BC, rng.choice([0, 0, 1, rng.randrange(256)]))  # text_transparent
        if k % 4 == 3:   # any words, the low byte an index of the graphics controller
            put16(m, 0xD9BD, rng.randrange(0x10000))
            put16(m, 0xD9BF, rng.randrange(0x10000))
        else:            # as text_set_colours stores them
            put16(m, 0xD9BD, rng.randrange(0x10) << 8)
            put16(m, 0xD9BF, rng.randrange(0x10) << 8)
        put8(m, 0xD9C1, rng.choice([0, 39, rng.randrange(40), rng.randrange(256)]))
        put8(m, 0xD9C2, rng.choice([0, 0xC0, rng.randrange(0xC1), rng.randrange(256)]))
        ch = rng.choice([rng.randrange(0x20, 0x80), rng.randrange(256)])
        put8(m, SCRATCH, ch)
        h.check('text_draw_char', m, stack_args=[SCRATCH], label='char %02X case %d' % (ch, k))
        n += 1
    return n


def dissolve_state(h, rng, k):
    """Pages 0 and 1 of the machine's game mode with random pixels (page 1 allocated by the
    original on the RAM-page cards, the card's memory on the EGA)."""
    m = game_state(h, rng)
    if h.machine == 'ega':
        random_planes(h, rng)
        page0, page1 = 0xA000, 0xA200
    else:
        m = after_original(h, m, 'gfx_alloc_page', [1])
        page0, page1 = 0xB800, get16(m, 0xDD33)
        size = 0x8000 if h.machine == 'tandy' else 0x4000
        randomize(m, page0 << 4, size, rng)
        randomize(m, page1 << 4, size, rng)
    put16(m, 0xD9B6, page0)
    put16(m, 0xD9B8, page1)
    if k % 3 == 2:  # other steps and rows (rows kept below 28h * 100h: the Tandy's DIV)
        randomize(m, DS_BASE + 0xD9CF, 0x40, rng)
        randomize(m, DS_BASE + 0xDA0F, 0x0E, rng)
        for i in range(8):
            put16(m, 0xDA1D + 2 * i, rng.randrange(0x2800))
    return m


def dissolve(h, rng, scale):
    n = 0
    poll = {'cga': '121b:060f', 'hercules': '121b:060f', 'tandy': '121b:0799', 'ega': None}[h.machine]
    for k in range((2 if h.machine == 'ega' else 5) * scale):
        m = dissolve_state(h, rng, k)
        if h.machine in ('cga', 'hercules') and k % 2:
            put16(m, 0xEED2, 0x0C)  # the CGA path's other mode
        h.check('dissolve_page1_to_0', m, tick=([poll], 'tick_counter') if poll else None,
                label='game mode %X case %d' % (get16(m, 0xEED2), k))
        n += 1
    return n


# ---------------------------------------------------------------- the test list

ALL = ('ega', 'cga', 'tandy', 'hercules')
TESTS = (on_machines(*ALL, 'vga')(ega_pal_set) + on_machines('vga')(ega_pal_set_modes)
         + on_machines(*ALL, 'vga')(ega_pal_register)
         + on_machines('ega', 'tandy', 'cga', 'vga')(gfx_set_ega_palette)
         + on_machines(*ALL, 'vga')(gfx_set_pal_reg)
         + on_machines(*ALL)(display_offset)
         + on_machines(*ALL, 'vga')(game_palette)
         + on_machines(*ALL)(palette_flash)
         + on_machines(*ALL)(screen_shake)
         + on_machines('ega')(text_ega)
         + on_machines(*ALL)(dissolve))
