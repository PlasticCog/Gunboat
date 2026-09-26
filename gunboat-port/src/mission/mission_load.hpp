#pragma once
// Mission loading (world.md §4, §5): the files of a mission and its start state. One function per
// original function.
#include "types.hpp"

namespace gb {

void mission_load();   // 05bd:14d6  far C
void mission_setup();  // 0919:3d78  far

} // namespace gb
