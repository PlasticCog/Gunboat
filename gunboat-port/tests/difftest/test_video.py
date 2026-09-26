"""The graphics library, text, palette and picture routines (video.md, platform.md §6-§7)."""
import struct

from gbdiff import DS_BASE, lin, put8, put16, randomize

VRAM = 0xA0000
SCRATCH = 0xF000       # DS offset for bitmaps and strings (BSS, below the stack)
PIC = 0x1094           # the pictures' decode buffer


def run_original(h, m, name, stack_args=()):
    """Memory after the original runs `name` (a set-up step whose own test compares the port)."""
    h.orig.set_memory(m)
    file_seg, off, far = h.sym.func(name)
    h.orig.call(file_seg, off, far, {}, stack_args)
    return bytearray(h.orig.memory())


def vga_state(h, rng, page1=True):
    """Mode 13h set by the original, page 1 allocated, random screen and page contents."""
    m = run_original(h, h.fresh_memory(), 'gfx_set_mode', [0x13])
    if page1:
        m = run_original(h, m, 'gfx_alloc_page', [1])
        seg = struct.unpack_from('<H', m, DS_BASE + 0xDD31 + 2)[0]
        randomize(m, seg << 4, 64000, rng)
    randomize(m, VRAM, 0x10000, rng)
    put16(m, 0xEED2, 0x13)                      # the game's video mode: VGA
    return m


def test_set_mode(h, rng, scale):
    n = 0
    for base in ('fresh', 'vga'):
        for mode in [-1, -2, 0x14, 0x7FFF, -0x8000] + list(range(0x14)):
            m = h.fresh_memory() if base == 'fresh' else vga_state(h, rng, page1=False)
            h.check('gfx_set_mode', m, stack_args=[mode & 0xFFFF], outputs=['ax'], label='%s mode %X' % (base, mode))
            n += 1
    return n


def test_mode_queries(h, rng, scale):
    n = 0
    for dcfc in [0xFF, 0x80, 0x00, 0x03, 0x13, 0x7F]:
        for bios_mode in (3, 0x13, 0x07):
            m = h.fresh_memory()
            put8(m, 0xDCFC, dcfc)
            m[0x449] = bios_mode
            h.check('gfx_saved_mode', m, outputs=['ax'], label='DCFC %02X BIOS %02X' % (dcfc, bios_mode))
            n += 1
    h.check('gfx_detect', h.fresh_memory(), outputs=['ax'])
    h.check('gfx_get_draw_seg', vga_state(h, rng), outputs=['ax'])
    return n + 2


def test_pages(h, rng, scale):
    n = 0
    for _ in range(30 * scale):
        m = vga_state(h, rng)
        put8(m, 0xDCF4, rng.randrange(8))
        put8(m, 0xDCFD, rng.choice([0, 0, 1, rng.randrange(8)]))
        for name in ('gfx_set_draw_page', 'gfx_set_copy_page', 'gfx_set_visible_page'):
            h.check(name, m, stack_args=[rng.choice([0, 1, 2, 7, 8, 9, 0xFFFF, rng.randrange(0x10000)])],
                    outputs=['ax'])
            n += 1
    for page in [-1, 0, 1, 2, 7, 8, 0x7FFF]:
        h.check('gfx_alloc_page', vga_state(h, rng, page1=False), stack_args=[page & 0xFFFF], outputs=['ax'],
                label='page %d' % page)
        h.check('gfx_free_page', vga_state(h, rng), stack_args=[page & 0xFFFF], outputs=['ax'], label='page %d' % page)
        n += 2
    return n


def test_drawing(h, rng, scale):
    n = 0
    for _ in range(60 * scale):
        m = vga_state(h, rng)
        put8(m, 0xDCF5, rng.randrange(256))
        put16(m, 0xDD2B, rng.choice([0xA000, struct.unpack_from('<H', m, DS_BASE + 0xDD33)[0]]))
        put16(m, 0xDD2D, rng.choice([0xA000, struct.unpack_from('<H', m, DS_BASE + 0xDD33)[0]]))
        x0 = rng.randrange(320)
        x1 = rng.randrange(max(0, x0 - 1), 320)
        y0 = rng.randrange(200)
        y1 = rng.randrange(y0, 200)
        rect = [x0, x1, y0, y1]
        h.check('gfx_move_to', m, stack_args=[rng.randrange(0x10000), rng.randrange(0x10000)], outputs=['ax'])
        h.check('gfx_set_colour', m, stack_args=[rng.randrange(0x10000)], outputs=['ax'])
        h.check('gfx_fill_rect', m, stack_args=rect, outputs=['ax'], label=str(rect))
        h.check('gfx_copy_rect_from_copy_page', m, stack_args=rect, outputs=['ax'], label=str(rect))
        h.check('gfx_copy_rect_to_copy_page', m, stack_args=rect, outputs=['ax'], label=str(rect))
        put16(m, 0xDCFF, rng.randrange(290))
        put16(m, 0xDD01, rng.randrange(20, 200))
        rows, width = rng.randrange(1, 20), rng.randrange(1, 4)
        randomize(m, DS_BASE + SCRATCH, rows * width, rng)
        h.check('gfx_draw_bitmap', m, stack_args=[SCRATCH, width, rows], outputs=['ax'])
        h.check('gfx_read_bitmap', m, stack_args=[SCRATCH, width, rows], outputs=['ax'])
        n += 7
    for _ in range(3):
        h.check('gfx_clear_page', vga_state(h, rng), outputs=['ax'])
        n += 1
    return n


def test_copy_rect_and_pixel(h, rng, scale):
    """gfx_copy_rect between pages 0 and 1 (destination by its bottom row), gfx_put_pixel with the
    clip window, ega_pal_set (nothing in mode 13h)."""
    n = 0
    for _ in range(60 * scale):
        m = vga_state(h, rng)
        x0 = rng.randrange(320)
        x1 = rng.randrange(x0, 320)
        y0 = rng.randrange(200)
        y1 = rng.randrange(y0, 200)
        dx = rng.randrange(0, 320 - (x1 - x0))
        dy = rng.randrange(y1 - y0, 200)
        src, dst = rng.randrange(2), rng.randrange(2)
        args = [x0, x1, y0, y1, dx, dy, src, dst]
        h.check('gfx_copy_rect', m, stack_args=args, outputs=['ax'], label=str(args))
        put8(m, 0xDCF5, rng.randrange(256))
        put16(m, 0xDD2D, rng.choice([0xA000, struct.unpack_from('<H', m, DS_BASE + 0xDD33)[0]]))
        for off in (0xDD03, 0xDD05, 0xDD07, 0xDD09):
            put16(m, off, rng.choice([0, 0x13F, 0xC7, rng.randrange(-20, 340) & 0xFFFF]))
        for _ in range(4):
            x, y = rng.randrange(-40, 360), rng.randrange(-40, 240)
            h.check('gfx_put_pixel', m, stack_args=[x & 0xFFFF, y & 0xFFFF], outputs=['ax'], label='(%d, %d)' % (x, y))
        h.check('ega_pal_set', m, stack_args=[rng.randrange(32), rng.randrange(0x10000)], outputs=['ax'])
        n += 6
    return n


def test_text_mode_exit(h, rng, scale):
    """gfx_set_mode(3) at exit, then text_exit_clear with the cursor home or not."""
    n = 0
    for cursor in (0, 0x0100, 0x1850):
        m = run_original(h, vga_state(h, rng), 'gfx_set_mode', [3])
        randomize(m, 0xB8000, 0x4000, rng)
        struct.pack_into('<H', m, 0x450, cursor)
        h.check('gfx_clear_page', m, outputs=['ax'], label='text mode')
        h.check('text_exit_clear', m, outputs=['ax'], label='cursor %04X' % cursor)
        n += 2
    h.check('text_exit_clear', vga_state(h, rng), outputs=['ax'], label='mode 13h')
    return n + 1


def test_text(h, rng, scale):
    n = 0
    for _ in range(300 * scale):
        m = vga_state(h, rng)
        put8(m, 0xD9BC, rng.choice([0, 0, 1, rng.randrange(256)]))  # text_transparent
        put16(m, 0x007A, rng.randrange(2))
        h.check('text_set_colours', m, stack_args=[rng.randrange(0x10000), rng.randrange(0x10000)])
        h.check('text_goto_cell', m, stack_args=[rng.randrange(25), rng.randrange(40)])
        h.check('text_goto', m, stack_args=[rng.randrange(192), rng.randrange(40)])
        ch = rng.choice([rng.randrange(0x20, 0x80), rng.randrange(256)])
        put8(m, SCRATCH, ch)
        # the pen and colour follow from the calls above; draw on the memory they produce
        m = run_original(h, m, 'text_set_colours', [rng.randrange(256), rng.randrange(256)])
        m = run_original(h, m, 'text_goto_cell', [rng.randrange(25), rng.randrange(40)])
        h.check('text_draw_char', m, stack_args=[SCRATCH], label='char %02X' % ch)
        h.check('far_normalize', m, stack_args=[rng.randrange(0x10000), rng.randrange(0x10000)], outputs=['ax', 'dx'])
        n += 5
    return n


def test_palette(h, rng, scale):
    n = 0
    for _ in range(20 * scale):
        m = vga_state(h, rng)
        m[DS_BASE + 0x08C4:DS_BASE + 0x08C4 + 225] = bytes(rng.randrange(64) for _ in range(225))
        dac = rng.randbytes(768)
        dac = bytes(v & 0x3F for v in dac)
        h.check('pal_apply', m, dac=dac)
        h.check('pal_black', m, dac=dac)
        h.check('pal_fade_in', m, dac=dac, tick=(['121b:0841'], 'tick_counter'))
        h.check('pal_fade_out', m, dac=dac, tick=(['121b:07eb'], 'tick_counter'))
        n += 4
    return n


def test_picture(h, rng, scale):
    n = 0
    for _ in range(100 * scale):
        m = vga_state(h, rng)
        put16(m, 0xD9B6, 0xA000)
        put16(m, 0xD9B8, struct.unpack_from('<H', m, DS_BASE + 0xDD33)[0])
        put16(m, 0x007A, rng.randrange(2))
        runs = rng.randrange(1, 1500)
        data = bytearray()
        for _ in range(runs):
            data += bytes([rng.randrange(256), rng.choice([0, 1, 2, 3, 200, 255, rng.randrange(256)])])
        m[DS_BASE + PIC:DS_BASE + PIC + len(data)] = data
        y = rng.choice([199, 150, rng.randrange(200)])
        h.check('picture_draw_vga', m, stack_args=[PIC, runs, y], label='%d runs from row %d' % (runs, y))
        n += 1
    return n


TESTS = [test_set_mode, test_mode_queries, test_pages, test_drawing, test_copy_rect_and_pixel, test_text_mode_exit, test_text, test_palette,
         test_picture]
