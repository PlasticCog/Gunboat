"""Differential testing: the original GB.EXE code in Unicorn against the port's C++, on the same memory.

Both sides get the same real-mode memory, 1 MB + 64 KB, in the port's layout (mem.hpp): the relocated
load image at 1000:0000, DGROUP at 2B73h, VGA at A000:0000. The original function runs in Unicorn until
it returns to the caller; the port's function runs through bridge.cpp (gb_difftest.dll). Then the whole
memory is compared, except the stack bytes below the test's start SP that the original used (the port
has no emulated stack), and so are the registers the function returns.

Unicorn is a development tool only; the game never links it.

    h = Harness()
    m = h.fresh_memory()
    randomize(m, DS_BASE, 0xF640, rng)
    h.check('boat_move', m)                      # raises Mismatch with a readable report
    h.check('heading_vector', m, regs={'ax': 0x40}, outputs=['ax'])
"""
import bisect
import csv
import ctypes
import json
import os
import pathlib
import shutil
import struct
import subprocess

import biosmodel
import dosmodel

from unicorn import (Uc, UcError, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_INSN,
                     UC_HOOK_MEM_INVALID)
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
                               UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP,
                               UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
                               UC_X86_REG_IP, UC_X86_REG_EFLAGS, UC_X86_INS_IN, UC_X86_INS_OUT)

ROOT = pathlib.Path(__file__).resolve().parents[3]
PORT = ROOT / 'gunboat-port'
RE = ROOT / 'reverse_engineering'
UNPACKED = RE / 'out' / 'GB_unp.exe'
GAME_DIR = ROOT / 'Original DOS version'
BUILD = pathlib.Path(os.environ.get('GB_BUILD_DIR', PORT / 'build'))

MEM_SIZE = 0x110000
LOAD_SEG = 0x1000
DGROUP = 0x2B73
DS_BASE = DGROUP << 4
STACK_BOTTOM, STACK_TOP = 0xF640, 0xFF08     # DS offsets (mem.hpp)
TEST_SP = 0xFF00                              # SP when the test calls a function
FAR_RET = (0xFFFF, 0x0010)                    # far return address: linear 100000h, never code
NEAR_RET_IP = 0xFFFF                          # near return offset; see Original.call
IMAGE_SIZE = 0x2A110

REGS = ['ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp', 'es']
UC_REGS = dict(ax=UC_X86_REG_AX, bx=UC_X86_REG_BX, cx=UC_X86_REG_CX, dx=UC_X86_REG_DX,
               si=UC_X86_REG_SI, di=UC_X86_REG_DI, bp=UC_X86_REG_BP, es=UC_X86_REG_ES)


def seg_of(file_seg):
    return LOAD_SEG + file_seg


def lin(seg, off):
    return (seg << 4) + off


class Mismatch(AssertionError):
    pass


# ---------------------------------------------------------------- the image and the symbols

_image = None


def load_image():
    """The relocated load image in a zeroed MEM_SIZE buffer, from GB_unp.exe (tools/unexepack.py):
    an implementation independent of the port's C++ loader, which test_core.py compares with it."""
    global _image
    if _image is None:
        data = UNPACKED.read_bytes()
        (magic, cblp, cp, crlc, cparhdr, _minalloc, _maxalloc, _ss, _sp, _csum, _ip, _cs,
         lfarlc, _ovno) = struct.unpack_from('<14H', data, 0)
        assert magic == 0x5A4D, UNPACKED
        body = data[cparhdr * 16:]
        assert len(body) == IMAGE_SIZE and crlc == 1584, 'unexpected GB_unp.exe'
        m = bytearray(MEM_SIZE)
        base = lin(LOAD_SEG, 0)
        m[base:base + len(body)] = body
        for i in range(crlc):
            off, seg = struct.unpack_from('<HH', data, lfarlc + 4 * i)
            at = base + lin(seg, off)
            struct.pack_into('<H', m, at, (struct.unpack_from('<H', m, at)[0] + LOAD_SEG) & 0xFFFF)
        _image = bytes(m)
    return _image


def returns_far(file_seg, off, entry):
    """True if the function returns with RETF. The index marks only far-call targets as far, but
    MSC also calls far functions with PUSH CS / CALL near, so the code decides."""
    from capstone import Cs, CS_ARCH_X86, CS_MODE_16
    image = load_image()
    start = lin(seg_of(file_seg), off)
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    for insn in md.disasm(image[start:start + 0x2000], off):  # the index's end can be early
        if insn.mnemonic in ('retf', 'lret'):
            return True
        if insn.mnemonic == 'ret':
            return False
    return entry['far'] if entry else True


class Symbols:
    """symbols.csv names, the calling convention from the function index, names for DS offsets."""

    def __init__(self):
        self.funcs, self.ds = {}, []
        index = {f['start']: f for f in json.loads((RE / 'map' / 'gb_functions.json').read_text())['functions']}
        with open(RE / 'symbols.csv', newline='', encoding='utf-8') as fh:
            for r in csv.DictReader(fh):
                a = r['address']
                if r['kind'] == 'func':
                    s, o = (int(x, 16) for x in a.split(':'))
                    self.funcs[r['name']] = (s, o, returns_far(s, o, index.get(a.lower())))
                elif r['kind'] == 'global' and a.upper().startswith('DS:'):
                    self.ds.append((int(a[3:], 16), r['name']))
        self.ds.sort()
        self._ds_keys = [o for o, _ in self.ds]

    def func(self, name):
        return self.funcs[name]

    def describe(self, linear):
        """Readable name of a linear address: a DGROUP symbol + offset, or seg:off in the image."""
        if DS_BASE <= linear < DS_BASE + 0x10000:
            off = linear - DS_BASE
            i = bisect.bisect_right(self._ds_keys, off) - 1
            name = ''
            if i >= 0 and off - self.ds[i][0] < 0x400:
                name = ' %s+%X' % (self.ds[i][1], off - self.ds[i][0]) if off != self.ds[i][0] else ' ' + self.ds[i][1]
            return 'DS:%04X%s' % (off, name)
        if lin(LOAD_SEG, 0) <= linear < lin(LOAD_SEG, 0) + IMAGE_SIZE:
            return 'image %05X' % (linear - lin(LOAD_SEG, 0))
        return 'linear %06X' % linear


# ---------------------------------------------------------------- the original (Unicorn)

class Original:
    """GB.EXE in Unicorn with the port's memory layout. INT, IN and OUT stop the run unless a test
    installs a handler (ints[n](uc), ins[port](uc) -> value, outs[port](uc, value))."""

    def __init__(self):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, MEM_SIZE)
        self.ints, self.ins, self.outs = {}, {}, {}
        self.error = None
        self.uc.hook_add(UC_HOOK_INTR, self._on_int)
        self.uc.hook_add(UC_HOOK_INSN, self._on_in, None, 1, 0, UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, self._on_out, None, 1, 0, UC_X86_INS_OUT)
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self._on_bad_mem)

    def _where(self):
        return '%04X:%04X' % (self.uc.reg_read(UC_X86_REG_CS), self.uc.reg_read(UC_X86_REG_IP))

    def fail(self, msg):
        """Stops the run; call() then raises Mismatch with msg."""
        if self.error is None:
            self.error = msg
        self.uc.emu_stop()

    _fail = fail

    def stub(self, file_seg, off, fn, near=False):
        """Replaces the original function at file_seg:off by fn(args) -> None | (ax, dx or None):
        args(i) is the i-th stack word after the return address. fn runs when the function is
        entered; then the stub returns to the caller (retf, or ret for near). fn may raise
        dosmodel.ProgramExit to end the run."""
        at = lin(seg_of(file_seg), off)

        def hook(uc, address, size, _):
            ss, sp = uc.reg_read(UC_X86_REG_SS), uc.reg_read(UC_X86_REG_SP)

            def word(k):
                return struct.unpack('<H', uc.mem_read((ss << 4) + ((sp + k) & 0xFFFF), 2))[0]
            first = 2 if near else 4
            try:
                result = fn(lambda i: word(first + 2 * i))
            except dosmodel.ProgramExit as e:
                self.exit_code = e.code
                self.fail('exit(%d)' % e.code)
                return
            if result is not None:
                ax, dx = result
                uc.reg_write(UC_X86_REG_AX, ax & 0xFFFF)
                if dx is not None:
                    uc.reg_write(UC_X86_REG_DX, dx & 0xFFFF)
            ip = word(0)
            if not near:
                uc.reg_write(UC_X86_REG_CS, word(2))
            uc.reg_write(UC_X86_REG_SP, (sp + (2 if near else 4)) & 0xFFFF)
            uc.reg_write(UC_X86_REG_IP, ip)
        self.uc.hook_add(UC_HOOK_CODE, hook, None, at, at)

    def _on_int(self, uc, intno, _):
        if intno in self.ints:
            self.ints[intno](uc)
        else:
            self._fail('INT %02Xh at %s (no handler)' % (intno, self._where()))

    def _on_in(self, uc, port, size, _):
        if port in self.ins:
            return self.ins[port](uc)
        self._fail('IN %Xh at %s (no handler)' % (port, self._where()))
        return 0

    def _on_out(self, uc, port, size, value, _):
        if port in self.outs:
            self.outs[port](uc, value)
        else:
            self._fail('OUT %Xh,%X at %s (no handler)' % (port, value, self._where()))

    def _on_bad_mem(self, uc, access, address, size, value, _):
        self._fail('invalid memory access %X at %s' % (address, self._where()))
        return False

    def set_memory(self, m):
        self.uc.mem_write(0, bytes(m))

    def memory(self):
        return bytes(self.uc.mem_read(0, MEM_SIZE))

    def call(self, file_seg, off, far, regs, stack_args=(), max_insns=5_000_000):
        """Calls file_seg:off like the game does (a C function: stack_args pushed right to left;
        an assembly routine: register arguments) and runs until it returns to the caller.

        The return address is a sentinel: far calls return to FFFF:0010 (outside the image). Near
        calls must return into their own segment, so they return to offset FFFF there; if the code
        itself reaches that linear address while deeper in the stack, the run steps over it and
        goes on. Returns the register file after the return."""
        uc = self.uc
        self.error = None
        cs = seg_of(file_seg)
        for r in REGS:
            uc.reg_write(UC_REGS[r], regs.get(r, DGROUP if r == 'es' else 0))
        uc.reg_write(UC_X86_REG_DS, DGROUP)
        uc.reg_write(UC_X86_REG_SS, DGROUP)
        uc.reg_write(UC_X86_REG_EFLAGS, 0x0002)
        sp = TEST_SP
        words = list(reversed(stack_args))
        if far:
            words += [FAR_RET[0], FAR_RET[1]]
            ret = lin(*FAR_RET)
        else:
            words += [NEAR_RET_IP]
            ret = lin(cs, NEAR_RET_IP)
        for w in words:
            sp -= 2
            uc.mem_write(DS_BASE + sp, struct.pack('<H', w & 0xFFFF))
        entry_sp = sp
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_CS, cs)
        pc = lin(cs, off)
        budget = max_insns
        while True:
            try:
                uc.emu_start(pc, ret, count=budget)
            except UcError as e:
                raise Mismatch('original faulted at %s: %s' % (self._where(), e))
            if self.error:
                raise Mismatch('original: ' + self.error)
            pc = lin(uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP))
            if pc != ret:
                raise Mismatch('original did not return within %d instructions (at %s)' % (max_insns, self._where()))
            if uc.reg_read(UC_X86_REG_SP) > entry_sp:
                break
            uc.emu_start(pc, 0 if pc else 1, count=1)  # the sentinel address inside the function
            pc = lin(uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP))
        return {r: uc.reg_read(UC_REGS[r]) for r in REGS}


# ---------------------------------------------------------------- the port (C++ DLL)

class _Regs(ctypes.Structure):
    _fields_ = [(r, ctypes.c_uint16) for r in REGS]


class Port:
    """The port's core, built as gb_difftest by CMake (build it first: run_all.py does)."""

    def __init__(self, dll=None):
        path = pathlib.Path(dll) if dll else BUILD / ('gb_difftest.dll' if os.name == 'nt' else 'gb_difftest.so')
        if not path.exists():
            raise FileNotFoundError('%s is missing: build the target gb_difftest first' % path)
        self.dll = ctypes.CDLL(str(path))
        self.dll.gb_mem.restype = ctypes.c_void_p
        self.dll.gb_error.restype = ctypes.c_char_p
        self.dll.gb_call.argtypes = [ctypes.c_char_p, ctypes.POINTER(_Regs), ctypes.POINTER(ctypes.c_uint16)]
        self.dll.gb_load_exe.argtypes = [ctypes.c_char_p]
        self.dll.gb_has.argtypes = [ctypes.c_char_p]
        self.dll.gb_dac_trace_data.restype = ctypes.c_void_p
        self.ptr = self.dll.gb_mem()
        assert self.dll.gb_mem_size() == MEM_SIZE

    def error(self):
        return self.dll.gb_error().decode()

    def set_memory(self, m):
        ctypes.memmove(self.ptr, bytes(m), MEM_SIZE)

    def memory(self):
        return ctypes.string_at(self.ptr, MEM_SIZE)

    def load_exe(self, path):
        if not self.dll.gb_load_exe(str(path).encode()):
            raise Mismatch('port: ' + self.error())

    def has(self, name):
        return bool(self.dll.gb_has(name.encode()))

    def call(self, name, regs, stack_args=()):
        r = _Regs(*[regs.get(n, DGROUP if n == 'es' else 0) for n in REGS])
        args = (ctypes.c_uint16 * max(1, len(stack_args)))(*[a & 0xFFFF for a in stack_args])
        if not self.dll.gb_call(name.encode(), ctypes.byref(r), args):
            raise Mismatch('port: ' + self.error())
        return {n: getattr(r, n) for n in REGS}


# ---------------------------------------------------------------- the comparison

def randomize(m, start, length, rng):
    """Fills m[start:start+length] with random bytes."""
    m[start:start + length] = rng.randbytes(length)


def put8(m, ds_off, v):
    m[DS_BASE + ds_off] = v & 0xFF


def put16(m, ds_off, v):
    struct.pack_into('<H', m, DS_BASE + ds_off, v & 0xFFFF)


class Harness:
    def __init__(self, dll=None):
        self.sym = Symbols()
        self.orig = Original()
        self.port = Port(dll)
        self.dos = dosmodel.DosModel(self.orig, GAME_DIR)
        self.bios = biosmodel.BiosModel(self.orig)
        self.port.dll.gb_set_game_dir(str(GAME_DIR).encode())
        self.cases = {}

    def fresh_memory(self):
        """The memory at start-up: the GB.EXE image, the DOS heap (one free block, as dos_heap_init
        leaves it), BSS and everything else zero."""
        m = bytearray(load_image())
        dosmodel.heap_init(m)
        biosmodel.bios_init(m)
        return m

    TICK_KINDS = {None: 0, 'tick_counter': 1}

    def set_tick(self, tick):
        """The timer model of the next check: None (no ticks), or (poll points, kind); kind
        'tick_counter' = DS:08C0 += 1 per tick (the menu timer's count)."""
        for h in getattr(self, '_tick_hooks', []):
            self.orig.uc.hook_del(h)
        self._tick_hooks = []
        points, kind = tick if tick else ((), None)
        self.port.dll.gb_set_tick(self.TICK_KINDS[kind])

        def on_poll(uc, address, size, _):
            if kind == 'tick_counter':
                a = DS_BASE + 0x08C0
                v = struct.unpack('<H', uc.mem_read(a, 2))[0]
                uc.mem_write(a, struct.pack('<H', (v + 1) & 0xFFFF))
        for p in points:
            s, o = (int(x, 16) for x in p.split(':'))
            at = lin(seg_of(s), o)
            self._tick_hooks.append(self.orig.uc.hook_add(UC_HOOK_CODE, on_poll, None, at, at))

    def reset_files(self):
        """Closes every DOS file on both sides."""
        self.dos.reset()
        self.port.dll.gb_dos_reset()

    def open_both(self, name):
        """Opens a game file for reading on both sides; returns the (equal) handle."""
        a = self.dos.open(name, 'rb')
        b = self.port.dll.gb_dos_open(name.encode(), b'rb')
        if a != b or a < 0:
            raise Mismatch('open_both(%s): model %d, port %d' % (name, a, b))
        return a

    def files_state(self):
        model = {fh: f.tell() for fh, f in self.dos.files.items()}
        port = {fh: self.port.dll.gb_dos_tell(fh) for fh in range(dosmodel.FIRST_HANDLE, dosmodel.MAX_HANDLES)}
        return model, {fh: pos for fh, pos in port.items() if pos >= 0}

    def check(self, name, m, regs=None, stack_args=(), outputs=(), label='', keep_files=False, dac=None,
              tick=None):
        """Runs `name` on both sides from memory m and compares all memory, `outputs`, the open DOS
        files and the VGA DAC. Files are closed first unless keep_files (after open_both). Both DACs
        start as `dac` (768 bytes, default black). `tick` = (list of file_seg:off poll points,
        kind): the original gets one timer tick each time it executes a poll point, the port one per
        host_pump() (see set_tick)."""
        regs = regs or {}
        if not keep_files:
            self.reset_files()
        start_dac = bytes(dac) if dac is not None else bytes(768)
        self.bios.dac[:] = start_dac
        self.port.dll.gb_dac_write(start_dac)
        self.bios.trace = bytearray()
        self.port.dll.gb_dac_trace_start()
        self.set_tick(tick)
        if not self.port.has(name):
            raise Mismatch('%s is not in bridge.cpp' % name)
        file_seg, off, far = self.sym.func(name)
        self.orig.set_memory(m)
        want = self.orig.call(file_seg, off, far, regs, stack_args)
        mo = self.orig.memory()
        self.port.set_memory(m)
        got = self.port.call(name, regs, stack_args)
        mp = self.port.memory()
        # The original's stack below the test SP differs by design (the port has no emulated stack).
        lo, hi = DS_BASE + STACK_BOTTOM, DS_BASE + TEST_SP
        problems = []
        if mo[:lo] != mp[:lo] or mo[hi:] != mp[hi:]:
            problems.append(self._memory_diff(m, mo, mp, lo, hi))
        for r in outputs:
            if want[r] != got[r]:
                problems.append('%s: original %04X, port %04X' % (r.upper(), want[r], got[r]))
        port_trace = ctypes.string_at(self.port.dll.gb_dac_trace_data(), self.port.dll.gb_dac_trace_size())
        if bytes(self.bios.trace) != port_trace:
            k = next((i for i in range(0, min(len(port_trace), len(self.bios.trace)), 4)
                      if self.bios.trace[i:i + 4] != port_trace[i:i + 4]), min(len(port_trace), len(self.bios.trace)))
            problems.append('DAC writes differ from write %d of %d (original) / %d (port): original %s, port %s' % (
                k // 4, len(self.bios.trace) // 4, len(port_trace) // 4, bytes(self.bios.trace[k:k + 4]).hex(),
                port_trace[k:k + 4].hex()))
        port_dac = ctypes.create_string_buffer(768)
        self.port.dll.gb_dac_read(port_dac)
        if bytes(self.bios.dac) != port_dac.raw:
            diff = [i // 3 for i in range(768) if self.bios.dac[i] != port_dac.raw[i]]
            problems.append('DAC differs at colours %s: original %s, port %s' % (
                sorted(set(diff))[:8], bytes(self.bios.dac[3 * diff[0]:3 * diff[0] + 3]).hex(),
                port_dac.raw[3 * diff[0]:3 * diff[0] + 3].hex()))
        model_files, port_files = self.files_state()
        if model_files != port_files:
            problems.append('open files (handle: position): original %s, port %s' % (model_files, port_files))
        self.cases[name] = self.cases.get(name, 0) + 1
        if problems:
            inputs = ', '.join('%s=%04X' % (k, v) for k, v in regs.items())
            raise Mismatch('%s%s (%s):\n%s' % (name, ' [%s]' % label if label else '', inputs or 'no register inputs',
                                               '\n'.join(problems)))
        return want

    def _memory_diff(self, before, mo, mp, skip_lo, skip_hi, limit=12):
        diffs = [i for i in range(MEM_SIZE) if mo[i] != mp[i] and not skip_lo <= i < skip_hi]
        lines = ['%d byte(s) differ:' % len(diffs)]
        runs = []
        for i in diffs:
            if runs and i == runs[-1][1] + 1:
                runs[-1][1] = i
            else:
                runs.append([i, i])
        for a, b in runs[:limit]:
            lines.append('  %-28s was %s original %s port %s' % (
                self.sym.describe(a), before[a:b + 1].hex(), mo[a:b + 1].hex(), mp[a:b + 1].hex()))
        if len(runs) > limit:
            lines.append('  ... %d more ranges' % (len(runs) - limit))
        return '\n'.join(lines)


def build(target='gb_difftest'):
    """Builds the test library (and what it depends on) in BUILD with CMake."""
    env = dict(os.environ)
    toolchain = pathlib.Path('C:/msys64/ucrt64/bin')
    if toolchain.exists():
        env['PATH'] = str(toolchain) + os.pathsep + env['PATH']
    cmake = shutil.which('cmake', path=env['PATH'])  # Windows looks up the program in our own PATH
    if not cmake:
        raise FileNotFoundError('cmake not found (MSYS2 UCRT64: C:/msys64/ucrt64/bin)')
    if not (BUILD / 'CMakeCache.txt').exists():
        subprocess.run([cmake, '-S', str(PORT), '-B', str(BUILD), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'],
                       check=True, env=env)
    subprocess.run([cmake, '--build', str(BUILD), '--target', target], check=True, env=env)
