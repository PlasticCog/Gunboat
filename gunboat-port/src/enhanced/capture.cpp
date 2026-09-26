// Capture of a finished 3D frame for the enhanced presentation (capture.hpp).
//
// Two things are derived by running the ported original routines on the game's memory and then
// putting every byte back (nothing else runs meanwhile: the host's frame hook is called between two
// passes of the mission loop, and these routines neither wait nor pump):
//  * where each pixel of page 0 comes from: view_present (or the chase view's copy) is run three
//    times with page 1's view window filled with its column, its row and its column ^ 80h; a page 0
//    pixel that differs between the first and the third run was copied from the window, the others
//    are cockpit, gun sprites or anything else not from the view;
//  * each drawn object's sprite at its natural size: sprite_view_angle for the entry, then
//    sprite_cache_build at size 17h (every column and row once) into a slot moved to spare memory.
#include "enhanced/capture.hpp"

#include <SDL3/SDL_timer.h>

#include <cstring>
#include <memory>

#include "mem.hpp"
#include "mission/mission.hpp"
#include "platform/gfx.hpp"
#include "render/render.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

// The game's memory saved before the scratch runs and put back after them.
class Scratch {
public:
    Scratch()
    {
        if (!saved_) saved_.reset(new u8[MEM_SIZE]);
        std::memcpy(saved_.get(), mem, MEM_SIZE);
    }
    ~Scratch() { restore(); }
    Scratch(const Scratch &) = delete;
    Scratch &operator=(const Scratch &) = delete;
    void restore() { std::memcpy(mem, saved_.get(), MEM_SIZE); }

private:
    static inline std::unique_ptr<u8[]> saved_;
};

constexpr u16 WINDOW_OFF = 0x5028;  // page 1: row 64, column 40

// Fills page 1's view window (both page tables name page 1: the game's for the view copies, the
// graphics library's for gfx_copy_rect) with value(row, column).
template <class F> void fill_window(F value)
{
    const u16 a = ds_u16(u16(DS_page_segments + 2)), b = ds_u16(u16(DS_gfx_page_seg + 2));
    for (int r = 0; r < VIEW_H; r++)
        for (int c = 0; c < VIEW_W; c++) {
            const u16 off = u16(WINDOW_OFF + r * 320 + c);
            mem_u8(a, off) = value(r, c);
            mem_u8(b, off) = value(r, c);
        }
}

// The copy of this frame's view to page 0: the chase view's rectangle (mission_run, 05bd:0346) or
// view_present, with every row of the view copied (the sky rows that did not change are copied too:
// they are the same picture).
void copy_view()
{
    ds_u8(DS_view_sky_top) = 0;
    ds_u8(DS_view_sky_top_prev) = 0;
    if (ds_u8(DS_chase_view) != 0) gfx_copy_rect(0x28, 0x127, 0x40, 0x7F, 0x28, 0x4B, 1, 0);
    else view_present(1, 0, 0);
}

void capture_map(Scene &sc, Scratch &scratch)
{
    static u8 pass[3][64000];
    for (int k = 0; k < 3; k++) {
        if (k) scratch.restore();
        fill_window([k](int r, int c) { return u8(k == 1 ? r : k == 2 ? c ^ 0x80 : c); });
        copy_view();
        std::memcpy(pass[k], mp(VRAM_SEG, 0), 64000);
        if (k == 0) {  // what view_present drew over the window on page 1 (the gun sprites)
            const u16 page1 = ds_u16(u16(DS_page_segments + 2));
            for (int r = 0; r < VIEW_H; r++)
                for (int c = 0; c < VIEW_W; c++)
                    sc.overlay[r * VIEW_W + c] = mem_u8(page1, u16(WINDOW_OFF + r * 320 + c)) != u8(c);
        }
    }
    scratch.restore();
    for (int p = 0; p < 64000; p++)
        sc.map[p] = pass[0][p] != pass[2][p] && pass[1][p] < VIEW_H ? u16(pass[1][p] << 8 | pass[0][p]) : NOT_VIEW;
}

// The slot the natural-size images are built in, its record moved to offset 0 of the video
// segment (in the scratch memory only) with room up to F000h.
constexpr u8 SCRATCH_SLOT = 0xB8;

// Reads one part of the built record as blit_record does: `rows` output rows of `width` bytes from
// `si`, the source row advancing by the repeat counts from `rep`.
SpritePart read_part(u16 seg, u16 si, u16 rep, int width, int rows)
{
    SpritePart part;
    part.w = width;
    part.h = rows;
    part.px.resize(size_t(width) * rows);
    u8 repeat = mem_u8(seg, rep);
    for (int r = 0; r < rows; r++) {
        for (int x = 0; x < width; x++) part.px[size_t(r) * width + x] = mem_u8(seg, u16(si + x));
        if (--repeat == 0) {
            rep = u16(rep + 1);
            repeat = mem_u8(seg, rep);
            si = u16(si + width);
        }
    }
    return part;
}

void capture_sprites(Scene &sc)
{
    const u16 count = ds_u16(DS_visible_count), first = ds_u16(DS_identify_first);
    const u16 seg = VRAM_SEG;
    const u16 table = CS_sprite_slot_pointers;
    for (u16 e = 0; e < MAX_ENTRIES; e++) sc.has_sprite[e] = false;
    for (u16 e = first; e < count && e < MAX_ENTRIES; e++) {
        if (ds_u8(u16(DS_visible_sprite + e)) == 0) continue;  // no cache slot: not drawn
        const u16 object = ds_u16(u16(DS_visible_object + 2 * e));
        if (ds_u8(u16(DS_object_word + object)) == 0x39) continue;  // never drawn
        sprite_view_angle(e, ds_u16(u16(DS_visible_distance_word + 2 * e)));
        ds_u8(DS_sprite_size) = 0x17;
        ds_u8(DS_sprite_slot) = SCRATCH_SLOT;
        ds_u16(DS_sprite_segment_a) = seg;
        seg_u16(CSSEG_sprite_slot_pointers, u16(table + 2 * (SCRATCH_SLOT - 1))) = 0;
        seg_u16(CSSEG_sprite_slot_pointers, u16(table + 2 * SCRATCH_SLOT)) = 0xF000;
        mem_u16(seg, 0) = 0xFFFF;  // no cached kind: always build
        sprite_cache_build();
        // The record: +0 kind, +1 size, +2 view, +3/+4 width and rows, +5/+6 the second part's,
        // +7 its column ratio, +8 the row repeat counts, +20h the pixels.
        SpriteImage &img = sc.sprites[e];
        const int w1 = mem_u8(seg, 3), h1 = mem_u8(seg, 4), w2 = mem_u8(seg, 5), h2 = mem_u8(seg, 6);
        if (w1 == 0 || h1 == 0) continue;
        img.part1 = read_part(seg, 0x20, 8, w1, h1);
        img.part2 = SpritePart{};
        img.shift = mem_u8(seg, 7);
        if (w2 != 0 && h2 != 0) {
            // blit_record: the second part's rows follow the source rows the first part used, its
            // repeat counts follow the ones that add up to the first part's height.
            u16 rep = 8;
            int rows = 0, left = h1;
            for (;;) {
                left = u8(left - mem_u8(seg, rep));
                rows++;
                if (left == 0 || rows > 0x100) break;
                rep = u16(rep + 1);
            }
            img.part2 = read_part(seg, u16(0x20 + rows * w1), u16(rep + 1), w2, h2);
        }
        sc.has_sprite[e] = true;
    }
}

} // namespace

void scene_capture(Scene &sc)
{
    static u32 serial;
    sc.time_ns = SDL_GetTicksNS();
    sc.serial = ++serial;
    std::memcpy(sc.ds, mp(DGROUP, 0), sizeof sc.ds);
    const u16 page1 = ds_u16(u16(DS_page_segments + 2));
    for (int r = 0; r < VIEW_H; r++) std::memcpy(sc.window + r * VIEW_W, mp(page1, u16(WINDOW_OFF + r * 320)), VIEW_W);
    {
        Scratch scratch;
        capture_map(sc, scratch);
        capture_sprites(sc);
    }
    sc.valid = true;
}

} // namespace gb
