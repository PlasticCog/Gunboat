#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace gb::original {
// Original DOS integer units. Angles make one turn in 256 units; the
// separate heading fraction stores eighths of one angle unit.
struct BoatState {
    uint16_t x = 0, y = 0;
    uint8_t xFraction = 0, yFraction = 0;
    int8_t speed = 0;
    std::array<uint8_t, 2> engineState{0, 0}, throttle{0, 0}, maxThrottle{59, 59};
    std::array<uint16_t, 2> fuel{0xc544, 0xc544};
    std::array<uint8_t, 2> fuelLeak{3, 3}, jetDamage{3, 3};
    std::array<uint8_t, 2> engineCondition{3, 3};
    // DOS secondary engine switch flags D521/22 and primary indicators D503/04.
    std::array<uint8_t, 2> engineDamage{0x11, 0x11}, fuelDamage{0x10, 0x10};
    // pilotCondition retains DS:D52C & 3: control-rate mode, not crew health.
    uint8_t jetAngle = 64, jetIndicatorPhase = 0, pilotCondition = 0;
    std::array<uint8_t, 4> headings{0, 0, 128, 128}, headingFractions{};
    int16_t pitchImpulse = 0;
    uint8_t speedBand = 0;
    int8_t bobVelocity = 0;
    uint16_t bobPosition = 0x7f00;
    uint8_t bobPeriod = 8, bobCountdown = 8, bobPhase = 0, seaState = 0;
    uint8_t pitchByte = 128, viewPitch = 82;
    std::array<uint8_t, 3> gunElevations{192, 192, 192};
    bool atPilot = true, movementLocked = false;
};
struct Thrust { int16_t forward = 0, turn = 0; };
struct ShoreContact { bool touching = false; uint8_t color = 0; };
// 10B71..10C18: edge-angle collision test on the first and last candidates
// identified by original projection 10755..107A0. Indices are VERTEX indices
// (the original DS:D901/D903 store twice this value); -1 means no candidate.
ShoreContact shoreline_contact(const std::vector<uint16_t> &relativeBearings,
                               const std::vector<uint8_t> &controls,
                               int first, int last, uint8_t viewHeading, uint8_t boatHeading);
// 1036D..10386: reverse speed on the transition into contact. Call when the
// scene is reprojected, matching the original renderer's contact state cache.
bool shoreline_response(BoatState &, bool touching, bool &previousContact);
class BoatPhysics {
    std::array<uint16_t, 65> sine{};
    std::array<uint8_t, 4> jetStep{};
    std::array<uint8_t, 2> jetOffset{};
    std::array<uint16_t, 4> bobMasks{};
    void throttle_input(BoatState &, int engine, uint8_t mask) const;
    static void turn(BoatState &, uint8_t amount, bool positive);
    static void accelerate(BoatState &, int8_t target);
    static void toggle_engine_unchecked(BoatState &, int index);
  public:
    explicit BoatPhysics(const std::vector<uint8_t> &unpackedExe);
    // image CF6C..D021/D0B3: mission start modes 0=off,1=70,2=55,other=idle8.
    static BoatState initial(uint16_t x, uint16_t y, uint8_t startMode = 0,
                             bool upgradedEngines = false);
    // Original pilot control mask (9B62): 1=forward,2=reverse,4=jet left,
    // 8=jet right,16=slow both throttles. Call on original input cadence.
    void controls(BoatState &, uint8_t mask) const;
    // Original 976B..97D8: engine switch and startup/shutdown countdown.
    static bool toggle_engine(BoatState &, int index);
    // Original B6B9: one engine's thrust, fuel and startup transitions.
    Thrust engine(BoatState &, int index, uint8_t randomByte) const;
    // Original B451..B4A8: both engines; two one-unit acceleration steps;
    // rotate hull and all gun headings together, preserving integer rounding.
    void propulsion(BoatState &, uint8_t randomByte) const;
    // Original 11078..11164: XY motion and map-edge clipping only. Shoreline
    // collision, wake animation and camera movement are separate routines.
    void move(BoatState &) const;
    // First part of 11270: pitch impulse decays by four and clamps to +/-24.
    static void decay_pitch(BoatState &);
    // Full original 11270..11387, including wave bob and gun-station elevation.
    void camera_pitch(BoatState &, uint8_t randomByte, unsigned station = 1) const;
    // 113FC..11412: normal (non-detached) camera coordinates in renderer units.
    static std::array<uint16_t, 2> camera_coordinates(const BoatState &);
};
} // namespace gb::original
