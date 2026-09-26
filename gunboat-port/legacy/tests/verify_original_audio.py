"""Compare native speaker effect state/tuning to Gunboat's original instructions.

Unicorn is a development oracle only. The native game never imports or uses it.
Build original_audio_trace first; pass its path as argv[1] if it is elsewhere.
"""
from pathlib import Path
import csv
import json
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN
from unicorn.x86_const import *

ROOT = Path(__file__).resolve().parents[3]
IMAGE = ROOT / 'reverse_engineering/out/gunboat_unpacked_image.bin'
NATIVE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'gunboat-port/out/original_audio_trace.exe'
DS = 0x2b730


class Oracle:
    def __init__(self):
        self.uc = u = Uc(UC_ARCH_X86, UC_MODE_16)
        u.mem_map(0, 0x100000)
        u.mem_write(0x10000, IMAGE.read_bytes())
        self.port61 = self.divisor = self.low = self.count = 0
        self.ports = []
        u.hook_add(UC_HOOK_INSN, self.out, None, 1, 0, UC_X86_INS_OUT)
        u.hook_add(UC_HOOK_INSN, self.inp, None, 1, 0, UC_X86_INS_IN)
        self.call(0x136d0)
        self.byte(0xda46, 1)

    def out(self, uc, port, size, value, _):
        self.ports.append((port, value))
        if port == 0x61:
            self.port61 = value
        if port == 0x42:
            if self.count % 2 == 0:
                self.low = value
            else:
                self.divisor = self.low | (value << 8)
            self.count += 1

    def inp(self, uc, port, size, _):
        return self.port61 if port == 0x61 else 0

    def byte(self, p, v):
        self.uc.mem_write(DS+p, bytes([v]))

    def mute(self, value):
        self.uc.mem_write(DS+0x7e, struct.pack('<H', int(value)))

    def word(self, p):
        return struct.unpack('<H', self.uc.mem_read(DS+p, 2))[0]

    def call(self, offset, far=False, ax=0, cs=0x22ed):
        u = self.uc
        for reg, value in [(UC_X86_REG_CS, cs), (UC_X86_REG_DS, 0x2b73),
                           (UC_X86_REG_SS, 0xa000), (UC_X86_REG_SP, 0xff00),
                           (UC_X86_REG_AX, ax)]:
            u.reg_write(reg, value)
        u.mem_write(0xaff00, struct.pack('<HH', 0xfff0, cs))
        u.emu_start(0x10000 + offset, cs*16+0xfff0, count=100000)
        assert u.reg_read(UC_X86_REG_IP) == 0xfff0

    def play(self, effect):
        self.call(0x12ef5, far=True, ax=effect)

    def engine(self, port, starboard):
        self.byte(0xb816, port)
        self.byte(0xb817, starboard)
        self.call(0xce59, cs=0x1919)

    def tick(self):
        self.call(0x12fbb)
        fields = [self.divisor, int((self.port61 & 3) == 3)]
        fields += [self.word(p) for p in (0xdaf0, 0xdaf2, 0xdad2, 0xdae4, 0xdade, 0xdb16, 0xda48)]
        fields += list(self.uc.mem_read(DS+0xda48, 0xdb1e-0xda48))
        return fields


def main():
    cases, checked = [], 0
    for throttle in [None, (0, 0), (20, 20), (50, 60), (99, 99), (128, 128), (255, 0)]:
        for effect in range(13):
            args = [str(NATIVE), str(IMAGE), str(effect), '700']
            oracle = Oracle()
            if throttle:
                args += list(map(str, throttle))
                oracle.engine(*throttle)
            oracle.play(effect)
            native = list(csv.reader(subprocess.check_output(args, text=True).splitlines()))
            for tick, row in enumerate(native):
                expected = oracle.tick()
                actual = list(map(int, row))
                if expected != actual:
                    mismatch = [(index, a, b) for index, (a,b) in enumerate(zip(expected, actual)) if a != b]
                    raise AssertionError((effect, throttle, tick, mismatch[:20]))
                checked += 1
            cases.append({'effect': effect, 'throttles': throttle, 'ticks': len(native)})
    oracle = Oracle()
    native = list(csv.reader(subprocess.check_output([str(NATIVE), str(IMAGE), '-1', '5000'], text=True).splitlines()))
    for tick, row in enumerate(native):
        if tick % 11 == 0:
            oracle.engine((tick*13)&255, (tick*17)&255)
        if tick % 47 == 0:
            oracle.play((tick//47)%13)
        oracle.mute(tick % 149 >= 137)
        expected, actual = oracle.tick(), list(map(int, row))
        if expected != actual:
            mismatch = [(index, a, b) for index, (a,b) in enumerate(zip(expected, actual)) if a != b]
            raise AssertionError(('interrupt/mute/engine scenario', tick, mismatch[:20]))
        checked += 1
    cases.append({'effect': 'interrupt/mute/engine scenario', 'ticks': len(native)})
    report = {'cases': len(cases), 'ticks_compared': checked, 'state_bytes_per_tick': 214,
              'result': 'exact', 'device': 'PC speaker', 'cases_detail': cases}
    path = ROOT / 'gunboat-port/out/original-audio-verification.json'
    path.write_text(json.dumps(report, indent=2))
    print(json.dumps({k:v for k,v in report.items() if k != 'cases_detail'}))


if __name__ == '__main__':
    main()
