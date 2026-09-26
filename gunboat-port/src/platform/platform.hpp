#pragma once
// Platform (platform.md): timers, input, files, memory, LZW, text and small helpers. One function
// per original function; the host layer (host.hpp) stands in for the hardware and DOS.
#include "types.hpp"

namespace gb {

u16 random();  // 0000:0780

} // namespace gb
