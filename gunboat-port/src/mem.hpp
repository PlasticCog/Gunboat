#pragma once
// Real-mode memory model (PORTING.md "Memory model").
//
// The unpacked GB.EXE load image sits at segment LOAD_SEG = 1000h with its MZ relocations applied,
// the same layout as the Ghidra project and the Unicorn tests. GB.EXE is an MSC 5.1 medium-model
// program, so:
//   file address SSSS:OOOO   segment seg_of(SSSS) = 1000h + SSSS, offset OOOO
//   DS:xxxx (DGROUP)         segment DGROUP = seg_of(1B73h) = 2B73h
// DGROUP holds the initialised data (DS:0000-E9DF), the BSS (E9E0-F63F, zero at start) and the stack
// (F640-FF07; SS = DS, start SP = FF08, from the MZ header SS:SP 2AD7:08C8). The far blocks that
// mem_alloc_all (0000:0a4e) allocates live in the heap between HEAP_BOTTOM and VRAM_SEG
// (platform.md §5). VGA page 0 is at A000:0000 (video.md §5).
//
// Every global, table, string and heap block of the original is read and written here, at its
// original address, through the accessors below; names come from symbols.hpp:
//   ds_u16(DS_mission_clock) += 1;   seg_u16(CSSEG_sine_table, CS_sine_table + 4 * i)
#include "types.hpp"

#include <string>

namespace gb {

constexpr u32 MEM_SIZE = 0x110000;  // 1 MB plus the 64 KB that FFFF:xxxx reaches
constexpr u16 LOAD_SEG = 0x1000;
constexpr u16 seg_of(u16 file_seg) { return u16(LOAD_SEG + file_seg); }
constexpr u16 DGROUP_FILE_SEG = 0x1B73;
constexpr u16 DGROUP = seg_of(DGROUP_FILE_SEG);
constexpr u16 DS_BSS_START = 0xE9E0;
constexpr u16 DS_STACK_BOTTOM = 0xF640;
constexpr u16 DS_STACK_TOP = 0xFF08;
constexpr u16 VRAM_SEG = 0xA000;
constexpr u16 HEAP_BOTTOM = u16(DGROUP + 0x1000);  // first segment above the 64 KB DGROUP
constexpr u16 HEAP_TOP = VRAM_SEG;                  // DOS memory ends where video memory begins

extern u8 mem[MEM_SIZE];

constexpr u32 lin(u16 seg, u16 off) { return (u32(seg) << 4) + off; }
inline u8 *mp(u16 seg, u16 off) { return mem + lin(seg, off); }

// Lvalues into mem[] at any byte address. Multi-byte values are little-endian like the host, may be
// unaligned and alias the bytes, so they get types that say so (GCC/Clang; MSVC on x86/x64 accepts
// the plain types).
#if defined(__GNUC__)
typedef u16 u16_m __attribute__((aligned(1), may_alias));
typedef s16 s16_m __attribute__((aligned(1), may_alias));
typedef u32 u32_m __attribute__((aligned(1), may_alias));
typedef s32 s32_m __attribute__((aligned(1), may_alias));
#else
typedef u16 u16_m;
typedef s16 s16_m;
typedef u32 u32_m;
typedef s32 s32_m;
#endif

// Any real-mode address: far pointers, heap blocks, VGA memory, the BIOS data area.
inline u8 &mem_u8(u16 seg, u16 off) { return *mp(seg, off); }
inline s8 &mem_s8(u16 seg, u16 off) { return *reinterpret_cast<s8 *>(mp(seg, off)); }
inline u16_m &mem_u16(u16 seg, u16 off) { return *reinterpret_cast<u16_m *>(mp(seg, off)); }
inline s16_m &mem_s16(u16 seg, u16 off) { return *reinterpret_cast<s16_m *>(mp(seg, off)); }
inline u32_m &mem_u32(u16 seg, u16 off) { return *reinterpret_cast<u32_m *>(mp(seg, off)); }
inline s32_m &mem_s32(u16 seg, u16 off) { return *reinterpret_cast<s32_m *>(mp(seg, off)); }

// DGROUP globals: DS_x offsets from symbols.hpp.
inline u8 &ds_u8(u16 off) { return mem_u8(DGROUP, off); }
inline s8 &ds_s8(u16 off) { return mem_s8(DGROUP, off); }
inline u16_m &ds_u16(u16 off) { return mem_u16(DGROUP, off); }
inline s16_m &ds_s16(u16 off) { return mem_s16(DGROUP, off); }
inline u32_m &ds_u32(u16 off) { return mem_u32(DGROUP, off); }
inline s32_m &ds_s32(u16 off) { return mem_s32(DGROUP, off); }

// Data in an image segment, by its file segment: tables and variables kept in code segments
// (CSSEG_x / CS_x from symbols.hpp), and code bytes the original reads as data.
inline u8 &seg_u8(u16 file_seg, u16 off) { return mem_u8(seg_of(file_seg), off); }
inline s8 &seg_s8(u16 file_seg, u16 off) { return mem_s8(seg_of(file_seg), off); }
inline u16_m &seg_u16(u16 file_seg, u16 off) { return mem_u16(seg_of(file_seg), off); }
inline s16_m &seg_s16(u16 file_seg, u16 off) { return mem_s16(seg_of(file_seg), off); }

// 16:16 far pointer as stored in memory (offset first). Arithmetic on it is far, not huge: the
// offset wraps within the segment, as in the original.
struct FarPtr {
    u16 off = 0, seg = 0;
};
inline FarPtr mem_far(u16 seg, u16 off) { return {mem_u16(seg, off), mem_u16(seg, u16(off + 2))}; }
inline void mem_far_set(u16 seg, u16 off, FarPtr p)
{
    mem_u16(seg, off) = p.off;
    mem_u16(seg, u16(off + 2)) = p.seg;
}
inline FarPtr ds_far(u16 off) { return mem_far(DGROUP, off); }
inline void ds_far_set(u16 off, FarPtr p) { mem_far_set(DGROUP, off, p); }
inline FarPtr far_add(FarPtr p, u16 n) { return {u16(p.off + n), p.seg}; }
inline u8 &far_u8(FarPtr p, u16 i = 0) { return mem_u8(p.seg, u16(p.off + i)); }
inline u16_m &far_u16(FarPtr p, u16 i = 0) { return mem_u16(p.seg, u16(p.off + i)); }
inline u8 *far_mp(FarPtr p) { return mp(p.seg, p.off); }
inline bool far_is_null(FarPtr p) { return p.off == 0 && p.seg == 0; }

// Division as the CPU performs it. The MSC runtime hooks INT 0: a divide error (zero divisor or a
// quotient that does not fit) prints "run-time error R6003 - integer divide by 0" and exits; the
// port does the same through div_error(). A call site that must clamp instead says so (PORT).
[[noreturn]] void div_error();

inline u16 div32_16(u32 dxax, u16 divisor, u16 *rem = nullptr)
{
    if (divisor == 0 || dxax / divisor > 0xFFFF) div_error();
    if (rem) *rem = u16(dxax % divisor);
    return u16(dxax / divisor);
}
inline s16 idiv32_16(s32 dxax, s16 divisor, s16 *rem = nullptr)
{
    if (divisor == 0) div_error();
    s32 q = dxax / divisor;  // truncates toward zero, like IDIV
    if (q > 32767 || q < -32768) div_error();
    if (rem) *rem = s16(dxax % divisor);
    return s16(q);
}
inline u16 div16_8(u16 ax, u8 divisor)  // returns AX: AL = quotient, AH = remainder
{
    if (divisor == 0 || ax / divisor > 0xFF) div_error();
    return u16((ax % divisor) << 8 | (ax / divisor));
}
inline u16 idiv16_8(s16 ax, s8 divisor)  // returns AX: AL = quotient, AH = remainder
{
    if (divisor == 0) div_error();
    int q = ax / divisor;
    if (q > 127 || q < -128) div_error();
    return u16(u8(s8(ax % divisor)) << 8 | u8(s8(q)));
}
// The MSC long-arithmetic helpers (_aFlmul, _aFldiv, _aFlrem, _aFulshr ...) are plain C++
// operators on s32/u32: C++ division truncates toward zero like the runtime's.

// What mem_load_exe found.
struct ExeInfo {
    u32 image_size = 0;   // load image bytes (code + initialised data)
    u32 relocations = 0;  // MZ relocations applied
    bool packed = false;  // the file was EXEPACK-compressed (the shipped GB.EXE is)
};

// Loads GB.EXE (EXEPACK-packed or already unpacked) into a cleared mem[] at LOAD_SEG, applies the
// relocations and checks that it is the expected build. The C runtime start-up is not run
// (PORT: the port's main takes its place), so the BSS and stack stay zero.
bool mem_load_exe(const std::string &path, ExeInfo &info, std::string &err);

} // namespace gb
