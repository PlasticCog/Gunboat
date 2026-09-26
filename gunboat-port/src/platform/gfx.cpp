// The graphics library, segments 137e-15e2 (video.md §1-§2), mode 13h paths. The Test Drive III
// port's platform/gfx.c (MIT) is the model; this build of the library differs (no colour maps,
// modes up to 13h, 8 pages), so each routine follows Gunboat's code.
//
// PORT: EGA (0Dh-12h), CGA (4-6), Tandy (8-0Ah) and Hercules (0Bh-0Ch) drawing paths are not
// ported: in those modes the primitives only do what they do before their mode dispatch.
#include "platform/gfx.hpp"

#include "mem.hpp"
#include "platform/card.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr u16 MODE13_X2 = 0x26;

bool mode13() { return ds_u16(DS_gfx_mode_x2) == MODE13_X2; }

// x + y * 320 as the library computes it: the bytes of y swapped (y * 256 for y < 256), plus that
// shifted right by 2.
u16 addr13(u16 x, u16 y)
{
    u16 ax = u16(y << 8 | y >> 8);
    const u16 bx = u16(x + ax);
    ax >>= 2;
    return u16(bx + ax);
}

u16 page_seg(u16 page) { return ds_u16(u16(DS_gfx_page_seg + 2 * page)); }

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
        copy_ds(0xDDC1, 0xDE41, 0x40);
        copy_ds(0xDD41, 0xDD81, 0x20);
        break;
    case 0x06:  // 00cb
        set_int43();
        [[fallthrough]];
    case 0x11:  // 00dd
        copy_ds(0xDDC1, 0xDE01, 0x40);
        copy_ds(0xDD41, 0xDD61, 0x20);
        break;
    case 0x08: case 0x09: case 0x0A:  // 00fd
        set_int43();
        [[fallthrough]];
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x12:  // 010f
        copy_ds(0xDDC1, 0xDE81, 0x40);
        copy_ds(0xDD41, 0xDDA1, 0x20);
        break;
    case 0x0B: case 0x0C:  // 002d Hercules: BIOS data, CRTC, clear B000h
        // PORT: the ports 3BFh/3B8h/3B4h are not modelled; the memory effects are.
        for (u16 i = 0; i < 0x10; i++) mem_u8(0, u16(0x449 + i)) = ds_u8(u16(0xE0F4 + i));
        for (u32 i = 0; i < 0x8000; i++) mem_u16(0xB000, u16(2 * i)) = 0;
        set_int43();
        copy_ds(0xDDC1, 0xDE01, 0x40);
        copy_ds(0xDD41, 0xDD61, 0x20);
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
        // PORT: graphics controller register 1 = 0Fh (port 3CEh) is not modelled.
        if (m > 0x10) {
            ds_u8(DS_mode_text_rows) = 0x1E;
            if (m == 0x12) {
                bios_dac_set_block(0, 0x10, {0xDEC1, DGROUP});
            } else {
                bios_dac_set(1, 0x3F, 0x3F, 0x3F);
            }
            // PORT: INT 10h AX=1002h (EGA attribute palette from DD61h / DDA1h) is not modelled.
        }
    }
    return 0;
}

// 1386:0004 gfx_detect (video.md §1): the BIOS display combination (INT 10h AX=1A00h) decides;
// the model answers VGA colour, so this returns 12h. PORT: the EGA / CGA / Hercules probes that
// follow a failed 1A00h call are not reached.
s16 gfx_detect()
{
    const u16 bx = bios_display_combination();
    switch (u8(bx)) {
    case 0x0C: return 0x13;
    case 0x08: return 0x12;
    case 0x0B: case 0x07: return 0x11;
    default: return 0x0D;  // PORT: stands in for the EGA/CGA/Hercules probes
    }
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

// 157d:000e gfx_set_draw_page (13h path 0026: the page's segment from the table)
void gfx_set_draw_page(s16 page)
{
    const u16 p = u16(page) & 7;
    if (!mode13()) return;  // PORT: other modes
    ds_u8(DS_gfx_draw_page) = u8(p);
    ds_u16(DS_gfx_draw_seg) = page_seg(p);
}

// 154e:000b gfx_set_copy_page (13h path 0023)
void gfx_set_copy_page(s16 page)
{
    const u16 p = u16(page) & 7;
    if (!mode13()) return;  // PORT: other modes
    ds_u8(DS_gfx_copy_page) = u8(p);
    ds_u16(DS_gfx_copy_seg) = page_seg(p);
}

// 1592:0004 gfx_set_visible_page (13h path 0068): mode 13h has one screen, so the new page and the
// visible one exchange their segments and their contents; the screen stays at A000h.
void gfx_set_visible_page(s16 page)
{
    const u16 p = u16(page) & 7;
    if (ds_u8(DS_gfx_visible_page) == p) return;
    if (!mode13()) return;  // PORT: other modes
    const u16 old = ds_u8(DS_gfx_visible_page);
    ds_u8(DS_gfx_visible_page) = u8(p);
    const u16 a = page_seg(p), c = page_seg(old);
    ds_u16(u16(DS_gfx_page_seg + 2 * old)) = a;
    ds_u16(u16(DS_gfx_page_seg + 2 * p)) = c;
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
// The DOS result is tested by value, not by the carry.
u16 gfx_alloc_page(s16 page)
{
    const u16 x2 = ds_u16(DS_gfx_mode_x2);
    const bool ram_pages = x2 == MODE13_X2 || (x2 >= 2 * 4 && x2 <= 2 * 0x0A);
    if (!ram_pages) return 1;  // text and EGA modes; PORT: Hercules (0Bh, 0Ch) not ported
    if (page <= 0 || page > 7) return 1;
    u16 err;
    const u16 seg = dos_alloc(u16(ds_u16(DS_mode_page_bytes) >> 4), &err);
    if (!seg) return err;
    ds_u16(u16(DS_gfx_page_seg + 2 * u16(page))) = seg;
    for (u16 n = u16(ds_u16(DS_mode_page_bytes) >> 1), i = 0; n; n--, i = u16(i + 2)) mem_u16(seg, i) = 0;
    return 0;
}

// 142c:0009 gfx_free_page (13h path 001a): frees the page's DOS block; the page falls back to the
// screen segment.
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
        return u8(ds_u8(u16(0xDD41 + c)) & mask);  // the colour map
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

// 14cf:0008 gfx_fill_rect (13h path 02a1): rows from y1 up to y0, x0..x1 inclusive.
void gfx_fill_rect(u16 x0, u16 x1, u16 y0, u16 y1)
{
    ds_u16(DS_fill_x0) = x0;
    ds_u16(DS_fill_x1) = x1;
    ds_u16(DS_fill_y0) = y0;
    if (!mode13()) return;  // PORT: other modes
    const u16 seg = ds_u16(DS_gfx_draw_seg);
    u16 di = addr13(x0, y1);
    u16 rows = u16(y1 + 1 - ds_u16(DS_fill_y0));
    const u16 width = u16(ds_u16(DS_fill_x1) + 1 - x0);
    const u8 c = ds_u8(DS_gfx_colour);
    const u16 step = u16(0x140 + width);
    do {
        for (u16 n = width; n; n--) mem_u8(seg, di++) = c;
        di = u16(di - step);
    } while (--rows);
}

// 15e2:0001 gfx_clear_page: the whole draw page; text modes with blank characters (0720h).
void gfx_clear_page()
{
    const u16 seg = ds_u16(DS_gfx_draw_seg);
    const u16 x2 = ds_u16(DS_gfx_mode_x2);
    const bool text = x2 <= 2 * 3 || x2 == 2 * 7;
    if (x2 >= 2 * 0x0D && x2 <= 2 * 0x12) return;  // PORT: EGA planar path
    const u16 value = text ? 0x0720 : 0x0000;
    for (u16 n = u16(ds_u16(DS_mode_page_bytes) >> 1), i = 0; n; n--, i = u16(i + 2)) mem_u16(seg, i) = value;
}

// 1502:0001 gfx_copy_rect_from_copy_page (13h path 01af): copy page -> draw page.
void gfx_copy_rect_from_copy_page(u16 x0, u16 x1, u16 y0, u16 y1)
{
    if (!mode13()) return;  // PORT: other modes
    copy_rect13(x0, x1, y0, y1, ds_u16(DS_gfx_draw_seg), ds_u16(DS_gfx_copy_seg));
}

// 1522:000e gfx_copy_rect_to_copy_page (13h path 01bc): draw page -> copy page.
void gfx_copy_rect_to_copy_page(u16 x0, u16 x1, u16 y0, u16 y1)
{
    if (!mode13()) return;  // PORT: other modes
    copy_rect13(x0, x1, y0, y1, ds_u16(DS_gfx_copy_seg), ds_u16(DS_gfx_draw_seg));
}

// 13e2:0002 gfx_draw_bitmap (13h path 0449): 1 bit per pixel from DS:bits, MSB left, set bits in
// the colour; the first row at the pen, the next ones upward.
void gfx_draw_bitmap(u16 bits_ds, u16 bytes_per_row, u16 rows)
{
    if (!mode13()) return;  // PORT: other modes
    const u16 seg = ds_u16(DS_gfx_draw_seg);
    u16 bx = addr13(ds_u16(DS_gfx_pen_x), ds_u16(DS_gfx_pen_y));
    const u8 c = ds_u8(DS_gfx_colour);
    u16 si = bits_ds;
    do {
        u16 p = bx;
        u16 n = bytes_per_row;
        do {
            const u8 b = ds_u8(si++);
            for (u8 m = 0x80; m; m >>= 1, p++)
                if (b & m) mem_u8(seg, p) = c;
        } while (--n);
        bx = u16(bx - 0x140);
    } while (--rows);
}

// 1432:000c gfx_read_bitmap (13h path 0314): the inverse: bit set where the pixel is the colour.
void gfx_read_bitmap(u16 bits_ds, u16 bytes_per_row, u16 rows)
{
    if (!mode13()) return;  // PORT: other modes
    const u16 seg = ds_u16(DS_gfx_draw_seg);
    u16 bx = addr13(ds_u16(DS_gfx_pen_x), ds_u16(DS_gfx_pen_y));
    const u8 c = ds_u8(DS_gfx_colour);
    u16 si = bits_ds;
    do {
        u16 p = bx;
        u16 n = bytes_per_row;
        do {
            u8 b = 0;
            for (u8 m = 0x80; m; m >>= 1, p++)
                if (mem_u8(seg, p) == c) b |= m;
            ds_u8(si++) = b;
        } while (--n);
        bx = u16(bx - 0x140);
    } while (--rows);
}

// 15a4:0006 gfx_copy_rect (13h path 019b): a rectangle of page src_page (x0..x1, rows y1 up to
// y0) to page dst_page at (dx, dy_bottom), rows upward. The interleaved layouts (mode_interleave
// 2000h, 6000h) have their own steps; mode 13h has none.
void gfx_copy_rect(u16 x0, u16 x1, u16 y0, u16 y1, u16 dx, u16 dy_bottom, u16 src_page, u16 dst_page)
{
    u16 rows = u16(y1 + 1 - y0);
    ds_u16(DS_fill_x0) = dx;
    ds_u16(DS_copy_dst_y) = dy_bottom;
    if (!mode13()) return;  // PORT: other modes
    u16 si = addr13(x0, y1);
    const u16 width = u16(x1 + 1 - x0);
    u16 di = addr13(ds_u16(DS_fill_x0), ds_u16(DS_copy_dst_y));
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

// 14b5:000d gfx_put_pixel (13h path 0169): one pixel in the current colour, inside the clip box.
void gfx_put_pixel(s16 x, s16 y)
{
    if (x < ds_s16(DS_clip_x_min) || x > ds_s16(DS_clip_x_max)) return;
    if (y < ds_s16(DS_clip_y_min) || y > ds_s16(DS_clip_y_max)) return;
    if (!mode13()) return;  // PORT: other modes
    mem_u8(ds_u16(DS_gfx_draw_seg), addr13(u16(x), u16(y))) = ds_u8(DS_gfx_colour);
}

// 1390:006e picture_hline: x0..x1 of the pen row in the current colour.
void picture_hline(u16 x0, u16 x1)
{
    ds_u16(DS_fill_x0) = x0;
    ds_u16(DS_fill_x1) = x1;
    if (!mode13()) return;  // PORT: other modes
    const u16 seg = ds_u16(DS_gfx_draw_seg);
    u16 di = addr13(x0, ds_u16(DS_gfx_pen_y));
    const u8 c = ds_u8(DS_gfx_colour);
    for (u16 n = u16(ds_u16(DS_fill_x1) + 1 - ds_u16(DS_fill_x0)); n; n--) mem_u8(seg, di++) = c;
}

// 1390:0000 picture_draw (platform.md §6): (colour, count) runs from DS:src at the pen, `width`
// pixels per row; a run that passes the right edge continues on the row above, at the pen's x.
// The colour and the pen row are restored afterwards.
void picture_draw(u16 src_ds, s16 runs, u16 width)
{
    const u8 saved_colour = ds_u8(DS_gfx_colour);
    const u16 saved_y = ds_u16(DS_gfx_pen_y);
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
            picture_hline(x, end);
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
