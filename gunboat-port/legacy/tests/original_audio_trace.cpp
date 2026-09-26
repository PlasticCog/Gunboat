#include "original_audio.hpp"
#include <iostream>
#include <string>
int main(int argc, char **argv) {
    if (argc < 4) return 2;
    gb::OriginalAudio audio(gb::read_file(argv[1]));
    int effect = std::stoi(argv[2]), ticks = std::stoi(argv[3]);
    if (argc >= 6) audio.set_engine(uint8_t(std::stoi(argv[4])), uint8_t(std::stoi(argv[5])));
    if (effect >= 0) audio.play(uint8_t(effect));
    for (int tick = 0; tick < ticks; ++tick) {
        if (effect < 0) {
            if (tick % 11 == 0) audio.set_engine(uint8_t(tick * 13), uint8_t(tick * 17));
            if (tick % 47 == 0) audio.play(uint8_t((tick / 47) % 13));
            audio.set_muted(tick % 149 >= 137);
        }
        audio.tick();
        auto s = audio.state();
        std::cout << s.divisor << ',' << s.on << ',' << s.active << ',' << s.channel << ','
                  << s.pointer << ',' << s.remaining << ',' << s.tempo << ',' << s.tempoOffset << ',' << s.loop;
        for (int p = 0xda48; p < 0xdb1e; ++p) std::cout << ',' << unsigned(audio.debug_byte(uint16_t(p)));
        std::cout << '\n';
    }
}
