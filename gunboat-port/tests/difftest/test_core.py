"""Core differential tests: the EXE loader and the first ported functions (random, boat motion)."""
from gbdiff import (DS_BASE, GAME_DIR, MEM_SIZE, STACK_BOTTOM, Mismatch, load_image, put8, put16,
                    randomize)


def test_loader(h, rng, scale):
    """The port's C++ loader on the shipped (EXEPACK-packed) GB.EXE gives exactly the memory of the
    Python loader on the unpacked file: same image, same relocations, all else zero."""
    h.port.load_exe(GAME_DIR / 'GB.EXE')
    got, want = h.port.memory(), load_image()
    if got != want:
        bad = [i for i in range(MEM_SIZE) if got[i] != want[i]]
        raise Mismatch('loader: %d bytes differ, first at %s' % (len(bad), h.sym.describe(bad[0])))
    return 1


def random_dgroup(h, rng):
    m = h.fresh_memory()
    randomize(m, DS_BASE, STACK_BOTTOM, rng)
    return m


def test_random(h, rng, scale):
    n = 2000 * scale
    for _ in range(n):
        h.check('random', random_dgroup(h, rng), outputs=['ax'])
    return n


def test_vec_scale(h, rng, scale):
    n = 0
    for al in range(256):
        for _ in range(4 * scale):
            h.check('vec_scale', random_dgroup(h, rng), regs={'ax': rng.randrange(0x10000) & 0xFF00 | al},
                    outputs=['ax'])
            n += 1
    return n


SPEEDS = [0x00, 0x01, 0x02, 0x3F, 0x40, 0x7F, 0x80, 0x81, 0xC0, 0xFF]


def test_heading_vector(h, rng, scale):
    n = 0
    for angle in range(256):
        for speed in SPEEDS + [rng.randrange(256) for _ in range(2 * scale)]:
            m = random_dgroup(h, rng)
            put8(m, 0xB828, speed)
            h.check('heading_vector', m, regs={'ax': rng.randrange(0x100) << 8 | angle}, outputs=['ax'],
                    label='angle %02X speed %02X' % (angle, speed))
            n += 1
    return n


EDGE_CELLS = [0x00, 0x04, 0x05, 0x06, 0x26, 0x27, 0x28, 0x3E, 0x3F, 0x40, 0xFF]


def test_boat_move(h, rng, scale):
    """Whole DGROUP random, with the boat near the map edges, extreme speeds and the stations 1-4
    (and random station words, which make the water code read other bytes as headings)."""
    n = 3000 * scale
    for i in range(n):
        m = random_dgroup(h, rng)
        for pos in (0xC12D, 0xC8FD):  # object_x[0], object_y[0]
            if rng.random() < 0.6:
                put16(m, pos, rng.choice(EDGE_CELLS) << 8 | rng.choice([0x00, 0x01, 0x02, 0xFD, 0xFE, 0xFF, rng.randrange(256)]))
        if rng.random() < 0.5:
            put8(m, 0xB828, rng.choice(SPEEDS))
        if rng.random() < 0.8:
            put16(m, 0x0086, rng.randrange(1, 10))
        h.check('boat_move', m, regs={'si': rng.randrange(0x10000)}, outputs=['si'], label='case %d' % i)
    return n


TESTS = [test_loader, test_random, test_vec_scale, test_heading_vector, test_boat_move]
