#include "original_weapons.hpp"
#include <algorithm>
#include <stdexcept>

namespace gb::original {
namespace {
int16_t sar3(uint16_t value) {
    int32_t signedValue = int16_t(value);
    return int16_t(signedValue >= 0 ? signedValue / 8 : -((-signedValue + 7) / 8));
}
}
Weapons::Weapons(const Bytes &exe) {
    if (exe.size() < 0x1b730 + 0xd644)
        throw std::runtime_error("Original executable is too short for weapon tables");
    for (unsigned i = 0; i < sine.size(); ++i) sine[i] = word(exe, 0xc692 + i * 2);
    for (unsigned i = 0; i < aimLimit.size(); ++i) aimLimit[i] = exe[0x1b730 + 0xd63e + i];
    for (unsigned i = 0; i < damageMatrix.size(); ++i) damageMatrix[i] = exe[0xc91f + i];
}
std::optional<FiredShot> Weapons::fire(WeaponState &s, int station) const {
    if (station < 2 || station > 4) return std::nullopt;
    unsigned mount = unsigned(station - 2);
    if ((s.condition[mount][0] | s.condition[mount][1]) & 1) return std::nullopt;
    uint8_t sound, weapon;
    if (station == 4) {
        // 1919:0cf4 (image 0x9e84).
        if (!s.loadout[2]) {
            if (s.reload4 != 8) return std::nullopt;
            --s.reload4;
            sound = 8; weapon = 2; s.flash[3] = 2;
        } else { sound = 2; weapon = 1; s.flash[3] = 1; }
    } else if (station == 3) {
        // 1919:0d45 (image 0x9ed5).
        if (s.loadout[1] > 1) { sound = 2; weapon = 1; s.flash[0] = 1; }
        else if (!s.loadout[1]) {
            if (s.reload3 != 48) return std::nullopt;
            --s.reload3;
            sound = 8; weapon = 3; s.flash[0] = 2;
        } else {
            if (!s.timingMode && !(s.frameCounter & 1)) return std::nullopt;
            sound = 1; weapon = 4; s.flash[0] = 2;
        }
    } else {
        // 1919:0dbd (image 0x9f4d).
        if (s.loadout[0]) { sound = 3; weapon = 5; s.flash[1] = 2; }
        else {
            ++s.alternatingBarrel;
            s.flash[s.alternatingBarrel & 1 ? 2 : 1] = 2;
            sound = 1; weapon = 4;
        }
    }
    return launch(s, mount, weapon, sound);
}
FiredShot Weapons::launch(WeaponState &s, unsigned mount, uint8_t weapon, uint8_t sound) const {
    // A072 calls C93F (free slot), CCCB (signed pitch correction), then C957
    // (flight delay, sub-angle interpolation and integer world coordinates).
    unsigned slot = 0;
    for (int i = 31; i >= 0; --i) if (!s.projectiles[unsigned(i)].ticks) { slot = unsigned(i); break; }
    int correction = int8_t(uint8_t(s.cameraPitch - s.referencePitch));
    uint8_t angle = uint8_t(std::clamp(int(s.aim[mount]) + correction, 0, 255));
    angle = std::min(angle, aimLimit[weapon]);
    Projectile p;
    p.kind = weapon;
    p.heading = s.heading[mount];
    p.headingFraction = s.headingFraction[mount];
    p.ticks = uint8_t(std::max(0, int(angle >> 3) - 14) + 1);
    p.range = uint8_t(std::max(0, int(uint8_t(-angle)) - 19));
    uint16_t scale = p.range <= 1 ? 0x7fff : uint16_t(0xffff / p.range);
    unsigned index = (p.heading & 63) * 2;
    uint8_t fraction = uint8_t(p.headingFraction << 6);
    uint16_t sx = sine[index], cx = sine[128 - index];
    uint16_t ds = uint16_t(sine[index + 1] - sx), dc = uint16_t(cx - sine[127 - index]);
    ds >>= 1; dc >>= 1;
    if (fraction & 128) { sx = uint16_t(sx + ds); cx = uint16_t(cx - dc); }
    ds >>= 1; dc >>= 1;
    if (fraction & 64) { sx = uint16_t(sx + ds); cx = uint16_t(cx - dc); }
    uint16_t x = uint16_t((uint32_t(scale) * sx) >> 16);
    uint16_t y = uint16_t((uint32_t(scale) * cx) >> 16);
    switch (p.heading >> 6) {
    case 1: std::swap(x,y); y = uint16_t(-y); break;
    case 2: x = uint16_t(-x); y = uint16_t(-y); break;
    case 3: std::swap(x,y); x = uint16_t(-x); break;
    default: break;
    }
    p.x = uint16_t(s.boatX + sar3(x));
    p.y = uint16_t(s.boatY + sar3(y));
    s.projectiles[slot] = p;
    return {weapon, sound, uint8_t(slot), p};
}
void Weapons::reload_tick(WeaponState &s) {
    // Original AE89: count down, then reload the ready sentinel.
    if (s.reload3 != 48 && !--s.reload3) s.reload3 = 48;
    if (s.reload4 != 8 && !--s.reload4) s.reload4 = 8;
}
void Weapons::flash_tick(WeaponState &s) {
    for (auto &flash : s.flash) if (flash) --flash;
}
std::vector<Projectile> Weapons::projectile_tick(WeaponState &s, bool frozen) {
    std::vector<Projectile> impacts;
    if (!frozen) for (int i = 31; i >= 0; --i) {
        auto &p = s.projectiles[unsigned(i)];
        if (p.ticks && !--p.ticks) impacts.push_back(p);
    }
    return impacts;
}
ObjectHit Weapons::hit(uint8_t kind, uint8_t flags, uint8_t classification, uint8_t weapon) const {
    if (weapon < 1 || weapon > 5) throw std::runtime_error("Original weapon ID must be 1..5");
    ObjectHit out{kind, flags, 0, false};
    if (!classification) return out;
    if (classification == 1) {
        if (weapon == 2 || weapon == 3) {
            out.kind = kind < 0x2a ? 0x30 : 0x31;
            out.destroyed = true;
        }
        return out;
    }
    uint8_t increment = damageMatrix[(classification & 0x18) | weapon];
    uint8_t damage = flags & 0x38;
    damage = increment & 0x80 ? uint8_t(damage | (increment & 0x7f)) : uint8_t(damage + increment);
    if (damage < 0x38) {
        out.flags = uint8_t((flags & 0xc7) | damage);
        return out;
    }
    uint8_t type = classification & 7;
    if (type < 4) out.kind = uint8_t(0x30 + type);
    else if (type == 4) out.kind = 0;
    else if (type == 5) out.kind = 0x38;
    else if (type == 6) out.kind = 0x37;
    else out.kind = classification == 7 ? 0x4b : 0x18;
    if (out.kind == 0x4b) { out.flags = 1; out.sound = 8; }
    out.destroyed = true;
    return out;
}
} // namespace gb::original
