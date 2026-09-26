#include "original_weapons.hpp"
#include <iostream>
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    gb::original::Weapons weapons(gb::read_file(argv[1]));
    for (unsigned i = 0; i < 2000; ++i) {
        gb::original::WeaponState s;
        int station = 2 + int(i % 3);
        for (unsigned m = 0; m < 3; ++m) {
            s.loadout[m] = uint8_t((i / 3 + m) % 3);
            s.aim[m] = uint8_t(i * 37 + m * 71);
            s.heading[m] = uint8_t(i * 13 + m * 79);
            s.headingFraction[m] = uint8_t(i * 11 + m * 3);
            s.condition[m][0] = uint8_t(i % 17 == 0);
            s.condition[m][1] = uint8_t(i % 19 == 0);
        }
        s.reload4 = i % 7 ? 8 : uint8_t(i);
        s.reload3 = i % 11 ? 48 : uint8_t(i);
        s.alternatingBarrel = uint8_t(i * 3);
        s.timingMode = uint8_t(i % 5);
        s.frameCounter = uint8_t(i);
        s.cameraPitch = uint8_t(i * 3); s.referencePitch = uint8_t(i * 17);
        s.boatX = uint16_t(i * 973); s.boatY = uint16_t(i * 829);
        for (unsigned p = 0; p < 32; ++p) s.projectiles[p].ticks = uint8_t(i % 4 == 0 || p < i % 32);
        auto shot = weapons.fire(s, station);
        if (argc >= 3) {
            weapons.reload_tick(s);
            weapons.flash_tick(s);
            weapons.projectile_tick(s, i % 3 == 0);
        }
        std::cout << i << ',' << bool(shot) << ',' << (shot ? unsigned(shot->sound) : 0)
                  << ',' << (shot ? unsigned(shot->weapon) : 0) << ',' << (shot ? unsigned(shot->slot) : 0)
                  << ',' << unsigned(s.reload4) << ',' << unsigned(s.reload3) << ',' << unsigned(s.alternatingBarrel);
        for (auto v : s.flash) std::cout << ',' << unsigned(v);
        for (const auto &p : s.projectiles)
            std::cout << ',' << unsigned(p.ticks) << ',' << unsigned(p.kind) << ',' << p.x << ',' << p.y
                      << ',' << unsigned(p.headingFraction) << ',' << unsigned(p.heading) << ',' << unsigned(p.range);
        std::cout << '\n';
    }
}
