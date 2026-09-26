#include "original_audio.hpp"
#include <algorithm>
#include <stdexcept>

namespace gb {
namespace {
constexpr uint32_t dsImage = 0x1b730;
uint16_t shr16(uint16_t value, unsigned shift) {
    return shift >= 16 ? 0 : uint16_t(value >> shift);
}
} // namespace
OriginalAudio::OriginalAudio(const Bytes &exe) {
    if (exe.size() < dsImage + 0xdc3c)
        throw std::runtime_error("Original executable is too short for Gunboat's sound programs");
    initial.assign(65536, 0);
    std::copy_n(exe.begin() + dsImage, std::min<size_t>(65536, exe.size() - dsImage), initial.begin());
    reset();
}
uint16_t OriginalAudio::get(uint16_t p) const {
    return uint16_t(data[p] | (uint16_t(data[uint16_t(p + 1)]) << 8));
}
void OriginalAudio::put(uint16_t p, uint16_t value) {
    data[p] = uint8_t(value);
    data[uint16_t(p + 1)] = uint8_t(value >> 8);
}
void OriginalAudio::reset() {
    data = initial;
    // 22ed:0800 initializes the PC speaker path. Other initial values are
    // deliberately inherited from the EXE, just as they are in DOS.
    for (uint16_t p : {0xdaf0, 0xdafc, 0xdaf2, 0xdaf4, 0xdaf6, 0xdaf8}) put(p, 0);
    put(0xda88, 0xda58);
    data[0xda8a] = 7;
    put(0xda8b, 1);
    put(0xda8d, 0);
    data[0xda8f] = 8;
    data[0xda46] = 1;
    divisor = 0;
    on = muted = false;
    timerPhase = speakerPhase = 0;
}
void OriginalAudio::play(uint8_t id) {
    if (id > 12) throw std::runtime_error("Gunboat sound effect ID must be 0..12");
    put(0xdad2, get(uint16_t(0xdb1e + id * 2)));
    put(0xdaf0, 1);
}
void OriginalAudio::set_engine(uint8_t port, uint8_t starboard, bool disabled) {
    // Original 1919:3cc9 (image 0xce59), including the eight-bit sum and
    // the transition from note numbers to the second octave encoding.
    uint8_t al = uint8_t(port + starboard);
    al >>= 2;
    if (disabled || !al) {
        data[0xdba2] = 1;
        return;
    }
    al >>= 1;
    uint8_t ah = al;
    data[0xdba0] = uint8_t(0x26 - al);
    data[0xdba2] = 0;
    if (ah >= 8) ah = uint8_t((ah >> 1) + 4);
    ++ah;
    al = ah;
    ah = uint8_t(ah + 4);
    constexpr uint16_t notes[] = {0xdba7, 0xdbad, 0xdba9, 0xdba5, 0xdbaf, 0xdbab};
    constexpr uint8_t steps[] = {2, 1, 1, 2, 2};
    bool upper = false;
    for (unsigned i = 0; i < 6; ++i) {
        upper = upper || al > 12;
        data[notes[i]] = upper ? ah : al;
        if (i < 5) {
            al = uint8_t(al + steps[i]);
            ah = uint8_t(ah + steps[i]);
        }
    }
    if (!get(0xdaf0)) play(6);
}
void OriginalAudio::duration() {
    // 22ed:0523. Preserve the original AH clobber in the dotted-note branch.
    uint8_t ah = data[uint16_t(get(0xdadc) + 1)];
    uint16_t value = uint16_t(get(0xdae2) + get(0xdb1a));
    value = shr16(value, ah & 7);
    if (ah & 8) {
        uint16_t half = uint16_t(value >> 1);
        ah = uint8_t(half >> 8);
        value = uint16_t(value + half);
    }
    if (ah & 16) {
        if (get(0xdac8) != 3) put(0xdac8, get(0xdac8) == 1 ? 3 : uint16_t(get(0xdac8) + 1));
    } else if (get(0xdac8)) put(0xdac8, uint16_t(get(0xdac8) + 1));
    put(0xdab4, 1);
    put(0xdaee, value);
}
void OriginalAudio::command() {
    put(0xdaee, 0);
    uint16_t p = get(0xdadc);
    uint8_t opcode = data[p] & 15, hi = data[p] >> 4, arg = data[uint16_t(p + 1)];
    if (opcode == 0) {
        on = false;
        put(0xdabe, 1);
        duration();
    } else if (opcode <= 12) {
        divisor = shr16(get(uint16_t(get(0xda88) + (opcode - 1) * 2)), hi);
        on = true;
        put(0xdabe, 0);
        duration();
    } else if (opcode == 13) {
        switch (hi) {
        case 0:
            put(0xdb08, arg ? 0 : 1);
            if (arg) data[0xdb14] = arg;
            break;
        case 1:
        case 2: {
            uint32_t product = uint32_t(get(0xdae2)) * arg;
            uint16_t value = uint16_t(product / 100 + (product % 100 > 49));
            put(0xdb1a, hi == 1 ? value : uint16_t(-value));
            break;
        }
        case 3: data[0xdb1c] = arg; break;
        case 4: put(0xdae2, uint16_t(arg * 4)); break;
        case 7: put(0xda48, arg); break;
        case 8:
            if (!get(0xda48) || (put(0xda48, uint16_t(get(0xda48) - 1)), get(0xda48)))
                put(0xdadc, uint16_t(p - arg));
            break;
        default: break; // Tandy envelope commands have no PC-speaker action.
        }
    } else if (opcode == 14) {
        if (!arg) on = false;
        else {
            uint16_t value = uint16_t(get(get(0xda88)) - uint16_t(arg << data[0xda8a]));
            divisor = shr16(value, hi);
            on = true;
            put(0xdaee, get(0xdb1c));
        }
    } else {
        put(0xdaf2, 0);
        on = false;
    }
    put(0xdadc, uint16_t(get(0xdadc) + 2));
}
void OriginalAudio::process() {
    put(0xdadc, get(0xdad2));
    put(0xdaee, get(0xdae4));
    put(0xdb08, get(0xdafe));
    put(0xdb14, get(0xdb0a));
    if (get(0xdaf2) != 2) {
        data[0xda51] = data[0xda50] = 0;
        put(0xdb08, 0); put(0xdb14, 6); put(0xdb1a, 0); put(0xdb1c, 1);
        put(0xdabe, 0); put(0xdac8, 0); put(0xdaee, 0); put(0xdaf2, 2);
    }
    int16_t remaining = int16_t(get(0xdaee));
    if (get(0xdb08) != 1 &&
        (remaining == int16_t(get(0xdb14)) || (remaining < int16_t(get(0xdb14)) && remaining == 4)))
        on = false;
    else if (!remaining) {
        unsigned bound = 0;
        do {
            if (++bound > 65536) throw std::runtime_error("Invalid original sound program loop");
            command();
        } while (get(0xdaf2) && !get(0xdaee));
    }
    put(0xdad2, get(0xdadc)); put(0xdae4, get(0xdaee));
    put(0xdafe, get(0xdb08)); put(0xdb0a, get(0xdb14));
}
void OriginalAudio::tick() {
    // Native translation of 22ed:00eb..0283, PC speaker path only.
    if (muted) { on = false; return; }
    if (!get(0xdaf0)) return;
    if (get(0xdaf0) == 1) { put(0xdaf0, 2); put(0xdaf2, 1); }
    put(0xdae2, get(0xdade)); put(0xdb1a, get(0xdb16));
    if (get(0xdaf2)) {
        if (get(0xdaf2) == 1 || !get(0xdae4)) process();
        else if (!get(0xdb08) && get(0xdae4) == get(0xdb14)) on = false;
    }
    put(0xdae4, uint16_t(get(0xdae4) - 1));
    put(0xdade, get(0xdae2)); put(0xdb16, get(0xdb1a));
    if (!get(0xdaf2)) { on = false; put(0xdaf0, 0); }
}
OriginalAudio::State OriginalAudio::state() const {
    return {divisor, on, get(0xdaf0), get(0xdaf2), get(0xdad2), get(0xdae4),
            get(0xdade), get(0xdb16), get(0xda48)};
}
uint16_t OriginalAudio::debug_word(uint16_t p) const { return get(p); }
uint8_t OriginalAudio::debug_byte(uint16_t p) const { return data[p]; }
void OriginalAudio::render(int16_t *out, int frames, int sampleRate) {
    if (!out || frames < 0 || sampleRate < 8000) throw std::runtime_error("Invalid native audio output");
    const uint64_t timerPeriod = uint64_t(tickDivisor) * unsigned(sampleRate);
    for (int i = 0; i < frames; ++i) {
        timerPhase += pitClock;
        while (timerPhase >= timerPeriod) { timerPhase -= timerPeriod; tick(); }
        const uint64_t period = uint64_t(divisor ? divisor : 65536) * unsigned(sampleRate);
        speakerPhase = (speakerPhase + pitClock) % period;
        int16_t value = on && !muted ? (speakerPhase < period / 2 ? 3500 : -3500) : 0;
        out[i * 2] = out[i * 2 + 1] = value;
    }
}
} // namespace gb
