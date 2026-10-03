// Impact debris (debris.hpp).
#include "enhanced/debris.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "enhanced/gameplay.hpp"
#include "enhanced/materials.hpp"
#include "host.hpp"
#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double SIZE = 2.5;     // the particles' size, times the table's
constexpr double LINGER = 0.25;  // seconds a particle lies on the ground, fading, before it is gone
// A hill hides a particle where it is nearer by this factor (its scale larger): the hill's depth is
// interpolated over its triangles, and debris flying off a hillside must not vanish into it.
constexpr double HIDDEN = 1.1;

// What a shot hit. Grass is any green ground (brush, jungle, grass): pale leaves and twigs that drift
// down, so that they show against the green; Wood has pale fresh splinters among the brown.
enum class Stuff : u8 { Ground, Water, Grass, Dirt, Metal, Wood, Flesh, Stone, Sand, COUNT };

// A shot's landing until its particles are made (when the view first shows its place).
struct Impact {
    double x, y, h;  // quarter units; height (0 the flat ground and the water)
    double floor;    // the height its particles come to rest at: the ground's there
    Stuff stuff;
    bool explosive;  // the grenade launcher and the mortar (weapons 2 and 3)
    Uint64 t;
};

struct Particle {
    double x, y, h;     // at birth
    double floor;       // the ground's height it falls back to
    double vx, vy, vh;  // per second
    double gravity;     // height units per second squared
    double drag;        // per second: the decay of the horizontal speed
    double life;        // seconds
    Uint64 born;
    u32 rgb;
    double size;        // height units
    bool streak;        // drawn as a short line back along its path (sparks)
    bool sinks;         // gone when it falls back (water drops), else it lies there a moment
};

// How each stuff flies apart: particles for a bullet (an explosive shot makes three times as many,
// faster and bigger); horizontal speeds in quarter units, vertical in height units, per second.
struct Spec {
    int n;
    u32 colour[3];
    double speed0, speed1, up0, up1, gravity, drag, life0, life1, size;
    bool streak;
};
const Spec SPECS[int(Stuff::COUNT)] = {
    {0, {0, 0, 0}, 0, 0, 0, 0, 0, 0, 0, 0, 0, false},                                          // Ground
    {16, {0xFFFFFF, 0xDDEEFF, 0xB0D4F0}, 5, 16, 24, 44, 85, 0.8, 0.55, 0.9, 0.65, false},       // Water
    {14, {0xD2E87E, 0x9AD24E, 0x6B4A2A}, 8, 22, 14, 30, 26, 1.6, 0.8, 1.4, 0.8, false},        // Grass
    {13, {0, 0, 0}, 6, 18, 7, 18, 28, 2.5, 0.7, 1.2, 0.95, false},                               // Dirt
    {12, {0xFFF4C8, 0xFFD050, 0xFF9A28}, 30, 70, 14, 36, 56, 0, 0.3, 0.6, 0.5, true},           // Metal
    {12, {0xE8CC94, 0xA87840, 0x5C3A1C}, 14, 34, 14, 30, 48, 0.5, 0.75, 1.3, 0.9, false},       // Wood
    {16, {0xE01C1C, 0xFF3A3A, 0xA80C0C}, 12, 30, 8, 24, 50, 0.5, 0.55, 0.95, 0.8, false},       // Flesh
    {10, {0x9A968C, 0xBAB6AC, 0x6C6862}, 16, 38, 12, 26, 52, 0.3, 0.6, 1.05, 0.7, false},       // Stone
    {13, {0xC8B07A, 0xB09460, 0xDCC894}, 8, 20, 6, 16, 24, 2.0, 0.7, 1.2, 1.05, false},         // Sand
};

std::vector<Impact> impacts;
std::vector<Particle> particles;
u32 rng = 0x13579BDFu;

double rnd()
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (rng >> 8) / double(1 << 24);
}
double between(double a, double b) { return a + (b - a) * rnd(); }

u32 shade(u32 c, double f)
{
    auto ch = [&](int s) { return u32(std::clamp(int(((c >> s) & 0xFF) * f), 0, 255)) << s; };
    return ch(16) | ch(8) | ch(0);
}

// The object kinds' stuff: their material (materials.hpp); bridges spark as metal, trees chip as wood.
Stuff stuff_of(u8 kind)
{
    switch (material_of(kind)) {
    case Material::Metal:
    case Material::Bridge: return Stuff::Metal;
    case Material::Wood:
    case Material::Tree: return Stuff::Wood;
    case Material::Sand: return Stuff::Sand;
    case Material::Flesh: return Stuff::Flesh;
    default: return Stuff::Stone;
    }
}

// A shot of `weapon` lands at (x, y) map units: on the ground unless it hits an object next. The game
// has it land at height 0; on a hillside its debris flies from the hill's surface there (else it would
// start inside the hill, hidden by it).
void on_shot_landed(u16 x, u16 y, u8 weapon)
{
    if (impacts.size() >= 64) impacts.erase(impacts.begin());
    const double h = terrain_height(x * 4.0, y * 4.0);
    impacts.push_back({x * 4.0, y * 4.0, h, h, Stuff::Ground, weapon == 2 || weapon == 3, SDL_GetTicksNS()});
}

// The shot that just landed hit an object of kind `kind`: its debris is the object's stuff, flying
// out of the game's own dust puff at the landing point (projectile_impact's explosion object).
void on_object_hit(u8 kind, u16)
{
    if (impacts.empty() || kind == 0x30 || kind == 0x31) return;  // the shot passes wrecks 30h/31h
    Impact &im = impacts.back();
    im.h = kind >= 0x2A && kind <= 0x2E ? between(3, 14) : between(1, 4);  // trees: up the trunk
    im.floor = 0;  // the objects stand at height 0 (their sprites)
    im.stuff = stuff_of(kind);
}

void spawn(const Impact &im, Stuff stuff, u32 ground)
{
    const Spec &sp = SPECS[int(stuff)];
    const double big = im.explosive ? 1.5 : 1.0, fast = im.explosive ? 1.7 : 1.0;
    const int n = sp.n * (im.explosive ? 3 : 1);
    u32 colour[3] = {sp.colour[0], sp.colour[1], sp.colour[2]};
    if (stuff == Stuff::Dirt) {
        colour[0] = ground;
        colour[1] = shade(ground, 1.25);
        colour[2] = shade(ground, 0.75);
    }
    if (particles.size() > 2000) return;
    for (int i = 0; i < n; i++) {
        Particle p;
        const double a = between(0, 2 * PI), v = between(sp.speed0, sp.speed1) * fast;
        p.x = im.x + between(-2, 2);
        p.y = im.y + between(-2, 2);
        p.h = im.h;
        p.floor = im.floor;
        p.vx = std::cos(a) * v;
        p.vy = std::sin(a) * v;
        p.vh = between(sp.up0, sp.up1) * fast;
        p.gravity = sp.gravity;
        p.drag = sp.drag;
        p.life = between(sp.life0, sp.life1);
        p.born = im.t;
        p.rgb = colour[i % 3];
        p.size = sp.size * SIZE * big * between(0.7, 1.3);
        p.streak = sp.streak;
        p.sinks = stuff == Stuff::Water;
        particles.push_back(p);
    }
}

// Where particle p is at `age` seconds.
void position(const Particle &p, double age, double &x, double &y, double &h)
{
    const double k = p.drag > 0 ? (1 - std::exp(-p.drag * age)) / p.drag : age;
    x = p.x + p.vx * k;
    y = p.y + p.vy * k;
    h = std::max(p.floor, p.h + p.vh * age - 0.5 * p.gravity * age * age);
}

// When particle p falls back to its ground.
double landing_time(const Particle &p)
{
    if (p.gravity <= 0) return 1e9;
    return (p.vh + std::sqrt(p.vh * p.vh + 2 * p.gravity * std::max(0.0, p.h - p.floor))) / p.gravity;
}

void blend(u32 *row, int x, u32 c, double a)
{
    const u32 d = row[x];
    auto ch = [&](int s) { return u32(((c >> s) & 0xFF) * a + ((d >> s) & 0xFF) * (1 - a) + 0.5) << s; };
    row[x] = 0xFF000000u | ch(16) | ch(8) | ch(0);
}

} // namespace

void debris_move_last(double x, double y, double h)
{
    if (impacts.empty()) return;
    Impact &im = impacts.back();
    im.x = x;
    im.y = y;
    im.h = h;
    im.floor = h;
    im.stuff = Stuff::Ground;
}

void debris_clear()
{
    impacts.clear();
    particles.clear();
}

void debris_install()
{
    host_add_shot_landed_observer(on_shot_landed);
    host_add_object_hit_observer(on_object_hit);
}

void debris_draw(const ViewProjection &proj, const ViewTarget &target, u32 *rgb, int pitch, const u32 *pal, u8 water)
{
    const Uint64 now = SDL_GetTicksNS();
    auto to_target = [&](double px, double py, double &tx, double &ty) {
        tx = (px - target.ox) * target.sx;
        ty = (py - target.oy) * target.sy;
        return tx >= 0 && ty >= 0 && tx < target.w && ty < target.h;
    };

    // The new impacts in this region: their particles, the ground's look from the pixel there.
    for (size_t k = 0; k < impacts.size();) {
        const Impact &im = impacts[k];
        double px, py, tx, ty;
        proj.point(im.x, im.y, im.h, px, py);
        if (!to_target(px, py, tx, ty)) {
            if (now - im.t > 300 * SDL_NS_PER_MS) impacts.erase(impacts.begin() + long(k));  // never in view
            else k++;
            continue;
        }
        Stuff stuff = im.stuff;
        u32 ground = 0;
        if (stuff == Stuff::Ground) {
            const u8 c = target.px[size_t(int(ty)) * target.w + size_t(int(tx))];
            ground = pal[c] & 0xFFFFFF;
            const int r = int(ground >> 16 & 0xFF), g = int(ground >> 8 & 0xFF), b = int(ground & 0xFF);
            if (c == water || c == u8(water | 7)) stuff = Stuff::Water;
            else if (g > r + 12 && g > b + 8) stuff = Stuff::Grass;
            else stuff = Stuff::Dirt;
        }
        spawn(im, stuff, ground);
        impacts.erase(impacts.begin() + long(k));
    }

    // The live particles.
    particles.erase(std::remove_if(particles.begin(), particles.end(),
                                   [&](const Particle &p) { return now > p.born + Uint64(p.life * 1e9); }),
                    particles.end());
    for (const Particle &p : particles) {
        const double age = (now - p.born) / 1e9;
        double x, y, h;
        position(p, age, x, y, h);
        // Back on the ground: a drop is gone into the water; the rest lies there, fading, a moment.
        double fade = 1.0;
        if (h <= p.floor && age > 0.05) {
            if (p.sinks) continue;
            const double land = landing_time(p);
            fade = 1.0 - (age - land) / LINGER;
            if (fade <= 0) continue;
        }
        double px, py, tx, ty;
        const double s = proj.point(x, y, h, px, py);
        if (!to_target(px, py, tx, ty)) continue;
        const double a = fade * (age < 0.6 * p.life ? 1.0 : std::max(0.0, (p.life - age) / (0.4 * p.life)));
        const int size = std::max(1, int(std::lround(s * p.size / 256.0 * target.sy)));
        // behind a hill: its pixels there are not drawn (ViewTarget::depth; sprites never hide it)
        const double hidden = s * 256.0 * HIDDEN;
        auto dot = [&](double cx, double cy, double alpha) {
            const int x0 = int(cx - size / 2.0), y0 = int(cy - size / 2.0);
            for (int j = std::max(0, y0); j < std::min(target.h, y0 + size); j++) {
                u32 *row = reinterpret_cast<u32 *>(reinterpret_cast<u8 *>(rgb) + size_t(j) * pitch);
                const u16 *depth = target.depth ? target.depth + size_t(j) * target.w : nullptr;
                for (int i = std::max(0, x0); i < std::min(target.w, x0 + size); i++)
                    if (!depth || depth[i] <= hidden) blend(row, i, p.rgb, alpha);
            }
        };
        dot(tx, ty, a);
        if (p.streak) {  // back along the path, fading
            double x2, y2, h2, px2, py2, tx2, ty2;
            position(p, std::max(0.0, age - 0.04), x2, y2, h2);
            proj.point(x2, y2, h2, px2, py2);
            to_target(px2, py2, tx2, ty2);
            const int steps = std::min(12, int(std::hypot(tx2 - tx, ty2 - ty)));
            for (int i = 1; i <= steps; i++) {
                const double f = double(i) / (steps + 1);
                dot(tx + (tx2 - tx) * f, ty + (ty2 - ty) * f, a * (1 - f));
            }
        }
    }
}

} // namespace gb
