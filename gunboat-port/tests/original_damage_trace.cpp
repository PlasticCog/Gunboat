#include "original_weapons.hpp"
#include <iostream>
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    gb::original::Weapons weapons(gb::read_file(argv[1]));
    for (unsigned c = 0; c < 256; ++c) for (unsigned w = 1; w <= 5; ++w) for (unsigned d = 0; d < 8; ++d) {
        uint8_t kind = uint8_t(1 + ((c + w + d) % 47)), flags = uint8_t((c & 0xc7) | (d << 3));
        auto result = weapons.hit(kind, flags, uint8_t(c), uint8_t(w));
        std::cout << unsigned(kind) << ',' << unsigned(flags) << ',' << c << ',' << w << ','
                  << unsigned(result.kind) << ',' << unsigned(result.flags) << ',' << unsigned(result.sound) << '\n';
    }
}
