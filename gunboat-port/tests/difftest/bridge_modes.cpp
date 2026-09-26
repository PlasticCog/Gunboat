// Bridge entries: the video modes other than VGA (video.md §6): routines of the EGA, CGA, Tandy and
// Hercules paths that have no VGA twin in the other bridge files.
#include "bridge.hpp"
#include "platform/platform.hpp"

using namespace gb;

BRIDGE(hercules_setup) { hercules_setup(); }
