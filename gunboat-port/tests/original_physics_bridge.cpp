#include "../src/original_physics.hpp"
#include <memory>
using namespace gb::original;
static std::unique_ptr<BoatPhysics> physics;
static uint16_t w(const uint8_t *m, int p) { return uint16_t(m[p] | (m[p + 1] << 8)); }
static void w(uint8_t *m, int p, uint16_t n) { m[p] = uint8_t(n); m[p + 1] = uint8_t(n >> 8); }
extern "C" __declspec(dllexport) void setup(const uint8_t *data, unsigned size) {
    physics = std::make_unique<BoatPhysics>(std::vector<uint8_t>(data, data + size));
}
extern "C" __declspec(dllexport) uint32_t run(uint8_t *m, int operation, int arg) {
    if (operation == 6) {
        std::vector<uint16_t> bearings(1024);
        std::vector<uint8_t> controls(1024);
        for (int i = 0; i < 1024; ++i) { bearings[i] = w(m, 0x2c96 + i * 2); controls[i] = m[0x1096 + i]; }
        auto contact = shoreline_contact(bearings, controls, arg, -1, m[0xd191], m[0xb81e]);
        return uint32_t(contact.touching) | (uint32_t(contact.color) << 8);
    }
    BoatState s;
    s.x = w(m, 0xc12d); s.y = w(m, 0xc8fd);
    s.xFraction = m[0xb826]; s.yFraction = m[0xb827]; s.speed = int8_t(m[0xb828]);
    s.jetAngle = m[0xb818]; s.jetIndicatorPhase = m[0xd632]; s.pilotCondition = m[0xd52c];
    s.pitchImpulse = int16_t(w(m, 0xd982)); s.speedBand = m[0xb819];
    s.bobVelocity = int8_t(m[0xb82a]); s.bobPosition = w(m, 0xb82b);
    s.bobPeriod = m[0xd968]; s.bobCountdown = m[0xd967]; s.bobPhase = m[0xd969];
    s.seaState = uint8_t(w(m, 0xb4ff)); s.pitchByte = m[0xb82d]; s.viewPitch = m[0xd193];
    s.gunElevations = {m[0xb837], m[0xb836], m[0xb838]};
    s.atPilot = m[0x86] == 1; s.movementLocked = m[0xd6b4] != 0;
    for (int i = 0; i < 2; ++i) {
        s.engineState[i] = m[0xb808+i]; s.throttle[i] = m[0xb816+i];
        s.maxThrottle[i] = m[0xb81c+i]; s.fuel[i] = w(m, 0xb80a+i*2);
        s.fuelLeak[i] = m[0xd506+i]; s.jetDamage[i] = m[0xd50a+i];
        s.engineCondition[i] = m[0xd508+i];
        s.engineDamage[i] = m[0xd521+i]; s.fuelDamage[i] = m[0xd503+i];
    }
    for (int i = 0; i < 4; ++i) { s.headings[i] = m[0xb81e + i]; s.headingFractions[i] = m[0xb822+i]; }
    Thrust t;
    if (operation == 0) physics->controls(s, uint8_t(arg));
    if (operation == 1) t = physics->engine(s, arg, m[0x8b]);
    if (operation == 2) physics->propulsion(s, m[0x8b]);
    if (operation == 3) physics->move(s);
    if (operation == 4) physics->decay_pitch(s);
    if (operation == 5) physics->camera_pitch(s, m[0x8a], w(m, 0x86));
    if (operation == 7) physics->toggle_engine(s, arg);
    w(m, 0xc12d, s.x); w(m, 0xc8fd, s.y);
    m[0xb826] = s.xFraction; m[0xb827] = s.yFraction; m[0xb828] = uint8_t(s.speed);
    m[0xb818] = s.jetAngle; m[0xd632] = s.jetIndicatorPhase;
    w(m, 0xd982, uint16_t(s.pitchImpulse)); m[0xb819] = s.speedBand;
    m[0xb82a] = uint8_t(s.bobVelocity); w(m, 0xb82b, s.bobPosition);
    m[0xd968] = s.bobPeriod; m[0xd967] = s.bobCountdown; m[0xd969] = s.bobPhase;
    m[0xb82d] = s.pitchByte; m[0xd193] = s.viewPitch;
    for (int i = 0; i < 2; ++i) {
        m[0xb808+i] = s.engineState[i]; m[0xb816+i] = s.throttle[i];
        m[0xb81c+i] = s.maxThrottle[i]; w(m, 0xb80a+i*2, s.fuel[i]);
        m[0xd521+i] = s.engineDamage[i]; m[0xd503+i] = s.fuelDamage[i];
    }
    for (int i = 0; i < 4; ++i) { m[0xb81e + i] = s.headings[i]; m[0xb822+i] = s.headingFractions[i]; }
    return uint16_t(t.forward) | (uint32_t(uint16_t(t.turn)) << 16);
}

