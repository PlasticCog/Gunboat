"""The palette routines of the graphics library and the game side of the video modes other than VGA
(video.md §1-§4), each on the machines it has a path for: ega_pal_set, ega_pal_register,
gfx_set_ega_palette, gfx_set_pal_reg, gfx_set_display_offset, ega_pal_entry / ega_pal_apply /
ega_pal_init, palette_flash, screen_shake_step, text_draw_char (the EGA planar path) and
dissolve_page1_to_0. The library is put in its mode by the original's gfx_set_mode (test_modes);
all memory and the whole card state are compared, and the DAC on the VGA machine.

The game routines further down (config_load, title_menu, the front end, the HQ quiz, the full-screen
stations) run on states the original reaches on each machine, with the original's calls of the
graphics library running the port's library (PortLibrary): the game code is compared whatever state
the library's paths of the other modes are in. Effects that only the drawing shows (the colour
patterns set before a picture_draw) are compared only as far as the port's library draws them."""
import contextlib
import ctypes
import os
import pathlib
import shutil
import struct
import tempfile

import cardmodel
from gbdiff import DS_BASE, GAME_DIR, MEM_SIZE, Mismatch, put8, put16, randomize, seg_of
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


# ---------------------------------------------------------------- game routines on the port's library

# The graphics library (segments 137e-15ea). In the tests below the original's calls of these run
# the port's functions (PortLibrary), so the game's own code is compared on every machine whatever
# state the library's ports of the other modes are in (the library is tested on its own).
LIBRARY = ['gfx_alloc_page', 'gfx_detect', 'picture_draw', 'gfx_line_to', 'gfx_draw_bitmap', 'gfx_free_page',
           'gfx_get_draw_seg', 'gfx_read_bitmap', 'gfx_saved_mode', 'gfx_move_to', 'ega_pal_register',
           'gfx_set_ega_palette', 'gfx_set_display_offset', 'ega_pal_set', 'gfx_put_pixel', 'gfx_fill_rect',
           'text_exit_clear', 'gfx_copy_rect_from_copy_page', 'gfx_copy_rect_to_copy_page', 'gfx_set_colour',
           'gfx_set_copy_page', 'gfx_set_mode', 'gfx_set_draw_page', 'gfx_set_visible_page', 'gfx_copy_rect',
           'gfx_fill_rect_clipped', 'gfx_clear_page']

BLOCK = 0x1000


class PortLibrary:
    """While active, each call of the original to a LIBRARY routine runs the port's routine instead,
    on the original's memory and card state (copied to the port and back), and returns its AX. The
    port's own card state is kept for its run of the check."""

    def __init__(self, h):
        self.h, self.hooks = h, []
        self.calls = 0

    def __enter__(self):
        for name in LIBRARY:
            file_seg, off, _ = self.h.sym.func(name)
            self.hooks.append(self.h.orig.stub(file_seg, off, self._call(name)))
        return self

    def __exit__(self, *exc):
        for hook in self.hooks:
            self.h.orig.uc.hook_del(hook)
        self.hooks = []

    def _call(self, name):
        def run(arg):
            h = self.h
            self.calls += 1
            args = [arg(i) for i in range(8)]
            before = h.orig.memory()
            h.port.set_memory(before)
            saved = ctypes.create_string_buffer(cardmodel.SIZE)
            h.port.dll.gb_card_get(saved)
            h.port.dll.gb_card_set(bytes(h.card.s))
            try:
                regs = h.port.call(name, {}, args)
            except Mismatch as e:
                h.port.dll.gb_card_set(saved.raw)
                h.orig.fail('%s (the port library): %s' % (name, e))
                return None
            card = ctypes.create_string_buffer(cardmodel.SIZE)
            h.port.dll.gb_card_get(card)
            h.card.s[:] = card.raw
            h.port.dll.gb_card_set(saved.raw)
            after = h.port.memory()
            if after != before:
                for at in range(0, MEM_SIZE, BLOCK):
                    if before[at:at + BLOCK] != after[at:at + BLOCK] and not (0xA0000 <= at < 0xB0000 and h.machine == 'ega'):
                        h.orig.uc.mem_write(at, after[at:at + BLOCK])
            return regs['ax'], None
        return run


CFG_MODE = {'ega': 0x0D, 'cga': 4, 'tandy': 9, 'hercules': 0x0C}
# Poll points: the VGA fades and dissolve, the CGA and Tandy dissolves, the title's sprite loop,
# bios_wait_ticks.
POLLS = ['121b:07eb', '121b:0841', '121b:0670', '121b:060f', '121b:0799', '00f2:03d0', '15d4:0019']
BIG = 2_000_000_000


@contextlib.contextmanager
def machine_game(h, single_page=0):
    """The game folder copied, its GUNBOAT.CFG choosing the machine's mode (Hercules: 0Ch), no
    joystick; both sides use the copy. The real game folder is never written."""
    tmp = pathlib.Path(tempfile.mkdtemp(prefix='gbdiff_modes_'))
    try:
        for f in GAME_DIR.iterdir():
            if f.is_file():
                shutil.copy2(f, tmp / f.name)
        (tmp / 'GUNBOAT.CFG').write_bytes(struct.pack('<HHH', CFG_MODE[h.machine], 0, single_page))
        h.set_game_dirs(tmp)
        yield tmp
    finally:
        h.set_game_dirs(GAME_DIR)
        shutil.rmtree(tmp, ignore_errors=True)


# GB_MODES_HYBRID=0: the game routines below run the original on its own library instead (the
# whole game code and library compared; for when the library's paths of every mode are ported).
HYBRID = os.environ.get('GB_MODES_HYBRID', '1') != '0'


def check_hybrid(h, name, m, keys=None, polls=(), **kw):
    """h.check with the original on the port's library (unless HYBRID is off), the sound model on
    and the poll points (POLLS and `polls`)."""
    from test_sound import sound
    kw.setdefault('max_insns', BIG)
    with sound(h), (PortLibrary(h) if HYBRID else contextlib.nullcontext()):
        return h.check(name, m, tick=(POLLS + list(polls), 'both', keys or []), **kw)


# A port routine of another package still a stub on this branch (the renderer's and the view copies'
# twins of a mode) ends the port's run with one of these; the checks that reach one are counted as
# skipped (and printed), not failed.
UNPORTED = ('is not ported yet', 'is not ported (EGA/Tandy/CGA parked)')


def check_or_skip(h, name, m, skipped, keys=None, polls=(), regs=None, **kw):
    """check_hybrid; 1, or 0 when the port reaches an unported routine of another package (its
    message in `skipped`). The port runs alone first (quick), so that the original's long run is
    only made for a check that can complete."""
    h.port.dll.gb_set_machine(cardmodel.MACHINES[h.machine])
    h.port.dll.gb_card_set(bytes(h.card_state) if h.card_state is not None else bytes(cardmodel.reset_state()))
    h.port.set_memory(m)
    h.set_tick((POLLS + list(polls), 'both', keys or []))
    try:
        h.port.call(name, regs or {})
    except Mismatch as e:
        text = str(e)
        if any(u in text for u in UNPORTED):
            skipped.append('%s: %s' % (name, text[6:]))
            return 0
    finally:
        h.set_tick(None)
        h.reset_files()
    check_hybrid(h, name, m, keys=keys, polls=polls, regs=regs, **kw)
    return 1


def report_skipped(h, test, skipped):
    if skipped:
        print('    %s on %s: %d check(s) skipped, the port reached routines not on this branch: %s'
              % (test, h.machine, len(skipped), '; '.join(sorted(set(skipped)))[:600]))


def config_load_cases(h, rng, scale):
    """config_load from GUNBOAT.CFG in the machine's mode (Hercules: the CGA mode and hercules_setup;
    CGA: the palette register), one or two pages."""
    n = 0
    for single in (0, 1):
        with machine_game(h, single):
            h.card_state = None  # the card as the machine starts
            check_hybrid(h, 'config_load', h.fresh_memory(), label='single page %d' % single)
            n += 1
    return n


def captured(h, key, build):
    """A state the original builds (build() from the reset card), cached per harness with the card
    state it leaves; h.card_state becomes that card state (the checks start from it)."""
    cache = h.__dict__.setdefault('_modes_states', {})
    if key not in cache:
        h.card.s[:] = cardmodel.reset_state()
        m = build()
        cache[key] = (bytes(m), bytes(h.card.s))
    m, card = cache[key]
    h.card_state = card
    return bytearray(m)


def main_state_modes(h):
    """test_title.main_state in the machine's mode (the original's config_load, kbd_install,
    mem_alloc_all), run by the original on its own library (inside machine_game)."""
    import test_title
    return captured(h, 'main', lambda: test_title.main_state(h))


def title_cases(h, rng, scale):
    """title_menu: the menu choices and the demo key; the whole intro on its first run."""
    import test_title
    n = 0
    with machine_game(h):
        for keys, first in (([0x0D], 0), ([0x96, 0, 0, 0x0D], 0), ([0x98, 0x96, 0x92, 0x0D], 0), ([0x44], 0),
                            ([0] * 40 + [0x20] + [0] * 400 + [0x0D], 1)):
            m = main_state_modes(h)
            put8(m, 0xF398, first)
            check_hybrid(h, 'title_menu', m, test_title.spaced(keys) if not first else keys, outputs=['ax'],
                         label='first run %d keys %s' % (first, bytes(keys).hex()))
            n += 1
    return n


def menu_cursor_cases(h, rng, scale):
    with machine_game(h):
        check_hybrid(h, 'menu_cursor_init', main_state_modes(h))
    return 1


def front_state_modes(h):
    """test_front.front_state in the machine's mode: the front end's set-up run by the original on
    its own library until it calls name_entry."""
    from test_sound import sound

    def build():
        m = main_state_modes(h)
        h.card.s[:] = h.card_state
        put16(m, 0x0082, 2)
        with sound(h):
            h.set_tick((POLLS, 'both', []))
            hook = h.orig.stub(0x02D2, 0x0BD8, lambda args: h.orig.fail('stop'))
            try:
                h.orig.set_memory(m)
                file_seg, off, far = h.sym.func('front_end')
                h.orig.call(file_seg, off, far, {}, (), max_insns=BIG)
                raise Mismatch('front_end returned before name_entry')
            except Mismatch as e:
                if str(e) != 'original: stop':
                    raise
            finally:
                h.orig.uc.hook_del(hook)
                h.set_tick(None)
        return h.orig.memory()
    return captured(h, 'front', build)


def region_state_modes(h, region, rank=5):
    """test_front.region_state on front_state_modes (its file loads run by the original)."""
    import test_front

    def build():
        saved = getattr(h, '_front_state', None)
        h._front_state = front_state_modes(h)
        h.card.s[:] = h.card_state
        try:
            return test_front.region_state(h, region, rank)
        finally:
            h._front_state = saved
    return captured(h, ('region', region, rank), build)


def front_cases(h, rng, scale):
    """The front end's screens with their colour patterns per mode: the office, the folders, the
    spec sheets, the maps, the outfitting; a whole front end (a new commander) and the quiz."""
    import test_front
    n = 0
    with machine_game(h):
        base = front_state_modes(h)
        for c1, c2 in ((ord('S'), ord('1')), (ord('D'), ord('6'))):
            check_hybrid(h, 'folder_draw', base, stack_args=[c1, c2], label='%02X %02X' % (c1, c2))
            n += 1
        check_hybrid(h, 'office_restore', base)
        check_hybrid(h, 'office_draw', base)
        n += 2
        for page in range(17):
            m = bytearray(base)
            put8(m, 0xB508, page)
            check_hybrid(h, 'spec_sheet_draw', m, stack_args=[5 if page % 2 else 9], label='page %d' % page)
            n += 1
        for region in range(3):
            base_r = region_state_modes(h, region)
            for mission, drawn in ((0, 0), (3, 0), (3, 1)):
                m = bytearray(base_r)
                put16(m, test_front.MISSION, mission)
                put16(m, 0xEA84, drawn)
                check_hybrid(h, 'map_draw', m, stack_args=[5], label='region %d mission %d drawn %d' % (region, mission, drawn))
                n += 1
        for item in range(4):
            m = bytearray(base)
            put8(m, 0xB4DA, item)
            put8(m, 0xB7FC, item & 1)
            check_hybrid(h, 'outfitting_draw', m, outputs=['ax'], label='item %d' % item)
            n += 1
        label, records, keys, code = test_front.front_flows()[0]
        m = main_state_modes(h)
        put16(m, 0x0082, 2)
        check_hybrid(h, 'front_end', m, test_front.spaced(keys, gap=test_front.FLOW_GAP), label=label, exit_code=code)
        m = main_state_modes(h)
        m = after_original(h, m, 'file_load_near', [0x0722, 0x6E54])  # DAT6.DAT, as test_front.hq_state
        put16(m, 0x0082, 1)
        h.set_pit2(0x5A)
        check_hybrid(h, 'hq_quiz', m, test_front.spaced([0x38, 0x0D], gap=4), outputs=['ax'], label='quiz')
        n += 2
    return n


def hud_cases(h, rng, scale):
    """The full-screen stations (the map, the damage report, the assignment) and the station
    helpers, in the machine's mode, from a mission the original runs there."""
    import test_hud
    n = 0
    with machine_game(h):
        for name, key, mission in (('map_screen', 'm', 3), ('damage_report_screen', '/', 3), ('assignment_screen', '.', 1)):
            m = captured(h, name, lambda: test_hud.screen_state(h, name, key, region=0, mission=mission, rank=5))
            check_hybrid(h, name, m, label=name)
            n += 1
        base = captured(h, 'pilot', lambda: test_hud.pilot(h))
        check_hybrid(h, 'station_screen_colours', base)
        n += 1
        for station in (1, 2, 5, 7, 8):
            m = bytearray(base)
            put16(m, 0x0086, station)
            check_hybrid(h, 'screen_clear', m, label='station %d' % station)
            n += 1
        # the pilot's instruments and the message line (its text in the mode's way)
        for i in range(6 * scale):
            m = bytearray(base)
            put8(m, 0xB818, rng.randrange(256))
            put8(m, 0xD6B9, rng.randrange(256))
            put16(m, 0xF346, rng.choice([0, 1, 2]))
            check_hybrid(h, 'jet_marker', m, regs={'ax': rng.randrange(0x10000)}, outputs=['ax'], label='case %d' % i)
            put8(m, 0xD649, 0xFF)  # the clock's minutes changed: the clock is drawn
            check_hybrid(h, 'message_line_draw', m, label='case %d' % i)
            n += 2
        right = captured(h, 'pilot_right', lambda: test_hud.pilot_right(h))
        for i in range(6 * scale):
            m = bytearray(right)
            if i:
                put8(m, 0xD6B8, rng.randrange(0x80))
                put8(m, 0xD50D, rng.randrange(256))
            check_hybrid(h, 'radar_scope', m, regs={'ax': rng.randrange(0x10000)}, outputs=['ax'], label='case %d' % i)
            n += 1
    return n


def mission_load_cases(h, rng, scale):
    """mission_load at its entry (the art, the palette file with ega_pal_init, LIGHTS.LZ on page 1
    with its patterns), by day and at night, in the machine's mode."""
    import test_hud
    n = 0
    with machine_game(h):
        for kw in (dict(region=0, mission=3, rank=5), dict(region=1, mission=2, rank=5, night=True), dict(practice=1)):
            m = captured(h, ('mission_load',) + tuple(sorted(kw.items())),
                         lambda: test_hud.station_state(h, 'mission_load', **kw))
            check_hybrid(h, 'mission_load', m, label=str(kw))
            n += 1
    return n


def station_snapshots(h, label, kw, keys):
    """(name, call, registers, memory, card) at the entries of the station screens as the original's
    mission_run reaches them on its own library (test_frame.loop_capture, with the card state)."""
    import test_frame
    from unicorn import UC_HOOK_CODE
    from gbdiff import REGS, UC_REGS, lin
    cache = h.__dict__.setdefault('_modes_snapshots', {})
    if label not in cache:
        names = ['pilot_screen', 'bow_screen', 'stern_screen', 'midship_screen', 'chase_view_screen', 'view_restore']
        out, counts, hooks = [], {}, []

        def make(name):
            def hook(uc, address, size, _):
                k = counts.get(name, 0)
                counts[name] = k + 1
                if k < 2:
                    r = {n: uc.reg_read(UC_REGS[n]) for n in REGS}
                    out.append((name, k, r, bytes(h.orig.memory()), bytes(h.card.s)))
            return hook
        h.card.s[:] = cardmodel.reset_state()
        m = test_frame.main_state(h, ctrl=True, **kw)
        for name in names:
            fs, off, _ = h.sym.func(name)
            at = lin(seg_of(fs), off)
            hooks.append(h.orig.uc.hook_add(UC_HOOK_CODE, make(name), None, at, at))
        from test_sound import sound
        try:
            with sound(h):
                h.set_tick((test_frame.loop_polls(), 'both', keys))
                h.orig.set_memory(m)
                fs, off, far = h.sym.func('mission_run')
                h.orig.call(fs, off, far, {}, (), max_insns=4_000_000_000)
        except Mismatch as e:
            if str(e) not in ('original: stop', 'original: exit(0)'):
                raise
        finally:
            for k in hooks:
                h.orig.uc.hook_del(k)
            h.set_tick(None)
        cache[label] = out
    return cache[label]


def station_cases(h, rng, scale):
    """The 3D stations' screens (pilot, bow, stern, midship, chase view, view_restore) as a tour of
    the stations reaches them in a mission on the machine, with their register flows (SI)."""
    import test_frame
    n, skipped = 0, []
    with machine_game(h):
        for label, kw in (('Vietnam 3', dict(region=0, mission=3, rank=5, weapons=(1, 0, 1))),
                          ('Colombia 2 (night)', dict(region=1, mission=2, rank=9, weapons=(0, 1, 2)))):
            snaps = station_snapshots(h, label, kw, test_frame.spaced(test_frame.TOUR + [ord('q')]))
            for name, k, regs, mem, card in snaps:
                h.card_state = card
                n += check_or_skip(h, name, bytearray(mem), skipped, regs=regs, outputs=['si'],
                                   polls=test_frame.loop_polls(), label='%s, %s call %d' % (label, name, k))
    report_skipped(h, 'station_cases', skipped)
    return n


def mission_cases(h, rng, scale):
    """Whole missions on the machine: the title's demo (ended by a key) and a tour of the stations
    ended by Ctrl+Q (quit_to_dos), from main's state."""
    import test_frame
    n, skipped = 0, []
    with machine_game(h):
        h.card.s[:] = cardmodel.reset_state()
        m = test_frame.main_state(h, demo=True, practice=1)
        h.card_state = bytes(h.card.s)
        n += check_or_skip(h, 'mission_run', m, skipped, keys=[0] * 400 + [0x20], polls=test_frame.loop_polls(),
                           max_insns=8_000_000_000, label='demo')
        h.card.s[:] = cardmodel.reset_state()
        m = test_frame.main_state(h, ctrl=True, region=0, mission=3, rank=5, weapons=(1, 0, 1))
        h.card_state = bytes(h.card.s)
        n += check_or_skip(h, 'mission_run', m, skipped, keys=test_frame.spaced(test_frame.TOUR + [ord('q')]),
                           polls=test_frame.loop_polls(), max_insns=8_000_000_000, label='Vietnam 3 tour', exit_code=0)
    report_skipped(h, 'mission_cases', skipped)
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
         + on_machines(*ALL)(dissolve)
         + on_machines(*ALL)(config_load_cases)
         + on_machines(*ALL)(menu_cursor_cases)
         + on_machines(*ALL)(title_cases)
         + on_machines(*ALL)(front_cases)
         + on_machines(*ALL)(hud_cases)
         + on_machines(*ALL)(mission_load_cases)
         + on_machines(*ALL)(station_cases)
         + on_machines(*ALL)(mission_cases))
