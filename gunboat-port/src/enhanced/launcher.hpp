#pragma once
// The launcher (src/enhanced/launcher.cpp): a settings screen in the game's window before the game
// starts: the game folder, the enhancements (each on or off, or the Original / Enhanced presets), the
// picture, the display and the sound device. Keyboard, mouse or gamepad.
#include "enhanced/settings.hpp"

namespace gb {

// Runs the launcher in the host's window (after host_init). Returns false if the player quits.
bool launcher_run(Settings &s);

// Whether `dir` holds a GB.EXE this port can run; `why` says what is wrong otherwise.
bool launcher_check_game_dir(const std::string &dir, std::string &why);

} // namespace gb
