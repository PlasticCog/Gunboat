// The view copies of CGA (mode 4; Hercules too) (hud.md §6; hud.hpp, views.cpp dispatch): the VGA
// routines' copies on the CGA layout, 80 bytes a row, the even rows at 0000h and the odd rows at
// 2000h of each page: a pixel column x is byte x / 4, so the VGA runs become runs of a quarter of
// the bytes. The routines step from row to row by toggling the bank (2000h) and moving one row
// pair (50h) when they come back to the even bank (going down) or leave it (going up); some test
// the source offset and move both. REP MOVSW runs copy word by word (views.cpp). Each returns SI.
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

// Both offsets `n` further, then to the next row down, tested on SI: xor both with 2000h; if SI is
// now in the odd bank both go back 50h (they had moved a whole row pair).
void next_row_back(u16 &di, u16 &si, u16 n)
{
    si = u16(si + n);
    di = u16(di + n);
    si ^= 0x2000;
    di ^= 0x2000;
    if (si & 0x2000) {
        si = u16(si - 0x50);
        di = u16(di - 0x50);
    }
}

// Both row starts to the next row down, tested on SI: xor both with 2000h; if SI is back in the
// even bank both move one row pair (50h).
void next_row_fwd(u16 &di, u16 &si)
{
    si ^= 0x2000;
    di ^= 0x2000;
    if (!(si & 0x2000)) {
        si = u16(si + 0x50);
        di = u16(di + 0x50);
    }
}

// One offset to the next row down (xor 2000h; back in the even bank + 50h).
u16 row_down(u16 v)
{
    v ^= 0x2000;
    if (!(v & 0x2000)) v = u16(v + 0x50);
    return v;
}

// The rows of view_copy_5..8_cga after view_copy_head_cga: `rows_a` rows of 6 words, then `rows_b`
// rows of two runs of `run` words `gap` bytes apart (the rest of each 50h-byte row skipped).
u16 view_copy_tail(u16 es, u16 ds, u8 rows_a, u8 rows_b, u16 run, u16 gap)
{
    DiSi p = view_copy_head_cga(es, ds);
    for (u8 dl = rows_a; dl; dl--) {
        movsw(es, p.di, ds, p.si, 6);
        next_row_back(p.di, p.si, 0x44);
    }
    for (u8 dl = rows_b; dl; dl--) {
        movsw(es, p.di, ds, p.si, run);
        p.si = u16(p.si + gap);
        p.di = u16(p.di + gap);
        movsw(es, p.di, ds, p.si, run);
        next_row_back(p.di, p.si, 0x44);
    }
    return p.si;
}

} // namespace

// 0919:411f view_copy_1_cga: view_copy_1_vga on the CGA layout: 8 rows from DS:12DE to ES:0CA0, a
// run of BL words (0Fh, 2 fewer per row) and, while BH (5, 2 fewer) is above 0, a run of BH words
// after a gap (destination DX = 8, source AX = 4, both growing by 8 per row). Then 64 source rows
// of 20 bytes from DS:0A0A (going down), each copied as 5 pieces of 4 bytes to 5 successive rows
// going up from the row above ES:24B6, each piece 4 bytes further right (DI + DCh per source row).
u16 view_copy_1_cga(u16 es, u16 ds)
{
    u16 di = 0x0CA0, si = 0x12DE;
    u8 bl = 0x0F, bh = 5;
    u16 dx = 8, ax = 4;
    do {
        u16 d = di, s = si;
        movsw(es, d, ds, s, bl);
        if (s8(bh) > 0) {  // OR BH,BH / JS / JE
            d = u16(d + dx);
            s = u16(s + ax);
            movsw(es, d, ds, s, bh);
        }
        next_row_fwd(di, si);
        ax = u16(ax + 8);
        dx = u16(dx + 8);
        bh = u8(bh - 2);
        bl = u8(bl - 2);
    } while (!(bl & 0x80));
    di = 0x24B6;
    si = 0x0A0A;
    for (u8 rows = 0x40; rows; rows--) {
        for (u8 k = 5; k; k--) {
            di ^= 0x2000;  // the row above
            if (di & 0x2000) di = u16(di - 0x50);
            movsw(es, di, ds, si, 2);
        }
        di = u16(di + 0xDC);
        si = row_down(u16(si - 0x14));
    }
    return si;
}

// 0919:41a3 view_copy_2_cga: view_copy_2_vga on the CGA layout: 8 rows from DS:12CA to ES:0C86,
// each two runs of BL words (0Fh, 2 fewer per row) with a gap (destination DX = 8, source AX = 4,
// both growing by 8 per row).
u16 view_copy_2_cga(u16 es, u16 ds)
{
    u16 di = 0x0C86, si = 0x12CA;
    u8 bl = 0x0F;
    u16 dx = 8, ax = 4;
    do {
        movsw(es, di, ds, si, bl);
        di = u16(di + dx);
        si = u16(si + ax);
        movsw(es, di, ds, si, bl);
        si = u16(si + 0x10);
        di = u16(di + 0x0C);
        si ^= 0x2000;
        di ^= 0x2000;
        if (si & 0x2000) {
            si = u16(si - 0x50);
            di = u16(di - 0x50);
        }
        ax = u16(ax + 8);
        dx = u16(dx + 8);
        bl = u8(bl - 2);
    } while (!(bl & 0x80));
    return si;
}

// 0919:41e5 view_copy_3_cga: view_copy_3_vga on the CGA layout: 8 rows from DS:12CA to ES:0C80: a
// run of BL words while BL is above 0 (5, 2 fewer per row), a gap, a run of BH words (0Fh, 2
// fewer); the gaps grow by 8 per row while BL stays positive, by 6 in the row where BL reaches -1,
// by 4 after. Then 64 source rows of 20 bytes from DS:0A36, each as 5 pieces of 4 bytes to 5
// successive rows going down from ES:0446, each piece 4 bytes further right; the next source row
// goes one row further down (DI - B4h).
u16 view_copy_3_cga(u16 es, u16 ds)
{
    u16 di = 0x0C80, si = 0x12CA;
    u8 bl = 5, bh = 0x0F;
    u16 dx = 8, ax = 4;
    do {
        u16 d = di, s = si;
        if (s8(bl) > 0) movsw(es, d, ds, s, bl);
        d = u16(d + dx);
        s = u16(s + ax);
        movsw(es, d, ds, s, bh);
        next_row_fwd(di, si);
        bl = u8(bl - 2);
        if (!(bl & 0x80)) {
            ax = u16(ax + 4);
            dx = u16(dx + 4);
        }
        if (bl == 0xFF) {
            ax = u16(ax + 2);
            dx = u16(dx + 2);
        }
        ax = u16(ax + 4);
        dx = u16(dx + 4);
        bh = u8(bh - 2);
    } while (!(bh & 0x80));
    di = 0x0446;
    si = 0x0A36;
    for (u8 rows = 0x40; rows; rows--) {
        for (u8 k = 5; k; k--) {
            movsw(es, di, ds, si, 2);
            di = row_down(di);
        }
        di = u16(di - 0xB4);
        si = row_down(u16(si - 0x14));
    }
    return si;
}

// 0919:427c view_copy_4_cga: view_copy_4_vga on the CGA layout, a fixed pattern of runs from DS:10EA
// to ES:08C8 (same offsets on both pages): 2, 18h and 2 words with gaps, 18h words on the next row,
// 9 rows of 16h words, 4 rows of 12h words, a row of 1, 0Ch and 1 words, 4 rows of 0Ch words.
u16 view_copy_4_cga(u16 es, u16 ds)
{
    u16 di = 0x08C8, si = 0x10EA;
    auto gap = [&](u16 n) {
        si = u16(si + n);
        di = u16(di + n);
    };
    movsw(es, di, ds, si, 2);
    gap(4);
    movsw(es, di, ds, si, 0x18);
    gap(4);
    movsw(es, di, ds, si, 2);
    gap(0x1FC8);
    movsw(es, di, ds, si, 0x18);
    gap(0x22);
    si = u16(si - 0x2000);
    di = u16(di - 0x2000);
    for (u8 dl = 9; dl; dl--) {
        movsw(es, di, ds, si, 0x16);
        next_row_back(di, si, 0x24);
    }
    gap(4);
    for (u8 dl = 4; dl; dl--) {
        movsw(es, di, ds, si, 0x12);
        next_row_back(di, si, 0x2C);
    }
    gap(2);
    movsw(es, di, ds, si, 1);
    gap(2);
    movsw(es, di, ds, si, 0x0C);
    gap(2);
    movsw(es, di, ds, si, 1);
    next_row_back(di, si, 0x34);
    for (u8 dl = 4; dl; dl--) {
        movsw(es, di, ds, si, 0x0C);
        next_row_back(di, si, 0x38);
    }
    return si;
}

// 0919:4468 view_copy_head_cga: view_copy_head_vga on the CGA layout: 9 rows of 1Ah words from
// DS:30F0 to ES:28CE (same row step on both pages), then 14h bytes further; returns DI and SI for
// the caller (CX is 0).
DiSi view_copy_head_cga(u16 es, u16 ds)
{
    u16 di = 0x28CE, si = 0x30F0;
    for (u8 dl = 9; dl; dl--) {
        movsw(es, di, ds, si, 0x1A);
        next_row_back(di, si, 0x1C);
    }
    return {u16(di + 0x14), u16(si + 0x14)};
}

// 0919:4366 view_copy_5_cga: view_copy_head_cga, 4 rows of 6 words, 6 rows of 1 + 1 words 8 bytes
// apart.
u16 view_copy_5_cga(u16 es, u16 ds) { return view_copy_tail(es, ds, 4, 6, 1, 8); }

// 0919:43bc view_copy_6_cga: view_copy_head_cga, 4 rows of 6 words, 6 rows of 2 + 2 words 4 bytes
// apart.
u16 view_copy_6_cga(u16 es, u16 ds) { return view_copy_tail(es, ds, 4, 6, 2, 4); }

// 0919:4412 view_copy_7_cga: view_copy_head_cga, 6 rows of 6 words, 4 rows of 2 + 2 words.
u16 view_copy_7_cga(u16 es, u16 ds) { return view_copy_tail(es, ds, 6, 4, 2, 4); }

// 0919:449a view_copy_8_cga: view_copy_head_cga, 2 rows of 6 words, 2 rows of 2 + 2 words.
u16 view_copy_8_cga(u16 es, u16 ds) { return view_copy_tail(es, ds, 2, 2, 2, 4); }

} // namespace gb
