// What the objects are made of (materials.hpp).
#include "enhanced/materials.hpp"

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

Material material_of(u8 kind)
{
    const u16 region = ds_u16(DS_region);
    const bool vietnam = region == 0;
    if ((kind >= 0x0A && kind <= 0x0C) || (kind == 0x14 && (vietnam || region == 3)) || kind == 0x19 ||
        kind == 0x1A || (kind >= 0x24 && kind <= 0x26) || (kind >= 0x33 && kind <= 0x35) ||
        (vietnam && (kind == 0x2F || kind == 0x32)))
        return Material::Flesh;  // people, bodies, the buffalo and the dead beast
    if ((kind >= 0x04 && kind <= 0x06) || kind == 0x1E || kind == 0x1F)
        return vietnam ? Material::Wood : Material::Metal;  // sampans and junks, else powerboats and skiffs
    switch (kind) {
    case 0x11: case 0x21: return Material::Bridge;
    case 0x01: case 0x02: case 0x03: case 0x07: case 0x12: case 0x13: case 0x14: case 0x17: case 0x18:
    case 0x22: case 0x23: case 0x28: case 0x29: case 0x37: case 0x38:
        return Material::Metal;  // armour, the guns, the missile, mines, trucks, helicopters, cars, buoys
    case 0x08: return Material::Sand;  // the mortar nest's sandbags
    case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: return Material::Tree;  // trees, the stump
    case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x16: case 0x1B: case 0x1C: case 0x1D: case 0x20:
        return Material::Wood;  // caches, huts, docks, the practice target, houses
    default: return Material::Stone;  // forts, statues, rubble, rocks
    }
}

} // namespace gb
