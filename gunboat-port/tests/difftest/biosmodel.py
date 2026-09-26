"""The port's BIOS model for the original code in Unicorn (mirror of src/platform/bios.cpp).

INT 10h video services on the BIOS data area (segment 0040h) and a VGA DAC, INT 1Ah, and the video
ports the graphics library touches. Keep it in step with bios.cpp.
"""
import struct

from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
                               UC_X86_REG_ES)

BDA = 0x400
MODE, COLS, PAGE_SIZE, PAGE_START, CURSOR, CURSOR_SHAPE, PAGE, TICKS = \
    0x49, 0x4A, 0x4C, 0x4E, 0x50, 0x60, 0x62, 0x6C
MODE_COLS = [40, 40, 80, 80, 40, 40, 80, 80, 20, 40, 80, 0, 0, 40, 80, 80, 80, 80, 80, 40]
MODE_PAGE = [0x800, 0x800, 0x1000, 0x1000, 0x4000, 0x4000, 0x4000, 0x1000, 0x4000, 0x8000, 0x8000, 0, 0,
             0x2000, 0x4000, 0x8000, 0x8000, 0xA000, 0xA000, 0xFA00]


def bios_init(m, machine='vga'):
    """bios_init on a memory image (bytearray), for the machine's video card."""
    m[BDA + 0x49:BDA + 0x6C + 5] = bytes(0x6C + 5 - 0x49)
    m[BDA + MODE] = 3
    struct.pack_into('<H', m, BDA + COLS, 80)
    struct.pack_into('<H', m, BDA + PAGE_SIZE, 0x1000)
    struct.pack_into('<H', m, BDA + CURSOR_SHAPE, 0x0607)
    struct.pack_into('<H', m, BDA + 0x63, 0x03D4)                           # the CRTC's port (colour)
    if machine != 'vga':    # the card for the adapter probes (bios.cpp)
        struct.pack_into('<H', m, BDA + 0x10, 0x0030 if machine == 'hercules' else 0x0020)
        if machine == 'hercules':
            struct.pack_into('<H', m, BDA + 0x63, 0x03B4)
        m[0xFC000] = 0x21 if machine == 'tandy' else 0x00
    if machine == 'ega':    # the EGA BIOS's information bytes (bios.cpp)
        m[BDA + 0x87] = 0x60
        m[BDA + 0x88] = 0x08
    struct.pack_into('<HHHH', m, 8 * 4, 0xFEA5, 0xF000, 0xE987, 0xF000)   # INT 8, INT 9: the BIOS


class BiosModel:
    def _in3da(self, uc):
        self.retrace ^= 1
        return 0x08 if self.retrace else 0x00

    def __init__(self, orig, machine='vga', card=None):
        self.orig = orig
        self.uc = orig.uc
        self.machine = machine
        self.card = card
        self.dac = bytearray(768)
        self.trace = bytearray()              # every DAC write in order: index, r, g, b
        orig.ints[0x10] = self.int10
        orig.ints[0x1A] = self.int1a
        # the input status: the vertical retrace bit alternates on each read, starting in the
        # retrace, so a wait for the retrace ends at once and a wait for its end at the next read
        self.retrace = 0
        orig.ins[0x3DA] = self._in3da
        orig.outs[0x3D4] = lambda uc, value: None     # the CRTC (the display start: gbdiff compares memory)
        for port in (0x3CE, 0x3C4, 0x3B4, 0x3B5, 0x3B8, 0x3BF):
            if port not in orig.outs:  # (the card model's, when there is one)
                orig.outs[port] = lambda uc, value: None  # graphics/sequencer/Hercules registers
        # system port B (61h): bit 7 keyboard acknowledge, bits 0-1 the speaker gate (read back as
        # written); the keyboard data port 60h (a test sets what IN 60h reads)
        self.port61 = 0
        orig.ins[0x61] = lambda uc: self.port61
        orig.outs[0x61] = self._out61
        orig.outs[0x60] = lambda uc, value: None
        orig.outs[0x20] = lambda uc, value: None    # PIC end of interrupt

    def _out61(self, uc, value):
        self.port61 = value & 0xFF

    def rd8(self, a):
        return self.uc.mem_read(a, 1)[0]

    def rd16(self, a):
        return struct.unpack('<H', self.uc.mem_read(a, 2))[0]

    def wr16(self, a, v):
        self.uc.mem_write(a, struct.pack('<H', v & 0xFFFF))

    def set_mode(self, al):
        uc = self.uc
        mode, clear = al & 0x7F, not (al & 0x80)
        uc.mem_write(BDA + MODE, bytes([mode]))
        if mode < 0x14 and MODE_COLS[mode]:
            self.wr16(BDA + COLS, MODE_COLS[mode])
            self.wr16(BDA + PAGE_SIZE, MODE_PAGE[mode])
        self.wr16(BDA + PAGE_START, 0)
        uc.mem_write(BDA + CURSOR, bytes(16))
        uc.mem_write(BDA + PAGE, b'\0')
        if clear:
            if mode <= 3:
                uc.mem_write(0xB8000, b'\x20\x07' * 0x2000)
            elif mode == 7:
                uc.mem_write(0xB0000, b'\x20\x07' * 0x2000)
            elif mode <= 6:
                uc.mem_write(0xB8000, bytes(0x4000))
            elif 8 <= mode <= 0x0A:
                uc.mem_write(0xB8000, bytes(0x8000))
            elif 0x0D <= mode <= 0x13 and self.machine != 'ega':
                uc.mem_write(0xA0000, bytes(0x10000))
        if self.machine != 'vga':
            self.card.bios_set_mode(mode, clear)
        if mode == 0x13:
            for i in range(256):
                self.dac_write(i, 0, 0, 0)

    def dac_write(self, i, r, g, b):
        v = bytes([r & 0x3F, g & 0x3F, b & 0x3F])
        self.dac[3 * i:3 * i + 3] = v
        self.trace += bytes([i]) + v

    def int10(self, uc):
        ax, bx, cx, dx = (uc.reg_read(r) for r in (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX))
        ah, al = ax >> 8, ax & 0xFF
        if ah == 0x00:
            self.set_mode(al)
        elif ah == 0x0F:
            uc.reg_write(UC_X86_REG_AX, self.rd8(BDA + COLS) << 8 | self.rd8(BDA + MODE))
            uc.reg_write(UC_X86_REG_BX, (bx & 0x00FF) | self.rd8(BDA + PAGE) << 8)
        elif ah == 0x03:
            uc.reg_write(UC_X86_REG_DX, self.rd16(BDA + CURSOR + 2 * ((bx >> 8) & 7)))
            uc.reg_write(UC_X86_REG_CX, self.rd16(BDA + CURSOR_SHAPE))
        elif ax == 0x1A00:
            if self.machine == 'vga':               # (the older cards' BIOSes do not know it)
                uc.reg_write(UC_X86_REG_AX, (ax & 0xFF00) | 0x1A)
                uc.reg_write(UC_X86_REG_BX, 0x0008)
        elif ah == 0x12 and bx & 0xFF == 0x10:      # the EGA information (EGA and VGA BIOSes)
            if self.machine in ('ega', 'vga'):
                info, switches = self.rd8(BDA + 0x87), self.rd8(BDA + 0x88)
                uc.reg_write(UC_X86_REG_BX, ((info >> 1) & 1) << 8 | ((info >> 5) & 3))
                uc.reg_write(UC_X86_REG_CX, (switches >> 4) << 8 | (switches & 0x0F))
        elif ah == 0x05:                            # the active display page
            uc.mem_write(BDA + PAGE, bytes([al]))
            self.wr16(BDA + PAGE_START, al * self.rd16(BDA + PAGE_SIZE))
        elif ax == 0x1010:
            self.dac_write(bx & 0xFF, dx >> 8, cx >> 8, cx)
        elif ax == 0x1012:
            es = uc.reg_read(UC_X86_REG_ES)
            table = bytes(uc.mem_read((es << 4) + dx, 3 * cx))
            for k in range(cx):
                self.dac_write((bx + k) & 0xFF, *table[3 * k:3 * k + 3])
        elif ah == 0x0B and self.machine != 'vga':
            self.card.bios_cga_palette(bx >> 8, bx & 0xFF)
        elif ax == 0x1000 and self.machine != 'vga':
            self.card.bios_palette_reg(bx & 0xFF, bx >> 8)
        elif ax == 0x1001 and self.machine != 'vga':
            self.card.bios_overscan(bx >> 8)
        elif ax == 0x1002:
            if self.machine != 'vga':
                es = uc.reg_read(UC_X86_REG_ES)
                table = bytes(uc.mem_read((es << 4) + dx, 17))
                for i in range(16):
                    self.card.bios_palette_reg(i, table[i])
                self.card.bios_overscan(table[16])
        else:
            self.orig.fail('INT 10h AX=%04X is not modelled' % ax)

    def int1a(self, uc):
        ax = uc.reg_read(UC_X86_REG_AX)
        if ax >> 8 != 0:
            self.orig.fail('INT 1Ah AX=%04X is not modelled' % ax)
            return
        t = struct.unpack('<I', uc.mem_read(BDA + TICKS, 4))[0]
        uc.reg_write(UC_X86_REG_CX, t >> 16)
        uc.reg_write(UC_X86_REG_DX, t & 0xFFFF)
        uc.reg_write(UC_X86_REG_AX, ax & 0xFF00 | self.rd8(BDA + 0x70))
        uc.mem_write(BDA + 0x70, b'\0')
