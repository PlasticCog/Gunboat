#pragma once
// Simulation (simulation.md): one function per original function. Near assembly routines with
// register arguments take and return the registers their callers use.
#include "types.hpp"

namespace gb {

u16 vec_scale(u8 al);          // 0919:3706  returns AX
u16 heading_vector(u8 angle);  // 0919:7e5f  AL = angle, returns AX
void boat_move();              // 0919:7ee8

} // namespace gb
