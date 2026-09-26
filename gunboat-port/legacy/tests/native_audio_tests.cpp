#include "original_audio.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
void append16(gb::Bytes &b, unsigned v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
void append32(gb::Bytes &b, unsigned v) { append16(b, v); append16(b, v >> 16); }
void tag(gb::Bytes &b, const char *s) { b.insert(b.end(), s, s + 4); }
void wave(const std::filesystem::path &path, const std::vector<int16_t> &pcm, unsigned rate) {
    gb::Bytes b;
    tag(b, "RIFF"); append32(b, unsigned(pcm.size() * 2 + 36)); tag(b, "WAVE");
    tag(b, "fmt "); append32(b, 16); append16(b, 1); append16(b, 2);
    append32(b, rate); append32(b, rate * 4); append16(b, 4); append16(b, 16);
    tag(b, "data"); append32(b, unsigned(pcm.size() * 2));
    for (int16_t v : pcm) append16(b, uint16_t(v));
    gb::write_file(path, b);
}
}
int main(int argc, char **argv) {
    try {
        if (argc < 2) return 2;
        gb::Archive archive(argv[1]);
        constexpr int rate = 48000, frames = rate * 2;
        gb::OriginalAudio a(archive.exe), b(archive.exe);
        std::vector<int16_t> whole(frames * 2), split(frames * 2);
        // Buffer boundaries must never change sound duration or PCM samples.
        a.play(8); b.play(8);
        a.render(whole.data(), frames, rate);
        for (int i = 0; i < frames;) {
            int count = std::min(frames - i, 1 + i % 977);
            b.render(split.data() + i * 2, count, rate);
            i += count;
        }
        require(whole == split, "Audio changes with SDL buffer size");
        require(std::any_of(whole.begin(), whole.end(), [](int16_t v) { return v != 0; }), "Silent original effect");
        require(!a.state().active, "Finite effect did not finish");
        a.reset(); a.set_engine(20, 20); a.render(whole.data(), frames, rate);
        require(a.state().active != 0, "Original engine loop stopped unexpectedly");
        a.play(2); a.render(whole.data(), frames, rate);
        require(!a.state().active, "Effect did not replace the engine sequence");
        a.set_engine(20, 20); require(a.state().active == 1, "Engine did not restart after effect");
        a.set_muted(true); a.render(whole.data(), frames, rate);
        require(std::all_of(whole.begin(), whole.end(), [](int16_t v) { return !v; }), "Mute emitted audio");
        require(a.state().active == 1, "Mute incorrectly advanced original sequencer");
        a.reset(); a.render(whole.data(), frames, rate);
        require(std::all_of(whole.begin(), whole.end(), [](int16_t v) { return !v; }), "Reset emitted audio");
        if (argc >= 3) {
            std::vector<int16_t> demo;
            for (unsigned id = 0; id < 13; ++id) {
                a.reset(); a.play(uint8_t(id)); a.render(whole.data(), frames, rate);
                demo.insert(demo.end(), whole.begin(), whole.end());
            }
            wave(argv[2], demo, rate);
        }
        std::cout << "Original audio PCM, scheduling, effect ownership, mute and reset checks passed.\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
