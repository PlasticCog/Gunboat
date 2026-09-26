#pragma once
#include "assets.hpp"
#include <array>
#include <optional>

namespace gb::original {
struct Projectile {
    uint8_t ticks = 0, kind = 0;
    uint16_t x = 0, y = 0;
    uint8_t headingFraction = 0, heading = 0, range = 0;
};
struct WeaponState {
    // Mount order: station 2 (bow), station 3, station 4.
    std::array<uint8_t, 3> loadout{}, aim{192,192,192}, heading{}, headingFraction{};
    std::array<std::array<uint8_t, 2>, 3> condition{};
    // Original DS:B830 and DS:B831. Ready values are 8 and 48.
    uint8_t reload4 = 8, reload3 = 48, alternatingBarrel = 0;
    // Original B839..B83C: middle, bow left, bow right, station 4.
    std::array<uint8_t, 4> flash{};
    uint8_t timingMode = 0, frameCounter = 0;
    uint8_t cameraPitch = 127, referencePitch = 128;
    uint16_t boatX = 0, boatY = 0;
    std::array<Projectile, 32> projectiles{};
};
struct FiredShot {
    uint8_t weapon = 0, sound = 0, slot = 0;
    Projectile projectile;
};
struct ObjectHit {
    uint8_t kind = 0, flags = 0, sound = 0;
    bool destroyed = false;
};
class Weapons {
    std::array<uint16_t, 129> sine{};
    std::array<uint8_t, 6> aimLimit{};
    std::array<uint8_t, 32> damageMatrix{};
    FiredShot launch(WeaponState &, unsigned mount, uint8_t weapon, uint8_t sound) const;
  public:
    explicit Weapons(const Bytes &unpackedExe);
    // One invocation of the original station firing routine. Caller supplies
    // the original control/AI cadence; no invented cooldown or heat system.
    std::optional<FiredShot> fire(WeaponState &, int originalStation) const;
    static void reload_tick(WeaponState &);
    static void flash_tick(WeaponState &);
    // Returns pending impacts in original descending slot order. Target
    // damage/explosion creation are separate, not yet translated here.
    static std::vector<Projectile> projectile_tick(WeaponState &, bool frozen = false);
    // CB8C..CC5B only: after original hit testing has selected an object.
    // Classification is world B data at 0x73 + 2*kind. World destruction,
    // secondary explosions, score and radio messages remain caller-owned.
    ObjectHit hit(uint8_t kind, uint8_t flags, uint8_t classification, uint8_t weapon) const;
};
} // namespace gb::original
