#pragma once
// Functions the ported code calls whose port is not in this tree yet (pending.cpp): each is fatal
// if reached, and goes away when its real port arrives.
#include "types.hpp"

namespace gb {

void mission_run();                  // 05bd:000a
void key_f4_reverse_course();        // 0919:0667  (calls route_point 0919:8754 and route_advance 0919:87e8)

} // namespace gb
