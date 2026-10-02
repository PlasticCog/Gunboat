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

// The camera of `cur`, interpolated from `prev` at t (smooth motion).
Camera interpolated_camera(const Scene &cur, const Scene *prev, double t)
{
    Camera cam = camera_of(cur);
    if (prev) {
        const Camera p = camera_of(*prev);
        const double back = 1.0 - t;
        cam.qx -= wrap16(cam.qx - p.qx) * back;
        cam.qy -= wrap16(cam.qy - p.qy) * back;
        cam.view -= wrap16(cam.view - p.view) * back;
        cam.horizon -= (cam.horizon - p.horizon) * back;
        cam.sprite_h -= (cam.sprite_h - p.sprite_h) * back;
    }
    return cam;
}

// project (0919:7523) in floating point: a point at (x, y) quarter units, height h, on the page, for
// the camera (qx, qy, view, horizon); returns its scale.
double project_point(double qx, double qy, double view, double horizon, double x, double y, double h, double &px,
                     double &py)
{
    const double dx = wrap16(x - qx), dy = wrap16(y - qy);
    const double d = wrap16(bearing16(dx, dy) - view);
    const double dist = std::hypot(dx, dy);
    const double s = dist > 32767.5 / 255.0 ? 32767.5 / dist : 255.0;
    px = VIEW_X + 130 + d / 128.0;
    py = horizon + std::max(0.0, 512.0 + s - s * h / 32.0) / 8.0;
    return s;
}

class Renderer {
public:
    Renderer(const Scene &cur, const Scene *prev, double t, const ViewTarget &target)
        : sc_(cur), prev_(prev), t_(t), tg_(target)
    {
        cam_ = interpolated_camera(cur, prev_, t_);
        view_shift_ = wrap16(double(cur.u16_at(DS_view_heading_low)) - cam_.view) / 128.0;
        if (!scale_table.ready) scale_table.build();
    }

    ViewHorizon run()
    {
        if (tg_.depth) std::fill(tg_.depth, tg_.depth + size_t(tg_.w) * tg_.h, u16(0));
        sky_and_water();
        water_marks();
        for (int i = 0; i < 0x400; i++) projected_[i] = false;
        if (tg_.far && sc_.far) far_world();
        group_b();
        group_a_and_sprites();
        spotlights();
        return horizon();
    }

private:
    const Scene &sc_;
    const Scene *prev_;
    double t_;
    const ViewTarget &tg_;
    Camera cam_;
    double view_shift_;  // columns the interpolated view lies left of the captured one
    bool projected_[0x400];
    double vx_[0x400], vy_[0x400], vs_[0x400];

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
    ViewHorizon horizon() const
    {
        const u8 shake = sc_.u8_at(DS_screen_shake);
        ViewHorizon v;
        v.y = VIEW_Y + cam_.horizon;
        v.sky = sc_.u8_at(DS_scene_colours);
        v.water = sc_.u8_at(u16(DS_scene_colours + 1));
        if (shake != 0) {
            v.sky = 0x0F;
            v.water = 0x0E;
        } else if (prev_ && prev_->u8_at(DS_screen_shake) == 1) {
            v.sky = 0x0E;
        }
        return v;
    }

    void sky_and_water() const
    {
        const ViewHorizon v = horizon();
        const u8 sky = v.sky, water = v.water;
        const double h = v.y;
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
        vs_[i] = point(sc_.u16_at(u16(DS_vertex_x + 2 * i)), sc_.u16_at(u16(DS_vertex_y + 2 * i)),
                       sc_.u16_at(u16(DS_vertex_height + 2 * i)) & 0xFF, vx_[i], vy_[i]);
    }

    // A point at (x, y) quarter units, height h, on the page; returns its scale.
    double point(double x, double y, double h, double &px, double &py) const
    {
        return project_point(cam_.qx, cam_.qy, cam_.view, cam_.horizon, x, y, h, px, py);
    }

    // ---- the extended draw distance: the far cells from the farthest in, each its group B (from its
    // last primitive down, as draw_group_b), its group A (farthest first) and the objects standing in
    // it (farthest first).
    void far_world()
    {
        const FarWorld &fw = *sc_.far;
        std::vector<std::pair<double, size_t>> cells;
        for (size_t k = 0; k < fw.cells.size(); k++)
            cells.push_back({std::hypot(wrap16(fw.cells[k].cx - cam_.qx), wrap16(fw.cells[k].cy - cam_.qy)), k});
        std::sort(cells.begin(), cells.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        std::vector<double> px, py, scale;
        auto project_all = [&](const std::vector<FarVertex> &v) {
            px.resize(v.size());
            py.resize(v.size());
            scale.resize(v.size());
            for (size_t i = 0; i < v.size(); i++) scale[i] = point(v[i].x, v[i].y, v[i].height, px[i], py[i]);
        };
        auto prim = [&](const std::vector<FarVertex> &v, int i) {
            const u8 ctrl = v[size_t(i)].control, mode = u8(ctrl >> 6);
            if (!(ctrl & 0x3F) || mode == 1) return;
            const int a = mode & 2 ? i - 1 : i, b = i + 1, c = i + 2;
            if (a < 0 || c >= int(v.size())) return;
            const double x0 = px[size_t(a)];
            const bool raised = v[size_t(a)].height || v[size_t(b)].height || v[size_t(c)].height;
            triangle(x0, py[size_t(a)], x0 + wrap16((px[size_t(b)] - x0) * 128.0) / 128.0, py[size_t(b)],
                     x0 + wrap16((px[size_t(c)] - x0) * 128.0) / 128.0, py[size_t(c)], ctrl & 0x3F,
                     scale[size_t(a)], scale[size_t(b)], scale[size_t(c)], raised);
        };
        std::vector<std::pair<double, int>> order;
        std::vector<std::pair<double, const FarObject *>> objects;
        for (const auto &ck : cells) {
            const FarCell &cell = fw.cells[ck.second];
            project_all(cell.b);
            for (int i = int(cell.b.size()) - 1; i >= 0; i--) prim(cell.b, i);
            project_all(cell.a);
            order.clear();
            for (int i = 0; i < int(cell.a.size()); i++) {
                const int a = (cell.a[size_t(i)].control & 0x80) ? i - 1 : i;
                if (a < 0 || i + 2 >= int(cell.a.size())) continue;
                order.push_back({std::max({scale[size_t(a)], scale[size_t(i + 1)], scale[size_t(i + 2)]}), i});
            }
            std::sort(order.begin(), order.end());
            for (const auto &o : order) prim(cell.a, o.second);
            // the objects standing in this cell: its scenery, and the authored ones inside it
            objects.clear();
            auto add = [&](const FarObject &obj) {
                objects.push_back({std::hypot(wrap16(obj.x * 4.0 - cam_.qx), wrap16(obj.y * 4.0 - cam_.qy)), &obj});
            };
            for (const FarObject &obj : cell.objects) add(obj);
            for (const FarObject &obj : sc_.far_objects)
                if (std::fabs(obj.x * 4.0 - cell.cx) <= 2048 && std::fabs(obj.y * 4.0 - cell.cy) <= 2048) add(obj);
            std::sort(objects.begin(), objects.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
            for (const auto &o : objects) far_object(*o.second);
        }
    }

    // A far object's sprite: its image in the view the capture saw it in.
    void far_object(const FarObject &obj) const
    {
        const double cdx = double(u16(obj.x << 2)) - sc_.u16_at(DS_camera_qx);
        const double cdy = double(u16(obj.y << 2)) - sc_.u16_at(DS_camera_qy);
        const u8 angle = u8(int(std::floor(std::atan2(cdx, cdy) * (128.0 / PI))) & 0xFF);
        const SpriteImage *img = far_sprite(obj.kind, far_view_byte(obj.kind, obj.flags, angle));
        if (!img) return;
        sprite_at(*img, obj.x, obj.y, obj.kind);
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
        auto height = [&](int k) { return sc_.u16_at(u16(DS_vertex_height + 2 * k)) & 0xFF; };
        triangle(x0, vy_[a], x1, vy_[b], x2, vy_[c], colour, vs_[a], vs_[b], vs_[c], height(a) || height(b) || height(c));
    }

    // fill_triangle and the span routine (0919:7802, 788e) fill the rows of the vertices' rows
    // inclusive, from the left edge rounded (+40h) to the right edge plus one column (+BFh), at least
    // one pixel: the triangle drawn here is grown by as much in page pixels (half a row up and down,
    // a column to the right), so that at any scale it covers what the original covers and thin or
    // distant terrain (beaches, far shores) keeps the size the original gives it.
    static constexpr double GROW_UP = 0.5, GROW_DOWN = 0.5, GROW_LEFT = 0.0, GROW_RIGHT = 1.0;

    // The triangle's vertices' scales s0..s2 and whether it is raised (a vertex above height 0) make
    // the target's depth there (ViewTarget::depth).
    void triangle(double x0, double y0, double x1, double y1, double x2, double y2, u8 c, double s0, double s1,
                  double s2, bool raised) const
    {
        double X[3] = {tx(x0), tx(x1), tx(x2)}, Y[3] = {ty(y0), ty(y1), ty(y2)}, S[3] = {s0, s1, s2};
        // sort by row
        for (int p = 0; p < 2; p++)
            for (int q = 0; q < 2 - p; q++)
                if (Y[q] > Y[q + 1]) {
                    std::swap(Y[q], Y[q + 1]);
                    std::swap(X[q], X[q + 1]);
                    std::swap(S[q], S[q + 1]);
                }
        const double up = GROW_UP * tg_.sy, down = GROW_DOWN * tg_.sy;
        const double left = GROW_LEFT * tg_.sx, right = GROW_RIGHT * tg_.sx;
        const double xmin = std::min({X[0], X[1], X[2]}), xmax = std::max({X[0], X[1], X[2]});
        if (xmax + right < 0 || xmin - left > tg_.w || Y[2] + down < 0 || Y[0] - up > tg_.h) return;
        const int j0 = std::max(int(std::ceil(Y[0] - up - 0.5)), 0);
        const int j1 = std::min(int(std::ceil(Y[2] + down - 0.5)), tg_.h);
        const bool flat = Y[2] - Y[0] < 1e-9;
        const Depth depth = tg_.depth ? depth_plane(X, Y, S, raised) : Depth{};
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
            const int i0 = int(std::ceil(l - left - 0.5)), i1 = int(std::ceil(r + right - 0.5));
            fill(j, i0, i1, c);
            if (tg_.depth) depth_span(j, i0, i1, depth);
        }
    }

    // A raised triangle's scale over the target's pixels: s = a x + b y + c at the pixels' centres, kept
    // within its vertices' (the scale is 1 / distance: nearly linear on the page over a triangle).
    struct Depth {
        double a = 0, b = 0, c = 0, lo = 0, hi = 0;
        bool raised = false;
    };
    static Depth depth_plane(const double X[3], const double Y[3], const double S[3], bool raised)
    {
        Depth d;
        d.raised = raised;
        if (!raised) return d;
        d.lo = std::min({S[0], S[1], S[2]});
        d.hi = std::max({S[0], S[1], S[2]});
        const double dx1 = X[1] - X[0], dy1 = Y[1] - Y[0], dx2 = X[2] - X[0], dy2 = Y[2] - Y[0];
        const double det = dx1 * dy2 - dx2 * dy1;
        if (std::fabs(det) < 1e-9) {
            d.c = d.hi;
            return d;
        }
        d.a = ((S[1] - S[0]) * dy2 - (S[2] - S[0]) * dy1) / det;
        d.b = (dx1 * (S[2] - S[0]) - dx2 * (S[1] - S[0])) / det;
        d.c = S[0] - d.a * X[0] - d.b * Y[0];
        return d;
    }
    // The depth of the pixels [i0, i1) of row j: the triangle's, or 0 for flat ground (drawn over a
    // hill, it is nearer than the hill there, and nothing on or above it lies behind it).
    void depth_span(int j, int i0, int i1, const Depth &d) const
    {
        if (j < 0 || j >= tg_.h) return;
        i0 = std::max(i0, 0);
        i1 = std::min(i1, tg_.w);
        if (i0 >= i1) return;
        u16 *row = tg_.depth + size_t(j) * tg_.w;
        if (!d.raised) {
            std::fill(row + i0, row + i1, u16(0));
            return;
        }
        const double y = j + 0.5;
        for (int i = i0; i < i1; i++) row[i] = u16(std::clamp(d.a * (i + 0.5) + d.b * y + d.c, d.lo, d.hi) * 256.0);
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
        const u16 object = sc_.u16_at(u16(DS_visible_object + 2 * e));
        double ox, oy;
        object_position(object, ox, oy);
        sprite_at(sc_.sprites[e], ox, oy, sc_.u8_at(u16(DS_object_word + object)));
    }

    // An image standing at the map position (ox, oy), at the size of its kind against the distance.
    void sprite_at(const SpriteImage &img, double ox, double oy, u8 kind) const
    {
        const double dx = wrap16(ox * 4 - cam_.qx), dy = wrap16(oy * 4 - cam_.qy);
        const double d = wrap16(bearing16(dx, dy) - cam_.view);
        const double dist_word = std::min(2.0 * std::hypot(dx, dy), double(0x7FFF));
        const double inv = dist_word < 256 ? 255.0 : std::min(255.0, 65535.0 / dist_word);
        const double world = double((sc_.u8_at(u16(DS_kind_sprite_info + kind)) & 0xFC) << 3);
        const double size = world == 0 && dist_word == 0 ? 0 : std::atan2(world, dist_word) * (128.0 / PI);
        double fx, fy;
        scale_table.factors(size, fx, fy);
        const double centre = VIEW_X + 128 + d / 128.0;
        const double bottom = 48 + inv / 8.0 + cam_.sprite_h;
        const double w1 = img.part1.w * fx, h1 = img.part1.h * fy;
        // its depth, as project_point's scale (a sprite half inside a hill keeps its own pixels clear)
        const double dist = std::hypot(dx, dy);
        const u16 depth = u16((dist > 32767.5 / 255.0 ? 32767.5 / dist : 255.0) * 256.0);
        blit(img.part1, centre - w1 / 2, bottom - h1, w1, h1, depth);
        if (img.part2.w) {
            const double w2 = img.part2.w * fx, h2 = img.part2.h * fy;
            const double off = w2 * img.shift / 512.0;
            blit(img.part2, centre - w2 / 2 - off, bottom - h1 - h2, w2, h2, depth);
        }
    }

    // The image scaled to the page rectangle (x, y, w, h), zero pixels transparent; the depth of its
    // pixels made no nearer than `depth`.
    void blit(const SpritePart &p, double x, double y, double w, double h, u16 depth) const
    {
        if (p.w == 0 || p.h == 0 || w <= 0 || h <= 0) return;
        const double X0 = tx(x), Y0 = ty(y), W = w * tg_.sx, H = h * tg_.sy;
        const int j0 = std::max(int(std::ceil(Y0 - 0.5)), 0), j1 = std::min(int(std::ceil(Y0 + H - 0.5)), tg_.h);
        const int i0 = std::max(int(std::ceil(X0 - 0.5)), 0), i1 = std::min(int(std::ceil(X0 + W - 0.5)), tg_.w);
        for (int j = j0; j < j1; j++) {
            const int v = std::min(int((j + 0.5 - Y0) / H * p.h), p.h - 1);
            const u8 *src = p.px.data() + size_t(v) * p.w;
            u8 *row = tg_.px + size_t(j) * tg_.w;
            u16 *drow = tg_.depth ? tg_.depth + size_t(j) * tg_.w : nullptr;
            for (int i = i0; i < i1; i++) {
                const int u = std::min(int((i + 0.5 - X0) / W * p.w), p.w - 1);
                if (!src[u]) continue;
                if (needed(j, i)) row[i] = src[u];
                if (drow && drow[i] > depth) drow[i] = depth;
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

double ViewProjection::point(double x, double y, double h, double &px, double &py) const
{
    return project_point(qx, qy, view, horizon, x, y, h, px, py);
}

ViewProjection view3d_projection(const Scene &cur, const Scene *prev, double t)
{
    const Camera c = interpolated_camera(cur, prev, t);
    ViewProjection p;
    p.qx = c.qx;
    p.qy = c.qy;
    p.view = c.view;
    p.horizon = c.horizon;
    return p;
}

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

void gun_frame_hires(const ViewTarget &tg, const Scene &sc, const Scene *before, double t)
{
    if (!sc.gun_frame) return;
    double b = sc.gun_bearing;
    if (before && before->gun_frame)
        b = before->gun_bearing + double(s8(u8(sc.gun_bearing - before->gun_bearing))) * std::clamp(t, 0.0, 1.0);
    b = std::fmod(b + 512.0, 256.0);
    const u8 colour = sc.gun_frame_colour;
    // the page rectangle [x0, x1) x [y0, y1): the target pixels whose centres lie in it
    auto rect = [&](double x0, double y0, double x1, double y1) {
        const int i0 = std::max(int(std::ceil((x0 - tg.ox) * tg.sx - 0.5)), 0);
        const int i1 = std::min(int(std::ceil((x1 - tg.ox) * tg.sx - 0.5)), tg.w);
        const int j0 = std::max(int(std::ceil((y0 - tg.oy) * tg.sy - 0.5)), 0);
        const int j1 = std::min(int(std::ceil((y1 - tg.oy) * tg.sy - 0.5)), tg.h);
        for (int j = j0; j < j1; j++) {
            if (i0 >= i1) break;
            std::memset(tg.px + size_t(j) * tg.w + i0, colour, size_t(i1 - i0));
            if (tg.depth) std::fill(tg.depth + size_t(j) * tg.w + i0, tg.depth + size_t(j) * tg.w + i1, u16(0xFFFF));
        }
    };
    // gfx_draw_bitmap of a piece: the first row at the pen, the next ones upward, MSB left
    auto piece = [&](const u8 *bits, double x, double y) {
        for (int k = 0; k < 32; k++)
            for (int c = 0; c < 8; c++)
                if (bits[k] & (0x80 >> c)) rect(x + c, y - k, x + c + 1, y - k + 1);
    };
    // gun_frame_draw (hud.md §6), the bearing as a real number
    double d = std::fmod(b - 0x40 + 256.0, 256.0);
    if (d <= 0x30) {
        const double v = 2 * (0x30 - d);
        piece(sc.gun_frame_left, 0x20 + v, 0x7F);
        if (v > 8) {
            piece(sc.gun_frame_left, 0x18 + v, 0x5F);
            rect(0x18 + v, 0x60, 0x20 + v, 0x80);
            if (v > 0x10) rect(0x28, 0x40, 0x19 + v, 0x80);
        }
    }
    d = std::fmod(b - 0xD0 + 256.0, 256.0);
    if (d <= 0x30) {
        const double v = 2 * d + 2;
        piece(sc.gun_frame_right, 0x128 - v, 0x7F);
        if (v > 8) {
            piece(sc.gun_frame_right, 0x130 - v, 0x5F);
            rect(0x130 - v, 0x60, 0x138 - v, 0x80);
            if (v > 0x10) rect(0x138 - v, 0x40, 0x128, 0x80);
        }
    }
}

ViewHorizon view3d_render(const Scene &cur, const Scene *prev, double t, const ViewTarget &target)
{
    if (prev && !view3d_compatible(*prev, cur)) prev = nullptr;
    Renderer r(cur, prev, std::clamp(t, 0.0, 1.0), target);
    return r.run();
}

} // namespace gb
