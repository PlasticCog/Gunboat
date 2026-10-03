#pragma once
// The AdLib sound effects in the game (sfx_fm.hpp): the effects driver's speaker notes played on FM
// instruments from the player's bank (sfx.ini).
namespace gb {

// Loads the bank and hooks the host's speaker (settings: effects = adlib).
void sfx_adlib_install();
void sfx_adlib_reset();  // every effect's own copy stopped (after a quickload); nothing if not installed

} // namespace gb
