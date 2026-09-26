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
MODE_COLS = [40, 40, 80, 80, 40, 40, 80, 80, 0, 0, 0, 0, 0, 40, 80, 80, 80, 80, 80, 40]
MODE_PAGE = [0x800, 0x800, 0x1000, 0x1000, 0x4000, 0x4000, 0x4000, 0x1000, 0, 0, 0, 0, 0,
             0x2000, 0x4000, 0x8000, 0x8000, 0xA000, 0xA000, 0xFA00]


def bios_init(m):
    """bios_init on a memory image (bytearray)."""
    m[BDA + 0x49:BDA + 0x6C + 5] = bytes(0x6C + 5 - 0x49)
    m[BDA + MODE] = 3
    struct.pack_into('<H', m, BDA + COLS, 80)
    struct.pack_into('<H', m, BDA + PAGE_SIZE, 0x1000)
    struct.pack_into('<H', m, BDA + CURSOR_SHAPE, 0x0607)


class BiosModel:
    def __init__(self, orig):
        self.orig = orig
        self.uc = orig.uc
        self.dac = bytearray(768)
        self.trace = bytearray()              # every DAC write in order: index, r, g, b
        orig.ints[0x10] = self.int10
        orig.ints[0x1A] = self.int1a
        orig.ins[0x3DA] = lambda uc: 0x08           # always in the vertical retrace (see gbdiff)
        for port in (0x3CE, 0x3C4, 0x3B4, 0x3B5, 0x3B8, 0x3BF):
            orig.outs[port] = lambda uc, value: None  # graphics/sequencer/Hercules registers

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
            elif 0x0D <= mode <= 0x13:
                uc.mem_write(0xA0000, bytes(0x10000))
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
            uc.reg_write(UC_X86_REG_AX, (ax & 0xFF00) | 0x1A)
            uc.reg_write(UC_X86_REG_BX, 0x0008)
        elif ax == 0x1010:
            self.dac_write(bx & 0xFF, dx >> 8, cx >> 8, cx)
        elif ax == 0x1012:
            es = uc.reg_read(UC_X86_REG_ES)
            table = bytes(uc.mem_read((es << 4) + dx, 3 * cx))
            for k in range(cx):
                self.dac_write((bx + k) & 0xFF, *table[3 * k:3 * k + 3])
        elif ax == 0x1002:
            pass                                    # EGA attribute palette: not modelled (bios.cpp)
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
