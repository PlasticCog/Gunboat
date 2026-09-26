// The view copies of view_present (hud.md §6): each copies the parts of the 3D view (page 1) that
// show through one station's cockpit openings. The far routine takes the source and destination
// page numbers, loads their segments from page_segments into DS and ES and calls the routine of the
// video mode; the VGA routines are fixed REP MOVSW sequences on 320-byte rows.
//
// The EGA, Tandy and CGA twins are in hud/views_ega.cpp, views_tandy.cpp and views_cga.cpp.
#include "hud/hud.hpp"

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// REP MOVSW from DS:SI to ES:DI (DF = 0): word by word, both offsets wrapping in their segment.
void movsw(u16 es, u16 &di, u16 ds, u16 &si, u16 cx)
{
    for (; cx; cx--) {
        const u8 lo = mem_u8(ds, si), hi = mem_u8(ds, u16(si + 1));
        mem_u8(es, di) = lo;
        mem_u8(es, u16(di + 1)) = hi;
        si = u16(si + 2);
        di = u16(di + 2);
    }
}

// The dispatch every view_copy_N shares (0919:8a32 ...): AL = the video mode's low byte; above 0Dh
// (VGA) the VGA routine runs with ES = page_segments[dst], DS = page_segments[src]. The far routine
// keeps DS and ES but not SI: the copy's source offset at its end reaches the callers (mission_run's
// SI, caller_si), so it is returned.
using Copy = u16 (*)(u16 es, u16 ds);

// The other modes (0919:8a4c ...): 0Dh EGA, 9-0Ch Tandy, below 9 CGA.
u16 dispatch(u16 src_page, u16 dst_page, Copy vga, Copy ega, Copy tandy, Copy cga, u16)
{
    const u8 mode = u8(ds_u16(DS_video_mode));
    const u16 es = ds_u16(u16(DS_page_segments + 2 * dst_page));
    const u16 ds = ds_u16(u16(DS_page_segments + 2 * src_page));
    if (mode > 0x0D) return vga(es, ds);
    if (mode == 0x0D) return ega(es, ds);
    if (mode >= 9) return tandy(es, ds);
    return cga(es, ds);
}

} // namespace

// 0919:8a32 view_copy_1 (hud.md §6)
u16 view_copy_1(u16 src_page, u16 dst_page, u16 si) { return dispatch(src_page, dst_page, view_copy_1_vga, view_copy_1_ega, view_copy_1_tandy, view_copy_1_cga, si); }
// 0919:8ad5 view_copy_2
u16 view_copy_2(u16 src_page, u16 dst_page, u16 si) { return dispatch(src_page, dst_page, view_copy_2_vga, view_copy_2_ega, view_copy_2_tandy, view_copy_2_cga, si); }
// 0919:8b43 view_copy_3
u16 view_copy_3(u16 src_page, u16 dst_page, u16 si) { return dispatch(src_page, dst_page, view_copy_3_vga, view_copy_3_ega, view_copy_3_tandy, view_copy_3_cga, si); }
// 0919:8bf9 view_copy_4
u16 view_copy_4(u16 src_page, u16 dst_page, u16 si) { return dispatch(src_page, dst_page, view_copy_4_vga, view_copy_4_ega, view_copy_4_tandy, view_copy_4_cga, si); }
// 0919:8cd3 view_copy_5
u16 view_copy_5(u16 src_page, u16 dst_page, u16 si) { return dispatch(src_page, dst_page, view_copy_5_vga, view_copy_5_ega, view_copy_5_tandy, view_copy_5_cga, si); }
// 0919:8d45 view_copy_6
u16 view_copy_6(u16 src_page, u16 dst_page, u16 si) { return dispatch(src_page, dst_page, view_copy_6_vga, view_copy_6_ega, view_copy_6_tandy, view_copy_6_cga, si); }
// 0919:8db7 view_copy_7
u16 view_copy_7(u16 src_page, u16 dst_page, u16 si) { return dispatch(src_page, dst_page, view_copy_7_vga, view_copy_7_ega, view_copy_7_tandy, view_copy_7_cga, si); }
// 0919:8e47 view_copy_8
u16 view_copy_8(u16 src_page, u16 dst_page, u16 si) { return dispatch(src_page, dst_page, view_copy_8_vga, view_copy_8_ega, view_copy_8_tandy, view_copy_8_cga, si); }

// 0919:8a72 view_copy_1_vga: 8 rows from DS:9678 to ES:6480, each a run of BL words and, while BH
// is above 0, a run of BH words after a gap (20h bytes growing by 20h per row at the destination,
// 10h growing by 20h at the source); BL and BH drop by 8 per row (60 and 20 words at first). Then
// 64 source rows of 80 bytes from DS:5028, each copied as 5 pieces of 16 bytes to 5 successive rows
// going up from one row above ES:26D8 + 320 * row, each piece 16 bytes further right.
u16 view_copy_1_vga(u16 es, u16 ds)
{
    u16 di = 0x6480, si = 0x9678;
    u8 bl = 0x3C, bh = 0x14;
    u16 dx = 0x20, ax = 0x10;
    do {
        u16 d = di, s = si;
        movsw(es, d, ds, s, bl);
        if (s8(bh) > 0) {  // OR BH,BH / JS / JE
            d = u16(d + dx);
            s = u16(s + ax);
            movsw(es, d, ds, s, bh);
        }
        si = u16(si + 0x140);
        di = u16(di + 0x140);
        ax = u16(ax + 0x20);
        dx = u16(dx + 0x20);
        bh = u8(bh - 8);
        bl = u8(bl - 8);
    } while (!(bl & 0x80));
    di = 0x26D8;
    si = 0x5028;
    for (u8 rows = 0x40; rows; rows--) {
        for (u8 k = 5; k; k--) {
            di = u16(di - 0x140);
            movsw(es, di, ds, si, 8);
        }
        di = u16(di + 0x730);
        si = u16(si + 0xF0);
    }
    return si;
}

// 0919:8b15 view_copy_2_vga: 8 rows from DS:9628 to ES:6418, each two runs of BL words (60, then 8
// fewer per row) with a gap of 20h (destination) / 10h (source) bytes growing by 20h per row.
u16 view_copy_2_vga(u16 es, u16 ds)
{
    u16 di = 0x6418, si = 0x9628;
    u8 bl = 0x3C;
    u16 dx = 0x20, ax = 0x10;
    do {
        movsw(es, di, ds, si, bl);
        di = u16(di + dx);
        si = u16(si + ax);
        movsw(es, di, ds, si, bl);
        si = u16(si + 0x40);
        di = u16(di + 0x30);
        ax = u16(ax + 0x20);
        dx = u16(dx + 0x20);
        bl = u8(bl - 8);
    } while (!(bl & 0x80));
    return si;
}

// 0919:8b83 view_copy_3_vga: 8 rows from DS:9628 to ES:6400: a run of BL words while BL is above 0
// (20, then 8 fewer per row), a gap, a run of BH words (60, then 8 fewer); the gaps grow by 20h per
// row while BL stays positive, by 18h in the row where BL reaches -4, by 10h after. Then 64 source
// rows of 80 bytes from DS:50D8, each as 5 pieces of 16 bytes to 5 successive rows going down from
// ES:2158, each piece 16 bytes further right; the next source row goes one row further down.
u16 view_copy_3_vga(u16 es, u16 ds)
{
    u16 di = 0x6400, si = 0x9628;
    u8 bl = 0x14, bh = 0x3C;
    u16 dx = 0x20, ax = 0x10;
    do {
        u16 d = di, s = si;
        if (s8(bl) > 0) movsw(es, d, ds, s, bl);
        d = u16(d + dx);
        s = u16(s + ax);
        movsw(es, d, ds, s, bh);
        si = u16(si + 0x140);
        di = u16(di + 0x140);
        bl = u8(bl - 8);
        if (!(bl & 0x80)) {
            ax = u16(ax + 0x10);
            dx = u16(dx + 0x10);
        }
        if (bl == 0xFC) {
            ax = u16(ax + 8);
            dx = u16(dx + 8);
        }
        ax = u16(ax + 0x10);
        dx = u16(dx + 0x10);
        bh = u8(bh - 8);
    } while (!(bh & 0x80));
    di = 0x2158;
    si = 0x50D8;
    for (u8 rows = 0x40; rows; rows--) {
        for (u8 k = 5; k; k--) {
            movsw(es, di, ds, si, 8);
            di = u16(di + 0x140);
        }
        di = u16(di - 0x550);
        si = u16(si + 0xF0);
    }
    return si;
}

// 0919:8c39 view_copy_4_vga: a fixed pattern of runs from DS:8728 to ES:4620 (same offsets on both
// pages): 8, 60h, 8 and 60h words with gaps, 9 rows of 58h words, 4 rows of 48h words, a row of 4,
// 30h and 4 words, 4 rows of 30h words.
u16 view_copy_4_vga(u16 es, u16 ds)
{
    u16 di = 0x4620, si = 0x8728;
    auto gap = [&](u16 n) {
        si = u16(si + n);
        di = u16(di + n);
    };
    movsw(es, di, ds, si, 8);
    gap(0x10);
    movsw(es, di, ds, si, 0x60);
    gap(0x10);
    movsw(es, di, ds, si, 8);
    gap(0x60);
    movsw(es, di, ds, si, 0x60);
    gap(0x88);
    for (u8 dl = 9; dl; dl--) {
        movsw(es, di, ds, si, 0x58);
        gap(0x90);
    }
    gap(0x10);
    for (u8 dl = 4; dl; dl--) {
        movsw(es, di, ds, si, 0x48);
        gap(0xB0);
    }
    gap(8);
    movsw(es, di, ds, si, 4);
    gap(8);
    movsw(es, di, ds, si, 0x30);
    gap(8);
    movsw(es, di, ds, si, 4);
    gap(0xD0);
    for (u8 dl = 4; dl; dl--) {
        movsw(es, di, ds, si, 0x30);
        gap(0xE0);
    }
    return si;
}

// 0919:8e29 view_copy_head_vga: 9 rows of 68h words from DS:8880 to ES:4778 (same row stride on
// both pages), then 50h bytes further; returns DI and SI for the caller (CX is 0).
DiSi view_copy_head_vga(u16 es, u16 ds)
{
    u16 di = 0x4778, si = 0x8880;
    for (u8 dl = 9; dl; dl--) {
        movsw(es, di, ds, si, 0x68);
        si = u16(si + 0x70);
        di = u16(di + 0x70);
    }
    return {u16(di + 0x50), u16(si + 0x50)};
}

namespace {

// The body view_copy_5..8_vga share after view_copy_head_vga: `rows_a` rows of 18h words, then
// `rows_b` rows of two runs of `run` words separated by `gap` bytes (both pages alike).
u16 view_copy_tail(u16 es, u16 ds, u8 rows_a, u8 rows_b, u16 run, u16 gap)
{
    DiSi p = view_copy_head_vga(es, ds);
    for (u8 dl = rows_a; dl; dl--) {
        movsw(es, p.di, ds, p.si, 0x18);
        p.si = u16(p.si + 0x110);
        p.di = u16(p.di + 0x110);
    }
    for (u8 dl = rows_b; dl; dl--) {
        movsw(es, p.di, ds, p.si, run);
        p.si = u16(p.si + gap);
        p.di = u16(p.di + gap);
        movsw(es, p.di, ds, p.si, run);
        p.si = u16(p.si + 0x110);
        p.di = u16(p.di + 0x110);
    }
    return p.si;
}

} // namespace

// 0919:8d13 view_copy_5_vga: view_copy_head_vga, 4 rows of 18h words, 6 rows of 4 + 4 words 20h
// bytes apart.
u16 view_copy_5_vga(u16 es, u16 ds) { return view_copy_tail(es, ds, 4, 6, 4, 0x20); }

// 0919:8d85 view_copy_6_vga: view_copy_head_vga, 4 rows of 18h words, 6 rows of 8 + 8 words 10h
// bytes apart.
u16 view_copy_6_vga(u16 es, u16 ds) { return view_copy_tail(es, ds, 4, 6, 8, 0x10); }

// 0919:8df7 view_copy_7_vga: view_copy_head_vga, 6 rows of 18h words, 4 rows of 8 + 8 words.
u16 view_copy_7_vga(u16 es, u16 ds) { return view_copy_tail(es, ds, 6, 4, 8, 0x10); }

// 0919:8e87 view_copy_8_vga: view_copy_head_vga, 2 rows of 18h words, 2 rows of 8 + 8 words.
u16 view_copy_8_vga(u16 es, u16 ds) { return view_copy_tail(es, ds, 2, 2, 8, 0x10); }

} // namespace gb
