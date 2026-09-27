// The graphics library, segments 137e-15e2 (video.md §1-§2, §7). Each primitive dispatches on the
// library mode (gfx_mode_x2) through a jump table in its own segment (one entry per mode 0-13h);
// the cases below name the handler each mode gets. Ported: the handlers of the modes Gunboat sets,
// 04h (CGA 320x200, 4 colours; the Hercules choice draws in it too), 09h (Tandy 320x200, 16
// colours), 0Dh (EGA 320x200, 16 colours; the same handlers serve 0Eh-12h) and 13h (VGA), and the
// text modes where they have their own code. The Test Drive III port's platform/gfx.c (MIT) was the
// model of the mode 13h paths; this build of the library differs (colour maps, 8 pages), so each
// routine follows Gunboat's code.
//
// PORT: the handlers of mode 6 (CGA 640x200) and of the Hercules modes 0Bh/0Ch are not ported:
// Gunboat never sets those modes (its Hercules choice runs in mode 4 and hercules_setup shows it),
// so in them the primitives only do what they do before their dispatch. Modes 8 and 0Ah (Tandy
// 160x200 and 640x200) draw nothing in the original either.
//
// Memory: CGA and Tandy pages are B800h and RAM pages in mem[]; the EGA pages are in the card's
// memory (page p at A000h + p * page bytes / 16), reached through vmem_read / vmem_write, with the
// graphics controller programmed through card_out16(3CEh).
#include "platform/gfx.hpp"

#include "host.hpp"
#include "mem.hpp"
#include "platform/card.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr u16 MODE13_X2 = 0x26;
constexpr u16 GC_PORT = 0x3CE;    // the EGA graphics controller: index, data at 3CFh
constexpr u16 CGA_SCREEN = 0xB800;  // the screen, named by the CGA / Tandy rectangle copies

// The library mode (gfx_set_mode sets 0-13h; each jump table has those entries).
u16 lib_mode() { return u16(ds_u16(DS_gfx_mode_x2) >> 1); }

u8 rol8(u8 v, unsigned n)
{
    n &= 7;
    return n ? u8(v << n | v >> (8 - n)) : v;
}
u8 ror8(u8 v, unsigned n)
{
    n &= 7;
    return n ? u8(v >> n | v << (8 - n)) : v;
}
u16 swap16(u16 v) { return u16(v << 8 | v >> 8); }

// x + y * 320 as the library computes it: the bytes of y swapped (y * 256 for y < 256), plus that
// shifted right by 2.
u16 addr13(u16 x, u16 y)
{
    u16 ax = u16(y << 8 | y >> 8);
    const u16 bx = u16(x + ax);
    ax >>= 2;
    return u16(bx + ax);
}

// CGA 2 bits a pixel, two banks (2000h) of 80-byte rows: x / 4 + 80 * (y >> 1) + 2000h * (y & 1),
// as the library computes it (XCHG AL, AH; SHR AX, 1; ADD BH, AL; XOR AL, AL; ...; SHR BX, 2 twice).
u16 addr_cga(u16 x, u16 y)
{
    u16 ax = u16(swap16(y) >> 1);
    u16 bx = u16(x + (u16(u8(ax)) << 8));  // ADD BH, AL (y & 1 in bit 7)
    ax &= 0xFF00;
    bx = u16(bx + ax);
    ax >>= 2;
    bx = u16(bx + ax);
    return u16(bx >> 2);
}

// Tandy 4 bits a pixel, four banks (2000h) of 160-byte rows: x / 2 + 160 * (y >> 2) + 2000h *
// (y & 3); y's two low bits go into the top of x * 4 through RCR.
u16 addr_tandy(u16 x, u16 y)
{
    u16 ax = y, bx = u16(x << 2);
    bx = u16(bx >> 1 | (ax & 1) << 15);  // SHR AX, 1; RCR BX, 1
    ax >>= 1;
    bx = u16(bx >> 1 | (ax & 1) << 15);
    ax >>= 1;
    bx >>= 1;
    ax = u16(swap16(ax) >> 1);
    bx = u16(bx + ax);
    ax >>= 2;
    return u16(bx + ax);
}

// EGA planar: x / 8 + y * bytes per row (MUL: the product's low word).
u16 addr_ega(u16 x, u16 y) { return u16((x >> 3) + u16(u32(y) * ds_u16(DS_mode_row_bytes))); }

u16 page_seg(u16 page) { return ds_u16(u16(DS_gfx_page_seg + 2 * page)); }

// A page in the video memory: the video segment + (page * page bytes) / 16 (the product's low word).
u16 page_seg_vram(u16 page)
{
    return u16(u16(u16(u32(page) * ds_u16(DS_mode_page_bytes)) >> 4) + ds_u16(DS_mode_video_seg));
}

// An EGA graphics controller register (OUT 3CEh, AX: the index in AL, the value in AH).
void gc_out(u8 index, u8 value) { card_out16(GC_PORT, u16(value << 8 | index)); }

// AND byte ptr es:[off], v on memory that may be the EGA's: a read (the latches) and a write.
void vmem_and(u16 seg, u16 off, u8 v) { vmem_write(seg, off, u8(vmem_read(seg, off) & v)); }

// A byte through a mask: the pattern where the mask is set, the old bits elsewhere.
u8 masked(u8 old, u8 pattern, u8 mask) { return u8((pattern & mask) | (old & u8(~mask))); }

void copy_ds(u16 dst, u16 src, u16 n)
{
    for (u16 i = 0; i < n; i++) ds_u8(u16(dst + i)) = ds_u8(u16(src + i));
}

// The 8x8 ROM font vector: DOS INT 21h AH=25h stores F000:FA6E as INT 43h.
void set_int43()
{
    mem_u16(0, 0x43 * 4) = 0xFA6E;
    mem_u16(0, 0x43 * 4 + 2) = 0xF000;
}

// A rectangle of the copy page and the draw page, rows from y1 up to y0 (1502:01af, 1522:01bc).
void copy_rect13(u16 x0, u16 x1, u16 y0, u16 y1, u16 dst_seg, u16 src_seg)
{
    u16 rows = u16(y1 + 1 - y0);
    const u16 width = u16(x1 + 1 - x0);
    u16 si = addr13(x0, y1);
    const u16 step = u16(0x140 + width);
    do {
        u16 di = si;
        for (u16 n = width; n; n--) mem_u8(dst_seg, di++) = mem_u8(src_seg, si++);
        si = u16(si - step);
    } while (--rows);
}

// The CGA / Tandy copies between the screen and the copy page (1502:0115 / 0139, 1522:0122 /
// 0146): `width` bytes at the same offset of both, `rows` rows upward; the row above by the
// interleave: 6000h (Tandy, four banks) steps back a bank or from bank 0 to bank 3 of the row
// above, any other (CGA) flips between the two banks.
void copy_rows_banked(u16 dst_seg, u16 src_seg, u16 si, u16 width, u16 rows)
{
    const u16 row = ds_u16(DS_mode_row_bytes);
    const bool four_banks = ds_u16(DS_mode_interleave) == 0x6000;
    u16 bx = rows;
    do {  // DEC BX; JNE: a count of 0 runs 65536 times
        u16 di = si;
        for (u16 n = width; n; n--) mem_u8(dst_seg, di++) = mem_u8(src_seg, si++);
        si = u16(si - width);
        if (four_banks) si = (si & 0xE000) ? u16(si - 0x2000) : u16((si | 0x6000) - row);
        else si = (si & 0x2000) ? u16(si ^ 0x2000) : u16((si - row) ^ 0x2000);
    } while (--bx);
}

// The EGA copies between the draw page and the copy page (1502:015d, 1522:016a): write mode 1 (the
// latches, all four planes at once), whole bytes x0 / 8 .. x1 / 8, rows y1 upward.
void copy_rect_ega(u16 x0, u16 x1, u16 y1, u16 rows, u16 dst_seg, u16 src_seg)
{
    gc_out(5, 1);
    const u16 row = ds_u16(DS_mode_row_bytes);
    u16 si = u16(x0 >> 3);
    const u16 width = u16((x1 >> 3) + 1 - si);
    si = u16(si + u16(u32(row) * y1));
    const u16 step = u16(row + width);
    u16 dx = rows;
    do {
        u16 di = si;
        for (u16 n = width; n; n--) vmem_write(dst_seg, di++, vmem_read(src_seg, si++));
        si = u16(si - step);
    } while (--dx);
    gc_out(5, 0);
}

// The rows of a CGA / Tandy filled rectangle (14cf:01a8) from es:di upward: the left byte through
// mask bh, dx whole bytes, the right byte through mask bl (dx < 0: both ends in one byte), all in
// the pattern; the row above by the interleave (from bank 0 to the last bank of the row above).
void fill_rows_banked(u16 es, u16 di, u16 rows, u16 dx, u8 bh, u8 bl, u8 pattern)
{
    if (s16(dx) < 0) {
        bh &= bl;
        bl = 0;
        dx = 0;
    }
    u16 cx = rows;
    do {  // LOOP: a count of 0 runs 65536 times
        mem_u8(es, di) = masked(mem_u8(es, di), pattern, bh);
        di++;
        for (u16 n = dx; n; n--) mem_u8(es, di++) = pattern;
        mem_u8(es, di) = masked(mem_u8(es, di), pattern, bl);
        di = u16(di - 1 - dx);
        if (di & 0xE000) di = u16(di - 0x2000);
        else di = u16((di | ds_u16(DS_mode_interleave)) - ds_u16(DS_mode_row_bytes));
    } while (--cx);
}

// A word of CGA / Tandy pixels through a mask (13e2): the bitmap's pixels (mask d, big-endian: the
// left pixels in the first byte) take the colour pattern (gfx_bitmap_pattern), the others stay.
void put_word_masked(u16 es, u16 bx, u16 d)
{
    const u16 ax = swap16(u16(d & ds_u16(DS_gfx_bitmap_pattern)));
    mem_u16(es, bx) = u16(ax | (swap16(u16(~d)) & mem_u16(es, bx)));
}

// Each bit k of a byte doubled into bits 2k and 2k+1 (13e2:0071, a CGA pixel per bit).
u16 double_bits(u8 b)
{
    u16 ax = u16(b << 8 | b), cx = 1, dx = 0;
    do {
        dx |= u16(ax & cx);
        ax = u16(ax << 1);
        cx = u16(cx << 1);
        dx |= u16(ax & cx);
        cx = u16(cx << 1);
    } while (cx);
    return dx;
}

// Bits 0-3 of a byte each made a nibble (13e2:0161: bit 0 -> 000Fh ... bit 3 -> F000h, a Tandy pixel
// per bit); the byte shifted right by 4 is left in *b.
u16 nibble_bits(u8 *b)
{
    u16 bx = 0x000F, dx = 0;
    for (int n = 0; n < 4; n++) {
        if (*b & 1) dx |= bx;
        *b = u8(*b >> 1);
        bx = u16(bx << 4 | bx >> 12);  // ROL BX, 1 four times
    }
    return dx;
}

// The row's bits of 16 bits of pixel matches (1432): each CF shifted out of the top, `skip` more
// bits dropped after each, into a byte (ADC; ROL; ... ROR).
u8 collect_bits(u16 ax, int count, int skip)
{
    u8 dl = 0;
    for (int n = 0; n < count; n++) {
        const u8 cf = u8(ax >> 15);
        ax = u16(ax << 1);
        dl = rol8(u8(dl + cf), 1);
        ax = u16(ax << skip);
    }
    return ror8(dl, 1);
}

// 3 bytes at es:bx (a word read big-endian, then the next byte) shifted left by cl and compared
// with the colour pattern: all bits of a pixel set where it has the colour (1432:006d / 0117).
u16 read_match(u16 es, u16 bx, u8 cl)
{
    u16 ax = swap16(mem_u16(es, bx));
    const u16 dx = u16(u16(mem_u8(es, u16(bx + 2))) << cl);
    ax = u16(ax << cl);
    ax = u16((ax & 0xFF00) | u8(u8(ax) | u8(dx >> 8)));  // OR AL, DH
    ax ^= ds_u16(DS_gfx_bitmap_pattern);
    return u16(~ax);
}

} // namespace

// 1555:0005 gfx_set_mode (video.md §1). Returns 0.
u16 gfx_set_mode(s16 mode)
{
    if (mode > 0x13) return 0;
    ds_u8(DS_gfx_mode) = u8(mode);
    u16 ax = u16(mode);
    if (mode < 0) ax = u16(s16(s8(bios_get_mode(nullptr))));  // INT 10h AH=0Fh, cbw
    bool set_bios_mode = true;
    switch (ax) {
    case 0x04: case 0x05:  // 0099
        set_int43();
        copy_ds(DS_gfx_dither, DS_dither_4, 0x40);
        copy_ds(DS_gfx_colour_map, DS_colour_map_4, 0x20);
        break;
    case 0x06:  // 00cb
        set_int43();
        [[fallthrough]];
    case 0x11:  // 00dd
        copy_ds(DS_gfx_dither, DS_dither_2, 0x40);
        copy_ds(DS_gfx_colour_map, DS_colour_map_2, 0x20);
        break;
    case 0x08: case 0x09: case 0x0A:  // 00fd
        set_int43();
        [[fallthrough]];
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x12:  // 010f
        copy_ds(DS_gfx_dither, DS_dither_16, 0x40);
        copy_ds(DS_gfx_colour_map, DS_colour_map_16, 0x20);
        break;
    case 0x0B: case 0x0C:  // 002d Hercules: BIOS data, the card, B000h-BFFFh cleared, no BIOS mode set
        for (u16 i = 0; i < 0x10; i++) mem_u8(0, u16(0x449 + i)) = ds_u8(u16(DS_herc_bda + i));
        card_out(0x3BF, 3);  // configuration: graphics and page 1 allowed
        card_out(0x3B8, 0);  // mode: video off
        for (u16 i = 0; i < 9; i++) card_out16(0x3B4, ds_u16(u16(DS_herc_crtc + 2 * i)));
        for (u32 i = 0; i < 0x8000; i++) mem_u16(0xB000, u16(2 * i)) = 0;
        card_out(0x3B8, 0x0A);  // graphics, video on, page 0
        set_int43();
        copy_ds(DS_gfx_dither, DS_dither_2, 0x40);
        copy_ds(DS_gfx_colour_map, DS_colour_map_2, 0x20);
        set_bios_mode = false;
        break;
    default:  // 0-3, 7, 13h: 012d
        break;
    }
    if (set_bios_mode) {  // 012d
        const s8 requested = ds_s8(DS_gfx_mode);
        ds_u8(DS_gfx_mode) = u8(ax);
        if (requested >= 0) bios_set_mode(u8(ax));
    }
    // 013c: the parameters of the mode
    u16 bx = u16(ds_u8(DS_gfx_mode) << 1);
    ds_u16(DS_gfx_mode_x2) = bx;
    bx = u16(bx + 2);
    ds_u8(DS_gfx_draw_page) = 0;
    ds_u8(DS_gfx_copy_page) = 0;
    ds_u8(DS_gfx_visible_page) = 0;
    ds_u8(DS_gfx_colour) = 0;
    ds_u8(DS_gfx_colour_raw) = 0;
    ds_u16(DS_gfx_e124) = 0;
    ds_u16(DS_gfx_pen_x) = 0;
    ds_u16(DS_gfx_pen_y) = 0;
    ds_u16(DS_mode_row_bytes) = ds_u16(u16(DS_mode_row_bytes + bx));
    ds_u16(DS_mode_text_cols) = ds_u16(u16(DS_mode_text_cols + bx));
    ds_u8(DS_mode_text_rows) = 0x19;
    ds_u16(DS_mode_page_bytes) = ds_u16(u16(DS_mode_page_bytes + bx));
    const u16 seg = ds_u16(u16(DS_mode_video_seg + bx));
    ds_u16(DS_mode_video_seg) = seg;
    ds_u16(DS_gfx_draw_seg) = seg;
    ds_u16(DS_gfx_copy_seg) = seg;
    ds_u16(DS_gfx_visible_seg) = seg;
    for (u16 i = 0; i < 8; i++) ds_u16(u16(DS_gfx_page_seg + 2 * i)) = seg;
    ds_u16(DS_mode_interleave) = ds_u16(u16(DS_mode_interleave + bx));
    const u16 w = ds_u16(u16(DS_mode_width + bx));
    ds_u16(DS_mode_width) = w;
    ds_u16(DS_clip_x_min) = 0;
    ds_u16(DS_clip_x_max) = u16(w - 1);
    ds_u16(DS_view_x_min) = 0;
    ds_u16(DS_view_x_max) = u16(w - 1);
    const u16 h = ds_u16(u16(DS_mode_height + bx));
    ds_u16(DS_mode_height) = h;
    ds_u16(DS_clip_y_min) = 0;
    ds_u16(DS_clip_y_max) = u16(h - 1);
    ds_u16(DS_view_y_min) = 0;
    ds_u16(DS_view_y_max) = u16(h - 1);
    const s8 m = ds_s8(DS_gfx_mode);
    if (m >= 0x0B && m <= 0x0C) {  // Hercules: page 1 at B800h; mode 0Ch has its own view box
        ds_u16(u16(DS_gfx_page_seg + 2)) = 0xB800;
        if (m == 0x0C) {
            ds_u16(DS_view_x_min) = 0x28;
            ds_u16(DS_view_x_max) = 0x2A7;
            ds_u16(DS_view_y_min) = 0x18;
            ds_u16(DS_view_y_max) = 0x143;
        }
    } else if (m >= 0x0D && m <= 0x12) {
        gc_out(1, 0x0F);  // enable set/reset on all four planes: the drawing takes gfx_set_colour's colour
        if (m > 0x10) {
            ds_u8(DS_mode_text_rows) = 0x1E;
            if (m == 0x12) {
                bios_dac_set_block(0, 0x10, {DS_dac_16, DGROUP});
                bios_palette_all({DS_colour_map_16, DGROUP});
            } else {
                bios_dac_set(1, 0x3F, 0x3F, 0x3F);
                bios_palette_all({DS_colour_map_2, DGROUP});
            }
        }
    }
    return 0;
}

// 1386:0004 gfx_detect (video.md §1): the display adapter. The BIOS display combination (INT 10h
// AX=1A00h) names a VGA or MCGA (11h-13h); else the EGA information (INT 10h AH=12h BL=10h) and the
// EGA BIOS byte 0040:0087 an EGA (0Dh; 0Fh monochrome; 10h with switches 9, an enhanced display);
// else the equipment word's monochrome display means an MDA (07h) or, if the status bit 7 of
// port 3BAh changes within 32768 reads, a Hercules card (0Bh); else FC00:0000 = 21h a Tandy (09h),
// or a CGA (04h).
s16 gfx_detect()
{
    u16 bx = 0;
    if (u8(bios_display_combination(&bx)) == 0x1A) {
        switch (u8(bx)) {
        case 0x0C: return 0x13;
        case 0x08: return 0x12;
        case 0x0B: case 0x07: return 0x11;
        default: break;
        }
    }
    // 0036: the EGA. (BH and CX before the call are what the caller left; only an EGA's answers
    // are used.)
    u16 cx = 0;
    bx = 0x0010;
    bios_ega_info(&bx, &cx);
    if (u8(bx) != 0x10) {
        const u8 info = mem_u8(0, 0x0487);
        if (!(info & 0x08)) {                   // the EGA is the active adapter
            if (info & 0x02) return 0x0F;       // with a monochrome display
            return (cx & 0x0F) == 9 ? 0x10 : 0x0D;
        }
    }
    // 0065
    if ((mem_u8(0, 0x0410) & 0x30) == 0x30) {  // a monochrome display: MDA or Hercules
        const u8 ah = card_in(0x3BA) & 0x80;
        u16 n = 0x8000;
        bool changed;
        do {  // LOOPE
            changed = (card_in(0x3BA) & 0x80) != ah;
        } while (!changed && --n);
        return changed ? 0x0B : 0x07;
    }
    return mem_u8(0xFC00, 0) == 0x21 ? 0x09 : 0x04;
}

// 1469:0008 gfx_saved_mode (platform.md §8): the library's mode, or the BIOS mode while the library
// has none (gfx_mode = FFh, as at start-up).
s16 gfx_saved_mode()
{
    const s8 m = ds_s8(DS_gfx_mode);
    if (m >= 0) return m;
    return s16(s8(bios_get_mode(nullptr)));
}

// 1432:0008 gfx_get_draw_seg
u16 gfx_get_draw_seg() { return ds_u16(DS_gfx_draw_seg); }

// 157d:000e gfx_set_draw_page (video.md §7.1): page & 7. Modes 4-0Ah and 13h (0026): the page's
// segment from the page table; the text modes and EGA 0Dh-10h (0038): the page in the video memory
// (video segment + page * page bytes / 16); Hercules (0022): pages 0 and 1 the card's, others the
// table's; 11h/12h (004c): nothing.
void gfx_set_draw_page(s16 page)
{
    const u16 p = u16(page) & 7;
    switch (lib_mode()) {
    case 0x0B: case 0x0C:
        if (p > 1) goto table;
        [[fallthrough]];
    case 0: case 1: case 2: case 3: case 0x0D: case 0x0E: case 0x0F: case 0x10:
        ds_u8(DS_gfx_draw_page) = u8(p);
        ds_u16(DS_gfx_draw_seg) = page_seg_vram(p);
        return;
    case 0x11: case 0x12:
        return;
    default:
    table:
        ds_u8(DS_gfx_draw_page) = u8(p);
        ds_u16(DS_gfx_draw_seg) = page_seg(p);
        return;
    }
}

// 154e:000b gfx_set_copy_page (video.md §7.1): as gfx_set_draw_page; in the text modes (0049) it
// does nothing.
void gfx_set_copy_page(s16 page)
{
    const u16 p = u16(page) & 7;
    switch (lib_mode()) {
    case 0x0B: case 0x0C:
        if (p > 1) goto table;
        [[fallthrough]];
    case 0x0D: case 0x0E: case 0x0F: case 0x10:  // 0035
        ds_u8(DS_gfx_copy_page) = u8(p);
        ds_u16(DS_gfx_copy_seg) = page_seg_vram(p);
        return;
    case 0: case 1: case 2: case 3: case 0x11: case 0x12:  // 0049
        return;
    default:  // 0023
    table:
        ds_u8(DS_gfx_copy_page) = u8(p);
        ds_u16(DS_gfx_copy_seg) = page_seg(p);
        return;
    }
}

// 1592:0004 gfx_set_visible_page (video.md §7.1): nothing if the page (& 7) is the visible one.
//  * modes 4-0Ah, 7 and 13h (0068): one screen, so the new page and the visible one exchange their
//    segments and their contents; the screen stays;
//  * text modes 0-3 (0025): INT 10h AH=05h;
//  * EGA 0Dh-10h (00bb): the page's offset in the BIOS data (0040:0062 page, 004E offset) and, after
//    the start and the end of a vertical retrace, in the CRTC start (3D4h registers 0Ch/0Dh); the
//    visible segment follows;
//  * Hercules (0033): page & 1, the BIOS data, the mode register (3B8h: 0Ah, page 1 with bit 7);
//  * 11h/12h: nothing (the page is not even stored).
void gfx_set_visible_page(s16 page)
{
    u16 ax = u16(page) & 7;
    if (ds_u8(DS_gfx_visible_page) == ax) return;
    switch (lib_mode()) {
    case 0: case 1: case 2: case 3:
        ds_u8(DS_gfx_visible_page) = u8(ax);
        bios_set_active_page(u8(ax));
        return;
    case 0x0B: case 0x0C: {
        ax &= 1;
        mem_u8(0x40, 0x62) = u8(ax);
        ds_u8(DS_gfx_visible_page) = u8(ax);
        const u16 offset = u16(u32(ax) * ds_u16(DS_mode_page_bytes));
        mem_u16(0x40, 0x4E) = offset;
        card_out(0x3B8, u8(ror8(u8(ds_u8(DS_gfx_visible_page) & 1), 1) | 0x0A));
        ds_u16(DS_gfx_visible_seg) = u16((offset >> 4) + ds_u16(DS_mode_video_seg));
        return;
    }
    case 0x0D: case 0x0E: case 0x0F: case 0x10: {
        mem_u8(0x40, 0x62) = u8(ax);
        ds_u8(DS_gfx_visible_page) = u8(ax);
        const u16 cx = u16(u32(ax) * ds_u16(DS_mode_page_bytes));
        mem_u16(0x40, 0x4E) = cx;
        // PORT: the host waits for its next retrace (the pace of the original's wait); the card's
        // status bit is then read as the original reads it (the card model toggles it on each read).
        host_wait_vretrace();
        while (!(card_in(0x3DA) & 0x08)) host_pump();
        while (card_in(0x3DA) & 0x08) host_pump();
        card_out16(0x3D4, u16((cx & 0xFF00) | 0x0C));
        card_out16(0x3D4, u16(cx << 8 | 0x0D));
        ds_u16(DS_gfx_visible_seg) = u16((cx >> 4) + ds_u16(DS_mode_video_seg));
        return;
    }
    case 0x11: case 0x12:
        return;
    default:
        break;
    }
    const u16 old = ds_u8(DS_gfx_visible_page);
    ds_u8(DS_gfx_visible_page) = u8(ax);
    const u16 a = page_seg(ax), c = page_seg(old);
    ds_u16(u16(DS_gfx_page_seg + 2 * old)) = a;
    ds_u16(u16(DS_gfx_page_seg + 2 * ax)) = c;
    ds_u16(DS_gfx_draw_seg) = page_seg(ds_u8(DS_gfx_draw_page));
    u16 n = u16(ds_u16(DS_mode_page_bytes) >> 1), i = 0;
    do {  // LOOP: a count of 0 runs 65536 times
        const u16 t = mem_u16(c, i);
        mem_u16(c, i) = mem_u16(a, i);
        mem_u16(a, i) = t;
        i = u16(i + 2);
    } while (--n);
}

// 137e:0000 gfx_alloc_page (13h path 0019): a DOS block of mode_page_bytes for pages 1..7, cleared.
// Modes 4-0Ah and 13h (0019); Hercules (0014) only for pages above 1 (0 and 1 are the card's); the
// text and EGA modes (005a) return 1 (the EGA's pages are in its memory). The DOS result is tested
// by value, not by the carry.
u16 gfx_alloc_page(s16 page)
{
    switch (lib_mode()) {
    case 0x0B: case 0x0C:
        if (s8(u8(page)) <= 1) return 1;  // CMP DL, 1; JLE
        break;
    case 4: case 5: case 6: case 7: case 8: case 9: case 0x0A: case 0x13:
        break;
    default:
        return 1;
    }
    if (page <= 0 || page > 7) return 1;
    u16 err;
    const u16 seg = dos_alloc(u16(ds_u16(DS_mode_page_bytes) >> 4), &err);
    if (!seg) return err;
    ds_u16(u16(DS_gfx_page_seg + 2 * u16(page))) = seg;
    for (u16 n = u16(ds_u16(DS_mode_page_bytes) >> 1), i = 0; n; n--, i = u16(i + 2)) mem_u16(seg, i) = 0;
    return 0;
}

// 142c:0009 gfx_free_page (13h path 001a): frees the page's DOS block; the page falls back to the
// screen segment. Modes 4-0Ch and 13h (001a); the others return 1.
u16 gfx_free_page(s16 page)
{
    const u16 x2 = ds_u16(DS_gfx_mode_x2);
    const bool ram_pages = x2 == MODE13_X2 || (x2 >= 2 * 4 && x2 <= 2 * 0x0C);
    if (!ram_pages) return 1;
    const u16 slot = u16(DS_gfx_page_seg + 2 * u16(page));
    const u16 err = dos_free(ds_u16(slot));
    if (err == 7 || err == 9) return err;
    ds_u16(slot) = ds_u16(DS_mode_video_seg);
    return 0;
}

// 147b:000c gfx_move_to
void gfx_move_to(s16 x, s16 y)
{
    ds_s16(DS_gfx_pen_x) = x;
    ds_s16(DS_gfx_pen_y) = y;
}

// 1543:000b gfx_set_colour (video.md §2): the colour of the drawing primitives, by the library's
// mode (jump table CS:0093 on DCF8): text modes (001c) and mode 13h (0087) keep it as given; the
// others take the colour map DD41h's entry for colour & 1Fh (the raw colour DCF6 then & 1Fh): CGA
// 4-colour modes 4/5 (0022) & 3, Tandy 8-0Ah (0036) & 0Fh, the 2-colour modes 6/0Bh/0Ch (004a) as it
// is, EGA 0Dh-12h (005b) & 0Fh, also written to the graphics controller's set/reset (0) and colour
// compare (2) registers. DCF5 = the colour used. The caller gets AX = 0.
void gfx_set_colour(s16 colour)
{
    u8 cl = u8(colour);
    const u16 mode = u16(ds_u16(DS_gfx_mode_x2) >> 1);
    auto mapped = [&](u8 mask) {
        const u16 c = u16(u16(colour) & 0x1F);
        ds_u8(DS_gfx_colour_raw) = u8(c);
        return u8(ds_u8(u16(DS_gfx_colour_map + c)) & mask);
    };
    switch (mode) {
    case 4: case 5: cl = mapped(3); break;                   // 0022
    case 8: case 9: case 0x0A: cl = mapped(0x0F); break;    // 0036
    case 6: case 0x0B: case 0x0C: cl = mapped(0xFF); break; // 004a
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12:  // 005b
        cl = mapped(0x0F);
        card_out16(0x3CE, u16(cl << 8 | 0x00));  // set/reset
        card_out16(0x3CE, u16(cl << 8 | 0x02));  // colour compare
        break;
    default: ds_u8(DS_gfx_colour_raw) = cl; break;          // 001c, 0087
    }
    ds_u8(DS_gfx_colour) = cl;
}

// 14cf:0008 gfx_fill_rect (video.md §7.2): x0..x1 inclusive, rows from y1 up to y0, in the colour;
// no clipping. The arguments are kept in fill_x0 / fill_x1 / fill_y0 in every mode.
//  * CGA 4/5 (0034), Tandy 9 (00fa): per row a left byte through a mask, whole bytes, a right byte
//    through a mask, in the colour's pixel pattern (the colour repeated: c * 55h, c * 11h);
//  * EGA 0Dh-12h (0209): the same bytes through the bit mask register, written in the set/reset
//    colour (AND on the memory: the latches keep the other pixels); the bit mask is left at the
//    right byte's;
//  * 13h (02a1): bytes; the text modes and modes 8 / 0Ah (02d3): nothing.
void gfx_fill_rect(u16 x0, u16 x1, u16 y0, u16 y1)
{
    ds_u16(DS_fill_x0) = x0;
    ds_u16(DS_fill_x1) = x1;
    ds_u16(DS_fill_y0) = y0;
    const u16 es = ds_u16(DS_gfx_draw_seg);
    const u16 rows = u16(y1 + 1 - y0);
    switch (lib_mode()) {
    case 4: case 5: {
        const u16 di = addr_cga(x0, y1);
        const u16 dx = u16((x1 >> 2) - 1 - (x0 >> 2));
        const u8 bh = u8(0xFF >> ((x0 & 3) * 2));
        const u8 bl = u8(~(0xFF >> (((x1 & 3) + 1) * 2)));
        const u8 c = ds_u8(DS_gfx_colour);
        const u8 al = u8(c << 2 | c);
        fill_rows_banked(es, di, rows, dx, bh, bl, u8(al << 4 | al));
        return;
    }
    case 9: {
        const u16 di = addr_tandy(x0, y1);
        const u16 dx = u16((x1 >> 1) - 1 - (x0 >> 1));
        const u8 bh = u8(0xFF >> ((x0 & 1) * 4));
        const u8 bl = u8(~(0xFF >> (((x1 & 1) + 1) * 4)));
        const u8 c = ds_u8(DS_gfx_colour);
        fill_rows_banked(es, di, rows, dx, bh, bl, u8(c << 4 | c));
        return;
    }
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: {
        u16 di = addr_ega(x0, y1);
        u16 dx = u16((x1 >> 3) - 1 - (x0 >> 3));
        u8 bh = u8(0xFF >> (x0 & 7));
        u8 bl = u8(~(0xFF >> ((x1 & 7) + 1)));
        if (s16(dx) < 0) {
            bh &= bl;
            bl = 0;
            dx = 0;
        }
        const u16 row = ds_u16(DS_mode_row_bytes);
        u16 cx = rows;
        do {
            gc_out(8, bh);
            vmem_and(es, di, bh);
            di++;
            if (dx) {
                gc_out(8, 0xFF);
                for (u16 n = dx; n; n--) vmem_and(es, di++, 0xFF);
            }
            gc_out(8, bl);
            vmem_and(es, di, bl);
            di = u16(di - 1 - dx - row);
        } while (--cx);
        return;
    }
    case 0x13:
        break;
    default:  // PORT: 6, 0Bh, 0Ch (not ported, see the top)
        return;
    }
    u16 di = addr13(x0, y1);
    u16 n_rows = rows;
    const u16 width = u16(ds_u16(DS_fill_x1) + 1 - x0);
    const u8 c = ds_u8(DS_gfx_colour);
    const u16 step = u16(0x140 + width);
    do {
        for (u16 n = width; n; n--) mem_u8(es, di++) = c;
        di = u16(di - step);
    } while (--n_rows);
}

// 15e2:0001 gfx_clear_page: the whole draw page (mode_page_bytes): text modes with blank characters
// (0720h, 0019), the packed modes and 13h with zeros (0025); EGA (002d): write mode 2, bit mask FFh,
// a zero byte written after a read (the latches), then write mode 0.
void gfx_clear_page()
{
    const u16 seg = ds_u16(DS_gfx_draw_seg);
    const u16 bytes = ds_u16(DS_mode_page_bytes);
    switch (lib_mode()) {
    case 0: case 1: case 2: case 3: case 7:
        for (u16 n = u16(bytes >> 1), i = 0; n; n--, i = u16(i + 2)) mem_u16(seg, i) = 0x0720;
        return;
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: {
        gc_out(5, 2);
        gc_out(8, 0xFF);
        u16 di = 0, n = bytes;
        do {  // LOOP: a count of 0 runs 65536 times
            vmem_read(seg, di);
            vmem_write(seg, di++, 0);
        } while (--n);
        gc_out(5, 0);
        return;
    }
    default:
        for (u16 n = u16(bytes >> 1), i = 0; n; n--, i = u16(i + 2)) mem_u16(seg, i) = 0x0000;
        return;
    }
}

// 1502:0001 gfx_copy_rect_from_copy_page (video.md §7.3): copy page -> draw page, x0..x1, rows y1 up
// to y0. CGA 4/5 (0021) and Tandy 9 (006d) copy whole bytes to the screen B800h itself, whatever the
// draw page; EGA (015d) through the latches; 13h (01af) bytes.
void gfx_copy_rect_from_copy_page(u16 x0, u16 x1, u16 y0, u16 y1)
{
    const u16 rows = u16(y1 + 1 - y0);
    switch (lib_mode()) {
    case 4: case 5:
        copy_rows_banked(CGA_SCREEN, ds_u16(DS_gfx_copy_seg), addr_cga(x0, y1), u16((x1 >> 2) + 1 - (x0 >> 2)), rows);
        return;
    case 9:
        copy_rows_banked(CGA_SCREEN, ds_u16(DS_gfx_copy_seg), addr_tandy(x0, y1), u16((x1 >> 1) + 1 - (x0 >> 1)), rows);
        return;
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12:
        copy_rect_ega(x0, x1, y1, rows, ds_u16(DS_gfx_draw_seg), ds_u16(DS_gfx_copy_seg));
        return;
    case 0x13:
        copy_rect13(x0, x1, y0, y1, ds_u16(DS_gfx_draw_seg), ds_u16(DS_gfx_copy_seg));
        return;
    default:  // text modes, 8, 0Ah (01e0): nothing; PORT: 6, 0Bh, 0Ch (not ported)
        return;
    }
}

// 1522:000e gfx_copy_rect_to_copy_page (video.md §7.3): draw page -> copy page; CGA 4/5 (002e) and
// Tandy 9 (007a) from the screen B800h itself; EGA (016a); 13h (01bc).
void gfx_copy_rect_to_copy_page(u16 x0, u16 x1, u16 y0, u16 y1)
{
    const u16 rows = u16(y1 + 1 - y0);
    switch (lib_mode()) {
    case 4: case 5:
        copy_rows_banked(ds_u16(DS_gfx_copy_seg), CGA_SCREEN, addr_cga(x0, y1), u16((x1 >> 2) + 1 - (x0 >> 2)), rows);
        return;
    case 9:
        copy_rows_banked(ds_u16(DS_gfx_copy_seg), CGA_SCREEN, addr_tandy(x0, y1), u16((x1 >> 1) + 1 - (x0 >> 1)), rows);
        return;
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12:
        copy_rect_ega(x0, x1, y1, rows, ds_u16(DS_gfx_copy_seg), ds_u16(DS_gfx_draw_seg));
        return;
    case 0x13:
        copy_rect13(x0, x1, y0, y1, ds_u16(DS_gfx_copy_seg), ds_u16(DS_gfx_draw_seg));
        return;
    default:  // text modes, 8, 0Ah (01ed): nothing; PORT: 6, 0Bh, 0Ch (not ported)
        return;
    }
}

// 13e2:0002 gfx_draw_bitmap (video.md §7.4): 1 bit per pixel from DS:bits, MSB left, set bits in
// the colour, clear bits untouched; the first row at the pen, the next ones upward; no clipping.
//  * CGA 4/5 (0021): each source byte, shifted to the pen's pixel in the byte, becomes a word of
//    2-bit pixels (the colour pattern through the bits, gfx_bitmap_pattern); a row not starting on a
//    byte ends with one more word of the bits left over; rows up by 2000h / 80;
//  * Tandy 9 (0112): each byte two words of 4-bit pixels; the word pair after the row is always
//    written (the test for "no bits left over" is of CL = 0, which is never: with the pen on an even
//    x the bits shift out and the words are written back unchanged); rows up by 2000h / 6000h, A0h;
//  * EGA 0Dh-12h (03fa): each byte through the bit mask register (AND on the memory, set/reset
//    colour); the bit mask is left at the last byte's;
//  * 13h (0449): bytes.
void gfx_draw_bitmap(u16 bits_ds, u16 bytes_per_row, u16 rows)
{
    const u16 es = ds_u16(DS_gfx_draw_seg);
    const u16 pen_x = ds_u16(DS_gfx_pen_x), pen_y = ds_u16(DS_gfx_pen_y);
    switch (lib_mode()) {
    case 4: case 5: {
        u16 bx = addr_cga(pen_x, pen_y);
        const u8 pattern = u8((ds_u8(DS_gfx_colour) & 3) * 0x55);
        ds_u16(DS_gfx_bitmap_pattern) = u16(pattern << 8 | pattern);
        const u8 cl = u8(((pen_x & 3) ^ 3) + 5);
        u16 si = bits_ds, di = rows;
        for (;;) {
            u16 dx = bytes_per_row;
            u8 prev = 0, cur = 0;
            const u16 row_start = bx;
            do {
                cur = ds_u8(si++);
                const u8 b = u8(u16(u16(prev << 8 | cur) << cl) >> 8);
                put_word_masked(es, bx, double_bits(b));
                bx = u16(bx + 2);
                prev = cur;
            } while (--dx);
            if (cl != 8) put_word_masked(es, bx, double_bits(u8(cur << cl)));
            bx = row_start;
            if (--di == 0) return;
            if (!(bx & 0x2000)) bx = u16(bx - 0x50);
            bx ^= 0x2000;
        }
    }
    case 9: {
        u16 bx = addr_tandy(pen_x, pen_y);
        const u8 pattern = u8((ds_u8(DS_gfx_colour) & 0x0F) * 0x11);
        ds_u16(DS_gfx_bitmap_pattern) = u16(pattern << 8 | pattern);
        const u8 cl = u8(((pen_x & 1) ^ 1) + 7);
        u16 si = bits_ds, di = rows;
        for (;;) {
            u16 dx = bytes_per_row;
            u8 prev = 0, cur = 0;
            const u16 row_start = bx;
            do {
                cur = ds_u8(si++);
                u8 b = u8(u16(u16(prev << 8 | cur) << cl) >> 8);
                const u16 right = nibble_bits(&b), left = nibble_bits(&b);
                put_word_masked(es, bx, left);
                put_word_masked(es, u16(bx + 2), right);
                bx = u16(bx + 4);
                prev = cur;
            } while (--dx);
            u8 b = u8(cur << cl);  // (0 when CL = 8)
            const u16 right = nibble_bits(&b), left = nibble_bits(&b);
            put_word_masked(es, bx, left);
            put_word_masked(es, u16(bx + 2), right);
            bx = row_start;
            if (--di == 0) return;
            bx = (bx & 0xE000) ? u16(bx - 0x2000) : u16((bx | 0x6000) - 0xA0);
        }
    }
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: {
        const u8 cl = u8(((pen_x & 7) ^ 7) + 1);
        u16 bx = addr_ega(pen_x, pen_y);
        const u16 row = ds_u16(DS_mode_row_bytes);
        u16 si = bits_ds, dx = rows;
        do {
            u16 di = bytes_per_row;
            u8 prev = 0, cur = 0;
            const u16 row_start = bx;
            do {
                cur = ds_u8(si++);
                const u8 ah = u8(u16(u16(prev << 8 | cur) << cl) >> 8);
                gc_out(8, ah);
                vmem_and(es, bx, ah);
                prev = cur;
                bx++;
            } while (--di);
            if (cl != 8) {
                const u8 ah = u8(cur << cl);
                gc_out(8, ah);
                vmem_and(es, bx, ah);
            }
            bx = u16(row_start - row);
        } while (--dx);
        return;
    }
    case 0x13:
        break;
    default:  // text modes, 8, 0Ah (047b): nothing; PORT: 6, 0Bh, 0Ch (not ported)
        return;
    }
    u16 bx = addr13(pen_x, pen_y);
    const u8 c = ds_u8(DS_gfx_colour);
    u16 si = bits_ds;
    do {
        u16 p = bx;
        u16 n = bytes_per_row;
        do {
            const u8 b = ds_u8(si++);
            for (u8 m = 0x80; m; m >>= 1, p++)
                if (b & m) mem_u8(es, p) = c;
        } while (--n);
        bx = u16(bx - 0x140);
    } while (--rows);
}

// 1432:000c gfx_read_bitmap (video.md §7.4): the inverse: a bit set where the pixel is the colour.
//  * CGA 4/5 (002b), Tandy 9 (00d5): 3 bytes read for each (big-endian, shifted to the pen's pixel)
//    and compared with the colour pattern; rows up as in gfx_draw_bitmap. The pattern is re-read
//    from gfx_bitmap_pattern for each byte;
//  * EGA 0Dh-12h (02cb): read mode 1 (colour compare) for the whole bitmap, then read mode 0;
//  * 13h (0314): bytes.
void gfx_read_bitmap(u16 bits_ds, u16 bytes_per_row, u16 rows)
{
    const u16 es = ds_u16(DS_gfx_draw_seg);
    const u16 pen_x = ds_u16(DS_gfx_pen_x), pen_y = ds_u16(DS_gfx_pen_y);
    switch (lib_mode()) {
    case 4: case 5: {
        u16 bx = addr_cga(pen_x, pen_y);
        const u8 pattern = u8((ds_u8(DS_gfx_colour) & 3) * 0x55);
        ds_u16(DS_gfx_bitmap_pattern) = u16(pattern << 8 | pattern);
        const u8 cl = u8((pen_x & 3) << 1);
        u16 si = bits_ds, dx = rows;
        for (;;) {
            u16 di = bytes_per_row;
            const u16 row_start = bx;
            do {
                u16 ax = read_match(es, bx, cl);
                bx = u16(bx + 2);
                ax &= u16(ax << 1);  // both bits of a pixel
                ds_u8(si++) = collect_bits(ax, 8, 1);
            } while (--di);
            bx = row_start;
            if (--dx == 0) return;
            if (!(bx & 0x2000)) bx = u16(bx - 0x50);
            bx ^= 0x2000;
        }
    }
    case 9: {
        u16 bx = addr_tandy(pen_x, pen_y);
        const u8 pattern = u8((ds_u8(DS_gfx_colour) & 0x0F) * 0x11);
        ds_u16(DS_gfx_bitmap_pattern) = u16(pattern << 8 | pattern);
        const u8 cl = u8((pen_x & 1) << 2);
        u16 si = bits_ds, dx = rows;
        for (;;) {
            u16 di = bytes_per_row;
            const u16 row_start = bx;
            do {
                u16 left = read_match(es, bx, cl);
                bx = u16(bx + 2);
                left &= u16(left << 1) & u16(left << 2) & u16(left << 3);  // all 4 bits of a pixel
                u16 right = read_match(es, bx, cl);
                bx = u16(bx + 2);
                right &= u16(right << 1) & u16(right << 2) & u16(right << 3);
                ds_u8(si++) = u8(u8(collect_bits(left, 4, 3) << 4) | collect_bits(right, 4, 3));
            } while (--di);
            bx = row_start;
            if (--dx == 0) return;
            bx = (bx & 0xE000) ? u16(bx - 0x2000) : u16((bx | 0x6000) - 0xA0);
        }
    }
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: {
        const u8 cl = u8(pen_x & 7);
        u16 bx = addr_ega(pen_x, pen_y);
        const u16 row = ds_u16(DS_mode_row_bytes);
        u16 si = bits_ds, di = rows;
        gc_out(5, 8);
        do {
            u16 dx = bytes_per_row;
            const u16 row_start = bx;
            do {
                const u8 lo = vmem_read(es, bx);
                const u8 hi = vmem_read(es, u16(bx + 1));
                ds_u8(si++) = u8(u16(u16(lo << 8 | hi) << cl) >> 8);
                bx++;
            } while (--dx);
            bx = u16(row_start - row);
        } while (--di);
        gc_out(5, 0);
        return;
    }
    case 0x13:
        break;
    default:  // text modes, 8, 0Ah (034a): nothing; PORT: 6, 0Bh, 0Ch (not ported)
        return;
    }
    u16 bx = addr13(pen_x, pen_y);
    const u8 c = ds_u8(DS_gfx_colour);
    u16 si = bits_ds;
    do {
        u16 p = bx;
        u16 n = bytes_per_row;
        do {
            u8 b = 0;
            for (u8 m = 0x80; m; m >>= 1, p++)
                if (mem_u8(es, p) == c) b |= m;
            ds_u8(si++) = b;
        } while (--n);
        bx = u16(bx - 0x140);
    } while (--rows);
}

// 15a4:0006 gfx_copy_rect (video.md §7.3): a rectangle of page src_page (x0..x1, rows y1 up to y0)
// to page dst_page at (dx, dy_bottom), rows upward, whole bytes in the packed modes. CGA 4/5 (0032),
// Tandy 9 (00c4) and 13h (019b) take the pages' segments from the page table and step the rows by
// the interleave (2000h: two banks, 6000h: four, else none); EGA (025c) computes the pages in the
// video memory (video segment + page * (page bytes / 16)) and copies through the latches.
void gfx_copy_rect(u16 x0, u16 x1, u16 y0, u16 y1, u16 dx, u16 dy_bottom, u16 src_page, u16 dst_page)
{
    u16 rows = u16(y1 + 1 - y0);
    ds_u16(DS_fill_x0) = dx;
    ds_u16(DS_copy_dst_y) = dy_bottom;
    u16 si, di, width;
    switch (lib_mode()) {
    case 4: case 5:
        si = addr_cga(x0, y1);
        width = u16((x1 >> 2) + 1 - (x0 >> 2));
        di = addr_cga(ds_u16(DS_fill_x0), ds_u16(DS_copy_dst_y));
        break;
    case 9:
        si = addr_tandy(x0, y1);
        width = u16((x1 >> 1) + 1 - (x0 >> 1));
        di = addr_tandy(ds_u16(DS_fill_x0), ds_u16(DS_copy_dst_y));
        break;
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: {
        gc_out(5, 1);
        const u16 row = ds_u16(DS_mode_row_bytes);
        si = u16(x0 >> 3);
        width = u16((x1 >> 3) + 1 - si);
        si = u16(si + u16(u32(row) * y1));
        di = u16((ds_u16(DS_fill_x0) >> 3) + u16(u32(ds_u16(DS_copy_dst_y)) * row));
        const u16 paragraphs = u16(ds_u16(DS_mode_page_bytes) >> 4);
        const u16 es = u16(u16(u32(dst_page) * paragraphs) + ds_u16(DS_mode_video_seg));
        const u16 ds = u16(u16(u32(src_page) * paragraphs) + ds_u16(DS_mode_video_seg));
        const u16 step = u16(row + width);
        do {
            for (u16 n = width; n; n--) vmem_write(es, di++, vmem_read(ds, si++));
            si = u16(si - step);
            di = u16(di - step);
        } while (--rows);
        gc_out(5, 0);
        return;
    }
    case 0x13:
        si = addr13(x0, y1);
        width = u16(x1 + 1 - x0);
        di = addr13(ds_u16(DS_fill_x0), ds_u16(DS_copy_dst_y));
        break;
    default:  // text modes, 8, 0Ah (02d9): nothing; PORT: 6, 0Bh, 0Ch (not ported)
        return;
    }
    // 01c1
    const u16 es = page_seg(dst_page), ds = page_seg(src_page);
    const u16 interleave = ds_u16(DS_mode_interleave), row = ds_u16(DS_mode_row_bytes);
    if (interleave == 0x6000) {
        do {
            for (u16 n = width; n; n--) mem_u8(es, di++) = mem_u8(ds, si++);
            di = u16(di - width);
            if (!(di & 0xE000)) di = u16((di | 0x8000) - row);
            si = u16(si - width);
            if (!(si & 0xE000)) si = u16((si | 0x8000) - row);
            di = u16(di - 0x2000);
            si = u16(si - 0x2000);
        } while (--rows);
    } else if (interleave == 0x2000) {
        do {
            for (u16 n = width; n; n--) mem_u8(es, di++) = mem_u8(ds, si++);
            si = u16(si - width);
            if (!(si & 0x2000)) si = u16(si - row);
            di = u16(di - width);
            if (!(di & 0x2000)) di = u16(di - row);
            si ^= 0x2000;
            di ^= 0x2000;
        } while (--rows);
    } else {
        const u16 step = u16(row + width);
        do {
            for (u16 n = width; n; n--) mem_u8(es, di++) = mem_u8(ds, si++);
            si = u16(si - step);
            di = u16(di - step);
        } while (--rows);
    }
}

// 14b5:000d gfx_put_pixel (video.md §7.2): one pixel in the current colour, inside the clip box
// (signed). CGA 4/5 (0042) and Tandy 9 (008a): the pixel's bits cleared and the colour ORed in
// (a colour above the mode's bits spills into the next pixels, as in the original); EGA (0141):
// through the bit mask register (left at the pixel's bit), set/reset colour; 13h (0169): a byte.
void gfx_put_pixel(s16 x, s16 y)
{
    if (x < ds_s16(DS_clip_x_min) || x > ds_s16(DS_clip_x_max)) return;
    if (y < ds_s16(DS_clip_y_min) || y > ds_s16(DS_clip_y_max)) return;
    const u16 es = ds_u16(DS_gfx_draw_seg);
    u16 bx;
    u8 cl, ch;
    switch (lib_mode()) {
    case 4: case 5:
        bx = addr_cga(u16(x), u16(y));
        cl = u8(((u16(x) & 3) ^ 3) << 1);
        ch = 0xFC;
        break;
    case 9:
        bx = addr_tandy(u16(x), u16(y));
        cl = u8(((u16(x) & 1) ^ 1) << 2);
        ch = 0xF0;
        break;
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: {
        const u8 ah = u8(0x80 >> (u16(x) & 7));
        bx = addr_ega(u16(x), u16(y));
        gc_out(8, ah);
        vmem_and(es, bx, ah);
        return;
    }
    case 0x13:
        mem_u8(ds_u16(DS_gfx_draw_seg), addr13(u16(x), u16(y))) = ds_u8(DS_gfx_colour);
        return;
    default:  // text modes, 8, 0Ah (017c): nothing; PORT: 6, 0Bh, 0Ch (not ported)
        return;
    }
    // 00d2
    const u8 ah = u8(ror8(mem_u8(es, bx), cl) & ch);
    mem_u8(es, bx) = rol8(u8(ah | ds_u8(DS_gfx_colour)), cl);
}

// 1390:006e picture_hline: x0..x1 of the pen row in the current colour.
void picture_hline(u16 es, u16 x0, u16 x1)
{
    // (A near routine: AX = x0, BX = x1, ES = the draw page's segment as picture_draw loads it;
    // video.md §7.5.) Outside mode 13h the colour's row pattern
    // gfx_dither[colour * 2 + (pen row & 1)] fills (colour * 2 as a signed byte: CBW):
    //  * CGA 4/5 (008a), Tandy 9 (0127): a left byte and a right byte through masks, whole bytes;
    //  * EGA 0Dh-12h (02cf): the dither entries are colours: this row's goes to the set/reset
    //    register; if the other row's differs, the even pixels (bit mask 55h) get this row's and the
    //    odd ones (AAh) the other's (set/reset left at it); the bit mask is left at the right byte's.
    // PORT: the text modes and 8 / 0Ah jump to picture_draw's exit (1390:005f), which would unwind
    // the wrong stack; Gunboat never draws a picture in them: here they draw nothing.
    ds_u16(DS_fill_x0) = x0;
    ds_u16(DS_fill_x1) = x1;
    const u16 y = ds_u16(DS_gfx_pen_y);
    const u16 colour2 = u16(s16(s8(u8(ds_u8(DS_gfx_colour) << 1))));  // SHL AL, 1; CBW
    u16 di, dx;
    u8 bh, bl;
    switch (lib_mode()) {
    case 4: case 5:
        di = addr_cga(x0, y);
        dx = u16((x1 >> 2) - 1 - (x0 >> 2));
        bh = u8(0xFF >> ((x0 & 3) * 2));
        bl = u8(~(0xFF >> (((x1 & 3) + 1) * 2)));
        break;
    case 9:
        di = addr_tandy(x0, y);
        dx = u16((x1 >> 1) - 1 - (x0 >> 1));
        bh = u8(0xFF >> ((x0 & 1) * 4));
        bl = u8(~(0xFF >> (((x1 & 1) + 1) * 4)));
        break;
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11: case 0x12: {
        di = addr_ega(x0, y);
        dx = u16((x1 >> 3) - 1 - (x0 >> 3));
        bh = u8(0xFF >> (x0 & 7));
        bl = u8(~(0xFF >> ((x1 & 7) + 1)));
        const u16 parity = y & 1;
        u16 si = u16(DS_gfx_dither + colour2 + parity);
        u8 ah = ds_u8(si);
        gc_out(0, ah);
        si = u16(si - parity + (parity ^ 1));
        const u8 al = ds_u8(si);
        if (s16(dx) < 0) {
            bh &= bl;
            bl = 0;
            dx = 0;
        }
        if (al == ah) {  // 0349: one colour
            gc_out(8, bh);
            vmem_and(es, di, bh);
            di++;
            if (dx) {
                gc_out(8, 0xFF);
                for (u16 n = dx; n; n--) vmem_and(es, di++, ah);
            }
            gc_out(8, bl);
            vmem_and(es, di, bl);
            return;
        }
        // 0387: two colours, even pixels then odd pixels
        u16 si2 = di;
        gc_out(8, u8(0x55 & bh));
        vmem_and(es, di, ah);
        di++;
        if (dx) {
            gc_out(8, 0x55);
            for (u16 n = dx; n; n--) vmem_and(es, di++, ah);
        }
        gc_out(8, u8(0x55 & bl));
        vmem_and(es, di, ah);
        ah = al;
        gc_out(0, ah);
        bh &= 0xAA;
        gc_out(8, bh);
        vmem_and(es, si2, ah);
        si2++;
        if (dx) {
            gc_out(8, 0xAA);
            for (u16 n = dx; n; n--) vmem_and(es, si2++, ah);
        }
        bl &= 0xAA;
        gc_out(8, bl);
        vmem_and(es, si2, ah);
        return;
    }
    case 0x13: {
        u16 p = addr13(x0, y);
        const u8 c = ds_u8(DS_gfx_colour);
        for (u16 n = u16(ds_u16(DS_fill_x1) + 1 - ds_u16(DS_fill_x0)); n; n--) mem_u8(es, p++) = c;
        return;
    }
    default:  // PORT: text modes, 8, 0Ah (above); 6, 0Bh, 0Ch (not ported)
        return;
    }
    // 018d: CGA / Tandy
    const u8 ah = ds_u8(u16(u16(y & 1) + colour2 + DS_gfx_dither));
    if (s16(dx) < 0) {
        bh &= bl;
        bl = 0;
        dx = 0;
    }
    mem_u8(es, di) = masked(mem_u8(es, di), ah, bh);
    di++;
    for (u16 n = dx; n; n--) mem_u8(es, di++) = ah;
    mem_u8(es, di) = masked(mem_u8(es, di), ah, bl);
}

// 1390:0000 picture_draw (platform.md §6): (colour, count) runs from DS:src at the pen, `width`
// pixels per row; a run that passes the right edge continues on the row above, at the pen's x.
// The colour and the pen row are restored afterwards. No mode dispatch of its own: picture_hline's.
void picture_draw(u16 src_ds, s16 runs, u16 width)
{
    const u8 saved_colour = ds_u8(DS_gfx_colour);
    const u16 saved_y = ds_u16(DS_gfx_pen_y);
    const u16 es = ds_u16(DS_gfx_draw_seg);
    u16 si = src_ds;
    u16 x = ds_u16(DS_gfx_pen_x);
    const u16 right = u16(width + x - 1);
    while (runs > 0) {
        ds_u8(DS_gfx_colour) = ds_u8(si++);
        u16 count = ds_u8(si++);
        runs--;
        while (count) {
            u16 end = u16(x + count - 1);
            if (s16(end) > s16(right)) end = right;
            count = u16(count + x - end - 1);
            picture_hline(es, x, end);
            x = u16(end + 1);
            if (s16(x) <= s16(right)) break;
            x = ds_u16(DS_gfx_pen_x);
            ds_u16(DS_gfx_pen_y)--;
        }
    }
    ds_u16(DS_gfx_pen_y) = saved_y;
    ds_u8(DS_gfx_colour) = saved_colour;
}

// 14ff:0001 text_exit_clear: in a text mode (mode_width 0), DOS prints CS:002C (an ANSI clear
// sequence; PORT: there is no console to print to) and the page is cleared unless the cursor is home.
void text_exit_clear()
{
    if (ds_u16(DS_mode_width) != 0) return;
    if (bios_get_cursor(ds_u8(DS_gfx_visible_page)) != 0) gfx_clear_page();
}

} // namespace gb
