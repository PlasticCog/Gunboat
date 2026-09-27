#pragma once
// Controller support (src/enhanced/): an Xbox-style controller plays the game by pressing its keys.
// Each control (a button, a trigger, a direction of a stick or of the D-pad) is mapped to one of the
// game's keys, and pressing the control presses the key, as the player would on the keyboard: the game
// itself is unchanged. The mapping is controller.ini in the settings folder (next to gunboat.ini),
// written by the mapping tool gunboat_controller (tools/controller_editor); without it the defaults
// below. The game (controller.cpp) and the tool share this file.
#include <SDL3/SDL.h>

#include <string>

namespace gb {

enum Control : int {
    PAD_A, PAD_B, PAD_X, PAD_Y, PAD_LB, PAD_RB, PAD_LT, PAD_RT, PAD_BACK, PAD_START, PAD_LS, PAD_RS,
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,                    // the D-pad
    LS_UP, LS_DOWN, LS_LEFT, LS_RIGHT, RS_UP, RS_DOWN, RS_LEFT, RS_RIGHT,  // the sticks' directions
    CONTROL_COUNT
};
const char *control_name(int c);  // "A", "Left stick up"
const char *control_id(int c);    // its key in controller.ini: "a", "left_stick_up"

// A game key a control can press: its id in controller.ini ("enter", "f10", "ctrl_q"), what it does in
// the game, and the key(s) pressed together.
struct Action {
    const char *id;
    const char *group;  // for the tool's lists: "Move", "Weapons", "Stations", "Crew", "Game", "Keys"
    const char *label;
    SDL_Scancode keys[2];
};
int action_count();
const Action &action(int i);
int action_index(const std::string &id);  // -1: none / unknown

struct ControllerMap {
    int action[CONTROL_COUNT];  // action index per control, -1 = nothing
    int deadzone = 50;          // percent of a stick's travel that presses its direction
};
void controls_defaults(ControllerMap &m);
// Reads a mapping (the defaults for what the file lacks); false when there is no file.
bool controls_load(const std::string &path, ControllerMap &m);
bool controls_save(const std::string &path, const ControllerMap &m);
std::string controls_path();  // controller.ini in the settings folder

} // namespace gb
