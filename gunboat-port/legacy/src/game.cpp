#include "game.hpp"
#include "simulation.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
extern "C" {
#include "host.h"
#include "mem.h"
#include "platform/vga.h"
u8 mem[0x110000];
}
namespace gb {
namespace {
constexpr int W = 320, H = 200;
std::array<bool, 128> held{}, pressed{};
void keyboard(u8 b) {
    if (b == 0xe0 || b == 0xe1)
        return;
    int key = b & 127;
    if (!(b & 128) && !held[key])
        pressed[key] = true;
    held[key] = !(b & 128);
}
void release_keys() {
    held.fill(false);
    pressed.fill(false);
}
bool take(int k) {
    bool value = pressed[k];
    pressed[k] = false;
    return value;
}
Bytes picture(const Archive &a, const std::string &name, int width) {
    const auto raw = decode_lzw(a.get(name));
    Bytes pixels;
    if (raw.size() % 2)
        throw std::runtime_error("Odd picture RLE length: " + name);
    for (size_t i = 0; i < raw.size(); i += 2) {
        pixels.insert(pixels.end(), raw[i + 1], raw[i]);
        if (pixels.size() > 64000)
            throw std::runtime_error("Picture too large: " + name);
    }
    if (pixels.empty() || pixels.size() % width)
        throw std::runtime_error("Invalid picture dimensions: " + name);
    const int height = int(pixels.size()) / width;
    for (int y = 0; y < height / 2; y++)
        for (int x = 0; x < width; x++)
            std::swap(pixels[y * width + x], pixels[(height - 1 - y) * width + x]);
    return pixels;
}
Bytes cockpit(const Archive &a) {
    auto b = picture(a, "BF1.LZ", 320), c = picture(a, "BF2.LZ", 320);
    b.insert(b.end(), c.begin(), c.end());
    if (b.size() != 64000)
        throw std::runtime_error("Invalid pilot cockpit size");
    return b;
}
// Small native UI font; original cockpit artwork is read from BF1/BF2.LZ.
const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-./%+?";
constexpr uint32_t glyphs[] = {
    0x0e8fe31, 0x1e8fa3e, 0x0f8420f, 0x1e8c63e, 0x1f87a1f, 0x1f87a10, 0x0f85e2f, 0x118fe31,
    0x1f2109f, 0x07212ae, 0x1197291, 0x108421f, 0x11dd631, 0x11cd671, 0x0e8c62e, 0x1e8fa10,
    0x0e8d66f, 0x1e8fa51, 0x0f8383e, 0x1f21084, 0x118c62e, 0x118c544, 0x118d771, 0x1151151,
    0x1151084, 0x1f1111f, 0x0e9d72e, 0x046108e, 0x0e8889f, 0x1e1703e, 0x118fc21, 0x1f8783e,
    0x0f87e2e, 0x1f11108, 0x0e8ba2e, 0x0e8f83e, 0x0040100, 0x0007c00, 0x0000004, 0x0111100,
    0x1921113, 0x0047c84, 0x0e88804};
struct Screen {
    u8 *pixels = mem + 0xa0000;
    std::array<float, W * H> depth{};
    int bottom = 166, center = 69;
    Vec3 eye;
    float yaw = 0, pitch = 0;
    void dot(int x, int y, u8 c) {
        if (x >= 0 && x < W && y >= 0 && y < H)
            pixels[y * W + x] = c;
    }
    void box(int x, int y, int w, int h, u8 c) {
        for (int row = std::max(0, y); row < std::min(H, y + h); row++)
            for (int col = std::max(0, x); col < std::min(W, x + w); col++)
                pixels[row * W + col] = c;
    }
    void line(int x0, int y0, int x1, int y1, u8 color) {
        // Bounded screen lines: callers use projected endpoints only after clipping.
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0),
            sy = y0 < y1 ? 1 : -1, e = dx + dy;
        for (int count = 0; count < 2000; count++) {
            dot(x0, y0, color);
            if (x0 == x1 && y0 == y1)
                break;
            int e2 = e * 2;
            if (e2 >= dy) {
                e += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                e += dx;
                y0 += sy;
            }
        }
    }
    void text(int x, int y, const std::string &s, u8 color = 35) {
        for (char c : s) {
            const char *p = std::find(alphabet, alphabet + 43, c);
            if (p != alphabet + 43) {
                uint32_t bits = glyphs[p - alphabet];
                for (int row = 0; row < 5; row++)
                    for (int col = 0; col < 5; col++)
                        if ((bits >> ((4 - row) * 5 + 4 - col)) & 1)
                            dot(x + col, y + row, color);
            }
            x += 6;
        }
    }
    void centered(int y, const std::string &s, u8 color = 35) {
        text((W - int(s.size()) * 6) / 2, y, s, color);
    }
    void begin(const Simulation &sim, bool instrument) {
        bottom = instrument ? 80 : 166;
        center = instrument ? 38 : 69;
        eye = sim.boat;
        eye.y = instrument ? .9f : 3.5f;
        yaw = sim.heading;
        pitch = instrument ? 0 : .28f;
        if (!instrument) {
            eye.x -= std::sin(yaw) * 5;
            eye.z += std::cos(yaw) * 5;
        }
        depth.fill(1e20f);
        box(0, 0, W, H, 32);
        box(0, 8, W, center - 8, 11);
        box(0, center, W, bottom - center, 9);
    }
    Vec3 camera(Vec3 p) const {
        float dx = p.x - eye.x, dz = p.z - eye.z, y = p.y - eye.y,
              z = dx * std::sin(yaw) - dz * std::cos(yaw);
        return {dx * std::cos(yaw) + dz * std::sin(yaw), y * std::cos(pitch) + z * std::sin(pitch),
                z * std::cos(pitch) - y * std::sin(pitch)};
    }
    Vec3 project(Vec3 p) const {
        return {160 + p.x * 160 / p.z, float(center) - p.y * 133.3333f / p.z, 1 / p.z};
    }
    void raster(Vec3 a, Vec3 b, Vec3 c, uint8_t color) {
        a = project(a);
        b = project(b);
        c = project(c);
        float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::abs(den) < 1e-6f)
            return;
        int x0 = int(std::floor(std::max(0.f, std::min({a.x, b.x, c.x})))),
            x1 = int(std::ceil(std::min(319.f, std::max({a.x, b.x, c.x}))));
        int y0 = int(std::floor(std::max(8.f, std::min({a.y, b.y, c.y})))),
            y1 = int(std::ceil(std::min(float(bottom - 1), std::max({a.y, b.y, c.y}))));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float u = ((b.y - c.y) * (x + .5f - c.x) + (c.x - b.x) * (y + .5f - c.y)) / den,
                      v = ((c.y - a.y) * (x + .5f - c.x) + (a.x - c.x) * (y + .5f - c.y)) / den,
                      w = 1 - u - v;
                if (u < 0 || v < 0 || w < 0)
                    continue;
                float z = 1 / (u * a.z + v * b.z + w * c.z);
                int i = y * W + x;
                if (z < depth[i]) {
                    depth[i] = z;
                    pixels[i] = color;
                }
            }
    }
    void triangle(const Triangle &t) {
        std::vector<Vec3> input;
        for (auto v : t.v)
            input.push_back(camera(v));
        std::vector<Vec3> polygon;
        for (size_t i = 0; i < input.size(); i++) {
            Vec3 a = input[i], b = input[(i + 1) % input.size()];
            bool ai = a.z >= .12f, bi = b.z >= .12f;
            if (ai)
                polygon.push_back(a);
            if (ai != bi) {
                float f = (.12f - a.z) / (b.z - a.z);
                polygon.push_back({a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, .12f});
            }
        }
        for (size_t i = 2; i < polygon.size(); i++)
            raster(polygon[0], polygon[i - 1], polygon[i], t.color);
    }
    void sprite(const Sprite &sprite, Vec3 at) {
        Vec3 p = camera(at);
        if (p.z < .15f || p.z > 180)
            return;
        Vec3 q = project(p);
        const float sx = 3.6f / p.z, sy = 3.f / p.z;
        float left = q.x - 120 * sx, top = q.y - 56 * sy;
        int x0 = std::max(0, int(std::floor(left))),
            x1 = std::min(W - 1, int(std::ceil(left + 256 * sx)));
        int y0 = std::max(8, int(std::floor(top))),
            y1 = std::min(bottom - 1, int(std::ceil(top + 64 * sy)));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                int u = int((x + .5f - left) / sx), v = int((y + .5f - top) / sy);
                if (u < 0 || u >= 256 || v < 0 || v >= 64)
                    continue;
                uint8_t color = sprite.pixels[v * 256 + u];
                int i = y * W + x;
                if (color && p.z <= depth[i] + .03f) {
                    pixels[i] = color;
                    depth[i] = p.z;
                }
            }
    }
    void segment(const Line &line) {
        auto a = camera(line.v[0]), b = camera(line.v[1]);
        if (a.z < .12f && b.z < .12f)
            return;
        if (a.z < .12f || b.z < .12f) {
            float f = (.12f - a.z) / (b.z - a.z);
            Vec3 c{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, .12f};
            if (a.z < .12f)
                a = c;
            else
                b = c;
        }
        a = project(a);
        b = project(b);
        float begin = 0, end = 1, dx = b.x - a.x, dy = b.y - a.y;
        auto clip = [&](float p, float q) {
            if (p == 0)
                return q >= 0;
            float r = q / p;
            if (p < 0) {
                if (r > end)
                    return false;
                begin = std::max(begin, r);
            } else {
                if (r < begin)
                    return false;
                end = std::min(end, r);
            }
            return true;
        };
        if (!clip(-dx, a.x) || !clip(dx, 319 - a.x) || !clip(-dy, a.y - 8) ||
            !clip(dy, bottom - 1 - a.y))
            return;
        int steps =
            std::max(1, int(std::ceil(std::max(std::abs(dx), std::abs(dy)) * (end - begin))));
        for (int i = 0; i <= steps; i++) {
            float f = begin + (end - begin) * i / steps;
            int x = int(std::round(a.x + dx * f)), y = int(std::round(a.y + dy * f));
            float z = 1 / (a.z + (b.z - a.z) * f);
            if (x >= 0 && x < W && y >= 8 && y < bottom && z <= depth[y * W + x] + .03f) {
                pixels[y * W + x] = line.color;
                depth[y * W + x] = z;
            }
        }
    }
};
using Cache = std::map<std::pair<int, int>, Sprite>;
Cache make_cache(const World &w) {
    Cache cache;
    std::set<int> kinds;
    for (auto o : w.objects)
        if (o.kind != 57)
            kinds.insert(o.kind);
    for (int kind : kinds)
        for (int a = 0; a < 8; a++)
            cache.emplace(std::make_pair(kind, a), w.sprites.decode(kind, a * 32));
    return cache;
}
std::string num(float n) {
    return std::to_string(int(std::round(n)));
}
std::string compass(float angle) {
    int degrees = (int(std::round(angle * 180 / pi)) + 720) % 360;
    std::string s = std::to_string(degrees);
    return std::string(3 - s.size(), '0') + s;
}
void chart(Screen &s, const World &w, const Navigation &nav, const Simulation &sim) {
    s.box(0, 8, 320, 158, 32);
    for (int y = 0; y < 154; y++)
        for (int x = 0; x < 248; x++) {
            float wx = -136 + (x + .5f) * 272 / 248, wz = -88 + (y + .5f) * 176 / 154;
            int i = nav.index(wx, wz);
            s.dot(x + 4, y + 10, i >= 0 && nav.land[i] ? 10 : 9);
        }
    auto px = [](float x) { return int(4 + (x + 136) * 248 / 272); };
    auto py = [](float z) { return int(10 + (z + 88) * 154 / 176); };
    for (auto o : w.objects)
        if (!o.scenery && o.index && o.kind < 57)
            s.dot(px(o.position.x), py(o.position.z), 7);
    int n = 0;
    for (auto t : sim.mission.targets) {
        int x = px(t.position.x), y = py(t.position.z);
        if (t.hp > 0) {
            s.box(x - 1, y - 1, 3, 3, 34);
            s.text(x + 3, y - 5, std::to_string(++n), 34);
        }
    }
    auto start = sim.mission.spawn;
    s.box(px(start.x) - 1, py(start.z) - 1, 3, 3, 33);
    s.text(px(start.x) + 3, py(start.z) + 3, "B", 33);
    int x = px(sim.boat.x), y = py(sim.boat.z);
    s.box(x - 1, y - 1, 3, 3, 35);
    s.line(x, y, x + int(std::sin(sim.heading) * 6), y - int(std::cos(sim.heading) * 6), 35);
    s.text(259, 16, "CHART", 35);
    s.text(259, 34, "WHITE", 35);
    s.text(259, 42, "BOAT", 35);
    s.text(259, 60, "RED", 34);
    s.text(259, 68, "TARGET", 34);
    s.text(259, 86, "GREEN", 33);
    s.text(259, 94, "BASE", 33);
    s.text(259, 130, "M CLOSE", 35);
}
void render(Screen &s, const World &w, const Navigation &nav, const Simulation &sim,
            const Cache &cache, const Bytes &panel, bool instruments, bool map, bool paused,
            float aim) {
    s.begin(sim, instruments);
    if (map)
        chart(s, w, nav, sim);
    else {
        for (auto t : w.triangles)
            s.triangle(t);
        for (auto line : w.lines)
            s.segment(line);
        for (size_t i = 0; i < w.objects.size(); i++) {
            auto o = w.objects[i];
            if (o.kind == 57 || (!o.scenery && o.index == 0))
                continue;
            bool destroyed = false;
            for (auto t : sim.mission.targets)
                if (t.source == int(i) && t.hp <= 0)
                    destroyed = true;
            if (destroyed)
                continue;
            int frame =
                (int(std::floor(bearing(o.position, s.eye) * 4 / pi + .5f)) + o.heading + 32) % 8;
            auto p = o.position;
            p.y = nav.elevation(p.x, p.z) + .015f;
            s.sprite(cache.at({o.kind, frame}), p);
        }
        if (!instruments) {
            for (auto o : w.objects)
                if (!o.scenery && o.index == 0) {
                    auto p = sim.boat;
                    p.y = .015f;
                    int frame =
                        (int(std::floor((bearing(p, s.eye) - sim.heading) * 4 / pi + .5f)) + 32) %
                        8;
                    s.sprite(cache.at({o.kind, frame}), p);
                    break;
                }
        }
        for (size_t i = 0; i < sim.mission.targets.size(); i++) {
            auto t = sim.mission.targets[i];
            if (t.hp <= 0)
                continue;
            auto p = t.position;
            p.y += 1.5f;
            auto c = s.camera(p);
            if (c.z <= .5f || c.z > 100)
                continue;
            auto q = s.project(c);
            int x = int(q.x), y = int(q.y);
            if (x < 9 || x > 311 || y < 10 || y > s.bottom - 10)
                continue;
            if (!nav.clear(s.eye, t.position))
                continue;
            s.text(x - 3, y, std::to_string(i + 1), 34);
            s.line(x - 4, y + 7, x + 4, y + 7, 34);
        }
        if (instruments) {
            for (int y = 8; y < H; y++)
                for (int x = 0; x < W; x++) {
                    auto c = panel[y * W + x];
                    if (y < 80 && (c == 1 || c == 11 || c == 16))
                        continue;
                    s.dot(x, y, c);
                }
            s.box(113, 133, 99, 47, 32);
            s.text(121, 139, "PBR STATUS", 33);
            s.text(121, 151, "HULL " + num(sim.hull), 35);
            s.text(121, 163, "KNOTS " + num(sim.speed * 6), 35);
            s.box(6, 88, 78, 24, 32);
            s.text(10, 92, "THROTTLE", 33);
            s.text(10, 102, num(sim.throttle * 100) + " PERCENT", 35);
        }
        Vec3 aimPoint{sim.boat.x + std::sin(sim.heading + aim) * 20, .65f,
                      sim.boat.z - std::cos(sim.heading + aim) * 20};
        auto aimScreen = s.project(s.camera(aimPoint));
        int cross = std::clamp(int(aimScreen.x), 10, 309),
            cy = std::clamp(int(aimScreen.y), 12, s.bottom - 8);
        s.line(cross - 6, cy, cross - 2, cy, 35);
        s.line(cross + 2, cy, cross + 6, cy, 35);
        s.line(cross, cy - 4, cross, cy - 2, 35);
        s.line(cross, cy + 2, cross, cy + 4, 35);
        if (sim.flash > 0) {
            s.line(instruments ? 125 : 90, instruments ? 72 : 160, cross - 3, cy + 2, 36);
            s.line(instruments ? 193 : 230, instruments ? 72 : 160, cross + 3, cy + 2, 36);
        }
    }
    s.box(0, 0, W, 8, 32);
    s.text(5, 1, "GUNBOAT / NATIVE PATROL", 33);
    s.text(258, 1, "WORLD " + std::to_string(w.number), 35);
    std::string course =
        "  BRG " + compass(bearing(sim.boat, sim.objective())) + " HDG " + compass(sim.heading);
    if (!instruments || map) {
        s.box(0, 167, W, 19, 32);
        s.text(5, 171,
               "HULL " + num(sim.hull) + "   KNOTS " + num(sim.speed * 6) + "   HEAT " +
                   num(sim.heat),
               35);
        s.text(5, 180,
               (sim.remaining() ? "CONTACTS " + std::to_string(sim.remaining()) + " RANGE " +
                                      num(distance(sim.boat, sim.objective()))
                                : "RETURN TO BASE") +
                   course,
               sim.remaining() ? 34 : 33);
    } else {
        s.box(0, 187, 320, 6, 32);
        s.text(4, 187,
               (sim.remaining() ? "CONTACTS " + std::to_string(sim.remaining()) + " RANGE " +
                                      num(distance(sim.boat, sim.objective()))
                                : "RETURN TO BASE") +
                   course,
               35);
    }
    s.box(0, 194, 320, 6, 32);
    s.text(4, 194, "M MAP  F5 VIEW  F1-F4 WORLD  ESC PAUSE", 35);
    if (sim.overheated)
        s.centered(instruments ? 83 : 155, "GUNS COOLING", 36);
    if (sim.damageFlash > 0) {
        s.box(0, 8, 3, 158, 34);
        s.box(317, 8, 3, 158, 34);
    }
    if (paused || sim.status != "playing") {
        s.box(21, 23, 278, 151, 32);
        s.box(21, 23, 278, 2, 33);
        s.centered(33,
                   sim.status == "won"    ? "PATROL COMPLETE"
                   : sim.status == "lost" ? "BOAT LOST"
                                          : "GUNBOAT / NATIVE SDL3",
                   33);
        s.centered(49, "ORIGINAL MAPS / SPRITES / COCKPIT");
        s.centered(59, "PROVISIONAL PATROL RULES", 36);
        s.text(35, 75, "W/S OR UP/DOWN    THROTTLE");
        s.text(35, 87, "A/D OR LEFT/RIGHT STEER");
        s.text(35, 99, "SPACE FIRE   Q/E AIM   X STOP");
        s.text(35, 111, "M CHART   F5 COCKPIT / CHASE VIEW");
        s.text(35, 123, "F1-F4 WORLD   R RESTART   F10 QUIT");
        s.centered(141, sim.status == "playing" ? "ENTER TO START / RESUME" : "R TO RESTART");
        s.centered(157,
                   sim.practice ? "PRACTICE / HULL DAMAGE OFF"
                                : "DESTROY 3 CONTACTS / RETURN TO BASE",
                   sim.practice ? 36 : 33);
    }
}
void state_file(const std::string &file, const Simulation &s, int world, bool map, bool paused,
                bool instruments) {
    if (file.empty())
        return;
    std::ofstream out(file);
    if (!out)
        throw std::runtime_error("Cannot write state file");
    out << std::setprecision(9) << "{\n  \"world\": " << world << ", \"status\": \"" << s.status
        << "\", \"time\": " << s.time << ",\n  \"x\": " << s.boat.x << ", \"z\": " << s.boat.z
        << ", \"heading\": " << s.heading << ", \"speed\": " << s.speed
        << ",\n  \"hull\": " << s.hull << ", \"shots\": " << s.shots << ", \"hits\": " << s.hits
        << ", \"remaining\": " << s.remaining() << ", \"collisions\": " << s.collisions
        << ",\n  \"paused\": " << (paused ? "true" : "false")
        << ", \"map\": " << (map ? "true" : "false")
        << ", \"cockpit\": " << (instruments ? "true" : "false") << "\n}\n";
}
void screenshot(const std::string &file) {
    if (file.empty())
        return;
    std::array<u32, 256> palette{};
    for (int i = 0; i < 256; i++) {
        u8 r, g, b;
        vga_dac_read(u8(i), &r, &g, &b);
        palette[i] = u32(r << 2 | r >> 4) << 16 | u32(g << 2 | g >> 4) << 8 | u32(b << 2 | b >> 4);
    }
    std::array<u32, W * H> pixels{};
    for (int i = 0; i < W * H; i++)
        pixels[i] = palette[mem[0xa0000 + i]];
    auto surface = SDL_CreateSurfaceFrom(W, H, SDL_PIXELFORMAT_XRGB8888, pixels.data(), W * 4);
    if (!surface)
        throw std::runtime_error(SDL_GetError());
    bool saved = SDL_SaveBMP(surface, file.c_str());
    SDL_DestroySurface(surface);
    if (!saved)
        throw std::runtime_error("Cannot save screenshot: " + file);
}
} // namespace
int play(const Archive &archive, const Options &options) {
    auto panel = cockpit(archive);
    std::unique_ptr<World> world;
    std::unique_ptr<Navigation> nav;
    std::unique_ptr<Simulation> sim;
    Cache cache;
    int number = options.world;
    auto load = [&]() {
        sim.reset();
        nav.reset();
        world = std::make_unique<World>(archive, number);
        nav = std::make_unique<Navigation>(*world);
        sim = std::make_unique<Simulation>(*nav, make_mission(*world, *nav), options.practice);
        cache = make_cache(*world);
    };
    load();
    if (!host_init(options.directory.string().c_str(), options.scale, options.fullscreen))
        return 2;
    struct Cleanup {
        ~Cleanup() {
            host_shutdown();
        }
    } cleanup;
    vga_init();
    for (int i = 0; i < 32; i++) {
        auto p = world->palette[i];
        vga_dac_write(u8(i), p[0], p[1], p[2]);
    }
    vga_dac_write(32, 3, 6, 7);
    vga_dac_write(33, 30, 54, 37);
    vga_dac_write(34, 63, 22, 16);
    vga_dac_write(35, 55, 57, 47);
    vga_dac_write(36, 63, 50, 20);
    host_set_kbd_handler(keyboard);
    host_set_focus_lost_handler(release_keys);
    Screen screen;
    bool paused = true, map = false, instruments = true, quit = false;
    float aim = 0;
    double accumulator = 0, start = SDL_GetTicksNS() / 1e9, last = start;
    render(screen, *world, *nav, *sim, cache, panel, instruments, map, paused, aim);
    while (!quit) {
        host_pump();
        double now = SDL_GetTicksNS() / 1e9, dt = std::min(.1, now - last);
        last = now;
        if (take(0x1c) && sim->status == "playing")
            paused = false;
        if (take(0x01))
            paused = !paused;
        if (take(0x32))
            map = !map;
        if (take(0x3f))
            instruments = !instruments;
        if (take(0x44))
            quit = true;
        bool reload = take(0x13);
        for (int k = 0; k < 4; k++)
            if (take(0x3b + k)) {
                number = k + 1;
                reload = true;
            }
        if (reload) {
            load();
            release_keys();
            paused = true;
            map = false;
            aim = 0;
            accumulator = 0;
            last = SDL_GetTicksNS() / 1e9;
        }
        if (!paused) {
            aim = std::clamp(aim + float(dt) * ((held[0x12] ? 1 : 0) - (held[0x10] ? 1 : 0)), -.7f,
                             .7f);
            if (held[0x2e])
                aim = 0;
            Input input;
            input.throttle = float((held[0x11] || held[0x48]) - (held[0x1f] || held[0x50]));
            input.steer = float((held[0x20] || held[0x4d]) - (held[0x1e] || held[0x4b]));
            input.stop = held[0x2d];
            input.fire = held[0x39];
            input.aim = aim;
            s16 gx = 0, gy = 0;
            u8 buttons = 0;
            if (host_joy_read(&gx, &gy, &buttons)) {
                if (std::abs(gx) > 5000)
                    input.steer = gx / 32768.f;
                if (std::abs(gy) > 5000)
                    input.throttle = -gy / 32768.f;
                input.fire = input.fire || (buttons & 1);
                input.stop = input.stop || (buttons & 2);
            }
            accumulator += dt;
            while (accumulator >= 1. / 60) {
                sim->step(1.f / 60, input);
                accumulator -= 1. / 60;
            }
        } else
            accumulator = 0;
        host_speaker(sim->flash > 0 ? uint16_t(500 + int(sim->time * 1000) % 900) : 0,
                     !paused && sim->flash > 0);
        render(screen, *world, *nav, *sim, cache, panel, instruments, map, paused, aim);
        if (options.seconds > 0 && now - start >= options.seconds)
            quit = true;
        SDL_Delay(1);
    }
    state_file(options.stateFile, *sim, number, map, paused, instruments);
    screenshot(options.screenshot);
    return 0;
}
void dump_assets(const Archive &archive, const std::filesystem::path &dir) {
    std::filesystem::create_directories(dir);
    write_file(dir / "unpacked.bin", archive.exe);
    write_file(dir / "cockpit.raw", cockpit(archive));
    for (const std::string name : {"BF1.LZ", "BF2.LZ", "BG1.LZ", "BG2.LZ", "MAP.LZ", "CLIP.LZ"})
        write_file(dir / (name + ".decoded"), decode_lzw(archive.get(name)));
    for (int n = 1; n <= 4; n++) {
        World world(archive, n);
        auto cache = make_cache(world);
        std::string prefix = "world" + std::to_string(n);
        std::filesystem::create_directories(dir / prefix);
        for (const auto &[key, sprite] : cache)
            write_file(
                dir / prefix /
                    (std::to_string(key.first) + "_" + std::to_string(key.second * 32) + ".raw"),
                Bytes(sprite.pixels.begin(), sprite.pixels.end()));
        std::ofstream objects(dir / (prefix + "-objects.csv"));
        objects << "kind,heading,index,x,y,scenery,cell\n";
        for (auto o : world.objects)
            objects << o.kind << ',' << o.heading << ',' << o.index << ',' << o.x << ',' << o.y
                    << ',' << o.scenery << ',' << o.cell << '\n';
        std::ofstream faces(dir / (prefix + "-triangles.csv"));
        faces << std::setprecision(9);
        for (auto t : world.triangles) {
            faces << int(t.color);
            for (auto v : t.v)
                faces << ',' << v.x << ',' << v.y << ',' << v.z;
            faces << '\n';
        }
        std::ofstream lines(dir / (prefix + "-lines.csv"));
        lines << std::setprecision(9);
        for (auto line : world.lines) {
            lines << int(line.color);
            for (auto v : line.v)
                lines << ',' << v.x << ',' << v.y << ',' << v.z;
            lines << '\n';
        }
    }
    std::cout << "Native asset fixtures written to " << dir.string() << '\n';
}
void self_test(const Archive &archive) {
    int checks = 0;
    auto require = [&](bool ok, const std::string &text) {
        ++checks;
        if (!ok)
            throw std::runtime_error("Self-test: " + text);
    };
    for (int n = 1; n <= 4; n++) {
        World w(archive, n);
        Navigation nav(w);
        auto m = make_mission(w, nav);
        Simulation s(nav, m, true);
        Flood flood(nav, m.spawn, m.clearance);
        require(s.occupy(s.boat, s.heading), "launch must fit hull");
        require(m.targets.size() == 3, "three contacts");
        require(!nav.water(-137, 0), "outside map blocked");
        size_t samples = 0;
        for (size_t ti = 0; ti < m.targets.size(); ti++) {
            auto target = m.targets[ti];
            auto route = flood.route(target.approach);
            require(!route.empty(), "reachable target approach");
            for (size_t k = 1; k < route.size(); k++)
                for (int sub = 0; sub <= 4; sub++) {
                    auto a = route[k - 1], b = route[k];
                    float f = sub / 4.f;
                    require(nav.water(a.x + (b.x - a.x) * f, a.z + (b.z - a.z) * f,
                                      m.clearance < .3f ? .12f : .25f),
                            "continuous route clearance");
                    ++samples;
                }
            s.boat = target.approach;
            s.boat.y = .65f;
            s.speed = 0;
            s.throttle = 0;
            for (int frame = 0; frame < 900 && s.mission.targets[ti].hp > 0; frame++) {
                Input in;
                in.stop = true;
                in.fire = true;
                in.aim = bearing(s.boat, target.position) - s.heading;
                s.step(1.f / 60, in);
            }
        }
        require(s.remaining() == 0, "destroy every contact with native combat");
        require(s.hits >= 24, "shots resolve hits");
        require(s.hull == 100, "practice prevents damage");
        s.boat = m.spawn;
        s.speed = 0;
        s.step(1.f / 60, {});
        require(s.status == "won", "return and stop completes patrol");
        Simulation lost(nav, m);
        lost.damage(120);
        require(lost.status == "lost" && lost.hull == 0, "damage failure");
        int shots = lost.shots;
        lost.fire(0);
        require(lost.shots == shots, "cannot fire after loss");
        Simulation moving(nav, m, true);
        auto original = moving.boat;
        for (int f = 0; f < 120; f++)
            moving.step(1.f / 60, Input{1, 0, 0, false, false});
        require(distance(original, moving.boat) > .1f, "throttle moves boat");
        for (int f = 0; f < 2000; f++)
            moving.step(1.f / 60, Input{1, 0, 0, false, false});
        require(moving.occupy(moving.boat, moving.heading), "collision retains safe position");
        require(moving.collisions > 0, "shore or boundary collision stops boat");
        require(cockpit(archive).size() == 64000, "native cockpit decode");
        std::cout << "World " << n << ": " << flood.reachable.size() << " connected cells, "
                  << samples
                  << " route samples, patrol win/loss/combat/navigation OK; launch adjustment "
                  << distance(m.originalSpawn, m.spawn) << '\n';
    }
    std::cout << checks << " native checks passed\n";
}
} // namespace gb
