"""Keyboard (platform.md §2): install, restore and the INT 9 handler on byte sequences."""
import contextlib

from gbdiff import put8, put16

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


def axis_count(v):
    """The port's gamepad axis -> game-port count (joystick.cpp axis_count; C++ division truncates)."""
    q = abs(v * 248) // 32768
    return (256 + (-q if v < 0 else q)) & 0xFFFF


@contextlib.contextmanager
def joystick_stubs(h, present, x, y, buttons):
    """The original's port-201h routines replaced by the port's host mapping (PORT: the gamepad),
    and the port's host stub given the same gamepad."""
    def axis(v):
        return lambda args: ((0xFFFF if ((args(0) - 1) & 1) or not present else axis_count(v)), None)
    hooks = [h.orig.stub(0x146A, 0x000B, axis(x)), h.orig.stub(0x15EA, 0x0003, axis(y)),
             h.orig.stub(0x15D7, 0x000D, lambda args: ((0 if ((args(0) - 1) & 1) or not present else buttons & 3), None))]
    h.port.dll.gb_set_joy(int(present), x, y, buttons)
    try:
        yield
    finally:
        for k in hooks:
            h.orig.uc.hook_del(k)
        h.port.dll.gb_set_joy(0, 0, 0, 0)


def test_joystick(h, rng, scale):
    """joystick_calibrate and joystick_read over gamepad positions, both sticks, present or not."""
    n = 0
    m0 = h.fresh_memory()
    values = [-32768, -32767, -20000, -1, 0, 1, 131, 20000, 32767]
    for _ in range(40 * scale):
        present = rng.random() < 0.85
        x, y = rng.choice(values + [rng.randrange(-32768, 32768)]), rng.choice(values + [rng.randrange(-32768, 32768)])
        buttons = rng.randrange(4)
        stick = rng.choice([1, 2, 0, 3])
        with joystick_stubs(h, present, x, y, buttons):
            m = bytearray(m0)
            for off in range(0xDD13, 0xDD23, 2):
                put16(m, off, rng.choice([0xFFFF, 0, 64, 128, 256, 320, rng.randrange(0x10000)]))
            h.check('joystick_calibrate', m, stack_args=[stick], outputs=['ax'],
                    label='stick %d present %d (%d, %d)' % (stick, present, x, y))
            h.check('joystick_read', m, stack_args=[stick, 0xF626, 0xF627],
                    label='stick %d present %d (%d, %d) buttons %d' % (stick, present, x, y, buttons))
            n += 2
    return n


TESTS = [test_install_restore, test_isr_sequences, test_joystick]
