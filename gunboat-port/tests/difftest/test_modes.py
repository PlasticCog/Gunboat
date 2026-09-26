"""The video modes other than VGA (video.md §6), each on its machine: the EGA, CGA, Tandy and Hercules
paths of the graphics library and the game. A test's `machine` attribute picks the harness (the
emulated video card, cardmodel.py); the card's state is compared with the memory."""
import cardmodel
from gbdiff import DS_BASE, randomize

MODES = {'ega': [0x0D, 0x0E], 'cga': [4, 5, 6], 'tandy': [8, 9, 0x0A], 'hercules': [0x0B, 0x0C, 4]}


def machine(name):
    """The decorator that puts a test on a machine."""
    def put(test):
        test.machine = name
        return test
    return put


def after_original(h, m, name, stack_args=()):
    """Memory and card state after the original runs `name` from m and the current card state (a
    set-up step whose own test compares the port); the card state becomes the next check's start."""
    h.card.s[:] = h.card_state if h.card_state is not None else cardmodel.reset_state()
    h.orig.set_memory(m)
    file_seg, off, far = h.sym.func(name)
    h.orig.call(file_seg, off, far, {}, stack_args)
    h.card_state = bytes(h.card.s)
    return bytearray(h.orig.memory())


def mode_state(h, rng, mode):
    """The library in `mode` (gfx_set_mode by the original) with a random DGROUP above the library's
    own tables (the game's variables)."""
    h.card_state = None
    m = after_original(h, h.fresh_memory(), 'gfx_set_mode', [mode])
    randomize(m, DS_BASE + 0x0100, 0x0800, rng)
    return m


def colour_cases(h, rng, scale):
    n = 0
    for mode in MODES[h.machine]:
        for k in range(24 * scale):
            m = mode_state(h, rng, mode)
            c = rng.choice([rng.randrange(0x10000), rng.randrange(0x20), k])
            h.check('gfx_set_colour', m, stack_args=[c], outputs=['ax'], label='mode %X colour %X' % (mode, c))
            n += 1
    return n


test_set_colour_ega = machine('ega')(lambda h, rng, scale: colour_cases(h, rng, scale))
test_set_colour_cga = machine('cga')(lambda h, rng, scale: colour_cases(h, rng, scale))
test_set_colour_tandy = machine('tandy')(lambda h, rng, scale: colour_cases(h, rng, scale))
test_set_colour_hercules = machine('hercules')(lambda h, rng, scale: colour_cases(h, rng, scale))
for _name in ('ega', 'cga', 'tandy', 'hercules'):
    globals()['test_set_colour_' + _name].__name__ = 'test_set_colour_' + _name


@machine('hercules')
def test_hercules_setup(h, rng, scale):
    n = 0
    for k in range(4 * scale):
        m = mode_state(h, rng, 4)
        randomize(m, 0xB8000, 0x8000, rng)
        h.check('hercules_setup', m, label='case %d' % k)
        n += 1
    return n


TESTS = [test_set_colour_ega, test_set_colour_cga, test_set_colour_tandy, test_set_colour_hercules,
         test_hercules_setup]
