"""The video card of the emulated machine for the original code in Unicorn (mirror of
src/platform/card.cpp): EGA planes and registers, CGA, Tandy and Hercules registers. Keep the two in
step.

The state is one bytearray laid out like the C++ CardState, so both sides can be set from and
compared with the same bytes. With an EGA machine the original's accesses to A000:0000-FFFF go
through Unicorn MMIO callbacks into the planes (as the CPU reaches an EGA's memory).
"""
import struct

from unicorn import UC_HOOK_MEM_READ

PLANES = 0x00000
LATCH = 0x40000
SEQ_INDEX, SEQ = 0x40004, 0x40005
GC_INDEX, GC = 0x4000A, 0x4000B
ATTR_FLIP, ATTR_INDEX, ATTR = 0x40014, 0x40015, 0x40016
CRTC_INDEX, CRTC = 0x4002B, 0x4002C
CGA_MODE, CGA_COLOUR = 0x40045, 0x40046
TANDY_INDEX, TANDY, TANDY_PAGE = 0x40047, 0x40048, 0x40068
HERC_INDEX, HERC, HERC_MODE, HERC_CONFIG = 0x40069, 0x4006A, 0x4007C, 0x4007D
STATUS = 0x4007E
SIZE = 0x4007F
COMPARED = STATUS          # the status toggle is the model's bookkeeping, not compared

MACHINES = {'vga': 0, 'ega': 1, 'cga': 2, 'tandy': 3, 'hercules': 4}


def reset_state():
    """card_reset."""
    s = bytearray(SIZE)
    s[SEQ + 2] = 0x0F
    s[GC + 7] = 0x0F
    s[GC + 8] = 0xFF
    s[ATTR + 0x12] = 0x0F
    return s


def ror8(v, n):
    n &= 7
    return ((v >> n) | (v << (8 - n))) & 0xFF if n else v


class CardModel:
    def __init__(self, orig, machine):
        self.orig = orig
        self.machine = machine
        self.s = reset_state()
        if machine == 'vga':  # the VGA tests keep their own port handlers (the card is not used)
            return
        for port in (0x3C0, 0x3C4, 0x3C5, 0x3CE, 0x3CF, 0x3D4, 0x3D5, 0x3D8, 0x3D9, 0x3DE, 0x3DF,
                     0x3B4, 0x3B5, 0x3B8, 0x3BF):
            orig.outs[port] = (lambda p: lambda uc, value: self.out(p, value))(port)
        orig.outs[0x3DA] = lambda uc, value: self.out(0x3DA, value)
        orig.ins[0x3DA] = lambda uc: self.inp(0x3DA)
        orig.ins[0x3BA] = lambda uc: self.inp(0x3BA)
        if machine == 'ega':
            orig.map_ega(self._mmio_read, self._mmio_write)
            # Unicorn splits an unaligned word read of MMIO into two aligned word reads (136Bh:
            # 136Ah and 136Ch); the CPU reads the two bytes it asks for, low first (the latches keep
            # the second). The memory hook sees the CPU's access first: its bytes are read there, and
            # the MMIO callbacks serve them (a byte only Unicorn's split asks for is not read).
            self.pending = None
            orig.uc.hook_add(UC_HOOK_MEM_READ, self._hook_read, None, 0xA0000, 0xAFFFF)

    # ---- ports
    def out(self, port, value):
        s = self.s
        value &= 0xFF
        if port == 0x3C4:
            s[SEQ_INDEX] = value & 7
        elif port == 0x3C5:
            if s[SEQ_INDEX] < 5:
                s[SEQ + s[SEQ_INDEX]] = value
        elif port == 0x3CE:
            s[GC_INDEX] = value & 0x0F
        elif port == 0x3CF:
            if s[GC_INDEX] < 9:
                s[GC + s[GC_INDEX]] = value
        elif port == 0x3C0:
            if not s[ATTR_FLIP]:
                s[ATTR_INDEX] = value & 0x3F
            elif (s[ATTR_INDEX] & 0x1F) < 0x15:
                s[ATTR + (s[ATTR_INDEX] & 0x1F)] = value
            s[ATTR_FLIP] ^= 1
        elif port == 0x3D4:
            s[CRTC_INDEX] = value & 0x1F
        elif port == 0x3D5:
            if s[CRTC_INDEX] < 0x19:
                s[CRTC + s[CRTC_INDEX]] = value
        elif port == 0x3D8:
            s[CGA_MODE] = value
        elif port == 0x3D9:
            s[CGA_COLOUR] = value
        elif port == 0x3DA:
            s[TANDY_INDEX] = value & 0x1F
        elif port == 0x3DE:
            s[TANDY + (s[TANDY_INDEX] & 0x1F)] = value
        elif port == 0x3DF:
            s[TANDY_PAGE] = value
        elif port == 0x3B4:
            s[HERC_INDEX] = value & 0x1F
        elif port == 0x3B5:
            if s[HERC_INDEX] < 0x12:
                s[HERC + s[HERC_INDEX]] = value
        elif port == 0x3B8:
            s[HERC_MODE] = value
        elif port == 0x3BF:
            s[HERC_CONFIG] = value

    def inp(self, port):
        s = self.s
        if port == 0x3DA:
            s[ATTR_FLIP] = 0
            s[STATUS] ^= 1
            return 0x08 if s[STATUS] else 0x00
        if port == 0x3BA:
            s[STATUS] ^= 1
            return 0x80 if s[STATUS] else 0x00
        return 0xFF

    # ---- EGA memory
    def ega_read(self, off):
        s = self.s
        for p in range(4):
            s[LATCH + p] = s[PLANES + p * 0x10000 + off]
        if s[GC + 5] & 0x08:
            r = 0xFF
            for p in range(4):
                if s[GC + 7] & (1 << p):
                    r &= ~(s[LATCH + p] ^ (0xFF if s[GC + 2] & (1 << p) else 0x00)) & 0xFF
            return r
        return s[LATCH + (s[GC + 4] & 3)]

    def ega_write(self, off, value):
        s = self.s
        mode, function, mask = s[GC + 5] & 3, (s[GC + 3] >> 3) & 3, s[GC + 8]
        for p in range(4):
            if not s[SEQ + 2] & (1 << p):
                continue
            latch = s[LATCH + p]
            if mode == 1:
                s[PLANES + p * 0x10000 + off] = latch
                continue
            if mode == 2:
                d = 0xFF if value & (1 << p) else 0x00
            elif s[GC + 1] & (1 << p):
                d = 0xFF if s[GC + 0] & (1 << p) else 0x00
            else:
                d = ror8(value, s[GC + 3] & 7)
            if function == 1:
                d &= latch
            elif function == 2:
                d |= latch
            elif function == 3:
                d ^= latch
            s[PLANES + p * 0x10000 + off] = (d & mask) | (latch & ~mask & 0xFF)

    def _hook_read(self, uc, access, address, size, value, _):
        self.pending = {}
        for k in range(size):
            off = (address - 0xA0000 + k) & 0xFFFF
            self.pending[off] = self.ega_read(off)

    def _mmio_read(self, uc, offset, size, _):
        v = 0
        for k in range(size):
            off = (offset + k) & 0xFFFF
            if self.pending is None:
                b = self.ega_read(off)
            else:
                b = self.pending.pop(off, 0)
            v |= b << (8 * k)
        if self.pending is not None and not self.pending:
            self.pending = None
        return v

    def _mmio_write(self, uc, offset, size, value, _):
        for k in range(size):
            self.ega_write((offset + k) & 0xFFFF, (value >> (8 * k)) & 0xFF)

    # ---- the BIOS's part (card.cpp card_bios_*)
    def bios_set_mode(self, mode, clear):
        s = self.s
        m = self.machine
        if m == 'ega':
            if mode in (0x0D, 0x0E):
                s[SEQ + 2] = 0x0F
                s[SEQ + 4] = 0x06
                s[GC:GC + 9] = bytes([0, 0, 0, 0, 0, 0, 0x05, 0x0F, 0xFF])
                for i in range(16):
                    s[ATTR + i] = i if i < 8 else 0x10 + (i - 8)
                s[ATTR + 0x10] = 0x01
                s[ATTR + 0x11] = 0x00
                s[ATTR + 0x12] = 0x0F
                s[ATTR + 0x13] = 0x00
                s[ATTR_FLIP] = 0
                s[CRTC + 0x0C] = s[CRTC + 0x0D] = 0
                s[CRTC + 0x13] = 0x14 if mode == 0x0D else 0x28
                if clear:
                    s[PLANES:PLANES + 0x40000] = bytes(0x40000)
                s[LATCH:LATCH + 4] = bytes(4)
        elif m in ('cga', 'hercules'):
            if 4 <= mode <= 6:
                s[CGA_MODE] = 0x1E if mode == 6 else 0x0E if mode == 5 else 0x0A
                s[CGA_COLOUR] = 0x3F if mode == 6 else 0x30
                s[CRTC + 0x0C] = s[CRTC + 0x0D] = 0
        elif m == 'tandy':
            if 8 <= mode <= 0x0A:
                s[CGA_MODE] = 0x0B if mode == 9 else 0x0A
                for i in range(16):
                    s[TANDY + 0x10 + i] = i
                s[TANDY + 3] = 0x10 if mode == 9 else 0x00
                s[TANDY_PAGE] = 0xF6
                s[CRTC + 0x0C] = s[CRTC + 0x0D] = 0
            elif 4 <= mode <= 6:
                s[CGA_MODE] = 0x1E if mode == 6 else 0x0A
                s[CGA_COLOUR] = 0x30

    def bios_cga_palette(self, bh, bl):
        s = self.s
        if bh == 0:
            s[CGA_COLOUR] = (s[CGA_COLOUR] & 0xE0) | (bl & 0x1F)
        else:
            s[CGA_COLOUR] = (s[CGA_COLOUR] & 0xDF) | ((bl & 1) << 5)

    def bios_palette_reg(self, index, value):
        s = self.s
        if self.machine == 'tandy':
            if index < 16:
                s[TANDY + 0x10 + index] = value & 0x0F
        elif index < 16:
            s[ATTR + index] = value & 0xFF

    def bios_overscan(self, value):
        s = self.s
        if self.machine == 'tandy':
            s[TANDY + 2] = value & 0x0F
        else:
            s[ATTR + 0x11] = value & 0xFF


def describe(state_a, state_b):
    """The first difference between two card states, readable."""
    for i in range(COMPARED):
        if state_a[i] != state_b[i]:
            if i < LATCH:
                return 'EGA plane %d offset %04X: original %02X, port %02X' % (i // 0x10000, i % 0x10000,
                                                                             state_a[i], state_b[i])
            names = [(LATCH, 'latch'), (SEQ_INDEX, 'seq index'), (SEQ, 'seq'), (GC_INDEX, 'gc index'), (GC, 'gc'),
                     (ATTR_FLIP, 'attr flip'), (ATTR_INDEX, 'attr index'), (ATTR, 'attr'), (CRTC_INDEX, 'crtc index'),
                     (CRTC, 'crtc'), (CGA_MODE, 'cga mode'), (CGA_COLOUR, 'cga colour'), (TANDY_INDEX, 'tandy index'),
                     (TANDY, 'tandy'), (TANDY_PAGE, 'tandy page'), (HERC_INDEX, 'herc index'), (HERC, 'herc'),
                     (HERC_MODE, 'herc mode'), (HERC_CONFIG, 'herc config')]
            base, name = max((b, n) for b, n in names if b <= i)
            return 'card %s[%d]: original %02X, port %02X' % (name, i - base, state_a[i], state_b[i])
    return None
