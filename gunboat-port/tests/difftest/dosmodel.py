"""The port's DOS for the original code in Unicorn (mirror of src/platform/dos.cpp).

The original calls DOS (INT 21h) and the MSC 5.1 runtime. The port replaces both: DOS by an MCB
chain in memory and host file handles, the runtime by models of its functions (crt_fopen, crt_fmalloc,
... marked PORT in dos.cpp). For whole-memory comparisons the original must see the same DOS, so this
module installs the same algorithms on the Unicorn side: an INT 21h handler, and stubs that replace
the runtime functions the port models. Keep it in step with dos.cpp.
"""
import os
import pathlib
import struct

from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
                               UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_EFLAGS, UC_X86_REG_SP,
                               UC_X86_REG_SS)

HEAP_BOTTOM, HEAP_TOP = 0x3B73, 0xA000
DGROUP = 0x2B73
DS_BASE = DGROUP << 4
MEM_SIZE = 0x110000
FIRST_HANDLE, MAX_HANDLES = 5, 20
MCB_OWNER_PROG = 8

# The runtime's _iob (dos.cpp)
IOB_FIRST, IOB_LASTPTR = 0xE324, 0xE43C
IOB_READ, IOB_WRITE, IOB_RW = 0x01, 0x02, 0x80

# Runtime functions the port replaces, file segment 15EE
RT_SEG = 0x15EE
RT_EXIT, RT_FCLOSE, RT_FOPEN, RT_FREAD, RT_FWRITE, RT_FFREE, RT_FMALLOC = \
    0x01A0, 0x023E, 0x0306, 0x0332, 0x0524, 0x06AC, 0x06C1


def heap_init(m):
    """dos_heap_init on a memory image (bytearray)."""
    base = HEAP_BOTTOM << 4
    m[base:base + 16] = bytes(16)
    m[base] = ord('Z')
    struct.pack_into('<HH', m, base + 1, 0, HEAP_TOP - HEAP_BOTTOM - 1)


class ProgramExit(Exception):
    def __init__(self, code):
        super().__init__('exit(%d)' % code)
        self.code = code


class DosModel:
    def __init__(self, orig, game_dir):
        self.orig = orig
        self.uc = orig.uc
        self.dir = pathlib.Path(game_dir)
        self.files = {}
        orig.ints[0x21] = self.int21
        for off, fn in ((RT_FMALLOC, self.rt_fmalloc), (RT_FFREE, self.rt_ffree), (RT_FOPEN, self.rt_fopen),
                        (RT_FREAD, self.rt_fread), (RT_FWRITE, self.rt_fwrite), (RT_FCLOSE, self.rt_fclose),
                        (RT_EXIT, self.rt_exit)):
            orig.stub(RT_SEG, off, fn)

    def reset(self):
        for f in self.files.values():
            f.close()
        self.files = {}

    # ---- memory access
    def rd8(self, seg, off):
        return self.uc.mem_read((seg << 4) + off, 1)[0]

    def rd16(self, seg, off):
        return struct.unpack('<H', self.uc.mem_read((seg << 4) + off, 2))[0]

    def wr8(self, seg, off, v):
        self.uc.mem_write((seg << 4) + off, bytes([v & 0xFF]))

    def wr16(self, seg, off, v):
        self.uc.mem_write((seg << 4) + off, struct.pack('<H', v & 0xFFFF))

    def ds_str(self, off):
        data = self.uc.mem_read(DS_BASE + off, 256)
        return bytes(data[:data.index(0)] if 0 in data else data).decode('latin-1')

    # ---- DOS memory (dos_alloc / dos_free)
    def mcb(self, m):
        return self.rd8(m, 0), self.rd16(m, 1), self.rd16(m, 3)

    def mcb_set(self, m, t, owner, size):
        self.wr8(m, 0, t)
        self.wr16(m, 1, owner)
        self.wr16(m, 3, size)

    def alloc(self, paragraphs):
        m = HEAP_BOTTOM
        while True:
            t, owner, size = self.mcb(m)
            if t not in (0x4D, 0x5A):
                return 0, 7
            if owner == 0:
                while t == 0x4D:
                    n = (m + 1 + size) & 0xFFFF
                    nt, nowner, nsize = self.mcb(n)
                    if nt not in (0x4D, 0x5A) or nowner != 0:
                        break
                    size = (size + 1 + nsize) & 0xFFFF
                    self.mcb_set(m, nt, 0, size)
                    t = nt
                if size >= paragraphs:
                    if size > paragraphs:
                        self.mcb_set((m + 1 + paragraphs) & 0xFFFF, t, 0, size - paragraphs - 1)
                        t = 0x4D
                    self.mcb_set(m, t, MCB_OWNER_PROG, paragraphs)
                    return (m + 1) & 0xFFFF, 0
            if t == 0x5A:
                break
            m = (m + 1 + size) & 0xFFFF
            if m >= HEAP_TOP:
                break
        return 0, 8

    def free(self, seg):
        m = (seg - 1) & 0xFFFF
        if seg <= HEAP_BOTTOM or seg >= HEAP_TOP:
            return 9
        t, owner, size = self.mcb(m)
        if t not in (0x4D, 0x5A):
            return 9
        self.mcb_set(m, t, 0, size)
        return 0

    # ---- DOS files
    def open(self, name, mode, create=False):
        fh = FIRST_HANDLE
        while fh < MAX_HANDLES and fh in self.files:
            fh += 1
        if fh >= MAX_HANDLES:
            return -1
        if len(name) > 1 and name[1] == ':':
            name = name[2:]
        name = name.replace('/', '\\').split('\\')[-1]
        path = self.dir / name
        if not path.exists():
            found = [p for p in self.dir.iterdir() if p.name.lower() == name.lower()] if self.dir.exists() else []
            if found:
                path = found[0]
            elif not create:
                return -1
        try:
            f = open(path, mode)
        except OSError:
            return -1
        self.files[fh] = f
        return fh

    def lseek(self, fh, offset, whence):
        f = self.files.get(fh)
        if f is None or whence > 2:
            return 0xFFFFFFFF
        try:
            f.seek(offset, whence)
        except (OSError, ValueError):
            return 0xFFFFFFFF
        return f.tell()

    def rw(self, fh, lin, n, write):
        f = self.files.get(fh)
        if f is None:
            return None
        if lin >= MEM_SIZE:
            return 0
        n = min(n, MEM_SIZE - lin)
        if write:
            k = f.write(bytes(self.uc.mem_read(lin, n)))
            f.flush()
            return k
        data = f.read(n)
        self.uc.mem_write(lin, data)
        return len(data)

    def close(self, fh):
        f = self.files.pop(fh, None)
        if f is None:
            return False
        f.close()
        return True

    # ---- INT 21h
    def _ret(self, ax, carry, dx=None):
        uc = self.uc
        uc.reg_write(UC_X86_REG_AX, ax & 0xFFFF)
        if dx is not None:
            uc.reg_write(UC_X86_REG_DX, dx & 0xFFFF)
        fl = uc.reg_read(UC_X86_REG_EFLAGS)
        uc.reg_write(UC_X86_REG_EFLAGS, (fl | 1) if carry else (fl & ~1))

    def int21(self, uc):
        ax, bx, cx, dx = (uc.reg_read(r) for r in (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX))
        ds, es = uc.reg_read(UC_X86_REG_DS), uc.reg_read(UC_X86_REG_ES)
        ah, al = ax >> 8, ax & 0xFF
        if ah == 0x3D:                                   # open
            name = bytes(uc.mem_read((ds << 4) + dx, 128)).split(b'\0')[0].decode('latin-1')
            mode = {0: 'rb', 1: 'r+b', 2: 'r+b'}.get(al & 3, 'rb')
            fh = self.open(name, mode)
            self._ret(fh if fh >= 0 else 2, fh < 0)
        elif ah == 0x3E:                                 # close
            ok = self.close(bx)
            self._ret(ax if ok else 6, not ok)
        elif ah in (0x3F, 0x40):                         # read / write
            k = self.rw(bx, (ds << 4) + dx, cx, ah == 0x40)
            self._ret(6 if k is None else k, k is None)
        elif ah == 0x42:                                 # seek
            offset = cx << 16 | dx
            if offset & 0x80000000:
                offset -= 0x100000000
            pos = self.lseek(bx, offset, al)
            self._ret(6 if pos == 0xFFFFFFFF else pos & 0xFFFF, pos == 0xFFFFFFFF,
                      None if pos == 0xFFFFFFFF else pos >> 16)
        elif ah == 0x48:                                 # allocate
            seg, err = self.alloc(bx)
            self._ret(seg if seg else err, seg == 0)
        elif ah == 0x49:                                 # free
            err = self.free(es)
            self._ret(err if err else ax, err != 0)
        else:
            self.orig.fail('INT 21h AX=%04X is not modelled' % ax)

    # ---- the runtime (stubs: args = the words after the far return address)
    def rt_fmalloc(self, args):
        size = args(0)
        if size >= 0xFFF1:
            return 0, 0
        seg, _ = self.alloc((size + 15) >> 4)
        return 0, seg

    def rt_ffree(self, args):
        off, seg = args(0), args(1)
        if off or seg:
            self.free(seg)
        return None

    def _getstream(self):
        last = self.rd16(DGROUP, IOB_LASTPTR)
        si = IOB_FIRST
        while True:
            if self.rd8(DGROUP, si + 6) & 0x83 == 0:
                self.wr16(DGROUP, si + 2, 0)
                self.wr8(DGROUP, si + 6, 0)
                self.wr16(DGROUP, si + 4, 0)
                self.wr16(DGROUP, si, 0)
                self.wr8(DGROUP, si + 7, 0xFF)
                return si
            if si == last:
                return 0
            si = (si + 8) & 0xFFFF

    def rt_fopen(self, args):
        m = self.ds_str(args(1))
        plus = '+' in m
        table = {'r': ('r+b' if plus else 'rb', IOB_RW if plus else IOB_READ, False),
                 'w': ('w+b' if plus else 'wb', IOB_RW if plus else IOB_WRITE, True),
                 'a': ('a+b' if plus else 'ab', IOB_RW if plus else IOB_WRITE, True)}
        if not m or m[0] not in table:
            return 0, None
        cm, flag, create = table[m[0]]
        s = self._getstream()
        if not s:
            return 0, None
        fh = self.open(self.ds_str(args(0)), cm, create)
        if fh < 0:
            return 0, None
        self.wr8(DGROUP, s + 6, flag)
        self.wr8(DGROUP, s + 7, fh)
        return s, None

    def _stream_handle(self, f):
        if f < IOB_FIRST or f > self.rd16(DGROUP, IOB_LASTPTR) or (f - IOB_FIRST) % 8:
            return None
        if self.rd8(DGROUP, f + 6) & 0x83 == 0:
            return None
        fh = self.rd8(DGROUP, f + 7)
        fh = fh - 256 if fh & 0x80 else fh
        return fh if fh in self.files else None

    def _rt_rw(self, args, write):
        off, seg, size, count, f = (args(i) for i in range(5))
        fh = self._stream_handle(f)
        if fh is None or size == 0 or count == 0:
            return 0, None
        total, at, done = size * count, (seg << 4) + off, 0
        while done < total:
            k = min(total - done, 0x8000)
            if at + done >= MEM_SIZE:
                break
            k = min(k, MEM_SIZE - (at + done))
            r = self.rw(fh, at + done, k, write)
            done += r
            if r < k:
                break
        return (done // size) & 0xFFFF, None

    def rt_fread(self, args):
        return self._rt_rw(args, False)

    def rt_fwrite(self, args):
        return self._rt_rw(args, True)

    def rt_fclose(self, args):
        f = args(0)
        fh = self._stream_handle(f)
        if fh is None:
            return 0xFFFF, None
        self.close(fh)
        self.wr8(DGROUP, f + 6, 0)
        self.wr8(DGROUP, f + 7, 0xFF)
        return 0, None

    def rt_exit(self, args):
        code = args(0)
        self.reset()
        raise ProgramExit(code)
