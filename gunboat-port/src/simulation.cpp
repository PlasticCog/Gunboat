#include "simulation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace gb {
float distance(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}
float bearing(Vec3 a, Vec3 b) {
    return std::atan2(b.x - a.x, a.z - b.z);
}
float wrap(float a) {
    while (a > pi)
        a -= 2 * pi;
    while (a < -pi)
        a += 2 * pi;
    return a;
}
Navigation::Navigation(const World &world) : land(width * height), heights(width * height) {
    for (const auto &t : world.triangles) {
        if (t.color == 9)
            continue;
        auto a = t.v[0], b = t.v[1], c = t.v[2];
        float den = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::abs(den) < 1e-8f)
            continue;
        int x0 = std::max(0, int(std::floor((std::min({a.x, b.x, c.x}) + 136) * 4))),
            x1 = std::min(width - 1, int(std::floor((std::max({a.x, b.x, c.x}) + 136) * 4)));
        int z0 = std::max(0, int(std::floor((std::min({a.z, b.z, c.z}) + 88) * 4))),
            z1 = std::min(height - 1, int(std::floor((std::max({a.z, b.z, c.z}) + 88) * 4)));
        bool bridge =
            (t.color == 4 || t.color == 7 || t.color == 8) && std::min({a.y, b.y, c.y}) > .6f;
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) {
                float px = -136 + (x + .5f) / 4, pz = -88 + (z + .5f) / 4;
                float u = ((b.z - c.z) * (px - c.x) + (c.x - b.x) * (pz - c.z)) / den,
                      v = ((c.z - a.z) * (px - c.x) + (a.x - c.x) * (pz - c.z)) / den,
                      w = 1 - u - v;
                if (u >= -1e-7f && v >= -1e-7f && w >= -1e-7f) {
                    int i = z * width + x;
                    if (!bridge)
                        land[i] = 1;
                    heights[i] = std::max(heights[i], u * a.y + v * b.y + w * c.y);
                }
            }
    }
}
int Navigation::index(float x, float z) const {
    int ix = int(std::floor((x + 136) * 4)), iz = int(std::floor((z + 88) * 4));
    return ix < 0 || iz < 0 || ix >= width || iz >= height ? -1 : iz * width + ix;
}
float Navigation::elevation(float x, float z) const {
    int i = index(x, z);
    return i < 0 ? 0 : heights[i];
}
bool Navigation::water(float x, float z, float r) const {
    auto sample = [&](float dx, float dz) {
        int i = index(x + dx, z + dz);
        return i >= 0 && !land[i];
    };
    if (!sample(0, 0))
        return false;
    for (float dz = -r; dz <= r; dz += .125f)
        for (float dx = -r; dx <= r; dx += .125f)
            if (dx * dx + dz * dz <= r * r && !sample(dx, dz))
                return false;
    if (r > 0)
        for (int a = 0; a < 24; a++)
            if (!sample(std::sin(a * pi / 12) * r, std::cos(a * pi / 12) * r))
                return false;
    return true;
}
bool Navigation::clear(Vec3 a, Vec3 b) const {
    int steps = int(std::ceil(distance(a, b) / .2f));
    for (int i = 1; i < steps; i++) {
        float f = float(i) / steps;
        if (elevation(a.x + (b.x - a.x) * f, a.z + (b.z - a.z) * f) > a.y + (b.y - a.y) * f + .12f)
            return false;
    }
    return true;
}
Vec3 Flood::point(int i) {
    return {-136 + (i % width + .5f) * .5f, 0, -88 + (i / width + .5f) * .5f};
}
int Flood::index(Vec3 p) {
    int x = int(std::floor((p.x + 136) * 2)), z = int(std::floor((p.z + 88) * 2));
    return x < 0 || z < 0 || x >= width || z >= height ? -1 : z * width + x;
}
Flood::Flood(const Navigation &nav, Vec3 start, float radius)
    : parent(width * height, -1), cost(width * height, std::numeric_limits<float>::infinity()) {
    int root = index(start);
    if (root < 0)
        return;
    auto p = point(root);
    if (!nav.water(p.x, p.z, radius))
        return;
    parent[root] = root;
    cost[root] = 0;
    reachable.push_back(root);
    for (size_t k = 0; k < reachable.size(); k++) {
        int i = reachable[k], x = i % width, z = i / width;
        for (auto [dx, dz] : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            if (x + dx < 0 || z + dz < 0 || x + dx >= width || z + dz >= height)
                continue;
            int j = i + dx + dz * width;
            if (parent[j] >= 0)
                continue;
            p = point(j);
            if (!nav.water(p.x, p.z, radius))
                continue;
            parent[j] = i;
            cost[j] = cost[i] + .5f;
            reachable.push_back(j);
        }
    }
}
std::vector<Vec3> Flood::route(Vec3 p) const {
    std::vector<Vec3> result;
    int i = index(p);
    if (i < 0 || parent[i] < 0)
        return result;
    while (parent[i] != i) {
        result.push_back(point(i));
        i = parent[i];
    }
    result.push_back(point(i));
    std::reverse(result.begin(), result.end());
    return result;
}
Mission make_mission(const World &world, const Navigation &nav) {
    Mission m;
    bool found = false;
    for (auto o : world.objects)
        if (!o.scenery && o.index == 0) {
            m.originalSpawn = o.position;
            found = true;
            break;
        }
    if (!found)
        throw std::runtime_error("Map has no player start");
    auto nearest = [&](Vec3 p, float radius) {
        for (float r = 0; r <= 12; r += .25f)
            for (int a = 0; a < 48; a++) {
                Vec3 q{p.x + std::sin(a * pi / 24) * r, 0, p.z + std::cos(a * pi / 24) * r};
                int i = Flood::index(q);
                if (i < 0)
                    continue;
                q = Flood::point(i);
                if (nav.water(q.x, q.z, radius))
                    return q;
            }
        throw std::runtime_error("No navigable launch near source start");
    };
    m.spawn = nearest(m.originalSpawn, .7f);
    Flood flood(nav, m.spawn, m.clearance);
    if (flood.reachable.size() < 500) {
        m.clearance = .16f;
        flood = Flood(nav, m.spawn, m.clearance);
    }
    if (flood.reachable.size() < 500) {
        std::vector<uint8_t> checked(Flood::width * Flood::height);
        for (int i : flood.reachable)
            checked[i] = 1;
        bool good = false;
        for (int r = 2; r <= 32 && !good; r += 2)
            for (int a = 0; a < 32; a++) {
                Vec3 p{m.originalSpawn.x + std::sin(a * pi / 16) * r, 0,
                       m.originalSpawn.z + std::cos(a * pi / 16) * r};
                int i = Flood::index(p);
                if (i < 0 || checked[i])
                    continue;
                p = Flood::point(i);
                if (!nav.water(p.x, p.z, .9f))
                    continue;
                Flood next(nav, p, m.clearance);
                for (int j : next.reachable)
                    checked[j] = 1;
                if (next.reachable.size() >= 500) {
                    m.spawn = p;
                    flood = std::move(next);
                    good = true;
                    break;
                }
            }
    }
    if (flood.reachable.empty())
        throw std::runtime_error("No connected launch water");
    m.spawn = Flood::point(flood.reachable.front());
    std::vector<Target> candidates;
    for (size_t k = 0; k < world.objects.size(); k++) {
        auto o = world.objects[k];
        if (o.scenery || o.index == 0 || o.kind >= 57 || distance(o.position, m.spawn) < 8)
            continue;
        Target target;
        target.source = int(k);
        target.position = o.position;
        target.position.y = nav.elevation(o.position.x, o.position.z) + .55f;
        float best = 1e20f;
        for (float dz = -3; dz <= 3; dz += .5f)
            for (float dx = -3; dx <= 3; dx += .5f) {
                Vec3 p{o.position.x + dx, 0, o.position.z + dz};
                int i = Flood::index(p);
                if (i < 0)
                    continue;
                float cost = flood.cost[i];
                if (cost < 10 || cost > 95)
                    continue;
                p = Flood::point(i);
                p.y = .65f;
                float d = distance(p, o.position);
                if (d < 1 || d > 3 || !nav.clear(p, target.position))
                    continue;
                float score = d + cost * .03f;
                if (score < best) {
                    best = score;
                    target.approach = p;
                    target.walk = cost;
                }
            }
        if (best < 1e19f)
            candidates.push_back(target);
    }
    std::sort(candidates.begin(), candidates.end(), [](const Target &a, const Target &b) {
        return a.walk == b.walk ? a.source < b.source : a.walk < b.walk;
    });
    for (auto t : candidates) {
        bool separated = true;
        for (auto other : m.targets)
            if (distance(t.position, other.position) < 8)
                separated = false;
        if (separated)
            m.targets.push_back(t);
        if (m.targets.size() == 3)
            break;
    }
    if (m.targets.size() != 3)
        throw std::runtime_error("Could not find three reachable patrol contacts");
    auto route = flood.route(m.targets[0].approach);
    m.heading = bearing(m.spawn, m.targets[0].approach);
    for (auto p : route)
        if (distance(p, m.spawn) > 3) {
            m.heading = bearing(m.spawn, p);
            break;
        }
    return m;
}
Simulation::Simulation(const Navigation &n, const Mission &m, bool p)
    : nav(n), mission(m), boat(m.spawn), heading(m.heading), practice(p) {
    boat.y = .65f;
}
int Simulation::remaining() const {
    int n = 0;
    for (auto t : mission.targets)
        if (t.hp > 0)
            ++n;
    return n;
}
Vec3 Simulation::objective() const {
    for (auto t : mission.targets)
        if (t.hp > 0)
            return t.position;
    return mission.spawn;
}
bool Simulation::occupy(Vec3 p, float h) const {
    if (mission.clearance < .3f)
        return nav.water(p.x, p.z, .12f);
    for (float offset : {-.4f, 0.f, .4f})
        if (!nav.water(p.x + std::sin(h) * offset, p.z - std::cos(h) * offset, .25f))
            return false;
    return true;
}
void Simulation::damage(float amount) {
    if (practice || status != "playing")
        return;
    hull = std::max(0.f, hull - amount);
    damageFlash = .25f;
    if (!hull)
        status = "lost";
}
bool Simulation::fire(float angle) {
    if (status != "playing" || reload > 0 || overheated)
        return false;
    ++shots;
    reload = .12f;
    flash = .06f;
    heat = std::min(100.f, heat + 8);
    if (heat >= 100)
        overheated = true;
    int hit = -1;
    float closest = 27;
    for (size_t i = 0; i < mission.targets.size(); i++) {
        auto t = mission.targets[i];
        if (t.hp <= 0)
            continue;
        float dx = t.position.x - boat.x, dz = t.position.z - boat.z;
        float along = dx * std::sin(angle) - dz * std::cos(angle),
              across = std::abs(dx * std::cos(angle) + dz * std::sin(angle));
        if (along > 0 && along < closest && across < .7f + along * .012f &&
            nav.clear(boat, t.position)) {
            hit = int(i);
            closest = along;
        }
    }
    if (hit >= 0) {
        ++hits;
        auto &t = mission.targets[hit];
        t.hp = std::max(0.f, t.hp - 14);
    }
    return true;
}
void Simulation::step(float dt, const Input &in) {
    if (status != "playing")
        return;
    dt = std::clamp(dt, 0.f, 1.f / 30);
    time += dt;
    reload = std::max(0.f, reload - dt);
    flash = std::max(0.f, flash - dt);
    damageFlash = std::max(0.f, damageFlash - dt);
    heat = std::max(0.f, heat - dt * 22);
    if (heat < 32)
        overheated = false;
    throttle = in.stop ? 0 : std::clamp(throttle + in.throttle * dt * .65f, -.35f, 1.f);
    float h =
        wrap(heading + in.steer * dt * (.38f + std::abs(speed) * .15f) * (speed < -.1f ? -1 : 1));
    if (occupy(boat, h))
        heading = h;
    speed += (throttle * 5.2f - speed) * std::min(1.f, dt * (in.stop ? 3 : 1.1f));
    Vec3 p{boat.x + std::sin(heading) * speed * dt, boat.y,
           boat.z - std::cos(heading) * speed * dt};
    if (occupy(p, heading))
        boat = p;
    else {
        if (std::abs(speed) > 2)
            damage(std::abs(speed) * 1.5f);
        if (std::abs(speed) > .05f)
            ++collisions;
        speed = 0;
        throttle = 0;
    }
    if (in.fire)
        fire(heading + in.aim);
    for (size_t i = 0; i < mission.targets.size(); i++) {
        auto &t = mission.targets[i];
        if (t.hp <= 0)
            continue;
        t.cooldown -= dt;
        if (t.cooldown <= 0 && distance(boat, t.position) < 19 && nav.clear(t.position, boat)) {
            damage(5);
            t.cooldown = 1.8f + float(i) * .25f;
        }
    }
    if (status == "playing" && !remaining() && distance(boat, mission.spawn) < 2 &&
        std::abs(speed) < .6f)
        status = "won";
}
} // namespace gb
