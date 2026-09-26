#pragma once
// The mission segment 05bd (world.md; hud.md §5-6): the mission loop, the world loading and the
// per-frame presentation of the view. One function per original function; the station screens of
// 05bd that package H ported are in hud/hud.hpp.
#include "types.hpp"

namespace gb {

// ---- the present (view_present.cpp), hud.md §6
void view_present(u16 src_page, u16 dst_page);  // 05bd:1fd4  far C; mission_run (1, 0), view_restore (1, view_page)

} // namespace gb
