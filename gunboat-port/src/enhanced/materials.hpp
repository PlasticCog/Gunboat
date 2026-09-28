#pragma once
// What the objects are made of, for the enhancements that sound or show a bullet's hit (the AdLib
// impact effects, sfx_adlib.cpp; the impact debris, debris.cpp): one table, so the two agree. From the
// object kinds (world.md §6.3) and the region, which names some kinds differently (Vietnam is region 0,
// the practice world 3). Reads the game's memory only.
#include "types.hpp"

namespace gb {

enum class Material : u8 {
    Metal,   // tanks, APCs, powerboats, the machine gun, the missile, mines, trucks, helicopters, cars,
             // the PBR, buoys, the capsized boat
    Wood,    // huts, docks, camps and caches, houses, the practice target; sampans and junks in Vietnam
    Tree,    // trees and the stump
    Bridge,  // the bridges' bases
    Stone,   // fortifications, statues, rubble, rocks
    Sand,    // the mortar nest's sandbags
    Flesh,   // people, bodies, the water buffalo and the dead beast (Vietnam)
};

Material material_of(u8 kind);

} // namespace gb
