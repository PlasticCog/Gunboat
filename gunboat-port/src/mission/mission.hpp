#pragma once
// The mission segment 05bd (world.md; hud.md §5-6): the mission loop, the world loading and the
// per-frame presentation of the view. One function per original function; the station screens of
// 05bd that package H ported are in hud/hud.hpp.
#include "types.hpp"

namespace gb {

// ---- the present (view_present.cpp), hud.md §6
u16 view_present(u16 src_page, u16 dst_page, u16 si);  // 05bd:1fd4  far C; mission_run (1, 0), view_restore (1, view_page)

// ---- the mission loop and the station screens (mission_run.cpp), world.md §3, hud.md §5. The
// screens and view_restore take the loop's SI on to game_frame and return the SI they leave
// (view_present's copy).
void mission_run();                 // 05bd:000a  far C
void mission_loop(u16 si);          // PORT: mission_run's loop and end (a quicksave's resume enters it)
u16 pilot_screen(u16 si);          // 05bd:048c
u16 chase_view_screen(u16 si);     // 05bd:07da
u16 bow_screen(u16 si);            // 05bd:08be
u16 stern_screen(u16 si);          // 05bd:0e9e
u16 midship_screen(u16 si);        // 05bd:10f4
u16 view_restore(u16 si);          // 05bd:144a

} // namespace gb
