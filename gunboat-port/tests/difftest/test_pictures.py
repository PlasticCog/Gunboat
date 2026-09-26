"""LZW pictures and the RLE drawers (platform.md §6): every .LZ picture of the archive."""
import struct
import sys

from gbdiff import DS_BASE, RE, put8, put16, randomize
from test_video import run_original, vga_state

sys.path.insert(0, str(RE / 'tools'))
from gbfile import extract  # noqa: E402
from gunboat_formats import read_datac  # noqa: E402

SRC_SEG = 0x8000        # the packed picture, in free heap memory
PIC = 0x1094            # decode buffer (DS:1094, as the game)

# (runs, argument) of the title pictures as title_menu draws them (title_flow_pseudocode.md §4)
TITLE_VGA = {'TITLE1A.LZ': (0x210C, 0x31), 'TITLE1B.LZ': (0x1ED4, 0x63), 'TITLE1C.LZ': (0x1832, 0x95),
             'TITLE1D.LZ': (0x1A23, 0xC7)}
TITLE_ALL = {'COPY.LZ': (0x1B5F, 0x140), 'ACCO.LZ': (0x0885, 0x140), 'TITLE3A.LZ': (0x1ECB, 0xA0)}


def base_memory(h, rng):
    """Mode 13h, page 1, and the far buffers and the LZW dictionary of mem_alloc_all."""
    return run_original(h, vga_state(h, rng), 'mem_alloc_all')


def decoded(h, rng, name):
    m = base_memory(h, rng)
    data = extract(name)
    m[SRC_SEG << 4:(SRC_SEG << 4) + len(data)] = data
    return m


def test_lzw_all_pictures(h, rng, scale):
    names = [r.candidate_name for r in read_datac() if r.candidate_name.upper().endswith('.LZ')]
    for name in names:
        h.check('lzw_decode_picture', decoded(h, rng, name), stack_args=[0, SRC_SEG, PIC, 0x2B73], label=name)
    return len(names)


def test_draw_title_pictures(h, rng, scale):
    n = 0
    for name, (runs, y) in TITLE_VGA.items():
        m = run_original(h, decoded(h, rng, name), 'lzw_decode_picture', [0, SRC_SEG, PIC, 0x2B73])
        seg1 = struct.unpack_from('<H', m, DS_BASE + 0xDD33)[0]
        put16(m, 0xD9B6, 0xA000)
        put16(m, 0xD9B8, seg1)
        for page in (0, 1):
            put16(m, 0x007A, page)
            h.check('picture_draw_vga', m, stack_args=[PIC, runs, y], label='%s page %d' % (name, page))
            n += 1
    for name, (runs, width) in TITLE_ALL.items():
        m = run_original(h, decoded(h, rng, name), 'lzw_decode_picture', [0, SRC_SEG, PIC, 0x2B73])
        for _ in range(3):
            put16(m, 0xDCFF, rng.randrange(0, 40))
            put16(m, 0xDD01, rng.randrange(150, 200))
            put8(m, 0xDCF5, rng.randrange(256))
            h.check('picture_draw', m, stack_args=[PIC, runs, width], outputs=['ax'], label=name)
            n += 1
    return n


def test_picture_draw_random(h, rng, scale):
    n = 0
    for _ in range(150 * scale):
        m = vga_state(h, rng)
        runs = rng.choice([0, 0xFFFF, 1, rng.randrange(1, 800)])
        data = bytearray()
        for _ in range(runs if runs < 0x8000 else 0):
            data += bytes([rng.randrange(256), rng.choice([0, 1, 2, 7, 255, rng.randrange(256)])])
        m[DS_BASE + PIC:DS_BASE + PIC + len(data)] = data
        width = rng.choice([1, 8, 0x18, 0xA0, 0x140, rng.randrange(1, 320)])
        put16(m, 0xDCFF, rng.randrange(0, 320 - min(width, 319)))
        put16(m, 0xDD01, rng.randrange(100, 200))
        put8(m, 0xDCF5, rng.randrange(256))
        h.check('picture_draw', m, stack_args=[PIC, runs, width], outputs=['ax'], label='%d runs width %d' % (runs, width))
        n += 1
    return n


TESTS = [test_lzw_all_pictures, test_draw_title_pictures, test_picture_draw_random]


def test_dissolve(h, rng, scale):
    n = 0
    for mode in (0x13, 0x13, 0x13, 0x0E):
        m = base_memory(h, rng)
        seg1 = struct.unpack_from('<H', m, DS_BASE + 0xDD33)[0]
        put16(m, 0xD9B6, 0xA000)
        put16(m, 0xD9B8, seg1)
        put16(m, 0xEED2, mode)
        h.check('dissolve_page1_to_0', m, tick=(['121b:0670'], 'tick_counter'), label='mode %X' % mode)
        n += 1
    return n


TESTS.append(test_dissolve)
