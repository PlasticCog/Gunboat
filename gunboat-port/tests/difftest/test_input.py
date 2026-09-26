"""Keyboard (platform.md §2): install, restore and the INT 9 handler on byte sequences."""
from gbdiff import put8

# make codes, their breaks, grey keys, Pause, locks, the ACK, shifts, keypad directions
BYTES = ([0x1E, 0x9E, 0x1C, 0x9C, 0x39, 0xB9, 0x01, 0x81, 0x2A, 0xAA, 0x36, 0xB6, 0x1D, 0x9D, 0x0E, 0x8E,
          0x3A, 0xBA, 0x45, 0xC5, 0x46, 0xC6, 0xFA, 0x29, 0xA9, 0x2B, 0xAB, 0x60] +
         list(range(0x47, 0x54)) + [b | 0x80 for b in range(0x47, 0x54)] + [0xE0, 0xE1, 0xE0, 0xE0])


def test_install_restore(h, rng, scale):
    m = h.fresh_memory()
    h.check('kbd_install', m)
    h.orig.set_memory(m)
    h.orig.call(*h.sym.func('kbd_install'), {})
    h.check('kbd_restore', bytearray(h.orig.memory()))
    return 2


def test_isr_sequences(h, rng, scale):
    n = 0
    for _ in range(40 * scale):
        m = h.fresh_memory()
        m[0x417] = rng.choice([0, 0x20, 0x40, 0x60, 0x03, rng.randrange(256)])  # shift/lock state
        for _ in range(rng.randrange(5, 60)):
            b = rng.choice(BYTES + [rng.randrange(256)])
            h.orig.ins[0x60] = lambda uc, b=b: b
            h.check('kbd_isr', m, stack_args=[b], label='byte %02X' % b)
            m = bytearray(h.orig.memory())  # continue from the state both sides reached
            n += 1
    return n


TESTS = [test_install_restore, test_isr_sequences]
