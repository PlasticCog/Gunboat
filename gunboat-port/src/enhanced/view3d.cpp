// The enhanced 3D view (view3d.hpp). Each step names the original routine it follows.
#include "enhanced/view3d.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr double PI = 3.14159265358979323846;

// A 16-bit angle or coordinate difference as the original's signed wrap-around: [-32768, 32768).
double wrap16(double v)
{
    v = std::fmod(v + 32768.0, 65536.0);
    if (v < 0) v += 65536.0;
    return v - 32768.0;
}

// Compass bearing of (dx, dy) in 65536 per turn: 0 along +Y, 16384 along +X (atan, 0919:3712, with
// the octant bases DS:D739).
double bearing16(double dx, double dy) { return std::atan2(dx, dy) * (32768.0 / PI); }

// terrain_setup (0919:736c): the horizon row of the view window, as bytes.
int horizon_row(u8 pitch, u8 ref)
{
    u8 bl = u8(pitch - 0x42 + 0x15);
    bl = u8(bl - u8(s8(u8(ref - 0x80)) >> 3));
    if (bl > 0x3D) bl = 0x3D;
    return bl;
}

// blit_place (0919:5d5a): the sprites' vertical reference, as bytes (without the u8 wrap).
int sprite_horizon(u8 pitch, u8 ref) { return (u8(-ref) >> 3) + pitch - 0x2C; }

struct Camera {
    double qx, qy;  // quarter units (DS:D972/D974)
    double view;    // view heading word (DS:D190)
    double horizon; // horizon row of the window (terrain_setup)
    double sprite_h;
};

Camera camera_of(const Scene &s)
{
    const u8 pitch = s.u8_at(DS_view_pitch), ref = s.u8_at(DS_pitch_reference);
    return {double(s.u16_at(DS_camera_qx)), double(s.u16_at(DS_camera_qy)), double(s.u16_at(DS_view_heading_low)),
            double(horizon_row(pitch, ref)), double(sprite_horizon(pitch, ref))};
}

// The sprite scale factors of sprite_scale_patterns (0919:6174) for the integer sizes: columns and
// rows per source pixel (the zoom plus the density of the size's pattern bits).
struct ScaleTable {
    double fx[0x60], fy[0x60];
    bool ready = false;
    void build()
    {
        auto bits = [](u16 at, int n) {
            int c = 0;
            for (int i = 0; i < n; i++) c += __builtin_popcount(seg_u8(CSSEG_sprite_scale_table, u16(at + i)));
            return c;
        };
        auto record = [](int ah) { return ah >= 0x18 ? 0 : (0x17 - ah) * 10; };
        for (int s = 0; s < 0x60; s++) {
            int ah = s, zoom = 0;
            if (ah >= 0x18) {
                ah -= 0x18;
                zoom++;
                if (ah >= 0x18) {
                    zoom++;
                    ah -= 0x18;
                }
            }
            const u16 rec = u16(CS_sprite_scale_table + record(ah));
            const double cols = bits(rec, 7) / 56.0;
            double rows = bits(u16(rec + 7), 3) / 24.0;
            if (ah >= 0x18) {
                zoom = 3;
                rows = bits(u16(CS_sprite_scale_table + record(ah - 0x18) + 7), 3) / 24.0;
            }
            fx[s] = std::min(zoom, 2) + cols;  // zoom 3 uses the zoom 2 row scalers
            fy[s] = zoom + rows;
        }
        ready = true;
    }
    void factors(double size, double &x, double &y) const
    {
        size = std::clamp(size, 0.0, double(0x5F));
        const int s = int(size);
        const double f = size - s;
        const int n = std::min(s + 1, 0x5F);
        x = fx[s] + (fx[n] - fx[s]) * f;
        y = fy[s] + (fy[n] - fy[s]) * f;
    }
} scale_table;

class Renderer {
public:
    Renderer(const Scene &cur, const Scene *prev, double t, const ViewTarget &target)
        : sc_(cur), prev_(prev), t_(t), tg_(target)
    {
        cam_ = camera_of(cur);
        if (prev_) {
            const Camera p = camera_of(*prev_);
            const double back = 1.0 - t_;
            cam_.qx -= wrap16(cam_.qx - p.qx) * back;
            cam_.qy -= wrap16(cam_.qy - p.qy) * back;
            cam_.view -= wrap16(cam_.view - p.view) * back;
            cam_.horizon -= (cam_.horizon - p.horizon) * back;
            cam_.sprite_h -= (cam_.sprite_h - p.sprite_h) * back;
        }
        view_shift_ = wrap16(double(cur.u16_at(DS_view_heading_low)) - cam_.view) / 128.0;
        if (!scale_table.ready) scale_table.build();
    }

    void run()
    {
        sky_and_water();
        water_marks();
        for (int i = 0; i < 0x400; i++) projected_[i] = false;
        group_b();
        group_a_and_sprites();
        spotlights();
    }

private:
    const Scene &sc_;
    const Scene *prev_;
    double t_;
    const ViewTarget &tg_;
    Camera cam_;
    double view_shift_;  // columns the interpolated view lies left of the captured one
    bool projected_[0x400];
    double vx_[0x400], vy_[0x400];

    // ---- target pixels
    double tx(double x) const { return (x - tg_.ox) * tg_.sx; }
    double ty(double y) const { return (y - tg_.oy) * tg_.sy; }
    bool needed(int j, int i) const
    {
        return (j >= tg_.need_y0 && j < tg_.need_y1) || i < tg_.skip0 || i >= tg_.skip1;
    }
    void fill(int j, int i0, int i1, u8 c) const
    {
        if (j < 0 || j >= tg_.h) return;
        i0 = std::max(i0, 0);
        i1 = std::min(i1, tg_.w);
        if (i0 >= i1) return;
        u8 *row = tg_.px + size_t(j) * tg_.w;
        if (j >= tg_.need_y0 && j < tg_.need_y1) {
            std::memset(row + i0, c, size_t(i1 - i0));
            return;
        }
        const int a = std::min(i1, tg_.skip0), b = std::max(i0, tg_.skip1);
        if (a > i0) std::memset(row + i0, c, size_t(a - i0));
        if (i1 > b) std::memset(row + b, c, size_t(i1 - b));
    }
    // Pixels whose centres lie in the page rectangle [x0, x1) x [y0, y1).
    void rect(double x0, double y0, double x1, double y1, u8 c) const
    {
        const int j0 = int(std::ceil(ty(y0) - 0.5)), j1 = int(std::ceil(ty(y1) - 0.5));
        const int i0 = int(std::ceil(tx(x0) - 0.5)), i1 = int(std::ceil(tx(x1) - 0.5));
        for (int j = std::max(j0, 0); j < std::min(j1, tg_.h); j++) fill(j, i0, i1, c);
    }

    // ---- sky_water_vga (0919:74c0): sky above the horizon row, two rows of colour 8, water below;
    // the flash (DS:D9B5 counting down) turns the sky 0Fh then 0Eh and the water 0Eh.
    void sky_and_water() const
    {
        const u8 shake = sc_.u8_at(DS_screen_shake);
        u8 sky = sc_.u8_at(DS_scene_colours), water = sc_.u8_at(u16(DS_scene_colours + 1));
        if (shake != 0) {
            sky = 0x0F;
            water = 0x0E;
        } else if (prev_ && prev_->u8_at(DS_screen_shake) == 1) {
            sky = 0x0E;
        }
        const double h = VIEW_Y + cam_.horizon;
        const double top = tg_.oy - 1, bottom = tg_.oy + tg_.h / tg_.sy + 1;
        const double left = tg_.ox - 1, right = tg_.ox + tg_.w / tg_.sx + 1;
        rect(left, top, right, h, sky);
        rect(left, h, right, h + 2, 8);
        rect(left, h + 2, right, bottom, water);
    }

    // ---- water_marks_vga (0919:7458) with detail high: 32 rows from the horizon line, one mark each
    // at its column (moved with the view's turn), larger and nearer marks as they age. The columns
    // are bytes, so the pattern repeats every 256 columns.
    void water_marks() const
    {
        if (sc_.u8_at(DS_detail_low) != 0) return;
        const u8 water = sc_.u8_at(u16(DS_scene_colours + 1));
        const u8 al = u8(water | 7), ah = water;
        const int first = (sc_.u8_at(DS_water_phase) >> 2) & 0x1F;
        const double top = VIEW_Y + cam_.horizon;
        for (int k = 0; k < 0x20; k++) {
            const double row = top + k;
            if (row >= VIEW_Y + VIEW_H - 1) break;  // DI >= 9EE8h: a row past the view
            const int m = (first + k) & 0x1F;
            const u8 age = sc_.u8_at(u16(DS_water_mark_age + m));
            const int cl = 0x20 - k;
            const double col = VIEW_X + sc_.u8_at(u16(DS_water_mark_x + m)) + view_shift_;
            struct Px {
                int dx, dy;
                u8 c;
            } px[8];
            int n = 0;
            px[n++] = {0, 0, al};
            if (cl <= 0x16 && age >= 0x0B) {
                px[n++] = {1, 0, al};
                px[0].c = ah;
                px[n++] = {2, -1, al};
                if (cl <= 0x0E && age >= 0x11) {
                    int end = 2;
                    if (age >= 0x17) {
                        px[1].c = ah;
                        px[2].c = ah;
                        if (age >= 0x1B) {
                            px[n++] = {2, -2, al};
                            end = 4;
                        }
                    }
                    if (end == 2) {
                        px[n++] = {2, 0, al};
                        end = 3;
                    }
                    px[n++] = {end, -1, al};
                    px[n++] = {end - 2, 1, al};
                }
            }
            for (double base = col - 256 * std::ceil((col - tg_.ox) / 256.0 + 1); base < tg_.ox + tg_.w / tg_.sx + 8;
                 base += 256)
                for (int p = 0; p < n; p++)
                    rect(base + px[p].dx, row + px[p].dy, base + px[p].dx + 1, row + px[p].dy + 1, px[p].c);
        }
    }

    // ---- project (0919:7523): bearing and scale of a vertex; its page position.
    void project(int i)
    {
        if (projected_[i]) return;
        projected_[i] = true;
        const double dx = wrap16(sc_.u16_at(u16(DS_vertex_x + 2 * i)) - cam_.qx);
        const double dy = wrap16(sc_.u16_at(u16(DS_vertex_y + 2 * i)) - cam_.qy);
        const double d = wrap16(bearing16(dx, dy) - cam_.view);
        const double dist = std::hypot(dx, dy);
        const double s = dist > 32767.5 / 255.0 ? 32767.5 / dist : 255.0;
        const double h = sc_.u16_at(u16(DS_vertex_height + 2 * i)) & 0xFF;
        vx_[i] = VIEW_X + 130 + d / 128.0;
        vy_[i] = cam_.horizon + std::max(0.0, 512.0 + s - s * h / 32.0) / 8.0;
    }

    // ---- draw_primitive (0919:767a): the triangle i, i+1, i+2 (mode 0) or i-1, i+1, i+2 (mode 2),
    // in its colour; the columns made continuous from the first vertex (bearings wrap at 16 bits).
    void primitive(int i, u8 mode, u8 colour)
    {
        if (mode == 1) return;  // lines: none in the shipped worlds (render3d.md §3.4)
        const int a = mode & 2 ? i - 1 : i, b = i + 1, c = i + 2;
        if (a < 0 || c >= 0x400) return;
        project(a);
        project(b);
        project(c);
        const double x0 = vx_[a];
        const double x1 = x0 + wrap16((vx_[b] - x0) * 128.0) / 128.0;
        const double x2 = x0 + wrap16((vx_[c] - x0) * 128.0) / 128.0;
        triangle(x0, vy_[a], x1, vy_[b], x2, vy_[c], colour);
    }

    // fill_triangle and the span routine (0919:7802, 788e) fill the rows of the vertices' rows
    // inclusive, from the left edge rounded (+40h) to the right edge plus one column (+BFh), at least
    // one pixel: the triangle drawn here is grown by as much in page pixels (half a row up and down,
    // a column to the right), so that at any scale it covers what the original covers and thin or
    // distant terrain (beaches, far shores) keeps the size the original gives it.
    static constexpr double GROW_UP = 0.5, GROW_DOWN = 0.5, GROW_LEFT = 0.0, GROW_RIGHT = 1.0;

    void triangle(double x0, double y0, double x1, double y1, double x2, double y2, u8 c) const
    {
        double X[3] = {tx(x0), tx(x1), tx(x2)}, Y[3] = {ty(y0), ty(y1), ty(y2)};
        // sort by row
        for (int p = 0; p < 2; p++)
            for (int q = 0; q < 2 - p; q++)
                if (Y[q] > Y[q + 1]) {
                    std::swap(Y[q], Y[q + 1]);
                    std::swap(X[q], X[q + 1]);
                }
        const double up = GROW_UP * tg_.sy, down = GROW_DOWN * tg_.sy;
        const double left = GROW_LEFT * tg_.sx, right = GROW_RIGHT * tg_.sx;
        const double xmin = std::min({X[0], X[1], X[2]}), xmax = std::max({X[0], X[1], X[2]});
        if (xmax + right < 0 || xmin - left > tg_.w || Y[2] + down < 0 || Y[0] - up > tg_.h) return;
        const int j0 = std::max(int(std::ceil(Y[0] - up - 0.5)), 0);
        const int j1 = std::min(int(std::ceil(Y[2] + down - 0.5)), tg_.h);
        const bool flat = Y[2] - Y[0] < 1e-9;
        // The cross-section of the triangle at row y: [l, r] widened by it.
        auto section = [&](double y, double &l, double &r) {
            const double xa = X[0] + (X[2] - X[0]) * (y - Y[0]) / (Y[2] - Y[0]);
            double xb;
            if (y < Y[1]) xb = X[0] + (X[1] - X[0]) * (y - Y[0]) / (Y[1] - Y[0]);
            else if (Y[2] > Y[1]) xb = X[1] + (X[2] - X[1]) * (y - Y[1]) / (Y[2] - Y[1]);
            else xb = X[1];  // on the flat bottom edge X[1]..X[2]
            l = std::min({l, xa, xb});
            r = std::max({r, xa, xb});
        };
        for (int j = j0; j < j1; j++) {
            const double yc = j + 0.5;
            double l = xmin, r = xmax;  // all on one row: between the widest pair
            if (!flat) {
                l = 1e30;
                r = -1e30;
                if (yc >= Y[0] && yc <= Y[2]) {
                    section(yc, l, r);
                } else {
                    // Above or below the triangle (its grown rows, and all rows of one thinner than
                    // a row): all of it that lies within the growth of this row.
                    const double lo = std::max(Y[0], yc - down), hi = std::min(Y[2], yc + up);
                    if (lo > hi) continue;
                    section(lo, l, r);
                    section(hi, l, r);
                    if (Y[1] > lo && Y[1] < hi) section(Y[1], l, r);
                }
            }
            fill(j, int(std::ceil(l - left - 0.5)), int(std::ceil(r + right - 0.5)), c);
        }
    }

    // ---- draw_group_b (0919:763f): group B from its last primitive down.
    void group_b()
    {
        int bx = std::min<int>(sc_.u16_at(DS_group_b_count), 0x1FE) + 0x200 - 1;
        for (; bx >= 0x200; bx--) {
            const u8 ctrl = sc_.u8_at(u16(DS_vertex_control + bx));
            if (ctrl & 0x3F) primitive(bx, u8(ctrl >> 6), ctrl & 0x3F);
        }
    }

    // ---- object_frame (0919:6e94): group A (in the frame's order) and the sprites (the frame's
    // list, farthest first) interleaved by the frame's depths, as the original.
    void group_a_and_sprites()
    {
        int si = std::min<int>(sc_.u16_at(DS_group_a_count), 0x1FE);
        int bx = sc_.u16_at(DS_identify_first);
        const int count = sc_.u16_at(DS_visible_count);
        for (int guard = 0; guard < 0x1000; guard++) {
            if (bx >= count && si == 0) break;
            bool sprite = si == 0;
            if (!sprite && bx < count) {
                const u16 w = sc_.u16_at(u16(DS_group_a_key + 2 * (si - 1)));
                const u8 mean = u8((u8(w) + u8(w >> 8)) >> 1);
                sprite = mean > sc_.u8_at(u16(DS_visible_elevation + bx));
            }
            if (sprite) {
                if (bx < MAX_ENTRIES && sc_.has_sprite[bx]) {
                    const u16 object = sc_.u16_at(u16(DS_visible_object + 2 * bx));
                    const bool culled = u8(sc_.u8_at(u16(DS_visible_bearing + bx)) + 8) > 0x90 && object < 0x48;
                    if (!culled) sprite_draw(bx);
                }
                bx++;
            } else {
                si--;
                const u16 prim = sc_.u16_at(u16(DS_group_a_order + 2 * si));
                if (prim <= 0x1FD) {
                    const u8 ctrl = sc_.u8_at(u16(DS_vertex_control + prim));
                    if (ctrl & 0x3F) primitive(prim, u8(ctrl >> 6), ctrl & 0x3F);
                }
            }
        }
    }

    // The object's position, between the two captures when the object is the same one.
    void object_position(u16 object, double &x, double &y) const
    {
        x = sc_.u16_at(u16(DS_object_x + object));
        y = sc_.u16_at(u16(DS_object_y + object));
        if (!prev_ || prev_->u8_at(u16(DS_object_word + object)) != sc_.u8_at(u16(DS_object_word + object))) return;
        const double dx = wrap16(x - prev_->u16_at(u16(DS_object_x + object)));
        const double dy = wrap16(y - prev_->u16_at(u16(DS_object_y + object)));
        if (std::fabs(dx) > 64 || std::fabs(dy) > 64) return;
        x -= dx * (1.0 - t_);
        y -= dy * (1.0 - t_);
    }

    // ---- visible_project (0919:6d92), sprite_view_angle (0919:60e0), blit_record (0919:5c71) and
    // blit_place (0919:5d5a): the entry's sprite at its bearing, standing at its inverse distance,
    // at the apparent size of the kind's size against the distance.
    void sprite_draw(int e) const
    {
        const SpriteImage &img = sc_.sprites[e];
        const u16 object = sc_.u16_at(u16(DS_visible_object + 2 * e));
        double ox, oy;
        object_position(object, ox, oy);
        const double dx = wrap16(ox * 4 - cam_.qx), dy = wrap16(oy * 4 - cam_.qy);
        const double d = wrap16(bearing16(dx, dy) - cam_.view);
        const double dist_word = std::min(2.0 * std::hypot(dx, dy), double(0x7FFF));
        const double inv = dist_word < 256 ? 255.0 : std::min(255.0, 65535.0 / dist_word);
        const u8 kind = sc_.u8_at(u16(DS_object_word + object));
        const double world = double((sc_.u8_at(u16(DS_kind_sprite_info + kind)) & 0xFC) << 3);
        const double size = world == 0 && dist_word == 0 ? 0 : std::atan2(world, dist_word) * (128.0 / PI);
        double fx, fy;
        scale_table.factors(size, fx, fy);
        const double centre = VIEW_X + 128 + d / 128.0;
        const double bottom = 48 + inv / 8.0 + cam_.sprite_h;
        const double w1 = img.part1.w * fx, h1 = img.part1.h * fy;
        blit(img.part1, centre - w1 / 2, bottom - h1, w1, h1);
        if (img.part2.w) {
            const double w2 = img.part2.w * fx, h2 = img.part2.h * fy;
            const double off = w2 * img.shift / 512.0;
            blit(img.part2, centre - w2 / 2 - off, bottom - h1 - h2, w2, h2);
        }
    }

    // The image scaled to the page rectangle (x, y, w, h), zero pixels transparent.
    void blit(const SpritePart &p, double x, double y, double w, double h) const
    {
        if (p.w == 0 || p.h == 0 || w <= 0 || h <= 0) return;
        const double X0 = tx(x), Y0 = ty(y), W = w * tg_.sx, H = h * tg_.sy;
        const int j0 = std::max(int(std::ceil(Y0 - 0.5)), 0), j1 = std::min(int(std::ceil(Y0 + H - 0.5)), tg_.h);
        const int i0 = std::max(int(std::ceil(X0 - 0.5)), 0), i1 = std::min(int(std::ceil(X0 + W - 0.5)), tg_.w);
        for (int j = j0; j < j1; j++) {
            const int v = std::min(int((j + 0.5 - Y0) / H * p.h), p.h - 1);
            const u8 *src = p.px.data() + size_t(v) * p.w;
            u8 *row = tg_.px + size_t(j) * tg_.w;
            for (int i = i0; i < i1; i++) {
                const int u = std::min(int((i + 0.5 - X0) / W * p.w), p.w - 1);
                if (src[u] && needed(j, i)) row[i] = src[u];
            }
        }
    }

    // ---- spotlights (0919:7a89) and spotlight_beam (0919:7b17, 7bbd): at night, not in the chase
    // view, with the main switch on: each lit light's beam, ten rows of the widths table, around its
    // gun's bearing; the pixels under it get bit 3 where bit 4 is clear.
    void spotlights() const
    {
        if (sc_.u8_at(DS_chase_view) != 0 || sc_.u8_at(DS_daylight) != 0 || (sc_.u8_at(DS_panel_switches) & 1)) return;
        auto aim = [&](u8 condition, int gun) {
            const u8 frac = u8(condition | sc_.u8_at(u16(DS_heading_fraction + gun)));
            const u8 elevation = gun == 1 ? sc_.u8_at(DS_elevation_bow)
                                 : gun == 2 ? sc_.u8_at(DS_elevation_midship)
                                            : sc_.u8_at(DS_elevation_stern);
            beam(u16(sc_.u8_at(u16(DS_heading + gun)) << 8 | frac), elevation);
        };
        if (!(sc_.u8_at(u16(DS_panel_switches + 8)) & 1)) {
            const u8 c = sc_.u8_at(DS_spotlight_front_condition) & 3;
            if (c != 2 && !(sc_.u8_at(DS_bow_mount) & 1)) aim(c, 1);
        }
        if (!(sc_.u8_at(u16(DS_panel_switches + 0x0D)) & 1)) {
            const u8 c = sc_.u8_at(DS_spotlight_rear_condition) & 3;
            if (c != 2 && !(sc_.u8_at(DS_midship_mount) & 1)) aim(c, 2);
        }
        if (!(sc_.u8_at(u16(DS_panel_switches + 0x10)) & 1) && !(sc_.u8_at(DS_stern_mount) & 1)) {
            const u8 c = sc_.u8_at(DS_spotlight_middle_condition) & 3;
            if (c != 2) aim(c, 3);
        }
    }

    void beam(u16 ax, u8 bl) const
    {
        const double rel = wrap16(double((ax & 0xFF00) | (ax & 7) << 5) - cam_.view) / 256.0;  // heading units
        const u8 station = u8(sc_.u16_at(DS_station));
        u8 cl = 0x80;
        if (station >= 2) cl = station == 2 ? sc_.u8_at(DS_elevation_bow)
                                : station == 3 ? sc_.u8_at(DS_elevation_midship)
                                               : sc_.u8_at(DS_elevation_stern);
        cl = u8(cl >> 1);
        cl = u8(cl - u8(bl >> 1));
        cl = u8(s8(u8(-cl)) >> 2);
        bl = u8(u8(-u8(bl - 0x10)) >> 4);
        const u8 bh = u8(sc_.u8_at(DS_view_pitch) + cl - 0x5E);
        bl = u8(bl - bh);
        if (bl & 0x80) bl = 0;
        if (bl > 0x12) bl = 0x12;
        const u8 first = u8(0x2D - cl);
        const u16 table = u16(u8(bl * 10) + CS_spotlight_widths);
        const double centre = VIEW_X + 128 + 2 * rel;
        for (int k = 0; k < 10; k++) {
            const int row = first + k;
            if (row >= 0x40) break;
            const int width = seg_u8(CSSEG_spotlight_widths, u16(table + k));
            const int half = 0xA0 - width;
            if (half <= 0) continue;
            const double y0 = VIEW_Y + row, y1 = y0 + 1;
            const int j0 = std::max(int(std::ceil(ty(y0) - 0.5)), 0), j1 = std::min(int(std::ceil(ty(y1) - 0.5)), tg_.h);
            const int i0 = std::max(int(std::ceil(tx(centre - half) - 0.5)), 0);
            const int i1 = std::min(int(std::ceil(tx(centre + half) - 0.5)), tg_.w);
            for (int j = j0; j < j1; j++) {
                u8 *r = tg_.px + size_t(j) * tg_.w;
                for (int i = i0; i < i1; i++)
                    if (!(r[i] & 0x10)) r[i] |= 8;
            }
        }
    }
};

} // namespace

bool view3d_compatible(const Scene &a, const Scene &b)
{
    if (!a.valid || !b.valid) return false;
    if (a.u16_at(DS_station) != b.u16_at(DS_station) || a.u16_at(DS_look_direction) != b.u16_at(DS_look_direction) ||
        a.u8_at(DS_chase_view) != b.u8_at(DS_chase_view))
        return false;
    if (b.time_ns - a.time_ns > 500'000'000ull) return false;
    return std::fabs(wrap16(double(a.u16_at(DS_view_heading_low)) - b.u16_at(DS_view_heading_low))) < 0x2000 &&
           std::fabs(wrap16(double(a.u16_at(DS_camera_qx)) - b.u16_at(DS_camera_qx))) < 0x800 &&
           std::fabs(wrap16(double(a.u16_at(DS_camera_qy)) - b.u16_at(DS_camera_qy))) < 0x800;
}

void view3d_render(const Scene &cur, const Scene *prev, double t, const ViewTarget &target)
{
    if (prev && !view3d_compatible(*prev, cur)) prev = nullptr;
    Renderer r(cur, prev, std::clamp(t, 0.0, 1.0), target);
    r.run();
}

} // namespace gb
