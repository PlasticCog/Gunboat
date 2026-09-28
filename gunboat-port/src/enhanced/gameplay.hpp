#pragma once
// Gameplay changes: options that change the game itself, unlike the enhancements, which only change
// what is shown. Each is off unless the player turns it on (launcher "Gameplay changes"); off, the
// game is the original's and the port's tests stand.
//
// Hills stop bullets: in the original a shot hits whatever it lands on, whatever lies between (the
// hit test sees the objects only). With this option on, the terrain the game has loaded (the 3 x 3
// cells around the boat) stops a shot whose line from the gun to its target passes under the ground:
// nothing behind the hill is hit, and the shot's debris flies off the hill; a grenade or mortar shell
// bursts at the hill's foot.
namespace gb {

struct Settings;
void gameplay_install(const Settings &s);

// The ground's height at (x, y) quarter units: the highest of the loaded terrain's triangles over the
// point (group A, then group B, as the renderer takes them), 0 (the water) where there is none. Reads
// the game's memory only (also used by the impact debris, which is not a gameplay change).
double terrain_height(double x, double y);

} // namespace gb
