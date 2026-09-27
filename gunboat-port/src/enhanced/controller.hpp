#pragma once
// Controller support in the game (controls.hpp): the controller's controls press the game's keys as
// controller.ini maps them.
namespace gb {

// Loads the mapping and follows the host's controller events.
void controller_install();

} // namespace gb
