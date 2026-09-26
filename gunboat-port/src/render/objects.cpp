// The visible-object list: its rebuild, projection and sorting, and the sprite cache slots of its
// entries (render3d.md §5.1-§5.3, §5.6).
#include "render/render.hpp"

#include <utility>

#include "host.hpp"
#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

u16_m &dist(u16 o) { return ds_u16(u16(DS_visible_distance_word + o)); }
u16_m &entry_object(u16 o) { return ds_u16(u16(DS_visible_object + o)); }

// Frees the sprite cache slot of entry si - 1 (DS:5188 + si), if it has one: the slot byte = 0 and
// its bit in the slot bitmap cleared (0919:6ac6-6ae4, 6d47-6d65, 6d6f-6d8d).
void slot_free(u16 si)
{
    u8 &slot = ds_u8(u16(DS_visible_sprite - 1 + si));
    if (slot == 0) return;
    const u8 n = u8(slot - 1);
    slot = 0;
    ds_u8(u16(DS_slot_bitmap + (n >> 3))) &= ds_u8(u16(DS_slot_clear_mask + (n & 7)));
}

// Swaps list entries at byte offsets si and si + 2: distance, object, bearing, fine bearing,
// inverse distance and negated bearing; not the sprite slot (render3d §5.3, a quirk kept).
void swap_with_next(u16 si)
{
    std::swap(dist(si), dist(u16(si + 2)));
    std::swap(entry_object(si), entry_object(u16(si + 2)));
    const u16 bx = u16(si >> 1);
    for (u16 array : {DS_visible_bearing, DS_visible_fine_bearing, DS_visible_elevation, DS_visible_neg_bearing})
        std::swap(ds_u8(u16(array + bx)), ds_u8(u16(array + bx + 1)));
}

// Is the object at offset o (2 x index) present and in the window of cells around the camera?
// dh/dl = the first cell's high bytes, bh/limit_y = the extents (0919:6a35-6a54).
bool in_window(u16 o, u8 dh, u8 bh, u8 dl)
{
    if (ds_u8(u16(DS_object_word + o)) == 0) return false;
    if (u8((ds_u8(u16(DS_object_x + 1 + o)) & 0xFC) - dh) > bh) return false;
    return u8((ds_u8(u16(DS_object_y + 1 + o)) & 0xFC) - dl) <= ds_u8(DS_scratch_b7e3);
}

// A bubble pass sequence over entries [si, di) of the list, descending distance, each pass ending
// at the last swap of the one before (list_bubble 0919:6c37-6ca3, list_bubble_range 0919:8fb3-9020).
// Returns the last swap offset of the final pass, or FFFFh when a pass made no swap.
u16 bubble_pass(u16 si, u16 di)
{
    u16 dx = dist(si);
    for (;;) {
        const u16 next = dist(u16(si + 2));
        if (dx < next) break;
        dx = next;
        si = u16(si + 2);
        if (si >= di) return 0xFFFF;
    }
    u16 last;
    for (;;) {
        swap_with_next(si);
        last = si;
        bool again = false;
        while ((si = u16(si + 2)) < di) {
            const u16 next = dist(u16(si + 2));
            if (dx < next) {
                again = true;
                break;
            }
            dx = next;
        }
        if (!again) return last;
    }
}

} // namespace

// 0919:69a9 visible_list_rebuild (render3d.md §5.1): entries 0-34 = objects 0-34; then the D9A4
// scenery objects of the terrain window; then the authored objects from 36 whose cell is in the
// window around the camera cell (1-3 cells per axis, by the visibility bits D9B2); with detail
// high also the far objects from 296 that pass the window test and whose bit of the rotating
// thinning word (a copy of D8BD in B7DC) is set; at most B5h entries. B83D = the count; the sprite
// slots of the entries beyond it (up to the previous count D8BF) are freed.
void visible_list_rebuild()
{
    entry_object(0) = 0;
    u16 si = 2;
    for (int k = 0; k < 0x11; k++) {
        entry_object(si) = si;
        si = u16(si + 2);
        entry_object(si) = si;
        si = u16(si + 2);
    }
    // PORT: with no scenery in the terrain window (D9A4 = 0) the original's LOOP runs 65536 times:
    // it writes entry offsets over all of DGROUP, its own stack included, and returns to a
    // garbage address. The shipped data has such windows: Mare Island (region 3, the practice
    // missions) has open-water cells whose whole window is scenery-free (grid cells 137 and 154:
    // boat X 05xx-07xx, Y 05xx-0Bxx), in the bay west of the practice start cell. The port stops
    // with a fatal error there instead of running on over a destroyed DGROUP.
    // TODO(verify): sail there in DOSBox (practice) to confirm the original's crash.
    u16 cx = ds_u8(DS_scenery_count);
    if (cx == 0)
        host_fatal("visible_list_rebuild (0919:69a9): no scenery in the terrain window; the original "
                   "overwrites its data and stack here and crashes");
    u16 di = u16(ds_u16(DS_authored_object_count) << 1);
    do {
        entry_object(si) = di;
        si = u16(si + 2);
        di = u16(di + 2);
    } while (--cx != 0);

    const u8 visible = ds_u8(DS_window_visibility);
    u8 dh = u8(ds_u16(DS_camera_x) >> 8) & 0xFC;
    u8 bh = 4;
    if (visible & 0xC1) {
        dh = u8(dh - 4);
        bh = u8(bh + 4);
    }
    if (!(visible & 0x1C)) bh = u8(bh - 4);
    u8 dl = u8(ds_u16(DS_camera_y) >> 8) & 0xFC;
    u8 ch = 4;
    if (visible & 0x70) {
        dl = u8(dl - 4);
        ch = u8(ch + 4);
    }
    if (!(visible & 0x07)) ch = u8(ch - 4);
    ds_u8(DS_scratch_b7e3) = ch;
    ds_u16(DS_scratch_b7dc) = ds_u16(DS_far_object_mask);
    bool full = false;
    u16 o = 0x48;
    const u16 authored_end = u16(ds_u16(DS_authored_object_count) << 1);
    do {
        if (in_window(o, dh, bh, dl)) {
            entry_object(si) = o;
            si = u16(si + 2);
            if (si >= 0x16A) {
                full = true;
                break;
            }
        }
        o = u16(o + 2);
    } while (o < authored_end);
    if (!full && ds_u8(DS_detail_low) == 0) {
        o = 0x250;
        const u16 far_end = u16(u16(ds_u16(DS_far_object_count) << 1) + o);
        do {
            u16_m &mask = ds_u16(DS_scratch_b7dc);
            const bool bit = mask & 0x8000;
            mask = u16(mask << 1 | mask >> 15);
            if (bit && in_window(o, dh, bh, dl)) {
                entry_object(si) = o;
                si = u16(si + 2);
                if (si >= 0x16A) break;
            }
            o = u16(o + 2);
        } while (o < far_end);
    }
    si >>= 1;
    ds_u16(DS_visible_count) = si;
    if (ds_u16(DS_visible_count_previous) > si) {
        do slot_free(++si);
        while (si < ds_u16(DS_visible_count_previous));
    }
    ds_u16(DS_visible_count_previous) = ds_u16(DS_visible_count);
}

// 0919:6d92 visible_project (render3d.md §5.2): for each entry with an object present (its kind
// byte compared with the entry index's high byte, 0 in practice), from the object's offset to the
// camera (atan): the distance max / cos (the sine table interpolated by the two low bits of D730,
// which are shifted out of it), doubled, 7FFFh when it overflows into bit 15 -> 4C96; the screen
// bearing (angle - view + 4Ch, one less when the fine part borrows) -> 4E00; the fine bearing
// ((B7E2 - D192) & 7) -> 4EB5; the inverse distance (FFFFh / distance, FFh under 256) -> 4F6A;
// the negated angle -> 50D4.
void visible_project()
{
    for (u16 bx = 0; bx != ds_u16(DS_visible_count); bx = u16(bx + 1)) {
        const u16 b2 = u16(bx << 1);
        const u16 si = entry_object(b2);
        bx = u16(b2 >> 1);
        if (ds_u8(u16(DS_object_word + si)) == u8(bx >> 8)) continue;
        const u16 cx = u16(u16(ds_u16(u16(DS_object_x + si)) << 2) - ds_u16(DS_camera_qx));
        const u16 dx = u16(u16(ds_u16(u16(DS_object_y + si)) << 2) - ds_u16(DS_camera_qy));
        const AtanOut a = atan(cx, dx);
        const u8 index = u8(u8(u8(-u8(ds_u8(DS_atan_raw) >> 1)) + 0xFF) >> 1);
        const u16 t = u16(CS_sine_table + u16(index << 1));
        u16 cosine = seg_u16(CSSEG_sine_table, t);
        const u16 half = u16(u16(seg_u16(CSSEG_sine_table, u16(t + 2)) - cosine) >> 1);
        u8 &raw = ds_u8(DS_atan_raw);
        bool bit = raw & 1;
        raw >>= 1;
        if (bit) cosine = u16(cosine + (half >> 1));
        bit = raw & 1;
        raw >>= 1;
        if (bit) cosine = u16(cosine + half);
        u16 distance = a.cx;
        if (cosine != 0) distance = div32_16(u32(a.dx) << 16, cosine);
        distance = u16(distance << 1);
        if (distance & 0x8000) distance = 0x7FFF;
        dist(b2) = distance;
        const u8 angle = u8(a.ax);
        ds_u8(u16(DS_visible_neg_bearing + bx)) = u8(-angle);
        ds_u8(u16(DS_visible_bearing + bx)) = u8(angle - ds_u8(DS_view_heading) + 0x4C);
        const u8 fine = ds_u8(DS_scratch_b7e2);
        if (fine < ds_u8(DS_view_heading_fraction)) ds_u8(u16(DS_visible_bearing + bx))--;
        ds_u8(u16(DS_visible_fine_bearing + bx)) = u8(fine - ds_u8(DS_view_heading_fraction)) & 7;
        const u16 d = dist(b2);
        u16 inverse = 0xFFFF;
        if (d >> 8) inverse = div32_16(0xFFFF, d);
        ds_u8(u16(DS_visible_elevation + bx)) = u8(inverse);
    }
}

// 0919:6c23 list_bubble (render3d.md §5.3): incremental bubble passes over the list by descending
// distance, from entry 1 (entry 0 too in chase view) to the count, each pass ending at the last
// swap of the one before; it stops when that swap was at entry 1 or 0.
void list_bubble()
{
    u16 cx = u16(u16(ds_u16(DS_visible_count) - 1) << 1);
    for (;;) {
        const u16 si = ds_u8(DS_chase_view) != 0 ? 0 : 2;
        const u16 last = bubble_pass(si, cx);
        if (last == 0xFFFF) return;
        cx = last;
        if (cx < 4) return;
    }
}

// 0919:8f53 list_swap (render3d.md §5.3): exchanges the entries at byte offsets SI and DI (not the
// sprite slot).
void list_swap(u16 si, u16 di)
{
    std::swap(dist(si), dist(di));
    std::swap(entry_object(si), entry_object(di));
    const u16 a = u16(si >> 1), b = u16(di >> 1);
    for (u16 array : {DS_visible_bearing, DS_visible_fine_bearing, DS_visible_elevation, DS_visible_neg_bearing})
        std::swap(ds_u8(u16(array + a)), ds_u8(u16(array + b)));
}

// 0919:8fa7 list_bubble_range (render3d.md §5.3): bubble passes over the entries first..last (byte
// offsets) by descending distance, until a pass ends at `first`.
void list_bubble_range(u16 first, u16 last)
{
    u16 di = last;
    for (;;) {
        const u16 cx = bubble_pass(first, di);
        if (cx == 0xFFFF) return;
        di = cx;
        if (!(di > first)) return;
    }
}

// 0919:8ece list_quicksort_range (render3d.md §5.3): quicksort of the entries first..last (byte
// offsets, the stride AX = 2 of every caller) by descending distance, the first entry as the
// pivot; partitions of up to 20 entries go to list_bubble_range.
void list_quicksort_range(u16 first, u16 last)
{
    const u16 pivot = dist(first);
    u16 si = u16(first + 2);
    u16 di = last;
    u16 t;
L8edd:
    if (pivot > dist(si)) goto L8f2a;
    si = u16(si + 2);
    if (si <= di) goto L8edd;
    si = first;
    if (si != di) list_swap(si, di);
L8ef3:
    di = u16(di - 2);
    t = u16(di - si);
    if (s16(t) > 0) {
        if (t > 0x28) list_quicksort_range(si, di);
        else list_bubble_range(si, di);
    }
    si = u16(di + 2);
L8f0d:
    si = u16(si + 2);
    di = last;
    t = u16(di - si);
    if (s16(t) > 0) {
        if (t > 0x28) list_quicksort_range(si, di);
        else list_bubble_range(si, di);
    }
    return;
L8f2a:
    if (pivot < dist(di)) goto L8f46;
    di = u16(di - 2);
    if (di >= si) goto L8f2a;
L8f36:
    di = u16(si - 2);
    si = first;
    if (si == di) goto L8f0d;
    list_swap(si, di);
    goto L8ef3;
L8f46:
    list_swap(si, di);
    si = u16(si + 2);
    if (di <= si) goto L8f36;
    di = u16(di - 2);
    goto L8edd;
}

// 0919:8eb9 list_quicksort (render3d.md §5.3): on scene rebuilds, entries 1 .. count - 1.
void list_quicksort()
{
    list_quicksort_range(2, u16(u16(ds_u16(DS_visible_count) - 1) << 1));
}

// 0919:6f3d sprite_slots_reset (render3d.md §5.6): all slots free but 69h (whose record would
// overlap another slot's), no entry holding one, the previous count 0.
void sprite_slots_reset()
{
    for (u16 i = 0; i < 0x18; i++) ds_u8(u16(DS_slot_bitmap + i)) = 0;
    ds_u8(u16(DS_slot_bitmap + 0x0D)) = 1;
    for (u16 i = 0; i <= 0xB4; i++) ds_u8(u16(DS_visible_sprite + i)) = 0;
    ds_u16(DS_visible_count_previous) = 0;
}

// 0919:5a9f sprite_cache_invalidate (world.md §3.3): the first byte (the cached kind) of each slot
// record cleared, from slot B8h down, then sprite_slots_reset. The segment changes at slot 6Ah,
// which is skipped: slot 69h is cleared in segment D885 although its record is in D883, and slot
// 6Ah is not cleared (kept).
void sprite_cache_invalidate()
{
    u16 es = ds_u16(DS_sprite_segment_a);
    for (u16 bx = 0x16E; !(bx & 0x8000); bx = u16(bx - 2)) {
        if (bx == 0xD2) {
            es = ds_u16(DS_sprite_segment_b);
            continue;
        }
        mem_u8(es, seg_u16(CSSEG_sprite_slot_pointers, u16(CS_sprite_slot_pointers + bx))) = 0;
    }
    sprite_slots_reset();
}

// 0919:7017 sprite_slot_alloc (render3d.md §5.6): a free slot of level AH for entry SI - 1 (DS:5188
// + SI = the slot number, 1-based): level 0 from bit 0 of D8D1 on, first skipping full words;
// levels 1 and 2 from their pool (D8E9/D8EC/D8F2) to the end of the bitmap. Nothing is stored
// when the byte found has no free bit, or the pool is full.
void sprite_slot_alloc(u8 ah, u16 si)
{
    u8 &entry_slot = ds_u8(u16(DS_visible_sprite - 1 + si));
    if (ah == 0) {
        u8 al = 1;
        u16 bx = DS_slot_bitmap;
        do {
            if (ds_u16(bx) != 0xFFFF) break;
            al = u8(al + 0x10);
            bx = u16(bx + 2);
        } while (bx != 0);
        u8 byte = ds_u8(bx);
        if (byte == 0xFF) {
            bx = u16(bx + 1);
            al = u8(al + 8);
            byte = ds_u8(bx);
        }
        u8 ch = 1;
        do {
            if (!(ch & byte)) {
                ds_u8(bx) |= ch;
                entry_slot = al;
                return;
            }
            al = u8(al + 1);
            ch = u8(ch << 1);
        } while (ch != 0);
        return;
    }
    u8 al = ds_u8(u16(DS_slot_level_first + ah));
    u16 cx = ds_u16(u16(DS_slot_level_mask + u16(ah << 1)));
    u16 bx = ds_u16(u16(DS_slot_level_byte + u16(ah << 1)));
    for (;;) {
        const u8 byte = ds_u8(bx) & u8(cx);
        if (byte != u8(cx)) {
            const u8 first = u8(8 - u8(cx >> 8));
            al = u8(al + first);
            u8 ch = ds_u8(u16(DS_slot_bit + first));
            do {
                if (!(ch & byte)) {
                    ds_u8(bx) |= ch;
                    entry_slot = al;
                    return;
                }
                al = u8(al + 1);
                ch = u8(ch << 1);
            } while (ch != 0);
            return;
        }
        cx = 0x08FF;
        al = u8(al + 8);
        bx = u16(bx + 1);
        if (bx >= 0xD8E8) return;
    }
}

// 0919:6cfa sprite_lod_entry (render3d.md §5.6): entry SI - 1 (object BX, distance class DL). Its
// slot is freed when the distance word is <= 10h, the kind is 0 or the class is beyond the detail
// limit D6C0. Otherwise the wanted level (2 under class 0Ah, 1 under 0Fh, else 0; at most the
// kind's levels) is kept if the slot has it, else the slot is freed and one of that level taken.
// Returns SI as the original leaves it (shifted left and back: bit 15 lost).
u16 sprite_lod_entry(u16 si, u8 dl, u16 bx)
{
    const u16 s2 = u16(si << 1);
    si = u16(s2 >> 1);
    if (dist(u16(s2 - 2)) <= 0x10) {
        slot_free(si);
        return si;
    }
    const u8 kind = u8(ds_u16(u16(DS_object_word + bx)));
    if (kind == 0 || dl > ds_u8(DS_detail_distance)) {
        slot_free(si);
        return si;
    }
    u8 level = 0;
    const u8 levels = ds_u8(u16(DS_kind_sprite_info + kind)) & 3;
    if (levels != 0 && dl < 0x0F) {
        level = 1;
        if (dl < 0x0A) level = 2;
        if (level > levels) level = levels;
    }
    const u8 slot = ds_u8(u16(DS_visible_sprite - 1 + si));
    if (slot != 0) {
        if (level == seg_u8(CSSEG_sprite_slot_level, u16(CS_sprite_slot_level + slot - 1))) return si;
        slot_free(si);
    }
    sprite_slot_alloc(level, si);
    return si;
}

// 0919:6cad sprite_lod_update (render3d.md §5.6): B83F = the first entry drawn (0 in chase view,
// else 1); sprite_lod_entry for the entries from the last one down to B83F, the permanent objects
// (offset >= 48h) first, then the temporary ones.
void sprite_lod_update()
{
    ds_u16(DS_identify_first) = ds_u8(DS_chase_view) == 0 ? 1 : 0;
    for (int pass = 0; pass < 2; pass++) {
        u16 si = ds_u16(DS_visible_count);
        do {
            const u16 b2 = u16(si << 1);
            const u8 dl = ds_u8(u16(DS_visible_distance_word - 1 + b2));
            const u16 object = entry_object(u16(b2 - 2));
            if ((object >= 0x48) == (pass == 0)) si = sprite_lod_entry(si, dl, object);
            si = u16(si - 1);
        } while (si > ds_u16(DS_identify_first));
    }
}

} // namespace gb
