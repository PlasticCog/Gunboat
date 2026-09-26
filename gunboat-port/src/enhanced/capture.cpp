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

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
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

// sprite_cache_build (0919:5aeb) of the kind and view byte now in DS:D868 / DS:D86A at size 17h, in
// the scratch slot, and the image read back as blit_record reads it. The record: +0 kind, +1 size,
// +2 view, +3/+4 width and rows, +5/+6 the second part's, +7 its column ratio, +8 the row repeat
// counts, +20h the pixels.
bool build_image(SpriteImage &img)
{
    const u16 seg = VRAM_SEG;
    const u16 table = CS_sprite_slot_pointers;
    ds_u8(DS_sprite_size) = 0x17;
    ds_u8(DS_sprite_slot) = SCRATCH_SLOT;
    ds_u16(DS_sprite_segment_a) = seg;
    seg_u16(CSSEG_sprite_slot_pointers, u16(table + 2 * (SCRATCH_SLOT - 1))) = 0;
    seg_u16(CSSEG_sprite_slot_pointers, u16(table + 2 * SCRATCH_SLOT)) = 0xF000;
    mem_u16(seg, 0) = 0xFFFF;  // no cached kind: always build
    sprite_cache_build();
    const int w1 = mem_u8(seg, 3), h1 = mem_u8(seg, 4), w2 = mem_u8(seg, 5), h2 = mem_u8(seg, 6);
    if (w1 == 0 || h1 == 0) return false;
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
    return true;
}

void capture_sprites(Scene &sc)
{
    const u16 count = ds_u16(DS_visible_count), first = ds_u16(DS_identify_first);
    for (u16 e = 0; e < MAX_ENTRIES; e++) sc.has_sprite[e] = false;
    for (u16 e = first; e < count && e < MAX_ENTRIES; e++) {
        if (ds_u8(u16(DS_visible_sprite + e)) == 0) continue;  // no cache slot: not drawn
        const u16 object = ds_u16(u16(DS_visible_object + 2 * e));
        if (ds_u8(u16(DS_object_word + object)) == 0x39) continue;  // never drawn
        sprite_view_angle(e, ds_u16(u16(DS_visible_distance_word + 2 * e)));
        sc.has_sprite[e] = build_image(sc.sprites[e]);
    }
}

// ---- the extended draw distance

constexpr int GRID_W = 17, GRID_H = 11;  // DS:B84E, u8[187]

// The grid cell (DS:B84E index) of a map position: grid_cell (terrain.cpp) without the quadrant.
int grid_cell_of(u16 x, u16 y)
{
    const u8 xc = u8(u8(x >> 8) - 4) >> 2, yc = u8(u8(y >> 8) - 4) >> 2;
    return (9 - yc) * GRID_W + xc + 1;
}

int chebyshev(int a, int b)
{
    return std::max(std::abs(a % GRID_W - b % GRID_W), std::abs(a / GRID_W - b / GRID_W));
}

// The cells 2..radius cells from the window's centre cell, each loaded as terrain_cells_update
// (0919:8408) loads a window cell: tile_load of the grid byte at the cell's corner, then the terrain
// structures standing in it; its vertices and scenery read back.
void load_far(FarWorld &fw, int radius)
{
    const u16 centre = ds_u16(DS_terrain_rebuild);
    fw.cells.clear();
    const int ccol = centre % GRID_W, crow = centre / GRID_W;
    for (int dr = -radius; dr <= radius; dr++)
        for (int dc = -radius; dc <= radius; dc++) {
            if (std::max(std::abs(dr), std::abs(dc)) < 2) continue;  // the game's own window
            const int col = ccol + dc, row = crow + dr;
            if (col < 0 || col >= GRID_W || row < 0 || row >= GRID_H) continue;
            const int cell = row * GRID_W + col;
            const u8 dh = u8(4 + 4 * (col - 1)), dl = u8(4 + 4 * (9 - row));  // the corner, high bytes
            ds_u16(DS_group_a_count) = 0;
            ds_u16(DS_group_b_count) = 0;
            ds_u8(DS_scenery_count) = 0;
            ds_u8(DS_window_overflow) = 0;
            ds_u16(DS_tile_origin_x) = u16(dh << 8);
            ds_u16(DS_tile_origin_y) = u16(dl << 8);
            tile_load(ds_u8(u16(DS_world_grid + cell)));
            u16 st = ds_u16(DS_structure_count);
            while (!(--st & 0x8000)) {
                const u16 o = u16(st << 1);
                const u16 x = ds_u16(u16(DS_structure_x + o)), y = ds_u16(u16(DS_structure_y + o));
                if (((x >> 8) & 0xFC) != dh || ((y >> 8) & 0xFC) != dl) continue;
                ds_u16(DS_tile_origin_x) = u16(x - 0x200);
                ds_u16(DS_tile_origin_y) = u16(y - 0x200);
                const u16 piece = ds_u16(u16(DS_structure_piece + o));
                tile_load(u8(u8(u8(piece) - 0x43) | (u8(piece >> 8) & 3) << 6));
            }
            FarCell fc;
            fc.cx = double(dh + 2) * 1024;  // map units x 4
            fc.cy = double(dl + 2) * 1024;
            auto read = [](u16 first, u16 count, std::vector<FarVertex> &out) {
                for (u16 i = first; i < u16(first + count) && i < 0x400; i++)
                    out.push_back({ds_u8(u16(DS_vertex_control + i)), u8(ds_u16(u16(DS_vertex_height + 2 * i))),
                                   ds_u16(u16(DS_vertex_x + 2 * i)), ds_u16(u16(DS_vertex_y + 2 * i))});
            };
            read(0, std::min<u16>(ds_u16(DS_group_a_count), 0x200), fc.a);
            read(0x200, std::min<u16>(ds_u16(DS_group_b_count), 0x200), fc.b);
            const u16 first_obj = ds_u16(DS_authored_object_count);
            for (u16 k = 0; k < ds_u8(DS_scenery_count); k++) {
                const u16 o = u16((first_obj + k) << 1);
                const u16 word = ds_u16(u16(DS_object_word + o));
                fc.objects.push_back({u8(word), u8(word >> 8), ds_u16(u16(DS_object_x + o)), ds_u16(u16(DS_object_y + o))});
            }
            fw.cells.push_back(std::move(fc));
        }
}

// The images of far objects by kind and view, made as they are first seen, for the region they were
// made in.
std::map<u32, SpriteImage> far_images;
u32 far_images_world = 0xFFFFFFFF;

void build_far_image(const FarObject &obj, double cam_x, double cam_y)
{
    const double dx = double(u16(obj.x << 2)) - cam_x, dy = double(u16(obj.y << 2)) - cam_y;
    const u8 angle = u8(int(std::floor(std::atan2(dx, dy) * (128.0 / 3.14159265358979323846))) & 0xFF);
    const u8 view = far_view_byte(obj.kind, obj.flags, angle);
    const u32 key = u32(obj.kind) << 8 | u8(u8(view + 2) >> 2);
    if (far_images.count(key)) return;
    ds_u8(DS_sprite_kind) = obj.kind;
    ds_u8(DS_sprite_view) = view;
    ds_u8(DS_sprite_zoom) = 0;
    SpriteImage img;
    if (!build_image(img)) img = SpriteImage{};
    far_images[key] = std::move(img);
}

u32 far_key()
{
    u32 k = 0;
    for (u16 off : {u16(DS_scene_colours), u16(DS_scene_colours + 2), u16(DS_shade_mask_a)})
        k = k * 0x9E3779B1u + ds_u16(off);
    return k ^ u32(ds_u16(DS_region)) << 24 ^ u32(ds_u16(DS_mission_number)) << 16;
}

} // namespace

u8 far_view_byte(u8 kind, u8 flags, u8 angle)
{
    if (kind == 0x39 || kind >= 0x3F) return 0;
    return u8(((flags & 7) << 5) - angle);
}

const SpriteImage *far_sprite(u8 kind, u8 view)
{
    const auto it = far_images.find(u32(kind) << 8 | u8(u8(view + 2) >> 2));
    return it == far_images.end() || it->second.part1.w == 0 ? nullptr : &it->second;
}

void scene_capture(Scene &sc, int far_radius, const Scene *previous)
{
    static u32 serial;
    sc.time_ns = SDL_GetTicksNS();
    sc.serial = ++serial;
    std::memcpy(sc.ds, mp(DGROUP, 0), sizeof sc.ds);
    const u16 page1 = ds_u16(u16(DS_page_segments + 2));
    for (int r = 0; r < VIEW_H; r++) std::memcpy(sc.window + r * VIEW_W, mp(page1, u16(WINDOW_OFF + r * 320)), VIEW_W);
    sc.far.reset();
    sc.far_objects.clear();
    {
        Scratch scratch;
        capture_map(sc, scratch);
        capture_sprites(sc);
        if (far_radius > 1) {
            const u16 centre = ds_u16(DS_terrain_rebuild);
            const u32 key = far_key();
            if (previous && previous->far && previous->far->centre == centre && previous->far->key == key &&
                previous->far->radius == far_radius) {
                sc.far = previous->far;
            } else {
                auto fw = std::make_shared<FarWorld>();
                fw->centre = centre;
                fw->key = key;
                fw->radius = far_radius;
                load_far(*fw, far_radius);
                sc.far = fw;
            }
            // The authored objects standing in the far cells (they move: read every frame).
            const u16 authored = ds_u16(DS_authored_object_count);
            for (u16 i = 36; i < authored && i < 1000; i++) {
                const u16 o = u16(i << 1);
                const u16 word = ds_u16(u16(DS_object_word + o));
                const u8 kind = u8(word);
                if (kind == 0 || kind == 0x39) continue;
                const u16 x = ds_u16(u16(DS_object_x + o)), y = ds_u16(u16(DS_object_y + o));
                const int d = chebyshev(grid_cell_of(x, y), centre);
                if (d < 2 || d > far_radius) continue;
                sc.far_objects.push_back({kind, u8(word >> 8), x, y});
            }
            // Their images in the views they are seen in now.
            const u32 world = u32(ds_u16(DS_region)) << 16 | ds_u16(DS_mission_number);
            if (world != far_images_world) {
                far_images.clear();
                far_images_world = world;
            }
            const double cx = ds_u16(DS_camera_qx), cy = ds_u16(DS_camera_qy);
            for (const FarCell &c : sc.far->cells)
                for (const FarObject &obj : c.objects) build_far_image(obj, cx, cy);
            for (const FarObject &obj : sc.far_objects) build_far_image(obj, cx, cy);
        }
    }
    sc.valid = true;
}

} // namespace gb
