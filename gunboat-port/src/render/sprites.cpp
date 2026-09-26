// Sprite images (render3d.md §5.7): the apparent size and view angle of an object, the scaled
// image built into its cache slot (far segments D883/D885) from the kind's rows in the world data,
// and the blitter that places it on the drawing page.
//
// The scalers walk bit patterns: the row pattern D8A4..D8A6 (24 bits, one per source row, shifted
// out top bit first) and the column pattern D889.. (bytes rotated left as their bits are used;
// BX = the byte, DL = its bits left). They are transcribed register by register.
#include "render/render.hpp"
#include "render/modes.hpp"

#include <utility>

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// Shared scratch bytes of the simulation used here: B7F8 (shot_elevation) is the width of the
// sprite or part being scaled or placed, B7E2 the first view row of a blit, B7EA (vec_product_hi)
// the view's vertical offset for the blit.
constexpr u16 SPRITE_WIDTH = DS_shot_elevation;
constexpr u16 BLIT_ROW = DS_scratch_b7e2;
constexpr u16 BLIT_HORIZON = DS_vec_product_hi;

u8 rol8(u8 v, unsigned n)
{
    n &= 7;
    return n ? u8(v << n | v >> (8 - n)) : v;
}

// rol byte, 1: returns the carry (the old bit 7).
bool rol1(u8 &b)
{
    const bool cf = b & 0x80;
    b = u8(b << 1 | b >> 7);
    return cf;
}

u8 &pattern(u16 bx) { return ds_u8(u16(DS_sprite_column_pattern + bx)); }

// The next row bit: shl D8A4 / rcl D8A5 / rcl D8A6, the carry out of D8A6.
bool row_pattern_next()
{
    u8 &a = ds_u8(DS_sprite_row_pattern), &b = ds_u8(u16(DS_sprite_row_pattern + 1)),
       &c = ds_u8(u16(DS_sprite_row_pattern + 2));
    const bool ca = a & 0x80, cb = b & 0x80, cc = c & 0x80;
    a = u8(a << 1);
    b = u8(b << 1 | ca);
    c = u8(c << 1 | cb);
    return cc;
}

// A column walk position: pattern byte bx, dl bits left in it.
struct Walk {
    u16 bx = 0;
    u8 dl = 8;
    bool rotate() { return rol1(pattern(bx)); }
    void step()
    {
        if (--dl == 0) {
            bx++;
            dl = 8;
        }
    }
    // mov cl, dl / rol byte [bx + pattern], cl: completes the current byte's rotation.
    void finish_byte() { pattern(bx) = rol8(pattern(bx), dl); }
};

struct Out {
    u16 es, di;
    void put(u8 v)
    {
        mem_u8(es, di) = v;
        di = u16(di + 1);
    }
};

// The two-face (box) rows 6244/6373/64ac. `zoom` = 0 (6244: a kept face bit counts one column
// when the column bit is set too), 1 (6373: a face bit gives one column, two with the column bit)
// or 2 (64ac: two, three).
u16 row_box(u16 es, u16 bx, u16 si, u8 cl, u16 di, int zoom)
{
    si = u16(si + 1);
    u8 dl = ds_u8(si);
    u8 al = u8(u8(ds_u8(DS_sprite_view) + 1) >> 1);
    u8 ch = 0, dh = 0;
    if (al & 0x10) bx = u16(bx + cl + dl);
    u8 ah = ds_u8(SPRITE_WIDTH);
    dh = ds_u8(DS_sprite_side);
    if (al & 8) {
        bx = u16(bx + cl);
        std::swap(cl, dl);
        std::swap(dh, ah);
    }
    ch = dl;
    dl = ah;
    const u16 f = u16((al & 7) << 1);
    ds_u8(DS_sprite_face_a) = ds_u8(u16(DS_sprite_face_patterns + f));
    ds_u8(DS_sprite_face_b) = ds_u8(u16(DS_sprite_face_patterns + f + 1));
    si = bx;
    const u8 face_a_cols = cl, face_b_cols = ch, width = dl, other = dh;
    u8 count = 0;
    // One face bit of the count: how many output columns it gives.
    auto count_bit = [&](bool face_bit, Walk &w) {
        if (face_bit) {
            count = u8(count + zoom);
            if (w.rotate()) count++;
        } else {
            w.rotate();
        }
        w.step();
    };
    // Face B's columns beyond the width (the part turned away): counted from bit width + B.
    Walk w;
    dh = u8(other - face_b_cols);
    if (dh != 0) {
        ah = u8(width + face_b_cols);
        u8 bits = rol8(ds_u8(DS_sprite_face_b), face_b_cols);
        w.bx = u8(ah >> 3);
        w.dl = 8;
        ah &= 7;
        if (ah) pattern(w.bx) = rol8(pattern(w.bx), ah);
        w.dl = u8(w.dl - ah);
        do count_bit(rol1(bits), w);
        while (--dh != 0);
        w.finish_byte();
    }
    // Face A's columns before the face: from bit 0.
    w.bx = 0;
    ah = u8(width - face_a_cols);
    w.dl = 8;
    if (ah != 0) {
        do count_bit(rol1(ds_u8(DS_sprite_face_a)), w);
        while (--ah != 0);
    }
    ds_u8(DS_sprite_pad) = count;
    Out out{es, di};
    // Left padding: half the counted columns, rounded up.
    {
        u8 n = ds_u8(DS_sprite_pad);
        if (n != 0) {
            const bool odd = n & 1;
            n >>= 1;
            for (; n; n--) out.put(0);
            if (odd) out.put(0);
        }
    }
    auto pixels = [&](u16 face, u8 n) {
        do {
            const u8 px = ds_u8(si);
            si = u16(si + 1);
            if (rol1(ds_u8(face))) {
                for (int k = 0; k < zoom; k++) out.put(px);
                if (w.rotate()) out.put(px);
            } else {
                w.rotate();
            }
            w.step();
        } while (--n != 0);
    };
    pixels(DS_sprite_face_a, face_a_cols);
    pixels(DS_sprite_face_b, face_b_cols);
    for (u8 n = u8(ds_u8(DS_sprite_pad) >> 1); n; n--) out.put(0);
    w.finish_byte();
    return out.di;
}

// The scrolling-strip (turn) rows 65eb/66a7/6764: the row's B7FA pixels start at the part of the
// strip the view angle turns in (B7FA x view / 32), the column pattern at width x view / 32.
// `copies` = the columns every kept pixel gives before the pattern bit (0, 1, 2).
u16 row_turn(u16 es, u16 bx, u8 cl, u16 di, int copies)
{
    const u8 n = cl & 0x7F;
    ds_u8(DS_sprite_row_pixels) = n;
    const u8 view = ds_u8(DS_sprite_view);
    u16 si = u16(bx + (u16(n * view) >> 5 & 0x1FF));
    const u16 start = u16(u16(ds_u8(SPRITE_WIDTH) * view) >> 5 & 0x1FF);
    Walk w;
    w.bx = u16(start >> 3);
    u16 cx = start & 7;
    if (cx != 0) {
        w.dl = u8(w.dl - cx);
        do w.rotate();
        while (--cx != 0);
    }
    ds_u8(DS_sprite_pad) = u8(u8(ds_u8(SPRITE_WIDTH) - ds_u8(DS_sprite_row_pixels)) >> 1);
    Out out{es, di};
    auto column = [&](bool keep, u8 px) {
        for (int k = 0; k < copies; k++) out.put(px);
        if (keep) out.put(px);
        w.step();
    };
    cx = ds_u8(DS_sprite_pad);
    if (cx != 0) {
        do column(w.rotate(), 0);
        while (--cx != 0);
    }
    cx = ds_u8(DS_sprite_row_pixels);
    do {
        const bool keep = w.rotate();
        const u8 px = ds_u8(si);
        si = u16(si + 1);
        column(keep, px);
    } while (--cx != 0);
    cx = ds_u8(DS_sprite_pad);
    if (cx != 0) {
        do column(w.rotate(), 0);
        while (--cx != 0);
    }
    cx = w.dl;
    do w.rotate();
    while (--cx != 0);
    return out.di;
}

// The centred (flat) rows 6824/68a4/6925: B7FA = type & 3Fh pixels between (width - B7FA) / 2
// padding columns each side, the column pattern from its start.
u16 row_flat(u16 es, u16 bx, u8 cl, u16 di, int copies)
{
    ds_u8(DS_sprite_row_pixels) = cl & 0x3F;
    u16 si = bx;
    Walk w;
    ds_u8(DS_sprite_pad) = u8(u8(ds_u8(SPRITE_WIDTH) - ds_u8(DS_sprite_row_pixels)) >> 1);
    Out out{es, di};
    auto column = [&](bool keep, u8 px) {
        for (int k = 0; k < copies; k++) out.put(px);
        if (keep) out.put(px);
        w.step();
    };
    u16 cx = ds_u8(DS_sprite_pad);
    if (cx != 0) {
        do column(w.rotate(), 0);
        while (--cx != 0);
    }
    cx = ds_u8(DS_sprite_row_pixels);
    do {
        const bool keep = w.rotate();
        const u8 px = ds_u8(si);
        si = u16(si + 1);
        column(keep, px);
    } while (--cx != 0);
    cx = ds_u8(DS_sprite_pad);
    if (cx != 0) {
        do column(w.rotate(), 0);
        while (--cx != 0);
    }
    cx = w.dl;
    do w.rotate();
    while (--cx != 0);
    return out.di;
}

// The rows of one part: from the row pointer table SI (offsets into the world B data 53A8) and the
// row-type table CX, B7F9 rows, until the slot end D87F would be passed. Each output row's repeat
// count goes to ES:[D87B]; the width (the last row's bytes) and the output row count to ES:[D87D].
// zoom 0: a row is kept when its row-pattern bit is set, repeated once; zoom >= 1: every row,
// repeated D865 times plus one more when the bit is set.
u16 scale_rows(u16 es, u16 cx, u16 si, u16 di, int zoom)
{
    ds_u8(DS_sprite_out_rows) = 0;
    u16 dx = 0;
    for (;;) {
        if (zoom == 0) {
            if (row_pattern_next()) {
                ds_u8(DS_sprite_out_rows)++;
                const u16 bx = u16(ds_u16(si) + DS_world_b_data);
                mem_u8(es, ds_u16(DS_sprite_repeat_ptr)) = 1;
                const u8 type = ds_u8(cx);
                const u16 entry = di;
                if (type & 0x80) di = sprite_row_turn(es, bx, type, di);
                else if (type & 0x40) di = sprite_row_flat(es, bx, type, di);
                else di = sprite_row_box(es, bx, cx, type, di);
                ds_u16(DS_sprite_repeat_ptr)++;
                dx = u16(di - entry);
            }
        } else {
            const u16 entry = di;
            ds_u8(DS_sprite_out_rows) = u8(ds_u8(DS_sprite_out_rows) + ds_u8(DS_sprite_zoom));
            const u16 bx = u16(ds_u16(si) + DS_world_b_data);
            const u8 type = ds_u8(cx);
            if (zoom == 1) {
                if (type & 0x80) di = sprite_row_turn_up(es, bx, type, di);
                else if (type & 0x40) di = sprite_row_flat_up(es, bx, type, di);
                else di = sprite_row_box_up(es, bx, cx, type, di);
            } else {
                if (type & 0x80) di = sprite_row_turn_up2(es, bx, type, di);
                else if (type & 0x40) di = sprite_row_flat_up2(es, bx, type, di);
                else di = sprite_row_box_up2(es, bx, cx, type, di);
            }
            dx = u16(di - entry);
            const u16 p = ds_u16(DS_sprite_repeat_ptr);
            mem_u8(es, p) = ds_u8(DS_sprite_zoom);
            if (row_pattern_next()) {
                ds_u8(DS_sprite_out_rows)++;
                mem_u8(es, p)++;
            }
            ds_u16(DS_sprite_repeat_ptr)++;
        }
        cx = u16(cx + 2);
        si = u16(si + 2);
        if (--ds_u8(DS_sprite_rows_left) == 0) break;
        // Room for another row of this width? When not, DI is left past the last row (kept: the
        // second part then starts there).
        di = u16(di + dx);
        if (di >= ds_u16(DS_sprite_slot_end)) break;
        di = u16(di - dx);
    }
    const u16 p = ds_u16(DS_sprite_size_ptr);
    mem_u8(es, p) = u8(dx);
    mem_u8(es, u16(p + 1)) = ds_u8(DS_sprite_out_rows);
    return di;
}

u16 rows_by_zoom(u16 es, u16 cx, u16 si, u16 di)
{
    const u8 zoom = ds_u8(DS_sprite_zoom);
    if (zoom < 1) return sprite_scale_rows(es, cx, si, di);
    if (zoom == 1) return sprite_scale_rows_up(es, cx, si, di);
    return sprite_scale_rows_up2(es, cx, si, di);
}

} // namespace

u16 sprite_row_box(u16 es, u16 bx, u16 si, u8 cl, u16 di) { return row_box(es, bx, si, cl, di, 0); }
u16 sprite_row_box_up(u16 es, u16 bx, u16 si, u8 cl, u16 di) { return row_box(es, bx, si, cl, di, 1); }
u16 sprite_row_box_up2(u16 es, u16 bx, u16 si, u8 cl, u16 di) { return row_box(es, bx, si, cl, di, 2); }
u16 sprite_row_turn(u16 es, u16 bx, u8 cl, u16 di) { return row_turn(es, bx, cl, di, 0); }
u16 sprite_row_turn_up(u16 es, u16 bx, u8 cl, u16 di) { return row_turn(es, bx, cl, di, 1); }
u16 sprite_row_turn_up2(u16 es, u16 bx, u8 cl, u16 di) { return row_turn(es, bx, cl, di, 2); }
u16 sprite_row_flat(u16 es, u16 bx, u8 cl, u16 di) { return row_flat(es, bx, cl, di, 0); }
u16 sprite_row_flat_up(u16 es, u16 bx, u8 cl, u16 di) { return row_flat(es, bx, cl, di, 1); }
u16 sprite_row_flat_up2(u16 es, u16 bx, u8 cl, u16 di) { return row_flat(es, bx, cl, di, 2); }

// 0919:5f4b, 0919:5fc8, 0919:6054: the rows of a part at zoom 0, 1 and 2-3.
u16 sprite_scale_rows(u16 es, u16 cx, u16 si, u16 di) { return scale_rows(es, cx, si, di, 0); }
u16 sprite_scale_rows_up(u16 es, u16 cx, u16 si, u16 di) { return scale_rows(es, cx, si, di, 1); }
u16 sprite_scale_rows_up2(u16 es, u16 cx, u16 si, u16 di) { return scale_rows(es, cx, si, di, 2); }

// 0919:6174 sprite_scale_patterns (render3d.md §5.7): from the size D864: the zoom D865 (+1 for
// each 18h above 18h and 30h) and the scale table record of the rest: 7 bytes of column pattern
// (repeated to 26 bytes at D889) and 3 bytes of row pattern (D8A4); a size of 48h and more sets the
// zoom to 3 and takes the row pattern from the record of the size 18h lower.
void sprite_scale_patterns()
{
    auto record = [](u8 ah) -> u16 {
        if (ah >= 0x18) return 0;
        const u8 a = u8(-ah + 0x17);
        return u16(u8(u8(a << 2) + a) << 1);
    };
    u8 ah = ds_u8(DS_sprite_size);
    if (ah >= 0x18) {
        ah = u8(ah - 0x18);
        ds_u8(DS_sprite_zoom)++;
        if (ah >= 0x18) {
            ds_u8(DS_sprite_zoom)++;
            ah = u8(ah - 0x18);
        }
    }
    u16 bx = u16(record(ah) + CS_sprite_scale_table);
    auto word = [](u16 at) { return seg_u16(CSSEG_sprite_scale_table, at); };
    // The 7 column-pattern bytes of the record, four times over D889..D8A2 (7 bytes apart).
    auto columns = [](u16 k) { return u16(DS_sprite_column_pattern + k); };
    u16 cx = word(bx);
    for (u16 k : {0, 7, 14, 21}) ds_u16(columns(k)) = cx;
    bx = u16(bx + 2);
    cx = word(bx);
    for (u16 k : {2, 9, 16, 23}) ds_u16(columns(k)) = cx;
    bx = u16(bx + 2);
    cx = word(bx);
    for (u16 k : {4, 11, 18}) ds_u16(columns(k)) = cx;
    ds_u8(columns(25)) = u8(cx);
    bx = u16(bx + 2);
    cx = word(bx);
    for (u16 k : {6, 13, 20}) ds_u8(columns(k)) = u8(cx);
    if (ah >= 0x18) {
        ah = u8(ah - 0x18);
        ds_u8(DS_sprite_zoom) = 3;
        bx = u16(record(ah) + CS_sprite_scale_table + 6);
        cx = word(bx);
    }
    ds_u8(DS_sprite_row_pattern) = u8(cx >> 8);
    bx = u16(bx + 2);
    ds_u16(u16(DS_sprite_row_pattern + 1)) = word(bx);
}

// 0919:60e0 sprite_view_angle (render3d.md §5.7): entry BX at distance DX. The view angle D86A =
// the object's facing (flags bits 0-2 x 32; the boat, entry offset 0: hull heading + 80h) + the
// negated bearing 50D4, 0 for the kinds 39h and >= 3Fh; the kind D868; the apparent size D864 =
// the angle of (size = (DS:53AC[kind] & FCh) x 8) against the distance, from the atan table.
void sprite_view_angle(u16 bx, u16 dx)
{
    const u8 neg_bearing = ds_u8(u16(DS_visible_neg_bearing + bx));
    const u16 object = ds_u16(u16(DS_visible_object + u16(bx << 1)));
    u8 al = u8(ds_u8(DS_heading) + 0x80);
    if (object != 0) al = u8((ds_u8(u16(DS_object_word + 1 + object)) & 7) << 5);
    al = u8(al + neg_bearing);
    const u8 kind = ds_u8(u16(DS_object_word + object));
    ds_u8(DS_sprite_kind) = kind;
    if (kind == 0x39 || kind >= 0x3F) al = 0;
    ds_u8(DS_sprite_view) = al;
    const u16 cx = u16((ds_u8(u16(DS_kind_sprite_info + kind)) & 0xFC) << 3);
    ds_u8(DS_sprite_zoom) = 0;
    ds_u8(DS_scratch_b7f5) = 0;
    u8 ratio = 0xFF;
    if (cx != dx) {
        u16 larger = dx, smaller = cx;
        if (cx > dx) {
            larger = cx;
            smaller = dx;
            ds_u8(DS_scratch_b7f5) = 4;
        }
        if (larger == 0) {
            ds_u8(DS_sprite_size) = 0;
            return;
        }
        ratio = u8(div32_16(u32(smaller) << 16, larger) >> 8);
    }
    const u8 v = rol8(seg_u8(CSSEG_atan_table, u16(CS_atan_table + ratio)), 5);
    u16 a = u16(u16(v << 8 | v) & 0x1FE0);
    if (ds_u8(DS_scratch_b7f5) != 0) {
        a = u16(-a);
        a = u16(u8(u8(a >> 8) + 0x40) << 8 | u8(a));
    }
    ds_u8(DS_sprite_size) = u8(a >> 8);
}

// 0919:5aeb sprite_cache_build (render3d.md §5.7): the image of kind D868 at size D864 and view
// (D86A + 2) / 4 (the kinds 30h/31h animated by D70C; the kinds 39h-3Bh at even sizes) in slot
// D863, unless the slot already holds exactly that (always rebuilt for kind 17h). The record: kind,
// size, view; width and rows of the part(s) (+3, +5), the second part's column ratio (+7), 24 row
// repeat counts (+8), the pixels (+20h). The kind's record in the world A data (8 bytes at 6E54 +
// 8 x kind): width and rows, the row pointer and row type tables, the side width and the second
// part (a record at 6E54 + [6E5A] + 8 x index).
void sprite_cache_build()
{
    u16 es = ds_u16(DS_sprite_segment_a);
    const u8 slot = ds_u8(DS_sprite_slot);
    if (slot < 0x69) es = ds_u16(DS_sprite_segment_b);
    u16 si = u16(u16(u8(slot - 1) << 1) + CS_sprite_slot_pointers);
    u16 di = seg_u16(CSSEG_sprite_slot_pointers, si);
    const u16 cached = mem_u16(es, di);
    di = u16(di + 2);
    ds_u16(DS_sprite_record) = di;
    u8 view = ds_u8(DS_sprite_view);
    const u8 kind = ds_u8(DS_sprite_kind);
    if (kind >= 0x39 && kind < 0x3C) ds_u8(DS_sprite_size) &= 0xFE;
    if (kind == 0x30) view = u8(view + ds_u8(DS_world_pass_counter));
    if (kind == 0x30 || kind == 0x31) view = u8(view + ds_u8(DS_world_pass_counter));
    view = u8(u8(view + 2) >> 2);
    if (u8(cached) == kind && u8(cached >> 8) == ds_u8(DS_sprite_size) && view == mem_u8(es, di) &&
        u8(cached) != 0x17)
        return;
    ds_u8(DS_sprite_view) = view;
    mem_u8(es, di) = view;
    di = u16(di - 1);
    mem_u8(es, di) = ds_u8(DS_sprite_size);
    di = u16(di - 1);
    mem_u8(es, di) = kind;
    si = u16(si + 2);
    ds_u16(DS_sprite_slot_end) = seg_u16(CSSEG_sprite_slot_pointers, si);
    sprite_scale_patterns();
    di = u16(di + 3);
    ds_u16(DS_sprite_size_ptr) = di;
    di = u16(di + 5);
    ds_u16(DS_sprite_repeat_ptr) = di;
    di = u16(di + 0x18);
    const u16 k = u16(ds_u8(DS_sprite_kind) << 3);
    u16 w = ds_u16(u16(DS_world_a_data + k));
    ds_u8(SPRITE_WIDTH) = u8(w);
    ds_u8(DS_sprite_rows_left) = u8(w >> 8);
    w = ds_u16(u16(DS_world_a_data + 6 + k));
    ds_u8(DS_sprite_side) = u8(w);
    ds_u8(u16(DS_sprite_side + 1)) = u8(w >> 8);
    const u16 part = u16(u16(u8(w >> 8) << 3) + ds_u16(u16(DS_world_a_data + 6)));
    w = ds_u16(u16(DS_world_a_data + part));
    ds_u8(DS_sprite_part_width) = u8(w);
    ds_u8(DS_sprite_part_rows) = u8(w >> 8);
    u8 shift = 0;
    const u16 side = ds_u16(u16(DS_world_a_data + 6 + part));
    if (ds_u8(DS_sprite_part_width) != 0) shift = u8(div16_8(u16(u8(side) << 8), ds_u8(DS_sprite_part_width)));
    ds_u8(DS_sprite_part_shift) = shift;
    ds_u16(DS_sprite_part_types) = u16(ds_u16(u16(DS_world_a_data + 4 + part)) + DS_world_a_data);
    ds_u16(DS_sprite_part_data) = u16(ds_u16(u16(DS_world_a_data + 2 + part)) + DS_world_a_data);
    const u16 types = u16(ds_u16(u16(DS_world_a_data + 4 + k)) + DS_world_a_data);
    si = u16(ds_u16(u16(DS_world_a_data + 2 + k)) + DS_world_a_data);
    di = rows_by_zoom(es, types, si, di);
    ds_u16(DS_sprite_size_ptr) = u16(ds_u16(DS_sprite_size_ptr) + 2);
    const u8 width2 = ds_u8(DS_sprite_part_width);
    if (width2 == 0) {
        mem_u8(es, ds_u16(DS_sprite_size_ptr)) = 0;
        mem_u8(es, u16(ds_u16(DS_sprite_size_ptr) + 1)) = 0;
        return;
    }
    ds_u8(SPRITE_WIDTH) = width2;
    ds_u8(DS_sprite_rows_left) = ds_u8(DS_sprite_part_rows);
    rows_by_zoom(es, ds_u16(DS_sprite_part_types), ds_u16(DS_sprite_part_data), di);
    mem_u8(es, u16(ds_u16(DS_sprite_size_ptr) + 2)) = ds_u8(DS_sprite_part_shift);
}

// 0919:5acc sprite_prepare (render3d.md §5.7): the view and size of entry BX (the size also to
// 501F), then its image in its slot.
void sprite_prepare(u16 bx)
{
    const u16 b2 = u16(bx << 1);
    bx = u16(b2 >> 1);
    sprite_view_angle(bx, ds_u16(u16(DS_visible_distance_word + b2)));
    ds_u8(u16(DS_visible_size + bx)) = ds_u8(DS_sprite_size);
    ds_u8(DS_sprite_slot) = ds_u8(u16(DS_visible_sprite + bx));
    sprite_cache_build();
}

// 0919:5e66 blit_rows_vga (render3d.md §5.7): rows from view row AL (screen row 48 + AL + 16),
// column 2 x B7F7 + the half column D873 (40 when clipped at the left, the source then skipping
// D875 columns), D862 columns each, zero pixels transparent, from the cache record in D887 at SI;
// each source row is repeated by its count at D87B. Rows past the page end stop it.
void blit_rows_vga(u8 al, u16 si)
{
    const u8 width = ds_u8(DS_sprite_blit_width);
    u8 repeat = ds_u8(DS_sprite_row_repeat);
    u16 ax = u16(u16(al << 8) >> 2);
    ax = u16(u8(u8(ax >> 8) + ds_u8(BLIT_ROW)) << 8 | u8(ax));
    ax = u16(ax + 0x3C00);
    u16 bx = u16(u16(ds_u8(DS_sprite_column) << 1) + ds_u16(DS_sprite_half_column));
    if (ds_u16(DS_sprite_clip_left) != 0) {
        bx = 0x28;
        si = u16(si + ds_u16(DS_sprite_clip_left));
    }
    u16 di = u16(ax + bx);
    if (di >= 0xA000) return;
    const u16 page = ds_u16(DS_draw_page_segment);
    const u16 record = ds_u16(DS_sprite_segment);
    do {
        // The row copy (0919:5ee5-5f1c) moves words and a last byte; a zero byte is skipped. Its
        // net effect is this byte loop (DI and SI are reset after the row).
        for (u16 k = 0; k < width; k++) {
            const u8 px = mem_u8(record, u16(si + k));
            if (px) mem_u8(page, u16(di + k)) = px;
        }
        di = u16(di + 0x140);
        if (--repeat == 0) {
            ds_u16(DS_sprite_repeat_ptr)++;
            repeat = mem_u8(record, ds_u16(DS_sprite_repeat_ptr));
            si = u16(si + ds_u8(SPRITE_WIDTH));
        }
        if (di >= 0xA000) return;
    } while (--ds_u8(DS_sprite_rows_left) != 0);
}

// 0919:5d5a blit_place (render3d.md §5.7): places a part of B7F8 columns and B7F9 rows (source
// ES:SI) of entry BX: the column from the bearing B7F7 less half the width (and the part offset
// D86C), clipped to columns 40..296 (one column past the view on the right, kept); the first row
// from the inverse distance 4F6A / 8 and the view's pitch, less the part height D86D; nothing when
// it starts at view row 50h or below; rows above the view (< 10h) are skipped; D965 lowered to the
// first row - 10h.
void blit_place(u16 es, u16 bx, u16 si)
{
    ds_u16(DS_sprite_clip_left) = 0;
    u16 cx = ds_u8(SPRITE_WIDTH);
    u8 al = u8(u8(cx) >> 1);
    al = u8(al + ds_u8(DS_sprite_part_offset));
    al = u8(al >> 1);
    al = u8(-al + ds_u8(DS_sprite_column) + 8);
    ds_u8(DS_sprite_column) = al;
    ds_u8(DS_sprite_blit_width) = u8(cx);
    u16 ax = u16(al << 1);
    if (ax < 0x28) {
        ax = u16(-ax + 0x28);
        ds_u16(DS_sprite_clip_left) = ax;
        ax = u16(-ax + cx);
        if ((ax & 0x8000) || ax == 0) return;
        ds_u8(DS_sprite_blit_width) = u8(ax);
    } else if (ax < 0x128) {
        ax = u16(-ax + 0x128);
        if (ax < cx) ds_u8(DS_sprite_blit_width) = u8(ax + 1);
    } else {
        ax = u16(ax | 0xFE00);
        ax = u16(ax + cx);
        if (ax & 0x8000) return;
        ax = u16(ax - 0x28);
        if ((ax & 0x8000) || ax == 0) return;
        ds_u8(DS_sprite_blit_width) = u8(ax);
        cx = u16((cx & 0xFF00) | u8(u8(cx) - u8(ax)));
        ds_u16(DS_sprite_clip_left) = cx;
    }
    ds_u8(BLIT_HORIZON) = u8(u8(u8(-ds_u8(DS_pitch_reference)) >> 3) + ds_u8(DS_view_pitch) - 0x2C);
    u8 row = u8(u8(ds_u8(u16(DS_visible_elevation + bx)) >> 3) + ds_u8(BLIT_HORIZON) - ds_u8(DS_sprite_part_height));
    if (s8(row) >= 0x50) return;
    u8 top = u8(row - 0x10);
    if (top & 0x80) top = 0;
    if (top < ds_u8(DS_view_sky_top)) ds_u8(DS_view_sky_top) = top;
    u16 di = ds_u16(DS_sprite_repeat_ptr);
    ds_u8(DS_sprite_row_repeat) = mem_u8(es, di);
    cx = ds_u8(SPRITE_WIDTH);
    while (s8(row) < 0x10) {
        if (--ds_u8(DS_sprite_row_repeat) == 0) {
            di = u16(di + 1);
            ds_u8(DS_sprite_row_repeat) = mem_u8(es, di);
            si = u16(si + cx);
        }
        if (--ds_u8(DS_sprite_rows_left) == 0) return;
        row = u8(row + 1);
    }
    ds_u16(DS_sprite_repeat_ptr) = di;
    ds_u8(BLIT_ROW) = row;
    // The mode's row copier (0919:5e3f, on the low byte of EED2): VGA 13h, EGA 0Dh, CGA 4 (only),
    // Tandy every other mode.
    // TODO(verify): the EGA and Tandy branches (render/modes.hpp).
    const u8 mode = u8(ds_u16(DS_video_mode));
    if (mode == 0x13) blit_rows_vga(row, si);
    else if (mode == 0x0D) blit_rows_ega(row, si);
    else if (mode >= 9 && mode < 0x0D) blit_rows_tandy(row, si);
    else if (mode == 4) blit_rows_cga(row, si);
    else render_parked("the sprite row copier");
}

// 0919:5c71 blit_record (render3d.md §5.7): draws entry BX's cached image (not kind 39h): the
// first part, then a second part (D870 columns, D871 rows) stacked on it, shifted right by
// D870 x D872 / 512 and starting at the source row where the first part's repeat counts reach its
// height.
void blit_record(u16 bx)
{
    if (ds_u8(DS_sprite_kind) == 0x39) return;
    u16 es = ds_u16(DS_sprite_segment_a);
    if (ds_u8(DS_sprite_slot) < 0x69) es = ds_u16(DS_sprite_segment_b);
    ds_u16(DS_sprite_segment) = es;
    u16 si = u16(ds_u16(DS_sprite_record) + 1);
    const u8 width = mem_u8(es, si), height = mem_u8(es, u16(si + 1));
    if (width == 0 || height == 0) return;
    ds_u8(SPRITE_WIDTH) = width;
    ds_u8(DS_sprite_rows_left) = height;
    ds_u8(DS_sprite_part_height) = height;
    const u8 bearing = ds_u8(u16(DS_visible_bearing + bx));
    ds_u8(DS_sprite_column) = bearing;
    ds_u16(DS_sprite_half_column) = u16(ds_u8(u16(DS_visible_fine_bearing + bx)) >> 2 & 1);
    const u16 part = mem_u16(es, u16(si + 2));
    if (part == 0) {
        si = u16(si + 5);
        ds_u16(DS_sprite_repeat_ptr) = si;
        si = u16(si + 0x18);
        ds_u8(DS_sprite_part_offset) = 0;
        blit_place(es, bx, si);
        return;
    }
    ds_u8(DS_sprite_part_width) = u8(part);
    ds_u8(DS_sprite_part_rows) = u8(part >> 8);
    ds_u8(DS_sprite_part_shift) = mem_u8(es, u16(si + 4));
    si = u16(si + 5);
    ds_u16(DS_sprite_repeat_ptr) = si;
    u16 di = si;
    si = u16(si + 0x18);
    ds_u8(DS_sprite_part_offset) = 0;
    blit_place(es, bx, si);
    ds_u8(DS_sprite_column) = bearing;
    if (ds_u8(DS_sprite_part_width) == 0) return;
    ds_u8(DS_sprite_rows_left) = height;
    u8 rows = 0, left = height;
    for (;;) {
        left = u8(left - mem_u8(es, di));
        if (left == 0) break;
        di = u16(di + 1);
        rows++;
    }
    di = u16(di + 1);
    rows++;
    ds_u16(DS_sprite_repeat_ptr) = di;
    si = u16(si + u16(rows * ds_u8(SPRITE_WIDTH)));
    const u8 width2 = ds_u8(DS_sprite_part_width);
    ds_u8(SPRITE_WIDTH) = width2;
    ds_u8(DS_sprite_part_offset) = u8(u16(u16(width2 * ds_u8(DS_sprite_part_shift)) >> 1) >> 8);
    const u8 rows2 = ds_u8(DS_sprite_part_rows);
    if (rows2 == 0) return;
    ds_u8(DS_sprite_rows_left) = rows2;
    ds_u8(DS_sprite_part_height) = u8(rows2 + ds_u8(DS_sprite_part_height));
    blit_place(es, bx, si);
}

} // namespace gb
