"""Disassembler for Ad Lib Inc.'s sound driver ADLIB.COM (V1.51), the INT 65h driver GB.EXE uses.

    python reverse_engineering/tools/adlib_dis.py [ADLIB.COM] [--all | --fn 1ee7 ...] [--data]

Reads the user's ADLIB.COM (default: "Game/ADLIB.COM"); never copies it. A .COM runs
at CS:0100; addresses below are CS offsets (file offset + 100h). The driver's data segment is
DS = CS + 25Ch (DS:0000 = CS:25C0), so `[x]` in a C function is DS:x. Recursive descent from the
INT 65h dispatch table (CS:0213, 24 entries; argument word counts at CS:0243) and the handler;
prints each reached function with its callers, names from NAMES (spec adlib_driver.md), DS
references named from DATA. --data dumps the initialised data segment.
"""
import pathlib
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_16
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT = ROOT / 'Game' / 'ADLIB.COM'
ORG = 0x100
DATA_PARA = 0x25C                   # DS = CS + 25Ch
DATA_BASE = DATA_PARA * 16          # DS:0000 = CS:25C0
TABLE, COUNTS, NFUNC = 0x213, 0x243, 24

# Names: spec/adlib_driver.md. The INT 65h function numbers are in brackets.
NAMES = {
    # start-up and installation (run once by DOS; not resident code paths)
    0x0100: 'com_start', 0x0273: 'install_int65', 0x0298: 'driver_installed', 0x02C0: 'keep_resident',
    0x02CE: 'exit', 0x0371: 'main', 0x04ED: 'parse_hex', 0x08AA: 'driver_setup', 0x20CF: 'SoundColdInit',
    0x2376: 'puts', 0x2538: 'putchar', 0x25A8: 'bdos',
    # resident assembly
    0x02EF: 'int65_handler', 0x0586: 'pit_set_ch0', 0x0595: 'clock_set_counters', 0x05B2: 'clock_set_rate',
    0x05D4: 'clock_install', 0x060E: 'clock_remove', 0x0621: 'clock_isr', 0x06CE: 'SndOutput',
    0x23BC: 'lcmp', 0x23D9: 'ldiv', 0x2481: 'lmul', 0x24AE: 'inp',
    # INT 65h functions used by GB.EXE and their callees
    0x08DC: 'fn00_Init', 0x0991: 'fn06_SetMode', 0x0C23: 'fn0A_SetValue', 0x1ee7: 'fn13_NoteOn',
    0x1FBF: 'fn14_NoteOff', 0x223A: 'fn15_SetVoiceTimbre',
    0x070A: 'init_event_pool', 0x074E: 'clear_voice_flags', 0x0773: 'init_voice_state', 0x079C: 'clear_event_ptrs',
    0x083E: 'set_tempo', 0x1212: 'event_slot_a', 0x129E: 'event_slot_b', 0x1491: 'init_event_queue',
    0x160F: 'set_clock_rate', 0x165A: 'SetPitchRange', 0x167E: 'SetWaveSel', 0x16B5: 'CalcPremFNum',
    0x1756: 'SetFNum', 0x17D6: 'InitFNums', 0x186F: 'InitFNumPtrs', 0x198E: 'InitSlotVolume',
    0x19B9: 'SetPercMode', 0x1B36: 'SetSlotParamValue', 0x1B60: 'SndSetPrm', 0x1BE2: 'SndSetAllPrm',
    0x1C1D: 'SndSKslLevel', 0x1C9D: 'SndSNoteSel', 0x1CC0: 'SndSFeedFm', 0x1D18: 'SndSAttDecay',
    0x1D59: 'SndSSusRelease', 0x1D9A: 'SndSAVEK', 0x1E4B: 'SndSAmVibRhythm', 0x1EA3: 'SndWaveSelect',
    0x2018: 'SetFreq', 0x20A6: 'SoundChut', 0x2100: 'SoundWarmInit', 0x21BA: 'InitSlotParams',
    0x2200: 'SetGParam', 0x2319: 'SetSlotParam', 0x0CF8: 'seq_tick',
}
# DS offsets of the driver's data segment (DS = CS + 25Ch)
DATA = {
    0x0002: 'dos_version', 0x0006: 'psp_seg', 0x0008: 'cmdline_ptr', 0x000A: 'stack_top', 0x000C: 'data_end',
    0x000E: 'free_bytes', 0x0010: 'heap_base', 0x0012: 'resident_end',
    0x031E: 'event_queue', 0x053A: 'queue_count', 0x0544: 'queue_head', 0x0546: 'queue_free', 0x0548: 'voice_state',
    0x057F: 'event_tab_a', 0x063B: 'event_pool', 0x063D: 'voice_flags', 0x06B6: 'clock_ticks', 0x06B8: 'tempo',
    0x06BA: 'fn0A_value', 0x06BC: 'seq_active', 0x06BE: 'seq_playing', 0x06C0: 'act_voice', 0x06C2: 'seq_start_lo',
    0x06C4: 'seq_start_hi', 0x06C6: 'mode', 0x06C8: 'event_types', 0x06D4: 'buffer', 0x06D6: 'buffer_nodes',
    0x06D8: 'ticks_per_beat', 0x06F6: 'genPort', 0x06F8: 'percBits', 0x06F9: 'percMasks', 0x06FE: 'voiceNote',
    0x0705: 'sd_pitch', 0x0706: 'tom_pitch', 0x0709: 'voiceKeyOn', 0x0714: 'noteDIV12', 0x0774: 'noteMOD12',
    0x07D4: 'slotRelVolume', 0x07F8: 'amDepth', 0x07F9: 'vibDepth', 0x07FA: 'noteSel', 0x07FB: 'percussion',
    0x07FC: 'pianoParamsOp0', 0x0818: 'pianoParamsOp1', 0x0834: 'paramSlot', 0x0930: 'slotVoice',
    0x0942: 'slotPerc', 0x094C: 'offsetSlot', 0x095E: 'carrierSlot', 0x0970: 'voiceSlot', 0x0982: 'fNumTbl',
    0x0BDA: 'halfToneOffset', 0x0BF0: 'fNumFreqPtr', 0x0C06: 'pitchRange', 0x0C08: 'pitchRangeStep',
    0x0C0A: 'modeWaveSel',
}


def load(path):
    return pathlib.Path(path).read_bytes()


class Dis:
    def __init__(self, code):
        self.code = code
        self.md = Cs(CS_ARCH_X86, CS_MODE_16)
        self.md.detail = True
        self.funcs = {}             # start -> set of callers
        self.insns = {}             # address -> insn
        self.owner = {}             # address -> function start
        self.walked = set()
        self.switches = {}          # jmp address -> (table, entries)

    def byte_at(self, a):
        return self.code[a - ORG]

    def word_at(self, a):
        return struct.unpack_from('<H', self.code, a - ORG)[0]

    def table(self):
        return [(i, self.word_at(TABLE + 2 * i), self.word_at(COUNTS + 2 * i)) for i in range(NFUNC)]

    def walk(self, start):
        if start in self.walked:
            return
        self.walked.add(start)
        self.funcs.setdefault(start, set())
        todo, seen = [start], set()
        while todo:
            a = todo.pop()
            while a not in seen and ORG <= a < ORG + len(self.code):
                seen.add(a)
                ins = next(self.md.disasm(self.code[a - ORG:a - ORG + 16], a), None)
                if ins is None:
                    break
                self.insns[a] = ins
                self.owner.setdefault(a, start)
                m = ins.mnemonic
                if m == 'call' and ins.operands[0].type == X86_OP_IMM:
                    t = ins.operands[0].imm & 0xFFFF
                    self.funcs.setdefault(t, set()).add(start)
                    self.pending.append(t)
                elif m.startswith('j') or m in ('loop', 'loope', 'loopne', 'jcxz'):
                    if ins.operands[0].type == X86_OP_IMM:
                        todo.append(ins.operands[0].imm & 0xFFFF)
                    elif m == 'jmp' and ins.operands[0].type == X86_OP_MEM:
                        # a C switch: CMP SI,n / JAE default / SHL SI,1 / JMP CS:[SI+table]
                        n = self.switch_size(a)
                        t = ins.operands[0].mem.disp & 0xFFFF
                        self.switches[a] = (t, n)
                        for k in range(n):
                            todo.append(self.word_at(t + 2 * k))
                    if m == 'jmp':
                        break
                if m in ('ret', 'retf', 'iret'):
                    break
                a += ins.size

    def switch_size(self, a):
        for back in range(4, 16):
            ins = next(self.md.disasm(self.code[a - back - ORG:a - ORG], a - back), None)
            if ins and ins.mnemonic == 'cmp' and ins.op_str.startswith('si, ') and ins.address + ins.size <= a:
                return ins.operands[1].imm & 0xFFFF
        raise ValueError('switch at %04x: no bound' % a)

    def run(self, entries):
        self.pending = list(entries)
        while self.pending:
            self.walk(self.pending.pop())

    def name(self, a):
        return NAMES.get(a, 'sub_%04x' % a)

    def ds_name(self, off):
        best = None
        for d, n in DATA.items():
            if d <= off and (best is None or d > best[0]):
                best = (d, n)
        if best and off - best[0] < 0x40:
            return best[1] if off == best[0] else '%s+%X' % (best[1], off - best[0])
        return None

    def show(self, start):
        callers = ', '.join(self.name(c) for c in sorted(self.funcs.get(start, ())))
        print('\n; ---- %04x %s%s' % (start, self.name(start), ('   (callers: %s)' % callers) if callers else ''))
        addrs = sorted(a for a, s in self.owner.items() if s == start)
        prev_end = None
        for a in addrs:
            ins = self.insns[a]
            if prev_end is not None and a != prev_end:
                print('        ...')
            prev_end = a + ins.size
            note = ''
            if a in self.switches:
                t, n = self.switches[a]
                note = '  ; switch: ' + ' '.join('%d:%04x' % (k, self.word_at(t + 2 * k)) for k in range(n))
            elif ins.mnemonic == 'call' and ins.operands[0].type == X86_OP_IMM:
                note = '  ; ' + self.name(ins.operands[0].imm & 0xFFFF)
            else:
                for op in ins.operands:
                    if op.type == X86_OP_MEM and op.mem.segment == 0 and op.mem.base == 0 and op.mem.index == 0:
                        n = self.ds_name(op.mem.disp & 0xFFFF)
                        if n:
                            note = '  ; ' + n
                    elif op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                        pass
            print('%04x  %-18s %s %s%s' % (a, ins.bytes.hex(), ins.mnemonic, ins.op_str, note))


def main():
    args = sys.argv[1:]
    path = DEFAULT
    if args and not args[0].startswith('--'):
        path = args.pop(0)
    d = Dis(load(path))
    if '--data' in args:
        seg = d.code[DATA_BASE - ORG:]
        for i in range(0, len(seg), 16):
            print('DS:%04x  %s' % (i, seg[i:i + 16].hex(' ')))
        return
    entries = [0x2EF] + [a for _, a, _ in d.table()]
    d.run(entries)
    print('; INT 65h functions (SI = n): CS:0213 handler table, CS:0243 argument word counts')
    for i, a, n in d.table():
        print(';   %02Xh  %04x %-24s %d word(s)' % (i, a, d.name(a), n))
    want = None
    if '--fn' in args:
        want = [int(x, 16) for x in args[args.index('--fn') + 1:]]
        d2 = Dis(d.code)
        d2.run(want)
        d = d2
    for s in sorted(d.funcs):
        d.show(s)


if __name__ == '__main__':
    main()
