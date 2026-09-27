#pragma once
// AdLib sound effects, an optional enhancement (src/enhanced/). The original plays its 13 sound
// effects on the PC speaker. With this option each effect can sound on an FM voice of a second,
// emulated OPL2 chip instead. The effects driver still runs exactly as the original's: every note it
// gives the speaker sounds with the effect's FM instrument, at the same pitch, for as long as the
// speaker would sound it. Only what reaches the ear changes.
//
// The instruments are a bank of 13 patches. The port ships with the bank in gunboat-port/data/sfx.ini,
// built into the program. The player edits it with the sound-effects editor (tools/sfx_editor), which
// saves sfx.ini in the settings folder next to gunboat.ini (and, run from a build in the source
// tree, data/sfx.ini too, so the next build ships the edits); the game reads the player's file at
// start-up, the built-in bank for what it lacks. This file has no SDL or game dependencies apart from the settings folder, so the
// editor shares it.
#include <string>

#include "types.hpp"

namespace gb {

constexpr int SFX_COUNT = 13;  // the effect programs of the driver (sound.md §2.1)
// The effects' chip is mixed at twice its output: one FM voice against the speaker's full square wave.
constexpr int SFX_GAIN = 2;

// Where an effect is heard: on its FM instrument, on the PC speaker as in the original, or not at all.
enum class SfxOutput : u8 { Adlib, Speaker, Silent };

// One OPL2 operator. The ranges are the chip's.
struct SfxOperator {
    u8 attack = 15;   // 0-15, 15 fastest
    u8 decay = 4;     // 0-15, 15 fastest
    u8 sustain = 4;   // 0-15 attenuation of the sustain level (3 dB steps), 0 loudest
    u8 release = 6;   // 0-15, 15 fastest
    u8 multiple = 1;  // 0-15 frequency multiple (0 = 1/2)
    u8 level = 0;     // 0-63 attenuation (0.75 dB steps), 0 loudest
    u8 waveform = 0;  // 0 sine, 1 half sine, 2 absolute sine, 3 quarter sine
    u8 ksl = 0;       // 0-3 key scale level
    bool tremolo = false, vibrato = false;
    bool sustained = true;  // EG type: hold the sustain level while the key is on
    bool ksr = false;       // key scale rate
};

struct SfxPatch {
    SfxOutput output = SfxOutput::Adlib;
    SfxOperator mod, car;
    u8 feedback = 0;         // 0-7, the modulator's self-modulation (7: noise-like)
    bool additive = false;   // false: the modulator modulates the carrier (FM); true: both are heard
    int transpose = 0;       // semitones, -36..36
    int volume = 100;        // percent, 0-100
    bool retrigger = false;  // a new pitch during a note attacks again (else it glides on)
    int jitter = 0;          // cents 0-1200: the pitch wobbles randomly on every driver tick (noise)
};

struct SfxBank {
    SfxPatch fx[SFX_COUNT];
};

const char *sfx_name(int id);         // a short name: "Key click"
const char *sfx_description(int id);  // what plays it in the game
// The defaults: the instruments the port ships with (gunboat-port/data/sfx.ini, built into the
// program), over the port's first instruments for anything that file lacks.
SfxPatch sfx_default_patch(int id);
void sfx_bank_defaults(SfxBank &b);
// Reads a bank (the defaults for what the file lacks); false when there is no file.
bool sfx_bank_load(const std::string &path, SfxBank &b);
bool sfx_bank_save(const std::string &path, const SfxBank &b);
// The shipped bank's file when the program runs from a build in the port's source tree
// (gunboat-port/build): gunboat-port/data/sfx.ini, which the next build builds in; else empty.
std::string sfx_source_bank_path();
// The patch as the lines sfx.ini keeps it ("key = value", one per line); equal patches, equal text.
std::string sfx_patch_text(const SfxPatch &p);
std::string sfx_bank_path();  // sfx.ini in the settings folder

// The effect whose program holds `pc` (the driver's program counter, a DGROUP offset): the one with
// the largest start not above it, from the driver's table of program starts; -1 if pc is before
// them all or far past them.
int sfx_effect_of(u16 pc, const u16 starts[SFX_COUNT]);

// The FM voices of the effects. They are fed the speaker changes of the effects driver and write
// OPL2 registers through `write`. Three channels take turns, so a note's release still sounds
// while the next one starts.
class SfxSynth {
public:
    using Write = void (*)(void *ctx, u8 reg, u8 value);
    SfxSynth(Write write, void *ctx) : write_(write), ctx_(ctx) {}
    void reset();  // the chip's set-up; every voice off
    void set_bank(const SfxBank &b) { bank_ = b; }
    const SfxBank &bank() const { return bank_; }
    // The effects driver of effect `id` sets the speaker: PIT channel 2's divisor (0 = 65536) and
    // whether the gate is open.
    void speaker(int id, u16 divisor, bool on);
    void tick();     // one step of the driver (236.7 Hz): the pitch jitter
    void silence();  // the sounding note is released
    bool sounding() const { return key_on_; }

private:
    static constexpr int CHANNELS = 3;
    void w(u8 reg, u8 value) { write_(ctx_, reg, value); }
    void program(int ch, const SfxPatch &p);
    void frequency(int ch, bool key);
    Write write_;
    void *ctx_;
    SfxBank bank_{};
    int id_ = -1;       // the effect of the sounding (or last) note
    int ch_ = 0;        // its channel
    bool key_on_ = false;
    u16 divisor_ = 0;
    int cents_ = 0;     // the jitter now
    u32 rng_ = 0x1990u;
};

} // namespace gb
