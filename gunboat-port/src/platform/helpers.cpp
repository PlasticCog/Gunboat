// Small helpers (platform.md §8).
#include "platform/platform.hpp"

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

} // namespace gb
