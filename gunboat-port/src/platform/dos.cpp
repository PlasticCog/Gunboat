// DOS and the C runtime (platform.md §4, §5): DOS memory as an MCB chain in mem[], DOS files as
// host handles, the DOS file wrappers 121b:050a-0575, and the MSC 5.1 runtime functions the game
// calls. Adapted from the Test Drive III port's platform/dos.c (MIT; THIRD_PARTY.md).
//
// DOS memory: an MCB chain inside mem[] from HEAP_BOTTOM to HEAP_TOP, kept as DOS keeps it (PORT:
// the arena starts at HEAP_BOTTOM instead of after the program's PSP block). Each block has a
// one-paragraph MCB before it: byte 0 'M' (more follow) or 'Z' (last), word 1 owner (0 = free),
// word 3 size in paragraphs.
//
// Files: DOS handles 5..19, lowest free first as DOS hands them out, are host FILE pointers (PORT:
// an OS resource, not game state). Names are DGROUP strings; a drive prefix and a directory part are
// ignored and the file is looked up case-insensitively in the game folder.
//
// The Unicorn tests run the same algorithms in Python (tests/difftest/dosmodel.py); keep the two
// in step.
#include "platform/platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "host.hpp"
#include "mem.hpp"

namespace gb {

namespace {

constexpr u16 MCB_OWNER_FREE = 0;
constexpr u16 MCB_OWNER_PROG = 8;  // PORT: any nonzero owner; DOS stores the PSP segment

u8 mcb_type(u16 m) { return mem_u8(m, 0); }
u16 mcb_owner(u16 m) { return mem_u16(m, 1); }
u16 mcb_size(u16 m) { return mem_u16(m, 3); }
void mcb_set(u16 m, u8 type, u16 owner, u16 size)
{
    mem_u8(m, 0) = type;
    mem_u16(m, 1) = owner;
    mem_u16(m, 3) = size;
}

constexpr int DOS_MAX_HANDLES = 20;
constexpr int DOS_FIRST_HANDLE = 5;
std::FILE *dos_files[DOS_MAX_HANDLES];

std::FILE *dos_file(s16 fh)
{
    if (fh < DOS_FIRST_HANDLE || fh >= DOS_MAX_HANDLES) return nullptr;
    return dos_files[fh];
}

// PORT: the file name part of a path ("A:NAME.EXT", "C:\X\NAME.EXT" -> "NAME.EXT").
const char *dos_base_name(const char *s)
{
    if (s[0] && s[1] == ':') s += 2;
    for (const char *p = s; *p; p++)
        if (*p == '\\' || *p == '/') s = p + 1;
    return s;
}

// DOS 3Fh / 40h on mem[]: the buffer is used linearly from seg:off, bounded by mem[].
u16 dos_rw(std::FILE *f, FarPtr buf, u16 n, bool write)
{
    const u32 at = lin(buf.seg, buf.off);
    if (at >= MEM_SIZE) return 0;
    if (n > MEM_SIZE - at) n = u16(MEM_SIZE - at);
    const size_t k = write ? std::fwrite(mem + at, 1, n, f) : std::fread(mem + at, 1, n, f);
    if (write) std::fflush(f);
    return u16(k);
}

// The runtime's stream table _iob: 8-byte FILE records from DS:E324 (stdin, stdout, stderr,
// stdaux, stdprn, then free ones) up to the record DS:E43C points at. +0 _ptr, +2 _cnt, +4 _base,
// +6 _flag (01h read, 02h write, 80h read/write), +7 _file (the DOS handle).
constexpr u16 IOB_FIRST = 0xE324;
constexpr u16 IOB_LASTPTR = 0xE43C;
constexpr u8 IOB_READ = 0x01, IOB_WRITE = 0x02, IOB_RW = 0x80;

std::FILE *crt_file(u16 f)
{
    if (f < IOB_FIRST || f > ds_u16(IOB_LASTPTR) || (f - IOB_FIRST) % 8 != 0) return nullptr;
    if ((ds_u8(u16(f + 6)) & 0x83) == 0) return nullptr;
    return dos_file(s16(s8(ds_u8(u16(f + 7)))));
}

// fread / fwrite on mem[]: whole items only.
u16 crt_rw(FarPtr buf, u16 size, u16 count, u16 f, bool write)
{
    std::FILE *fp = crt_file(f);
    if (!fp || size == 0 || count == 0) return 0;
    const u32 total = u32(size) * count, at = lin(buf.seg, buf.off);
    u32 done = 0;
    while (done < total) {  // in pieces, the buffer used linearly
        u32 k = std::min<u32>(total - done, 0x8000);
        if (at + done >= MEM_SIZE) break;
        k = std::min<u32>(k, MEM_SIZE - (at + done));
        const size_t r = write ? std::fwrite(mem + at + done, 1, k, fp) : std::fread(mem + at + done, 1, k, fp);
        done += u32(r);
        if (r < k) break;
    }
    if (write) std::fflush(fp);
    return u16(done / size);
}

} // namespace

const char *ds_str(u16 off) { return reinterpret_cast<const char *>(mp(DGROUP, off)); }

// ---------------------------------------------------------------- DOS memory

void dos_heap_init()
{
    std::memset(mp(HEAP_BOTTOM, 0), 0, 16);
    mcb_set(HEAP_BOTTOM, 'Z', MCB_OWNER_FREE, u16(HEAP_TOP - HEAP_BOTTOM - 1));
}

u16 dos_alloc(u16 paragraphs, u16 *err)
{
    u16 m = HEAP_BOTTOM;
    for (;;) {
        u8 t = mcb_type(m);
        if (t != 'M' && t != 'Z') {  // arena trashed
            if (err) *err = 7;
            return 0;
        }
        if (mcb_owner(m) == MCB_OWNER_FREE) {
            // join the following free blocks first, as DOS does while it searches
            while (t == 'M') {
                const u16 n = u16(m + 1 + mcb_size(m));
                const u8 nt = mcb_type(n);
                if ((nt != 'M' && nt != 'Z') || mcb_owner(n) != MCB_OWNER_FREE) break;
                mcb_set(m, nt, MCB_OWNER_FREE, u16(mcb_size(m) + 1 + mcb_size(n)));
                t = nt;
            }
            const u16 size = mcb_size(m);
            if (size >= paragraphs) {  // first fit
                if (size > paragraphs) {
                    mcb_set(u16(m + 1 + paragraphs), t, MCB_OWNER_FREE, u16(size - paragraphs - 1));
                    t = 'M';
                }
                mcb_set(m, t, MCB_OWNER_PROG, paragraphs);
                if (err) *err = 0;
                return u16(m + 1);
            }
        }
        if (t == 'Z') break;
        m = u16(m + 1 + mcb_size(m));
        if (m >= HEAP_TOP) break;
    }
    if (err) *err = 8;
    return 0;
}

u16 dos_free(u16 seg)
{
    const u16 m = u16(seg - 1);
    if (seg <= HEAP_BOTTOM || seg >= HEAP_TOP) return 9;
    const u8 t = mcb_type(m);
    if (t != 'M' && t != 'Z') return 9;
    mcb_set(m, t, MCB_OWNER_FREE, mcb_size(m));
    return 0;
}

// ---------------------------------------------------------------- DOS files

s16 dos_open(u16 name_ds, const char *mode, bool create) { return dos_open_name(ds_str(name_ds), mode, create); }

s16 dos_open_name(const char *name, const char *mode, bool create)
{
    s16 fh = DOS_FIRST_HANDLE;
    while (fh < DOS_MAX_HANDLES && dos_files[fh]) fh++;
    if (fh >= DOS_MAX_HANDLES) return -1;  // error 4: too many open files
    const char *base = dos_base_name(name);
    if (!*base) return -1;  // PORT: no file name: DOS finds nothing, POSIX fopen would open the folder
    char *path = host_game_path(base, create);
    if (!path) return -1;  // error 2: file not found
    std::FILE *f = std::fopen(path, mode);
    host_free(path);
    if (!f) return -1;
    dos_files[fh] = f;
    return fh;
}

u32 dos_lseek(s16 fh, s32 offset, u8 whence)
{
    std::FILE *f = dos_file(fh);
    if (!f || whence > 2) return 0xFFFFFFFFu;
    if (std::fseek(f, offset, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END) != 0) return 0xFFFFFFFFu;
    const long pos = std::ftell(f);
    return pos < 0 ? 0xFFFFFFFFu : u32(pos);
}

u16 dos_read_handle(s16 fh, FarPtr buf, u16 n, bool *ok)
{
    std::FILE *f = dos_file(fh);
    if (ok) *ok = f != nullptr;
    return f ? dos_rw(f, buf, n, false) : 6;  // CF set, AX = 6 (invalid handle)
}

u16 dos_write_handle(s16 fh, FarPtr buf, u16 n, bool *ok)
{
    std::FILE *f = dos_file(fh);
    if (ok) *ok = f != nullptr;
    return f ? dos_rw(f, buf, n, true) : 6;
}

bool dos_close_handle(s16 fh)
{
    std::FILE *f = dos_file(fh);
    if (!f) return false;
    std::fclose(f);
    dos_files[fh] = nullptr;
    return true;
}

s32 dos_tell(s16 fh)
{
    std::FILE *f = dos_file(fh);
    return f ? s32(std::ftell(f)) : -1;
}

void dos_close_all()
{
    for (int fh = DOS_FIRST_HANDLE; fh < DOS_MAX_HANDLES; fh++)
        if (dos_files[fh]) {
            std::fclose(dos_files[fh]);
            dos_files[fh] = nullptr;
        }
}

// 121b:050a dos_seek (platform.md §4): INT 21h 4200h to hi:lo; returns 1, or 0 on an error.
s16 dos_seek(s16 fh, u16 lo, u16 hi)
{
    return dos_lseek(fh, s32(u32(hi) << 16 | lo), 0) == 0xFFFFFFFFu ? 0 : 1;
}

// 121b:0524 dos_open_read (platform.md §4): INT 21h 3D00h; the handle, or -1.
s16 dos_open_read(u16 name_ds) { return dos_open(name_ds, "rb", false); }

// 121b:0536 dos_file_size (platform.md §4): the low word of the size; the file is rewound.
u16 dos_file_size(s16 fh)
{
    const u32 size = dos_lseek(fh, 0, 2);
    dos_lseek(fh, 0, 0);
    return u16(size);
}

// 121b:055c dos_read (platform.md §4): INT 21h 3Fh; AX = the bytes read, or the DOS error code.
u16 dos_read(FarPtr buf, u16 n, s16 fh) { return dos_read_handle(fh, buf, n, nullptr); }

// 121b:0575 dos_close (platform.md §4): INT 21h 3Eh.
void dos_close(s16 fh) { dos_close_handle(fh); }

// ---------------------------------------------------------------- MSC 5.1 runtime

// 15ee:16d0 _getstream: the first free _iob record, cleared; 0 if none.
u16 crt_getstream()
{
    const u16 last = ds_u16(IOB_LASTPTR);
    for (u16 si = IOB_FIRST;; si = u16(si + 8)) {
        if ((ds_u8(u16(si + 6)) & 0x83) == 0) {
            ds_u16(u16(si + 2)) = 0;
            ds_u8(u16(si + 6)) = 0;
            ds_u16(u16(si + 4)) = 0;
            ds_u16(si) = 0;
            ds_u8(u16(si + 7)) = 0xFF;
            return si;
        }
        if (si == last) return 0;
    }
}

// 15ee:0306 fopen (platform.md §4). PORT: a model of the runtime's result: the _iob record gets
// its flag and handle, but no buffer (the port reads and writes the file directly), and text mode
// (CR-LF translation) is not modelled; the game opens its files in binary ("rb", "wb+").
u16 crt_fopen(u16 name_ds, u16 mode_ds)
{
    const char *m = ds_str(mode_ds);
    const bool plus = std::strchr(m, '+') != nullptr;
    const char *cm;
    u8 flag;
    bool create = false;
    switch (m[0]) {
    case 'r': cm = plus ? "r+b" : "rb"; flag = plus ? IOB_RW : IOB_READ; break;
    case 'w': cm = plus ? "w+b" : "wb"; flag = plus ? IOB_RW : IOB_WRITE; create = true; break;
    case 'a': cm = plus ? "a+b" : "ab"; flag = plus ? IOB_RW : IOB_WRITE; create = true; break;
    default: return 0;
    }
    const u16 s = crt_getstream();
    if (!s) return 0;
    const s16 fh = dos_open(name_ds, cm, create);
    if (fh < 0) return 0;
    ds_u8(u16(s + 6)) = flag;
    ds_u8(u16(s + 7)) = u8(fh);
    return s;
}

// 15ee:0332 fread (medium model: the buffer is a DGROUP offset)
u16 crt_fread(u16 buf_ds, u16 size, u16 count, u16 f) { return crt_rw({buf_ds, DGROUP}, size, count, f, false); }

// 15ee:0524 fwrite
u16 crt_fwrite(u16 buf_ds, u16 size, u16 count, u16 f) { return crt_rw({buf_ds, DGROUP}, size, count, f, true); }

// 15ee:023e fclose
s16 crt_fclose(u16 f)
{
    if (!crt_file(f)) return -1;
    dos_close_handle(s16(s8(ds_u8(u16(f + 7)))));
    ds_u8(u16(f + 6)) = 0;
    ds_u8(u16(f + 7)) = 0xFF;
    return 0;
}

// 15ee:06c1 _fmalloc. PORT: each block is its own DOS block (seg:0000, (size + 15) / 16
// paragraphs) instead of the runtime's far heap with block headers and its fallback to the near
// heap; callers only use the far pointer. Sizes >= FFF1h fail as in the original.
FarPtr crt_fmalloc(u16 size)
{
    if (size >= 0xFFF1) return {};
    const u16 seg = dos_alloc(u16((u32(size) + 15) >> 4), nullptr);
    return {0, seg};
}

// 15ee:06ac _ffree
void crt_ffree(FarPtr p)
{
    if (far_is_null(p)) return;
    dos_free(p.seg);
}

// 15ee:0786 strcpy (MSC): returns dst.
u16 crt_strcpy(u16 dst_ds, u16 src_ds)
{
    u16 i = 0;
    do ds_u8(u16(dst_ds + i)) = ds_u8(u16(src_ds + i));
    while (ds_u8(u16(src_ds + i++)) != 0);
    return dst_ds;
}

// 15ee:07d4 strncmp (MSC): the length is limited by the first NUL of s1 within n (REPNE SCASB,
// the NUL included), then the strings are compared (REPE CMPSB); returns 0, -1 or 1 by the last
// bytes compared.
s16 crt_strncmp(u16 s1_ds, u16 s2_ds, u16 n)
{
    if (n == 0) return 0;
    u16 len = 0;
    while (len < n) {  // REPNE SCASB: counts up to and including the NUL
        len++;
        if (ds_u8(u16(s1_ds + len - 1)) == 0) break;
    }
    u16 i = 0;
    while (i < len) {  // REPE CMPSB
        const u8 a = ds_u8(u16(s2_ds + i)), b = ds_u8(u16(s1_ds + i));
        i++;
        if (a != b) break;
    }
    const u8 a = ds_u8(u16(s2_ds + i - 1)), b = ds_u8(u16(s1_ds + i - 1));
    if (a > b) return -1;  // CMP AL,[DI-1] / JA: NOT 0
    if (a == b) return 0;
    return 1;              // DEC CX twice, NOT
}

// 15ee:01a0 exit: flushes and closes the files, then the host ends the program.
void crt_exit(s16 code)
{
    dos_close_all();
    host_exit(code);
}

} // namespace gb
