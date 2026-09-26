#pragma once
#include "assets.hpp"
#include <cstdint>

namespace gb {
// Native translation of Gunboat's PC-speaker effect player (22ed:0025,
// 22ed:00eb..095f), with programs and tuning read from the original EXE.
// This is the gameplay effect player, not the separate title-music driver.
class OriginalAudio {
  public:
    struct State {
        uint16_t divisor = 0;
        bool on = false;
        uint16_t active = 0, channel = 0, pointer = 0, remaining = 0;
        uint16_t tempo = 0, tempoOffset = 0, loop = 0;
    };
    static constexpr uint32_t pitClock = 1193182;
    static constexpr uint16_t tickDivisor = 0x13b1;
    explicit OriginalAudio(const Bytes &unpackedExe);
    void reset();
    void play(uint8_t originalEffectId);
    // Inputs are the original throttle bytes, not a normalized speed.
    void set_engine(uint8_t port, uint8_t starboard, bool disabled = false);
    void set_muted(bool value) { muted = value; }
    void tick();
    State state() const;
    // Writes stereo PCM. Call on the same thread as play()/set_engine().
    void render(int16_t *stereo, int frames, int sampleRate);
    // Development visibility for direct original-code comparisons.
    uint16_t debug_word(uint16_t offset) const;
    uint8_t debug_byte(uint16_t offset) const;

  private:
    Bytes initial, data;
    uint16_t divisor = 0;
    bool on = false, muted = false;
    uint64_t timerPhase = 0, speakerPhase = 0;
    uint16_t get(uint16_t offset) const;
    void put(uint16_t offset, uint16_t value);
    void command();
    void duration();
    void process();
};
} // namespace gb
