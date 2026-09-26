// The view copies of Tandy (mode 9) (hud.md §6, views.cpp dispatch): the twins of the VGA
// routines on Tandy pages (4 bits a pixel, 160 bytes a row, four banks 2000h apart, render/mode_tandy.cpp).
// Each is a fixed sequence of REP MOVSW runs from DS:SI to ES:DI (the same outline as its VGA twin,
// in half as many bytes); the next row is + 2000h less the bytes done, and 7FFFh, + A0h back in bank
// 0. Each returns SI (the dispatch passes it to the callers, as the VGA twins).
#include "hud/hud.hpp"

#include "mem.hpp"

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

// Both offsets + n, and 7FFFh; + A0h to both when SI is back in bank 0 (the row below: n = 2000h
// less the bytes the row copied).
void next_rows(u16 &di, u16 &si, u16 n)
{
    si = u16((si + n) & 0x7FFF);
    di = u16((di + n) & 0x7FFF);
    if (!(si & 0x6000)) {
        si = u16(si + 0xA0);
        di = u16(di + 0xA0);
    }
}

// The row below DI alone (+ 2000h, and 7FFFh, + A0h back in bank 0).
u16 next_row(u16 di)
{
    di = u16((di + 0x2000) & 0x7FFF);
    if (!(di & 0x6000)) di = u16(di + 0xA0);
    return di;
}

} // namespace

// 0919:5062 view_copy_1_tandy (twin of view_copy_1_vga): 8 rows from DS:12FC to ES:0CC0, each a run
// of BL words and, while BH is above 0, a run of BH words after a gap (10h bytes growing by 10h per
// row at the destination, 8 growing by 10h at the source); BL and BH drop by 4 per row (30 and 10
// words at first). Then 64 source rows of 40 bytes from DS:0A14, each copied as 5 pieces of 8 bytes
// to 5 successive rows going up from the row above ES:646C, each piece 8 bytes further right.
u16 view_copy_1_tandy(u16 es, u16 ds)
{
    u16 di = 0x0CC0, si = 0x12FC;
    u8 bl = 0x1E, bh = 0x0A;
    u16 dx = 0x10, ax = 8;
    do {
        u16 d = di, s = si;
        movsw(es, d, ds, s, bl);
        if (s8(bh) > 0) {  // OR BH,BH / JS / JE
            d = u16(d + dx);
            s = u16(s + ax);
            movsw(es, d, ds, s, bh);
        }
        next_rows(di, si, 0x2000);
        ax = u16(ax + 0x10);
        dx = u16(dx + 0x10);
        bh = u8(bh - 4);
        bl = u8(bl - 4);
    } while (!(bl & 0x80));
    di = 0x646C;
    si = 0x0A14;
    for (u8 rows = 0x40; rows; rows--) {
        for (u8 k = 5; k; k--) {
            if (!(di & 0x6000)) {  // the row above bank 0 is bank 3 of the row group before
                di = u16(di - 0xA0);
                di ^= 0x8000;
            }
            di = u16(di - 0x2000);
            movsw(es, di, ds, si, 4);
        }
        di = u16(di + 0x78);
        if (di & 0x4000) di = u16(di + 0xA0);
        di ^= 0x4000;
        si = u16((si + 0x1FD8) & 0x7FFF);
        if (!(si & 0x6000)) si = u16(si + 0xA0);
    }
    return si;
}

// 0919:5104 view_copy_2_tandy (twin of view_copy_2_vga): 8 rows from DS:12D4 to ES:0C8C, each two
// runs of BL words (30, then 4 fewer per row) with a gap of 10h (destination) / 8 (source) bytes
// growing by 10h per row.
u16 view_copy_2_tandy(u16 es, u16 ds)
{
    u16 di = 0x0C8C, si = 0x12D4;
    u8 bl = 0x1E;
    u16 dx = 0x10, ax = 8;
    do {
        movsw(es, di, ds, si, bl);
        di = u16(di + dx);
        si = u16(si + ax);
        movsw(es, di, ds, si, bl);
        si = u16(si + 0x1F80);
        di = u16(di + 0x1F78);
        ax = u16(ax + 0x10);
        dx = u16(dx + 0x10);
        next_rows(di, si, 0);  // (the masks and the bank test only)
        bl = u8(bl - 4);
    } while (!(bl & 0x80));
    return si;
}

// 0919:514a view_copy_3_tandy (twin of view_copy_3_vga): 8 rows from DS:12D4 to ES:0C80: a run of
// BL words while BL is above 0 (10, then 4 fewer per row), a gap, a run of BH words (30, then 4
// fewer); the gaps grow by 10h per row while BL stays positive, by 0Ch in the row where BL reaches
// -2, by 8 after. Then 64 source rows of 40 bytes from DS:0A6C, each as 5 pieces of 8 bytes to 5
// successive rows going down from ES:442C, each piece 8 bytes further right; the next source row
// goes one row further down.
u16 view_copy_3_tandy(u16 es, u16 ds)
{
    u16 di = 0x0C80, si = 0x12D4;
    u8 bl = 0x0A, bh = 0x1E;
    u16 dx = 0x10, ax = 8;
    do {
        u16 d = di, s = si;
        if (s8(bl) > 0) movsw(es, d, ds, s, bl);
        d = u16(d + dx);
        s = u16(s + ax);
        movsw(es, d, ds, s, bh);
        next_rows(di, si, 0x2000);
        bl = u8(bl - 4);
        if (!(bl & 0x80)) {
            ax = u16(ax + 8);
            dx = u16(dx + 8);
        }
        if (bl == 0xFE) {
            ax = u16(ax + 4);
            dx = u16(dx + 4);
        }
        ax = u16(ax + 8);
        dx = u16(dx + 8);
        bh = u8(bh - 4);
    } while (!(bh & 0x80));
    di = 0x442C;
    si = 0x0A6C;
    for (u8 rows = 0x40; rows; rows--) {
        for (u8 k = 5; k; k--) {
            movsw(es, di, ds, si, 4);
            di = next_row(di);
        }
        di = u16(di - 0xC8);
        si = u16((si + 0x1FD8) & 0x7FFF);
        if (!(si & 0x6000)) si = u16(si + 0xA0);
    }
    return si;
}

// 0919:51f2 view_copy_4_tandy (twin of view_copy_4_vga): a fixed pattern of runs from DS:10F4 to
// ES:08D0 (same offsets on both pages): 4, 30h and 4 words with gaps, 30h words, 9 rows of 2Ch
// words, 4 rows of 24h words, a row of 2, 18h and 2 words, 4 rows of 18h words.
u16 view_copy_4_tandy(u16 es, u16 ds)
{
    u16 di = 0x08D0, si = 0x10F4;
    auto gap = [&](u16 n) {
        si = u16(si + n);
        di = u16(di + n);
    };
    movsw(es, di, ds, si, 4);
    gap(8);
    movsw(es, di, ds, si, 0x30);
    gap(8);
    movsw(es, di, ds, si, 4);
    gap(0x1F90);
    movsw(es, di, ds, si, 0x30);
    gap(0x1FA4);
    for (u8 dl = 9; dl; dl--) {
        movsw(es, di, ds, si, 0x2C);
        next_rows(di, si, 0x1FA8);
    }
    gap(8);
    for (u8 dl = 4; dl; dl--) {
        movsw(es, di, ds, si, 0x24);
        next_rows(di, si, 0x1FB8);
    }
    gap(4);
    movsw(es, di, ds, si, 2);
    gap(4);
    movsw(es, di, ds, si, 0x18);
    gap(4);
    movsw(es, di, ds, si, 2);
    next_rows(di, si, 0x1FC8);
    for (u8 dl = 4; dl; dl--) {
        movsw(es, di, ds, si, 0x18);
        next_rows(di, si, 0x1FD0);
    }
    return si;
}

// 0919:5400 view_copy_head_tandy (twin of view_copy_head_vga): 9 rows of 34h words from DS:3100 to
// ES:28DC (same row step on both pages), then 28h bytes further; returns DI and SI for the caller
// (CX is 0).
DiSi view_copy_head_tandy(u16 es, u16 ds)
{
    u16 di = 0x28DC, si = 0x3100;
    for (u8 dl = 9; dl; dl--) {
        movsw(es, di, ds, si, 0x34);
        next_rows(di, si, 0x1F98);
    }
    return {u16(di + 0x28), u16(si + 0x28)};
}

namespace {

// The body view_copy_5..8_tandy share after view_copy_head_tandy: `rows_a` rows of 0Ch words, then
// `rows_b` rows of two runs of `run` words separated by `gap` bytes (both pages alike).
u16 view_copy_tail(u16 es, u16 ds, u8 rows_a, u8 rows_b, u16 run, u16 gap)
{
    DiSi p = view_copy_head_tandy(es, ds);
    for (u8 dl = rows_a; dl; dl--) {
        movsw(es, p.di, ds, p.si, 0x0C);
        next_rows(p.di, p.si, 0x1FE8);
    }
    for (u8 dl = rows_b; dl; dl--) {
        movsw(es, p.di, ds, p.si, run);
        p.si = u16(p.si + gap);
        p.di = u16(p.di + gap);
        movsw(es, p.di, ds, p.si, run);
        next_rows(p.di, p.si, 0x1FE8);
    }
    return p.si;
}

} // namespace

// 0919:52e6 view_copy_5_tandy: view_copy_head_tandy, 4 rows of 0Ch words, 6 rows of 2 + 2 words
// 10h bytes apart.
u16 view_copy_5_tandy(u16 es, u16 ds) { return view_copy_tail(es, ds, 4, 6, 2, 0x10); }

// 0919:5344 view_copy_6_tandy: view_copy_head_tandy, 4 rows of 0Ch words, 6 rows of 4 + 4 words
// 8 bytes apart.
u16 view_copy_6_tandy(u16 es, u16 ds) { return view_copy_tail(es, ds, 4, 6, 4, 8); }

// 0919:53a2 view_copy_7_tandy: view_copy_head_tandy, 6 rows of 0Ch words, 4 rows of 4 + 4 words.
u16 view_copy_7_tandy(u16 es, u16 ds) { return view_copy_tail(es, ds, 6, 4, 4, 8); }

// 0919:5436 view_copy_8_tandy: view_copy_head_tandy, 2 rows of 0Ch words, 2 rows of 4 + 4 words.
u16 view_copy_8_tandy(u16 es, u16 ds) { return view_copy_tail(es, ds, 2, 2, 4, 8); }

} // namespace gb
