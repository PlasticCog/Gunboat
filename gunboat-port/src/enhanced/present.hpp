#pragma once
// The enhanced presentation (src/enhanced/present.cpp): the host's picture replaced by one drawn at
// the window's resolution. The 320x200 frame the game drew is shown with the player's aspect and
// filter; with the enhancements on, the 3D view seen through the cockpit is drawn again at the
// window's resolution (view3d), between the game's frames when smooth motion is on, and beside the
// picture in a wide window. F11 switches between the enhanced and the original picture.
#include "enhanced/settings.hpp"

namespace gb {

// Installs the presenter, the frame hook and the hotkeys in the host (after host_init) and sizes the
// window for the settings.
void enhanced_install(const Settings &s);

// Developer check (GB_VIEW_CHECK=1): each captured frame's view drawn again at the original's size
// and compared with the original's pixels; the totals are printed at exit.
void enhanced_report();

} // namespace gb
