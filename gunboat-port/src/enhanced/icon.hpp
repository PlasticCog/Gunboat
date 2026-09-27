#pragma once
// The window's icon (src/enhanced/icon.cpp). The player's own title screen, shrunk: made from their
// game files the first time the game shows it (the game tells the host, host_title_shown) and kept in
// the settings folder as title_icon.bmp for the next starts. Nothing of the original is distributed:
// until then, and with no game files, it is the port's own icon (tools/make_icon.py), which the
// executables also carry as their file icon.
#include "types.hpp"

namespace gb {

// Sets the window's icon (the kept title screen, else the port's own) and follows the title.
void icon_install();
// The presenter's composed frame (XRGB, w x h): when the title screen has just been shown, it becomes
// the icon.
void icon_frame(const u32 *xrgb, int w, int h);

} // namespace gb
