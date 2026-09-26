#include "original_physics.hpp"
#include <algorithm>
#include <stdexcept>

namespace gb::original {
namespace {
uint8_t u8(int n) { return static_cast<uint8_t>(n); }
int16_t s16(int n) { return static_cast<int16_t>(static_cast<uint16_t>(n)); }
int16_t sar(int16_t n) { return n >= 0 ? n / 2 : static_cast<int16_t>(-((-int(n) + 1) / 2)); }
}
BoatPhysics::BoatPhysics(const std::vector<uint8_t> &exe) {
    constexpr size_t table = 0x9190 + 0x3502, ds = 0x1b730;
    if (exe.size() < ds + 0xd982) throw std::runtime_error("Original physics tables missing");
    for (size_t i = 0; i < sine.size(); ++i)
        sine[i] = uint16_t(exe[table + i * 4] | (exe[table + i * 4 + 1] << 8));
    for (size_t i = 0; i < jetStep.size(); ++i) jetStep[i] = exe[ds + 0xd62e + i];
    jetOffset = {exe[ds + 0xd6b6], exe[ds + 0xd6b7]};
    for (size_t i = 0; i < bobMasks.size(); ++i)
        bobMasks[i] = uint16_t(exe[ds + 0xd97a + i * 2] | (exe[ds + 0xd97b + i * 2] << 8));
}
BoatState BoatPhysics::initial(uint16_t x, uint16_t y, uint8_t startMode, bool upgraded) {
    BoatState s;
    s.x = x; s.y = y;
    s.pilotCondition = startMode ? 1 : 0;
    s.maxThrottle.fill(upgraded ? 103 : 59);
    if (startMode) {
        s.engineState.fill(1);
        s.throttle.fill(startMode == 1 ? 70 : startMode == 2 ? 55 : 8);
        s.engineDamage.fill(0x10); s.fuelDamage.fill(0x13);
    }
    return s;
}
void BoatPhysics::throttle_input(BoatState &s, int i, uint8_t mask) const {
    if (s.engineState[i] != 1) return;
    auto &t = s.throttle[i];
    if (mask & 2) {
        if (s.jetAngle & 128) { if (t < s.maxThrottle[i]) ++t; }
        else if (t == 8) { s.jetAngle |= 128; ++t; }
        else if (t >= s.throttle[i ^ 1]) --t;
    } else if (mask & 1) {
        if (!(s.jetAngle & 128)) { if (t < s.maxThrottle[i]) ++t; }
        else if (t == 8) { s.jetAngle &= 127; ++t; }
        else if (t >= s.throttle[i ^ 1]) --t;
    }
}
void BoatPhysics::controls(BoatState &s, uint8_t mask) const {
    if (mask & 16) {
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 2; ++i)
                if (s.throttle[i] > 8 && s.throttle[i] >= s.throttle[i ^ 1]) --s.throttle[i];
    }
    for (int j = 0; j < 4; ++j) {
        throttle_input(s, 0, mask); throttle_input(s, 1, mask);
    }
    if (!(mask & 12)) return;
    const uint8_t old = s.jetAngle & 127, reverse = s.jetAngle & 128;
    const uint8_t step = jetStep[s.pilotCondition & 3];
    int next = old;
    bool saturated = false;
    if (mask & 4) {
        next = old - step;
        if (next < 0) { next = 0; saturated = true; }
        else if (old > 64 && next < 64) next = 64;
    } else {
        next = old + step;
        if (next >= 125) { next = 125; saturated = true; }
        else if (old < 64 && next > 64) next = 64;
    }
    s.jetAngle = u8(next | reverse);
    if (!saturated) s.jetIndicatorPhase = u8((s.jetIndicatorPhase + ((mask & 4) ? 2 : 1)) % 3);
}
void BoatPhysics::toggle_engine_unchecked(BoatState &s, int i) {
    // 955B/9308/91B7 state changes; cockpit drawing has no simulation effect.
    auto &secondary = s.engineDamage[i];
    auto &primary = s.fuelDamage[i];
    if (!(secondary & 4)) secondary ^= 1;
    if (!(primary & 4)) primary = uint8_t((primary & 0x78) | ((secondary & 1) ? 0 : 3));
    const uint8_t mode = primary & 7;
    if (mode != 0 && mode != 3) return;
    if (mode == 3 && !(primary & 4)) primary = uint8_t((primary & 0x78) | 2);
    uint8_t transition = s.engineState[i] & 252;
    s.engineState[i] = transition ? uint8_t(transition ^ 255) : uint8_t((secondary & 1) ? 128 : 127);
}
bool BoatPhysics::toggle_engine(BoatState &s, int i) {
    if (i < 0 || i > 1) throw std::out_of_range("engine index");
    if (!s.fuel[i] || (s.engineCondition[i] & 3) == 2) return false;
    toggle_engine_unchecked(s, i);
    return true;
}
Thrust BoatPhysics::engine(BoatState &s, int i, uint8_t randomByte) const {
    if (i < 0 || i > 1) throw std::out_of_range("engine index");
    const auto stop = [&] {
        s.fuel[i] = 0;
        if (!(s.engineDamage[i] & 1)) toggle_engine_unchecked(s, i);
        s.engineDamage[i] = 0x15; s.fuelDamage[i] = 0x95;
    };
    auto leak = s.fuelLeak[i] & 3;
    if (leak != 3) {
        unsigned loss = unsigned(leak) * 16;
        if (s.fuel[i] > loss) s.fuel[i] = uint16_t(s.fuel[i] - loss);
        else stop();
    }
    Thrust t;
    const uint8_t previous = s.engineState[i];
    if (!previous) { s.throttle[i] = 0; s.jetAngle &= 127; }
    else if (previous != 1) {
        if (previous & 128) {
            s.engineState[i] = u8(previous + 1);
            if (s.engineState[i]) ++s.engineState[i];
        } else {
            --s.engineState[i];
            if (previous != 2) --s.engineState[i];
            if ((previous == 2 || previous == 3) && !(s.fuelDamage[i] & 4))
                s.fuelDamage[i] = uint8_t((s.fuelDamage[i] & 0x78) | 3);
        }
        if (s.engineState[i]) s.throttle[i] = u8(8 - ((s.engineState[i] & 0x70) >> 4));
        else s.throttle[i] = 0;
        s.jetAngle &= 127;
    } else {
        const uint8_t power = s.throttle[i] >= 9 ? u8(s.throttle[i] - 9) : 0;
        uint8_t angle = u8(s.jetAngle + jetOffset[i]);
        if (angle & 128) angle = u8(-angle);
        const unsigned index = angle > 64 ? u8(128 - angle) : angle;
        if (index > 64) throw std::runtime_error("Invalid original waterjet angle");
        const uint32_t strength = uint32_t(power) * 128;
        t.forward = int16_t((strength * sine[index]) >> 16);
        if (s.jetAngle & 128) t.forward = s16(-int(uint16_t(t.forward) >> 1));
        t.turn = int16_t((strength * sine[64 - index]) >> 16);
        if (angle > 64) t.turn = s16(-t.turn);
    }
    if (!(randomByte & 31)) {
        unsigned loss = unsigned(s.throttle[i]) >> 2;
        if (s.fuel[i] > loss) s.fuel[i] = uint16_t(s.fuel[i] - loss);
        else stop();
    }
    if (s.atPilot && (s.jetDamage[i] & 3) != 3) {
        t.forward = sar(t.forward); t.turn = sar(t.turn);
        if ((s.jetDamage[i] & 3) == 2) { t.forward = sar(t.forward); t.turn = sar(t.turn); }
    }
    return t;
}
void BoatPhysics::accelerate(BoatState &s, int8_t target) {
    if (s.movementLocked || s.speed == target) return;
    int delta = s.speed < target ? 1 : -1;
    s.speed = static_cast<int8_t>(s.speed + delta);
    s.pitchImpulse = s16(s.pitchImpulse + delta * 5);
}
void BoatPhysics::turn(BoatState &s, uint8_t amount, bool positive) {
    for (int i = 0; i < 4; ++i) {
        int n = s.headingFractions[i] + (positive ? int(amount) : -int(amount));
        int whole = n >= 0 ? n / 8 : -((-n + 7) / 8);
        s.headings[i] = u8(s.headings[i] + whole);
        s.headingFractions[i] = u8(n) & 7;
    }
}
void BoatPhysics::propulsion(BoatState &s, uint8_t randomByte) const {
    auto port = engine(s, 0, randomByte), starboard = engine(s, 1, randomByte);
    int16_t forward = s16(port.forward + starboard.forward), torque = s16(port.turn + starboard.turn);
    const int8_t target = static_cast<int8_t>(uint16_t(forward) >> 8);
    accelerate(s, target); accelerate(s, target);
    unsigned absTorque = torque < 0 ? unsigned(-int(torque)) : unsigned(torque);
    uint8_t amount = uint8_t(std::min(absTorque >> 9, 55u));
    turn(s, amount, torque < 0);
    // Original writes speedBand only for the nonnegative-torque branch.
    if (torque >= 0) s.speedBand = uint8_t((s.speed < 0 ? -int(s.speed) : int(s.speed)) >> 3);
}
void BoatPhysics::move(BoatState &s) const {
    unsigned angle = s.headings[0], lookup = angle & 63;
    if (angle & 64) lookup = 64 - lookup;
    unsigned speed = s.speed < 0 ? unsigned(-int(s.speed)) : unsigned(s.speed);
    if (s.speed < 0) angle ^= 128;
    unsigned dx = ((speed * (sine[lookup] >> 8)) >> 8) * 8;
    unsigned dy = ((speed * (sine[64 - lookup] >> 8)) >> 8) * 8;
    const auto moveAxis = [](uint16_t &value, uint8_t &fraction, unsigned amount,
                             bool negative, unsigned limit) {
        unsigned whole = amount >> 8;
        const uint8_t part = uint8_t(amount);
        if (negative) {
            bool borrow = fraction < part; fraction = uint8_t(fraction - part);
            whole += borrow;
            uint8_t low = uint8_t(value), high = uint8_t(value >> 8);
            borrow = low < whole; low = uint8_t(low - whole);
            if (borrow) { if (high < 5) return; --high; }
            value = uint16_t((high << 8) | low);
        } else {
            bool carry = unsigned(fraction) + part > 255; fraction = uint8_t(fraction + part);
            whole += carry;
            uint8_t low = uint8_t(value), high = uint8_t(value >> 8);
            carry = unsigned(low) + whole > 255; low = uint8_t(low + whole);
            if (carry) { if (high >= limit) return; ++high; }
            value = uint16_t((high << 8) | low);
        }
    };
    moveAxis(s.y, s.yFraction, dy, (angle & 192) == 64 || (angle & 192) == 128, 0x27);
    moveAxis(s.x, s.xFraction, dx, (angle & 128) != 0, 0x3f);
}
void BoatPhysics::decay_pitch(BoatState &s) {
    int n = s.pitchImpulse;
    if (n > 0) n = std::max(n - 4, 0);
    else if (n < 0) n = std::min(n + 4, 0);
    s.pitchImpulse = int16_t(std::clamp(n, -24, 24));
}
void BoatPhysics::camera_pitch(BoatState &s, uint8_t randomByte, unsigned station) const {
    decay_pitch(s);
    s.pitchByte = u8(-(s.pitchImpulse * 2 + 128)) & 248;
    const int delta = int(s.bobVelocity) * 128;
    const int sum = int(s.bobPosition) + delta;
    const bool overflow = delta >= 0 ? sum > 65535 : sum <= 0;
    if (overflow) { s.bobPeriod = u8(s.bobPeriod - s.bobCountdown); s.bobCountdown = 1; }
    else s.bobPosition = uint16_t(sum);
    int pitch = s.bobPosition >> 8;
    if (station >= 2) {
        unsigned index = station == 2 ? 0 : station == 3 ? 1 : 2;
        pitch += int(s.gunElevations[index]) - s.pitchByte;
        if (pitch < 0) pitch = 8;
    }
    s.viewPitch = uint8_t((std::clamp(pitch, 8, 496) >> 3) + 67);
    --s.bobCountdown;
    if (s.bobCountdown) return;
    s.bobCountdown = s.bobPeriod;
    s.bobPhase = u8(s.bobPhase + 1) & 3;
    if (s.bobPhase & 1) { s.bobVelocity = static_cast<int8_t>(-s.bobVelocity); return; }
    const uint16_t seed = uint16_t(randomByte * 257) & bobMasks[s.seaState & 3];
    uint8_t period = u8(uint8_t(seed) + 29 - s.speedBand * 3);
    if (!period) ++period;
    s.bobPeriod = s.bobCountdown = period;
    uint8_t velocity = uint8_t(seed >> 8);
    velocity = u8((velocity << 2) | (velocity >> 6));
    velocity = u8(velocity + 1 + s.speedBand);
    if (s.bobPhase & 2) velocity = u8(-velocity);
    s.bobVelocity = static_cast<int8_t>(velocity);
}
std::array<uint16_t, 2> BoatPhysics::camera_coordinates(const BoatState &s) {
    return {uint16_t(s.x * 4 + (s.xFraction >> 6)), uint16_t(s.y * 4 + (s.yFraction >> 6))};
}
ShoreContact shoreline_contact(const std::vector<uint16_t> &bearings,
                               const std::vector<uint8_t> &controls,
                               int first, int last, uint8_t viewHeading, uint8_t boatHeading) {
    ShoreContact out;
    const uint8_t offset = u8(viewHeading - boatHeading - 20);
    const auto control = [&](int i) { return i >= 0 && size_t(i) < controls.size() ? controls[size_t(i)] : uint8_t(0); };
    const auto bearing = [&](int i) { return i >= 0 && size_t(i) < bearings.size() ? uint8_t(bearings[size_t(i)] >> 8) : uint8_t(0); };
    const auto vertex = [&](int i) {
        if (i < 0 || size_t(i) >= controls.size() || size_t(i) >= bearings.size()) return;
        uint8_t center = u8(bearing(i) + offset);
        const auto edge = [&](int neighbor) {
            uint8_t endpoint = u8(bearing(neighbor) + offset);
            if (center & 128) {
                endpoint = u8(endpoint - 128);
                if ((endpoint & 128) && endpoint <= 192) endpoint = 127;
                if (center < 172) { if (int8_t(endpoint) >= 44) out.touching = true; }
                else { if (int8_t(endpoint) < 44) out.touching = true; }
            } else {
                if ((endpoint & 128) && endpoint <= 192) endpoint = 127;
                if (center < 44) { if (int8_t(endpoint) >= 44) out.touching = true; }
                else { if (int8_t(endpoint) < 44) out.touching = true; }
            }
        };
        if (i >= 1 && control(i - 2) && !(control(i - 2) & 128)) edge(i - 2);
        if (control(i - 1)) edge(i - 1);
        out.color = control(i);
        if (!out.color) return;
        edge(i + 1);
        if (!(out.color & 128)) edge(i + 2);
    };
    vertex(first);
    if (!out.touching) vertex(last);
    return out;
}
bool shoreline_response(BoatState &s, bool touching, bool &previousContact) {
    bool impact = touching && !previousContact;
    if (impact) s.speed = s.speed < 0 ? 16 : -16;
    previousContact = touching;
    return impact;
}
} // namespace gb::original
