"""The graphics library's primitives in the EGA, CGA, Tandy and Hercules modes (video.md §7), each on
its machine (test_modes.py: a harness per video card). Randomized coordinates, colours, pages and
card state; every check compares all memory and the whole card state (the EGA planes, latches and
registers). Edge cases: byte edges of the masks, one-byte and reversed-by-one spans, odd rows of the
interleaved memory, colours above the mode's bits, random graphics controller registers on the EGA."""
import ctypes
import struct

import cardmodel
from gbdiff import DS_BASE, put8, put16, randomize
from test_modes import after_original, machine, mode_state

SCRATCH = 0xF000        # DS offset for bitmaps (BSS, below the stack)
PIC = 0x1094            # picture runs
DITHER = 0xDDC1         # gfx_dither
PAGE_SEG = 0xDD31       # gfx_page_seg[8]

LIB_MODES = {'ega': [0x0D], 'cga': [4, 5], 'tandy': [9], 'hercules': [4]}
SCREEN_BYTES = {4: 0x4000, 5: 0x4000, 9: 0x8000}


def u16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def lib_state(h, rng, mode=None):
    """The library in the machine's mode (set up by the original: gfx_set_mode, hercules_setup on the
    Hercules machine, pages 1 and 2 allocated outside the EGA, gfx_set_colour), random DGROUP
    variables, random screen and page contents (on the EGA random planes and latches)."""
    mode = rng.choice(LIB_MODES[h.machine]) if mode is None else mode
    m = mode_state(h, rng, mode)
    if h.machine == 'hercules':
        m = after_original(h, m, 'hercules_setup')
    if mode in SCREEN_BYTES:
        m = after_original(h, m, 'gfx_alloc_page', [1])
        m = after_original(h, m, 'gfx_alloc_page', [2])
        randomize(m, 0xB8000, SCREEN_BYTES[mode], rng)
        for p in (1, 2):
            randomize(m, u16(m, PAGE_SEG + 2 * p) << 4, SCREEN_BYTES[mode], rng)
    m = after_original(h, m, 'gfx_set_colour', [rng.randrange(0x20)])
    if h.machine == 'ega':
        s = bytearray(h.card_state)
        s[0:0x40000] = rng.randbytes(0x40000)
        s[cardmodel.LATCH:cardmodel.LATCH + 4] = rng.randbytes(4)
        h.card_state = bytes(s)
    return m


def shake_card(h, rng):
    """Sometimes random graphics controller and sequencer registers on the EGA (the routines then
    draw through whatever the card is set to)."""
    if h.machine != 'ega' or rng.random() >= 0.3:
        return
    s = bytearray(h.card_state)
    for i in rng.sample(range(9), rng.randrange(1, 5)):
        s[cardmodel.GC + i] = rng.randrange(256)
    if rng.random() < 0.5:
        s[cardmodel.SEQ + 2] = rng.randrange(16)
    s[cardmodel.GC_INDEX] = rng.randrange(16)
    h.card_state = bytes(s)


def pages(m, h):
    """The segments the draw and copy pages may have."""
    if h.machine == 'ega':
        return [0xA000, 0xA200, 0xA400]
    return [0xB800, u16(m, PAGE_SEG + 2), u16(m, PAGE_SEG + 4)]


def pick_x(rng, w):
    """An x, often at a byte edge of the packed / planar memory."""
    if rng.random() < 0.5:
        return min(w - 1, rng.randrange(w // 8) * 8 + rng.choice([0, 1, 2, 3, 4, 5, 6, 7]))
    return rng.randrange(w)


def pick_span(rng, w):
    x0 = pick_x(rng, w)
    r = rng.random()
    if r < 0.1:
        x1 = max(0, x0 - 1)                                # reversed by one
    elif r < 0.3:
        x1 = min(w - 1, (x0 | 7) - rng.randrange(0, 8))    # within the byte
        x1 = max(x1, x0)
    elif r < 0.5:
        x1 = min(w - 1, x0 + rng.randrange(1, 24))
    else:
        x1 = rng.randrange(x0, w)
    return x0, x1


def pick_rows(rng, hgt=200):
    y0 = rng.randrange(hgt)
    y1 = rng.choice([y0, min(hgt - 1, y0 + 1), min(hgt - 1, y0 + rng.randrange(2, 9)), rng.randrange(y0, hgt)])
    return y0, y1


def colour_byte(rng):
    return rng.choice([rng.randrange(4), rng.randrange(16), rng.randrange(0x20), rng.randrange(256)])


class StatusCheck:
    """The retrace toggle of the card's status ports as well (CardCheck leaves it out): the EGA page
    flip and the Hercules probe must read their status port as many times as the original."""

    def before(self, h):
        pass

    def after(self, h):
        port = ctypes.create_string_buffer(cardmodel.SIZE)
        h.port.dll.gb_card_get(port)
        a, b = h.card.s[cardmodel.STATUS], port.raw[cardmodel.STATUS]
        return [] if a == b else ['card status toggle: original %d, port %d' % (a, b)]


def on(name, test):
    """The test on the machine its name ends with, the status toggle compared too."""
    def run(h, rng, scale):
        check = StatusCheck()
        h.extensions.append(check)
        try:
            return test(h, rng, scale)
        finally:
            h.extensions.remove(check)
    run.__name__ = name
    return machine(name.rsplit('_', 1)[1])(run)


# ---- gfx_set_mode, gfx_detect, gfx_saved_mode

def set_mode_cases(h, rng, scale):
    n = 0
    for base in ('fresh', 'lib'):
        for mode in [-1, 0x14] + list(range(0x14)):
            if base == 'fresh':
                h.card_state = None
                m = h.fresh_memory()
            else:
                m = lib_state(h, rng)
            h.check('gfx_set_mode', m, stack_args=[mode & 0xFFFF], outputs=['ax'], label='%s mode %X' % (base, mode))
            n += 1
    return n


def detect_cases(h, rng, scale):
    """gfx_detect as the machine's BIOS answers it, with the BIOS bytes the probes read varied (EGA:
    the information bytes 0040:0087/0088; all: the equipment word and the Tandy's ROM byte), and
    gfx_saved_mode."""
    n = 0
    variants = [{}]
    if h.machine == 'ega':
        for info in (0x60, 0x62, 0x68, 0x00, rng.randrange(256)):
            for switches in (0x08, 0x09, 0x06, 0xF9, rng.randrange(256)):
                variants.append({0x487: info, 0x488: switches})
    for equipment in (0x20, 0x30, 0x10):
        for rom in (0x21, 0x00, 0xFF):
            variants.append({0x410: equipment, 0xFC000: rom})
    for v in variants:
        h.card_state = None
        if rng.random() < 0.5:                                  # the status toggle's phase
            s = cardmodel.reset_state()
            s[cardmodel.STATUS] = 1
            h.card_state = bytes(s)
        m = h.fresh_memory()
        for a, b in v.items():
            m[a] = b
        h.check('gfx_detect', m, outputs=['ax'], label=str(v))
        n += 1
    for dcfc in (0xFF, 0x80, 0x00, 0x04, 0x09, 0x0D):
        for bios_mode in (3, 4, 7, 9, 0x0D):
            m = h.fresh_memory()
            put8(m, 0xDCFC, dcfc)
            m[0x449] = bios_mode
            h.check('gfx_saved_mode', m, outputs=['ax'], label='DCFC %02X BIOS %02X' % (dcfc, bios_mode))
            n += 1
    return n


# ---- pages

PAGE_MODES = list(range(0x14))


def page_cases(h, rng, scale):
    """gfx_set_draw_page / gfx_set_copy_page / gfx_set_visible_page in every mode on this machine
    (their handlers differ by mode: page table, pages in the video memory, the EGA's CRTC start, the
    Hercules mode register, INT 10h AH=05h), gfx_alloc_page / gfx_free_page."""
    n = 0
    for k in range(12 * scale):
        for mode in PAGE_MODES:
            h.card_state = None
            m = mode_state(h, rng, mode)
            if mode in (4, 5, 6, 8, 9, 0x0A, 0x13) and rng.random() < 0.7:
                m = after_original(h, m, 'gfx_alloc_page', [1])
                if mode != 0x13:
                    m = after_original(h, m, 'gfx_alloc_page', [2])
            put8(m, 0xDCF4, rng.randrange(8))
            put8(m, 0xDCFD, rng.choice([0, 0, 1, 2, rng.randrange(8)]))
            if h.machine == 'ega':
                s = bytearray(h.card_state)
                s[cardmodel.STATUS] = rng.randrange(2)
                s[cardmodel.ATTR_FLIP] = rng.randrange(2)
                h.card_state = bytes(s)
            for name in ('gfx_set_draw_page', 'gfx_set_copy_page', 'gfx_set_visible_page'):
                page = rng.choice([0, 1, 2, 3, 7, 8, 9, 0xFFFF, rng.randrange(0x10000)])
                h.check(name, m, stack_args=[page], outputs=['ax'], label='mode %X page %X' % (mode, page))
                n += 1
    for mode in (4, 9, 0x0B, 0x0C, 0x0D, 3, 7, 0x13):
        for page in (-1, 0, 1, 2, 7, 8, 0x101, 0x180, 0x7FFF):
            h.card_state = None
            m = mode_state(h, rng, mode)
            h.check('gfx_alloc_page', m, stack_args=[page & 0xFFFF], outputs=['ax'], label='mode %X page %X' % (mode, page))
            n += 1
            if mode in (4, 9, 0x13):
                m = after_original(h, m, 'gfx_alloc_page', [1])
            h.check('gfx_free_page', m, stack_args=[page & 0xFFFF], outputs=['ax'], label='mode %X page %X' % (mode, page))
            n += 1
    return n


# ---- rectangles, pixels, lines, clear

def fill_cases(h, rng, scale):
    """gfx_fill_rect (the masks at byte edges, one-byte spans, odd rows), gfx_fill_rect_clipped,
    gfx_put_pixel (clip box, colours above the mode's bits), gfx_line_to, gfx_clear_page."""
    n = 0
    for k in range(40 * scale):
        m = lib_state(h, rng)
        shake_card(h, rng)
        put16(m, 0xDD2D, rng.choice(pages(m, h)))
        if rng.random() < 0.3:
            put8(m, 0xDCF5, colour_byte(rng))
        for _ in range(3):
            x0, x1 = pick_span(rng, 320)
            y0, y1 = pick_rows(rng)
            h.check('gfx_fill_rect', m, stack_args=[x0, x1, y0, y1], outputs=['ax'], label=str([x0, x1, y0, y1]))
            n += 1
        # (the rectangles and clip boxes stay ordered: a reversed one runs the original's row loop
        # 65536 times, which the game never asks for)
        xs, ys = sorted(rng.randrange(-30, 350) for _ in range(2)), sorted(rng.randrange(-30, 230) for _ in range(2))
        rect = [xs[0] & 0xFFFF, xs[1] & 0xFFFF, ys[0] & 0xFFFF, ys[1] & 0xFFFF]
        h.check('gfx_fill_rect_clipped', m, stack_args=rect, outputs=['ax'], label=str(rect))
        n += 1
        if rng.random() < 0.5:
            xs, ys = sorted(rng.randrange(-20, 340) for _ in range(2)), sorted(rng.randrange(-20, 220) for _ in range(2))
            for off, v in zip((0xDD05, 0xDD03, 0xDD09, 0xDD07), xs + ys):
                put16(m, off, v & 0xFFFF)
        for _ in range(4):
            x, y = rng.randrange(-40, 360), rng.randrange(-40, 240)
            h.check('gfx_put_pixel', m, stack_args=[x & 0xFFFF, y & 0xFFFF], outputs=['ax'], label='(%d, %d)' % (x, y))
            n += 1
        put16(m, 0xDCFF, rng.randrange(-20, 340) & 0xFFFF)
        put16(m, 0xDD01, rng.randrange(-20, 220) & 0xFFFF)
        x, y = rng.randrange(-20, 340), rng.randrange(-20, 220)
        if rng.random() < 0.3:
            x = u16(m, 0xDCFF)
        elif rng.random() < 0.3:
            y = u16(m, 0xDD01)
        h.check('gfx_line_to', m, stack_args=[x & 0xFFFF, y & 0xFFFF], outputs=['ax'], label='to (%d, %d)' % (x, y))
        n += 1
        if k % 4 == 0:
            h.check('gfx_clear_page', m, outputs=['ax'])
            n += 1
    return n


def copy_cases(h, rng, scale):
    """gfx_copy_rect_from_copy_page / gfx_copy_rect_to_copy_page (CGA / Tandy: to or from the screen
    B800h itself; EGA: through the latches) and gfx_copy_rect between pages."""
    n = 0
    for _ in range(40 * scale):
        m = lib_state(h, rng)
        shake_card(h, rng)
        put16(m, 0xDD2B, rng.choice(pages(m, h)))
        put16(m, 0xDD2D, rng.choice(pages(m, h)))
        for _ in range(2):
            x0, x1 = pick_span(rng, 320)
            y0, y1 = pick_rows(rng)
            rect = [x0, x1, y0, y1]
            h.check('gfx_copy_rect_from_copy_page', m, stack_args=rect, outputs=['ax'], label=str(rect))
            h.check('gfx_copy_rect_to_copy_page', m, stack_args=rect, outputs=['ax'], label=str(rect))
            n += 2
        for _ in range(2):
            x0, x1 = pick_span(rng, 320)
            x1 = max(x1, x0)
            y0, y1 = pick_rows(rng)
            dx = pick_x(rng, 320 - (x1 - x0))
            dy = rng.randrange(y1 - y0, 200)
            src, dst = rng.randrange(3), rng.randrange(3)
            if h.machine == 'ega' and rng.random() < 0.2:
                src, dst = rng.randrange(8), rng.randrange(8)
            args = [x0, x1, y0, y1, dx, dy, src, dst]
            h.check('gfx_copy_rect', m, stack_args=args, outputs=['ax'], label=str(args))
            n += 1
    return n


def bitmap_cases(h, rng, scale):
    """gfx_draw_bitmap / gfx_read_bitmap at every pixel position in a byte, 1-4 bytes a row, 1-20
    rows (odd and even first rows)."""
    n = 0
    for _ in range(50 * scale):
        m = lib_state(h, rng)
        shake_card(h, rng)
        put16(m, 0xDD2D, rng.choice(pages(m, h)))
        if rng.random() < 0.3:
            put8(m, 0xDCF5, colour_byte(rng))
        rows, width = rng.randrange(1, 21), rng.randrange(1, 5)
        put16(m, 0xDCFF, rng.randrange(0, 320 - 8 * width - 8))
        put16(m, 0xDD01, rng.randrange(rows - 1, 200))
        randomize(m, DS_BASE + SCRATCH, rows * width, rng)
        label = 'pen (%d, %d) %d x %d' % (u16(m, 0xDCFF), u16(m, 0xDD01), width, rows)
        h.check('gfx_draw_bitmap', m, stack_args=[SCRATCH, width, rows], outputs=['ax'], label=label)
        h.check('gfx_read_bitmap', m, stack_args=[SCRATCH, width, rows], outputs=['ax'], label=label)
        n += 2
    return n


def picture_cases(h, rng, scale):
    """picture_hline (the colour's row patterns gfx_dither, colours whose doubled byte is negative,
    EGA one or two colours a row) and picture_draw (runs wrapping onto the row above)."""
    n = 0
    for k in range(40 * scale):
        m = lib_state(h, rng)
        shake_card(h, rng)
        put16(m, 0xDD2D, rng.choice(pages(m, h)))
        if rng.random() < 0.5:
            randomize(m, DS_BASE + DITHER, 0x40, rng)
            if h.machine == 'ega' and rng.random() < 0.5:   # the EGA's dither entries are colours
                for i in range(0x40):
                    m[DS_BASE + DITHER + i] &= 0x0F
        for _ in range(3):
            put8(m, 0xDCF5, rng.choice([rng.randrange(0x20), rng.randrange(0x20), rng.randrange(256)]))
            put16(m, 0xDD01, rng.randrange(200))
            x0, x1 = pick_span(rng, 320)
            es = rng.choice([u16(m, 0xDD2D), u16(m, 0xDD2D), rng.choice(pages(m, h))])
            h.check('picture_hline', m, regs={'ax': x0, 'bx': x1, 'es': es}, label='%d..%d es %04X' % (x0, x1, es))
            n += 1
        width = rng.choice([8, 16, 0x30, 0x100, rng.randrange(1, 64)])
        x = pick_x(rng, 320 - width + 1)
        put16(m, 0xDCFF, x)
        put16(m, 0xDD01, rng.randrange(40, 200))
        runs = rng.randrange(1, 120)
        data = bytearray()
        for _ in range(runs):
            data += bytes([rng.choice([rng.randrange(0x20), rng.randrange(256)]),
                           rng.choice([0, 1, 2, 3, 7, 8, 9, min(width, 0xFF), rng.randrange(256)])])
        m[DS_BASE + PIC:DS_BASE + PIC + len(data)] = data
        h.check('picture_draw', m, stack_args=[PIC, runs, width], outputs=['ax'], label='%d runs, width %d at x %d' % (runs, width, x))
        n += 1
    return n


# ---- the tests, per machine

test_lib_set_mode_ega = on('test_lib_set_mode_ega', set_mode_cases)
test_lib_set_mode_cga = on('test_lib_set_mode_cga', set_mode_cases)
test_lib_set_mode_tandy = on('test_lib_set_mode_tandy', set_mode_cases)
test_lib_set_mode_hercules = on('test_lib_set_mode_hercules', set_mode_cases)
test_lib_detect_ega = on('test_lib_detect_ega', detect_cases)
test_lib_detect_cga = on('test_lib_detect_cga', detect_cases)
test_lib_detect_tandy = on('test_lib_detect_tandy', detect_cases)
test_lib_detect_hercules = on('test_lib_detect_hercules', detect_cases)
test_lib_pages_ega = on('test_lib_pages_ega', page_cases)
test_lib_pages_cga = on('test_lib_pages_cga', page_cases)
test_lib_pages_tandy = on('test_lib_pages_tandy', page_cases)
test_lib_pages_hercules = on('test_lib_pages_hercules', page_cases)
test_lib_fill_ega = on('test_lib_fill_ega', fill_cases)
test_lib_fill_cga = on('test_lib_fill_cga', fill_cases)
test_lib_fill_tandy = on('test_lib_fill_tandy', fill_cases)
test_lib_fill_hercules = on('test_lib_fill_hercules', fill_cases)
test_lib_copy_ega = on('test_lib_copy_ega', copy_cases)
test_lib_copy_cga = on('test_lib_copy_cga', copy_cases)
test_lib_copy_tandy = on('test_lib_copy_tandy', copy_cases)
test_lib_bitmap_ega = on('test_lib_bitmap_ega', bitmap_cases)
test_lib_bitmap_cga = on('test_lib_bitmap_cga', bitmap_cases)
test_lib_bitmap_tandy = on('test_lib_bitmap_tandy', bitmap_cases)
test_lib_picture_ega = on('test_lib_picture_ega', picture_cases)
test_lib_picture_cga = on('test_lib_picture_cga', picture_cases)
test_lib_picture_tandy = on('test_lib_picture_tandy', picture_cases)
test_lib_picture_hercules = on('test_lib_picture_hercules', picture_cases)

TESTS = [test_lib_set_mode_ega, test_lib_set_mode_cga, test_lib_set_mode_tandy, test_lib_set_mode_hercules,
         test_lib_detect_ega, test_lib_detect_cga, test_lib_detect_tandy, test_lib_detect_hercules,
         test_lib_pages_ega, test_lib_pages_cga, test_lib_pages_tandy, test_lib_pages_hercules,
         test_lib_fill_ega, test_lib_fill_cga, test_lib_fill_tandy, test_lib_fill_hercules,
         test_lib_copy_ega, test_lib_copy_cga, test_lib_copy_tandy,
         test_lib_bitmap_ega, test_lib_bitmap_cga, test_lib_bitmap_tandy,
         test_lib_picture_ega, test_lib_picture_cga, test_lib_picture_tandy, test_lib_picture_hercules]
