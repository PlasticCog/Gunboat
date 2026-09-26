// Small helpers (platform.md §8).
#include "platform/platform.hpp"

#include "host.hpp"
#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

// 0000:0780 random (platform.md §8, simulation.md §11.1): the 32-bit LCG on rng_state, through the
// runtime's _aFlmul (low 32 bits of the product). Returns AX = bits 16..30 of the new state; DX,
// the unmasked high word, is read by no caller.
u16 random()
{
    const u32 state = ds_u32(DS_rng_state) * 0x41C64E6Du + 0x3039u;
    ds_u32(DS_rng_state) = state;
    return u16(state >> 16) & 0x7FFF;
}

// 121b:036e world_a_base: the DGROUP offset of DAT6.DAT's copy (texts and tables are relative to it).
u16 world_a_base() { return 0x6E54; }

// 121b:0372 pit_random: base + the low byte of PIT channel 2's counter (IN 42h), a value that
// depends on the moment (the headquarters' question).
u16 pit_random(u16 base) { return u16(base + host_pit2_low()); }

} // namespace gb
