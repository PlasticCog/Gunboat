#pragma once
// The AdLib sound effects in the game (sfx_fm.hpp): the effects driver's speaker notes played on FM
// instruments from the player's bank (sfx.ini).
namespace gb {

// Loads the bank and hooks the host's speaker (settings: effects = adlib).
void sfx_adlib_install();

} // namespace gb
