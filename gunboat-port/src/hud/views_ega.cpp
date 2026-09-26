// The view copies of EGA (mode 0Dh) (hud.md §6.1; views.cpp dispatch): the VGA copies' openings in
// bytes of 8 pixels, 40 bytes per row, moved by REP MOVSB after ega_gc_setup: each read from the
// source page (DS) loads the latches with its four planes, each write to the destination page (ES)
// stores them (bit mask 0). On an EGA machine the pages are the card's memory (A000h + 200h per
// page), so the bytes go through vmem_read/vmem_write; the routine returns SI like the VGA ones.
#include "hud/hud.hpp"

#include "platform/card.hpp"
#include "render/modes.hpp"

namespace gb {

namespace {

// REP MOVSB from DS:SI to ES:DI (DF = 0), through the card.
void movsb(u16 es, u16 &di, u16 ds, u16 &si, u16 cx)
{
    for (; cx; cx--) {
        vmem_write(es, di, vmem_read(ds, si));
        si = u16(si + 1);
        di = u16(di + 1);
    }
}

} // namespace

// 0919:49a4 view_copy_1_ega (the pilot's openings; view_copy_1_vga in bytes): 8 rows from DS:12CF to
// ES:0C90, each a run of BL bytes and, while BH is above 0, a run of BH bytes after a gap (4 bytes
// growing by 4 per row at the destination, 2 growing by 4 at the source); BL and BH drop by 2 per
// row (15 and 5 at first). Then 64 source rows of 10 bytes from DS:0A05, each copied as 5 pieces of
// 2 bytes to 5 successive rows going up from one row above ES:04DB + 40 * row, each piece 2 bytes
// further right.
u16 view_copy_1_ega(u16 es, u16 ds)
{
    ega_gc_setup();
    u16 di = 0x0C90, si = 0x12CF;
    u8 bl = 0x0F, bh = 5;
    u16 dx = 4, ax = 2;
    do {
        u16 d = di, s = si;
        movsb(es, d, ds, s, bl);
        if (s8(bh) > 0) {  // OR BH,BH / JS / JE
            d = u16(d + dx);
            s = u16(s + ax);
            movsb(es, d, ds, s, bh);
        }
        si = u16(si + 0x28);
        di = u16(di + 0x28);
        ax = u16(ax + 4);
        dx = u16(dx + 4);
        bh = u8(bh - 2);
        bl = u8(bl - 2);
    } while (!(bl & 0x80));
    di = 0x04DB;
    si = 0x0A05;
    for (u8 rows = 0x40; rows; rows--) {
        for (u8 k = 5; k; k--) {
            di = u16(di - 0x28);
            movsb(es, di, ds, si, 2);
        }
        di = u16(di + 0xE6);
        si = u16(si + 0x1E);
    }
    return si;
}

// 0919:4a06 view_copy_2_ega: 8 rows from DS:12C5 to ES:0C83, each two runs of BL bytes (15, then 2
// fewer per row) with a gap of 4 (destination) / 2 (source) bytes growing by 4 per row.
u16 view_copy_2_ega(u16 es, u16 ds)
{
    ega_gc_setup();
    u16 di = 0x0C83, si = 0x12C5;
    u8 bl = 0x0F;
    u16 dx = 4, ax = 2;
    do {
        movsb(es, di, ds, si, bl);
        di = u16(di + dx);
        si = u16(si + ax);
        movsb(es, di, ds, si, bl);
        si = u16(si + 8);
        di = u16(di + 6);
        ax = u16(ax + 4);
        dx = u16(dx + 4);
        bl = u8(bl - 2);
    } while (!(bl & 0x80));
    return si;
}

// 0919:4a37 view_copy_3_ega: 8 rows from DS:12C5 to ES:0C80: a run of BL bytes while BL is above 0
// (5, then 2 fewer per row), a gap, a run of BH bytes (15, then 2 fewer); the gaps grow by 4 per row
// while BL stays positive, by 3 in the row where BL reaches -1, by 2 after. Then 64 source rows of
// 10 bytes from DS:0A1B, each as 5 pieces of 2 bytes to 5 successive rows going down from ES:042B,
// each piece 2 bytes further right; the next source row goes one row further down.
u16 view_copy_3_ega(u16 es, u16 ds)
{
    ega_gc_setup();
    u16 di = 0x0C80, si = 0x12C5;
    u8 bl = 5, bh = 0x0F;
    u16 dx = 4, ax = 2;
    do {
        u16 d = di, s = si;
        if (s8(bl) > 0) movsb(es, d, ds, s, bl);
        d = u16(d + dx);
        s = u16(s + ax);
        movsb(es, d, ds, s, bh);
        si = u16(si + 0x28);
        di = u16(di + 0x28);
        bl = u8(bl - 2);
        if (!(bl & 0x80)) {
            ax = u16(ax + 2);
            dx = u16(dx + 2);
        }
        if (bl == 0xFF) {
            ax = u16(ax + 1);
            dx = u16(dx + 1);
        }
        ax = u16(ax + 2);
        dx = u16(dx + 2);
        bh = u8(bh - 2);
    } while (!(bh & 0x80));
    di = 0x042B;
    si = 0x0A1B;
    for (u8 rows = 0x40; rows; rows--) {
        for (u8 k = 5; k; k--) {
            movsb(es, di, ds, si, 2);
            di = u16(di + 0x28);
        }
        di = u16(di - 0xAA);
        si = u16(si + 0x1E);
    }
    return si;
}

// 0919:4aac view_copy_4_ega: a fixed pattern of runs from DS:10E5 to ES:08C4 (same offsets on both
// pages): 2, 18h, 2 and 18h bytes with gaps, 9 rows of 16h bytes, 4 rows of 12h bytes, a row of 1,
// 0Ch and 1 bytes, 4 rows of 0Ch bytes.
u16 view_copy_4_ega(u16 es, u16 ds)
{
    ega_gc_setup();
    u16 di = 0x08C4, si = 0x10E5;
    auto gap = [&](u16 n) {
        si = u16(si + n);
        di = u16(di + n);
    };
    movsb(es, di, ds, si, 2);
    gap(2);
    movsb(es, di, ds, si, 0x18);
    gap(2);
    movsb(es, di, ds, si, 2);
    gap(0x0C);
    movsb(es, di, ds, si, 0x18);
    gap(0x11);
    for (u8 dl = 9; dl; dl--) {
        movsb(es, di, ds, si, 0x16);
        gap(0x12);
    }
    gap(2);
    for (u8 dl = 4; dl; dl--) {
        movsb(es, di, ds, si, 0x12);
        gap(0x16);
    }
    gap(1);
    movsb(es, di, ds, si, 1);
    gap(1);
    movsb(es, di, ds, si, 0x0C);
    gap(1);
    movsb(es, di, ds, si, 1);
    gap(0x1A);
    for (u8 dl = 4; dl; dl--) {
        movsb(es, di, ds, si, 0x0C);
        gap(0x1C);
    }
    return si;
}

// 0919:4bf7 view_copy_head_ega: ega_gc_setup, then 9 rows of 1Ah bytes from DS:1110 to ES:08EF (same
// row stride on both pages), then 0Ah bytes further; returns DI and SI for the caller (CX is 0).
DiSi view_copy_head_ega(u16 es, u16 ds)
{
    ega_gc_setup();
    u16 di = 0x08EF, si = 0x1110;
    for (u8 dl = 9; dl; dl--) {
        movsb(es, di, ds, si, 0x1A);
        si = u16(si + 0x0E);
        di = u16(di + 0x0E);
    }
    return {u16(di + 0x0A), u16(si + 0x0A)};
}

namespace {

// The body view_copy_5..8_ega share after view_copy_head_ega: `rows_a` rows of 6 bytes, then
// `rows_b` rows of two runs of `run` bytes separated by `gap` bytes (both pages alike).
u16 view_copy_tail(u16 es, u16 ds, u8 rows_a, u8 rows_b, u16 run, u16 gap)
{
    DiSi p = view_copy_head_ega(es, ds);
    for (u8 dl = rows_a; dl; dl--) {
        movsb(es, p.di, ds, p.si, 6);
        p.si = u16(p.si + 0x22);
        p.di = u16(p.di + 0x22);
    }
    for (u8 dl = rows_b; dl; dl--) {
        movsb(es, p.di, ds, p.si, run);
        p.si = u16(p.si + gap);
        p.di = u16(p.di + gap);
        movsb(es, p.di, ds, p.si, run);
        p.si = u16(p.si + 0x22);
        p.di = u16(p.di + 0x22);
    }
    return p.si;
}

} // namespace

// 0919:4b3f view_copy_5_ega: view_copy_head_ega, 4 rows of 6 bytes, 6 rows of 1 + 1 bytes 4 apart.
u16 view_copy_5_ega(u16 es, u16 ds) { return view_copy_tail(es, ds, 4, 6, 1, 4); }

// 0919:4b6d view_copy_6_ega: view_copy_head_ega, 4 rows of 6 bytes, 6 rows of 2 + 2 bytes 2 apart.
u16 view_copy_6_ega(u16 es, u16 ds) { return view_copy_tail(es, ds, 4, 6, 2, 2); }

// 0919:4b9b view_copy_7_ega: view_copy_head_ega, 6 rows of 6 bytes, 4 rows of 2 + 2 bytes.
u16 view_copy_7_ega(u16 es, u16 ds) { return view_copy_tail(es, ds, 6, 4, 2, 2); }

// 0919:4bc9 view_copy_8_ega: view_copy_head_ega, 2 rows of 6 bytes, 2 rows of 2 + 2 bytes.
u16 view_copy_8_ega(u16 es, u16 ds) { return view_copy_tail(es, ds, 2, 2, 2, 2); }

} // namespace gb
