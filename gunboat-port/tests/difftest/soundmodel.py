"""The port's sound hardware for the original code in Unicorn (mirror of src/sound/hw.cpp).

The modelled machine has a PC speaker, and an AdLib when a test installs ADLIB.COM (sound.md §3.4):

* PIT: port 43h takes the control words 36h (channel 0, low/high byte, mode 3) and B6h (channel 2,
  the same); ports 40h and 42h take the divisor as two bytes. A completed channel-0 divisor is a
  timer event, compared with the port's host_set_timer() calls. Other control words are not
  modelled (the run fails).
* The speaker: one event (divisor, on) per completed channel-2 divisor, and one per write to port
  61h that changes bits 0-1; on = both bits set. These are compared with the port's host_speaker()
  calls (hw.cpp). Port 61h itself is the latch of biosmodel.py (h.bios.port61).
* The AdLib's OPL2 (mirror of adl_snd_output / adl_inp in sound/adlib_driver.cpp): OUT 388h latches
  the register index, OUT 389h writes the latched register: one event (register, value), compared
  with the port's host_opl_write() calls. IN 388h (the status) reads 06h (no timer flags; the OPL2's
  low bits). INT 65h goes through the interrupt vector (FLAGS, CS, IP pushed, IF and TF cleared), to
  ADLIB.COM's handler when a test installed it; a null vector fails the run (no driver).
* Absent devices: the MPU-401 (330h/331h) and the Game Blaster / Sound Blaster ports (base 200h..2F0h
  + 0..0Fh) read FFh; the Creative ports ignore writes; a write to the MPU-401 fails the run (it can
  only follow an acknowledge). The Tandy ports C0h/C1h ignore writes (parked).
* The BIOS timer handler F000:FEA5 (the INT 8 vector of biosmodel.bios_init), which the effects
  timer interrupt chains to: the tick count 0040:006C + 1 with the midnight flag, as bios_tick in
  bios.cpp, then IRET.

Use: model = SoundModel(h); model.install(); h.extensions.append(model) ... model.uninstall().
The hardware state (the channel-2 divisor, port 61h) carries over from one check to the next;
before() gives the port the original's state, after() compares the logs and the final state.
"""
import ctypes
import struct

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_CS, UC_X86_REG_EFLAGS, UC_X86_REG_IP, UC_X86_REG_SP, UC_X86_REG_SS

BIOS_INT8 = 0xFFEA5            # F000:FEA5
BDA_TICKS, BDA_MIDNIGHT = 0x46C, 0x470
MPU_PORTS = (0x330, 0x331)
CREATIVE_PORTS = range(0x200, 0x300)
TANDY_PORTS = (0xC0, 0xC1)
OPL_INDEX, OPL_DATA = 0x388, 0x389
OPL_STATUS = 0x06


class SoundModel:
    def __init__(self, h):
        self.h = h
        self.orig = h.orig
        dll = h.port.dll
        dll.gb_speaker_hw_set.argtypes = [ctypes.c_uint16, ctypes.c_uint8]
        dll.gb_speaker_hw_get.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.POINTER(ctypes.c_uint8)]
        dll.gb_speaker_log.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.c_int]
        dll.gb_timer_log.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.c_int]
        dll.gb_opl_log.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
        self.dll = dll
        self.divisor = 0            # PIT channel 2: the last completed divisor
        self.low = [0, 0, 0]        # the low byte written, per channel
        self.high_next = [False, False, False]
        self.events = []            # (divisor, on)
        self.timer = []             # channel-0 divisors
        self.creative_writes = 0
        self.opl_index = 0
        self.opl = []               # (register, value)
        self._saved = None
        self._hook = None

    # ---- installation
    def install(self):
        o = self.orig
        ports = ([0x40, 0x42, 0x43, 0x61, OPL_INDEX, OPL_DATA] + list(MPU_PORTS) + list(CREATIVE_PORTS) +
                 list(TANDY_PORTS))
        self._saved = ({p: o.ins.get(p) for p in ports}, {p: o.outs.get(p) for p in ports}, o.ints.get(0x65))
        o.outs[0x43] = self._out43
        o.outs[0x40] = lambda uc, v: self._out_counter(0, v)
        o.outs[0x42] = lambda uc, v: self._out_counter(2, v)
        o.ins[0x61] = lambda uc: self.h.bios.port61
        o.outs[0x61] = self._out61
        for p in MPU_PORTS:
            o.ins[p] = lambda uc: 0xFF
            o.outs[p] = lambda uc, v, p=p: o.fail('OUT %Xh,%02X: the modelled machine has no MPU-401' % (p, v))
        for p in CREATIVE_PORTS:                        # (201h too: base 2F8h + 8 wraps in DL, then INC DX)
            o.ins[p] = lambda uc: 0xFF
            o.outs[p] = self._out_creative
        for p in TANDY_PORTS:
            o.outs[p] = lambda uc, v: None
        o.ins[OPL_INDEX] = lambda uc: OPL_STATUS
        o.outs[OPL_INDEX] = self._out_opl_index
        o.outs[OPL_DATA] = self._out_opl_data
        o.ints[0x65] = self._int65
        self._hook = o.uc.hook_add(UC_HOOK_CODE, self._bios_int8, None, BIOS_INT8, BIOS_INT8)

    def uninstall(self):
        o = self.orig
        ins, outs, int65 = self._saved
        if int65 is None:
            o.ints.pop(0x65, None)
        else:
            o.ints[0x65] = int65
        for table, saved in ((o.ins, ins), (o.outs, outs)):
            for p, fn in saved.items():
                if fn is None:
                    table.pop(p, None)
                else:
                    table[p] = fn
        o.uc.hook_del(self._hook)
        if self in self.h.extensions:
            self.h.extensions.remove(self)

    # ---- the hardware
    def set_state(self, divisor, gate_bits):
        """Sets the speaker hardware (both sides get it at the next check)."""
        self.divisor = divisor & 0xFFFF
        self.h.bios.port61 = (self.h.bios.port61 & 0xFC) | (gate_bits & 3)

    def _on(self):
        return self.h.bios.port61 & 3 == 3

    def _out43(self, uc, v):
        if v == 0x36:
            self.high_next[0] = False
        elif v == 0xB6:
            self.high_next[2] = False
        else:
            self.orig.fail('OUT 43h,%02X: PIT control word not modelled' % v)

    def _out_counter(self, ch, v):
        if not self.high_next[ch]:
            self.low[ch] = v
            self.high_next[ch] = True
            return
        self.high_next[ch] = False
        d = self.low[ch] | v << 8
        if ch == 0:
            self.timer.append(d)
        else:
            self.divisor = d
            self.events.append((d, self._on()))

    def _out61(self, uc, v):
        old = self.h.bios.port61 & 3
        self.h.bios.port61 = v & 0xFF
        if v & 3 != old:
            self.events.append((self.divisor, self._on()))

    def _out_creative(self, uc, v):
        self.creative_writes += 1

    def _out_opl_index(self, uc, v):
        self.opl_index = v & 0xFF

    def _out_opl_data(self, uc, v):
        self.opl.append((self.opl_index, v & 0xFF))

    def _int65(self, uc):
        """INT 65h as the CPU does it: through the vector (the IP after the INT instruction)."""
        off, seg = struct.unpack('<HH', uc.mem_read(0x65 * 4, 4))
        if off == 0 and seg == 0:
            self.orig.fail('INT 65h: no handler (no AdLib driver installed)')
            return
        ss, sp = uc.reg_read(UC_X86_REG_SS), uc.reg_read(UC_X86_REG_SP)
        flags = uc.reg_read(UC_X86_REG_EFLAGS)
        sp = (sp - 6) & 0xFFFF
        uc.mem_write((ss << 4) + sp, struct.pack('<HHH', uc.reg_read(UC_X86_REG_IP), uc.reg_read(UC_X86_REG_CS),
                                                 flags & 0xFFFF))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_EFLAGS, flags & ~0x300)
        uc.reg_write(UC_X86_REG_CS, seg)
        uc.reg_write(UC_X86_REG_IP, off)

    def _bios_int8(self, uc, address, size, _):
        t = struct.unpack('<I', uc.mem_read(BDA_TICKS, 4))[0] + 1
        if t >= 0x1800B0:
            t = 0
            uc.mem_write(BDA_MIDNIGHT, b'\x01')
        uc.mem_write(BDA_TICKS, struct.pack('<I', t))
        ss, sp = uc.reg_read(UC_X86_REG_SS), uc.reg_read(UC_X86_REG_SP)
        ip, cs, flags = struct.unpack('<HHH', uc.mem_read((ss << 4) + sp, 6))
        uc.reg_write(UC_X86_REG_SP, (sp + 6) & 0xFFFF)
        uc.reg_write(UC_X86_REG_EFLAGS, flags)
        uc.reg_write(UC_X86_REG_CS, cs)
        uc.reg_write(UC_X86_REG_IP, ip)

    # ---- the harness extension
    def before(self, h):
        self.events, self.timer, self.opl = [], [], []
        self.high_next = [False, False, False]
        self.dll.gb_speaker_hw_set(self.divisor, h.bios.port61 & 3)
        self.dll.gb_speaker_clear()
        self.dll.gb_timer_clear()
        self.dll.gb_opl_clear()

    def after(self, h):
        problems = []
        buf = (ctypes.c_uint16 * 8192)()
        n = self.dll.gb_speaker_log(buf, 4096)
        port = [(buf[2 * i], bool(buf[2 * i + 1])) for i in range(min(n, 4096))]
        if port != self.events:
            k = next((i for i in range(min(len(port), len(self.events))) if port[i] != self.events[i]),
                     min(len(port), len(self.events)))
            problems.append('speaker events differ from event %d of %d (original) / %d (port): original %s, port %s'
                            % (k, len(self.events), n, self.events[k:k + 3], port[k:k + 3]))
        n = self.dll.gb_timer_log(buf, 8192)
        timer = list(buf[:min(n, 8192)])
        if timer != self.timer:
            problems.append('PIT channel 0 divisors differ: original %s, port %s'
                            % (['%04X' % d for d in self.timer], ['%04X' % d for d in timer]))
        d, g = ctypes.c_uint16(), ctypes.c_uint8()
        self.dll.gb_speaker_hw_get(ctypes.byref(d), ctypes.byref(g))
        if (d.value, g.value) != (self.divisor, h.bios.port61 & 3):
            problems.append('speaker state: original divisor %04X gate %d, port divisor %04X gate %d'
                            % (self.divisor, h.bios.port61 & 3, d.value, g.value))
        if any(self.high_next):
            problems.append('the original left a PIT divisor half written')
        n = self.dll.gb_opl_log(None, 0)
        obuf = (ctypes.c_uint8 * (2 * max(n, 1)))()
        self.dll.gb_opl_log(obuf, n)
        port = [(obuf[2 * i], obuf[2 * i + 1]) for i in range(n)]
        if port != self.opl:
            k = next((i for i in range(min(len(port), len(self.opl))) if port[i] != self.opl[i]),
                     min(len(port), len(self.opl)))
            problems.append('OPL writes differ from write %d of %d (original) / %d (port): original %s, port %s'
                            % (k, len(self.opl), n, ['%02X=%02X' % w for w in self.opl[k:k + 4]],
                               ['%02X=%02X' % w for w in port[k:k + 4]]))
        return problems
