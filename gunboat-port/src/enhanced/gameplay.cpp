// Gameplay changes (gameplay.hpp).
#include "enhanced/gameplay.hpp"

#include <algorithm>
#include <cmath>

#include "enhanced/debris.hpp"
#include "enhanced/settings.hpp"
#include "host.hpp"
#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr double EYE = 32;          // the gun's height: the camera's (render3d: 32 height units)
constexpr double TARGET_UP = 8;     // a target's height above its ground that the shot aims at
constexpr double CLEAR = 48;        // quarter units at each end of the line that do not count (the boat
                                    // and the target stand on their own ground)
constexpr double STEP = 24;         // quarter units between the samples of the line
constexpr double FOOT = 2;          // height units: a hill's foot, where the ground is flat again

// A 16-bit coordinate difference as the game's wrap-around.
double wrap16(double v)
{
    v = std::fmod(v + 32768.0, 65536.0);
    if (v < 0) v += 65536.0;
    return v - 32768.0;
}

} // namespace

double terrain_height(double x, double y)
{
    double best = 0;
    auto vertex = [&](int i, double &vx, double &vy, double &vh) {
        vx = wrap16(ds_u16(u16(DS_vertex_x + 2 * i)) - x);
        vy = wrap16(ds_u16(u16(DS_vertex_y + 2 * i)) - y);
        vh = ds_u16(u16(DS_vertex_height + 2 * i)) & 0xFF;
    };
    auto triangle = [&](int i) {
        const u8 ctrl = ds_u8(u16(DS_vertex_control + i));
        const u8 mode = u8(ctrl >> 6);
        if (!(ctrl & 0x3F) || mode == 1) return;
        const int a = mode & 2 ? i - 1 : i, b = i + 1, c = i + 2;
        if (a < 0 || c >= 0x400) return;
        double ax, ay, ah, bx, by, bh, cx, cy, ch;
        vertex(a, ax, ay, ah);
        vertex(b, bx, by, bh);
        vertex(c, cx, cy, ch);
        // barycentric coordinates of the point (0, 0) relative to the triangle
        const double d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::fabs(d) < 1e-9) return;
        const double u = ((by - cy) * (0 - cx) + (cx - bx) * (0 - cy)) / d;
        const double v = ((cy - ay) * (0 - cx) + (ax - cx) * (0 - cy)) / d;
        const double w = 1 - u - v;
        if (u < -1e-6 || v < -1e-6 || w < -1e-6) return;
        best = std::max(best, u * ah + v * bh + w * ch);
    };
    const int na = std::min<int>(ds_u16(DS_group_a_count), 0x1FE);
    for (int i = 0; i < na; i++) triangle(i);
    const int nb = std::min<int>(ds_u16(DS_group_b_count), 0x1FE);
    for (int i = 0x200; i < 0x200 + nb; i++) triangle(i);
    return best;
}

namespace {

// Where the line from the gun to the target at (tx, ty) first passes under the ground, if it does.
bool blocked(double tx, double ty, double &bx, double &by, double &bh)
{
    const double gx = ds_u16(DS_camera_qx), gy = ds_u16(DS_camera_qy);
    const double dx = wrap16(tx - gx), dy = wrap16(ty - gy), dist = std::hypot(dx, dy);
    if (dist < 2 * CLEAR) return false;
    const double h1 = terrain_height(tx, ty) + TARGET_UP;
    for (double s = CLEAR; s < dist - CLEAR; s += STEP) {
        const double f = s / dist, x = gx + dx * f, y = gy + dy * f, line = EYE + (h1 - EYE) * f;
        const double h = terrain_height(x, y);
        if (h > line) {
            bx = x;
            by = y;
            bh = h;
            return true;
        }
    }
    return false;
}

// hit_objects asks before object `obj` is hit.
bool shot_blocked(u16 obj)
{
    double bx, by, bh;
    return blocked(ds_u16(u16(DS_object_x + obj)) * 4.0, ds_u16(u16(DS_object_y + obj)) * 4.0, bx, by, bh);
}

// A grenade (weapon 2) or mortar shell (3) landing at (x, y) map units behind a hill bursts at the
// hill's foot on the gun's side instead: back along the line from where the hill stops it to where
// the ground is flat again (the game's objects stand at height 0: its explosion drawn on the hillside
// would sink into the hill). Nothing behind the hill is hit (shot_blocked).
bool shell_stopped(u8 weapon, u16 &x, u16 &y)
{
    if (weapon != 2 && weapon != 3) return false;
    double bx, by, bh;
    if (!blocked(x * 4.0, y * 4.0, bx, by, bh)) return false;
    const double gx = ds_u16(DS_camera_qx), gy = ds_u16(DS_camera_qy);
    const double dx = wrap16(bx - gx), dy = wrap16(by - gy), dist = std::hypot(dx, dy);
    double s = dist;
    while (s > CLEAR && terrain_height(gx + dx * s / dist, gy + dy * s / dist) > FOOT) s -= STEP / 3;
    s = std::max(s, CLEAR);
    auto map_unit = [](double q) { return u16(std::lround(std::fmod(q + 65536.0, 65536.0) / 4.0)); };
    x = map_unit(gx + dx * s / dist);
    y = map_unit(gy + dy * s / dist);
    return true;
}

// A shot lands at (x, y) map units: behind a hill, its debris flies off the hill.
void shot_landed(u16 x, u16 y, u8)
{
    double bx, by, bh;
    if (blocked(x * 4.0, y * 4.0, bx, by, bh)) debris_move_last(bx, by, bh);
}

} // namespace

void gameplay_install(const Settings &s)
{
    if (s.hills_stop_bullets) {
        host_set_shot_blocked_handler(shot_blocked);
        host_set_shell_stopped_handler(shell_stopped);
        host_add_shot_landed_observer(shot_landed);  // after the debris' own (debris_install)
    }
}

} // namespace gb
