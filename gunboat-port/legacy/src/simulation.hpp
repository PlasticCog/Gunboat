#pragma once
#include "assets.hpp"
#include <string>
namespace gb {
constexpr float pi = 3.14159265358979323846f;
float distance(Vec3 a, Vec3 b);
float bearing(Vec3 a, Vec3 b);
float wrap(float angle);
struct Navigation {
    static constexpr int width = 1088, height = 704;
    std::vector<uint8_t> land;
    std::vector<float> heights;
    explicit Navigation(const World &);
    int index(float x, float z) const;
    float elevation(float x, float z) const;
    bool water(float x, float z, float radius = 0) const;
    bool clear(Vec3 a, Vec3 b) const;
};
struct Flood {
    static constexpr int width = 544, height = 352;
    std::vector<int> parent, reachable;
    std::vector<float> cost;
    Flood(const Navigation &, Vec3, float);
    static Vec3 point(int);
    static int index(Vec3);
    std::vector<Vec3> route(Vec3) const;
};
struct Target {
    int source;
    Vec3 position, approach;
    float hp = 100, cooldown = 2, walk = 0;
};
struct Mission {
    Vec3 originalSpawn, spawn;
    float heading = 0, clearance = .7f;
    std::vector<Target> targets;
};
Mission make_mission(const World &, const Navigation &);
struct Input {
    float throttle = 0, steer = 0, aim = 0;
    bool stop = false, fire = false;
};
struct Simulation {
    const Navigation &nav;
    Mission mission;
    Vec3 boat;
    float heading, speed = 0, throttle = 0, hull = 100, heat = 0, reload = 0, time = 0, flash = 0,
                   damageFlash = 0;
    bool practice, overheated = false;
    int shots = 0, hits = 0, collisions = 0;
    std::string status = "playing";
    explicit Simulation(const Navigation &, const Mission &, bool practice = false);
    int remaining() const;
    Vec3 objective() const;
    bool occupy(Vec3, float) const;
    void damage(float);
    bool fire(float angle);
    void step(float dt, const Input &);
};
} // namespace gb
