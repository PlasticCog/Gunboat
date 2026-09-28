// Controller mapping: the controls, the game's keys and the file (controls.hpp).
#include "enhanced/controls.hpp"

#include <cstdlib>
#include <fstream>

namespace gb {

namespace {

struct ControlInfo {
    const char *name, *id;
};
const ControlInfo CONTROLS[CONTROL_COUNT] = {
    {"A", "a"}, {"B", "b"}, {"X", "x"}, {"Y", "y"}, {"Left bumper (LB)", "lb"}, {"Right bumper (RB)", "rb"},
    {"Left trigger (LT)", "lt"}, {"Right trigger (RT)", "rt"}, {"View / Back", "back"}, {"Menu / Start", "start"},
    {"Left stick click", "left_stick_click"}, {"Right stick click", "right_stick_click"},
    {"D-pad up", "dpad_up"}, {"D-pad down", "dpad_down"}, {"D-pad left", "dpad_left"}, {"D-pad right", "dpad_right"},
    {"Left stick up", "left_stick_up"}, {"Left stick down", "left_stick_down"}, {"Left stick left", "left_stick_left"},
    {"Left stick right", "left_stick_right"}, {"Right stick up", "right_stick_up"},
    {"Right stick down", "right_stick_down"}, {"Right stick left", "right_stick_left"},
    {"Right stick right", "right_stick_right"},
};

constexpr SDL_Scancode NO = SDL_SCANCODE_UNKNOWN;

// The game's keys (simulation.md §3: the key handlers; the manual's controls).
const Action ACTIONS[] = {
    {"up", "Move", "Up: throttle up (pilot), aim up (guns), menus", {SDL_SCANCODE_UP, NO}},
    {"down", "Move", "Down: throttle down (pilot), aim down (guns), menus", {SDL_SCANCODE_DOWN, NO}},
    {"left", "Move", "Left: steer left (pilot), aim left (guns), menus", {SDL_SCANCODE_LEFT, NO}},
    {"right", "Move", "Right: steer right (pilot), aim right (guns), menus", {SDL_SCANCODE_RIGHT, NO}},
    {"enter", "Weapons", "Enter: fire (guns), slow down (pilot), select", {SDL_SCANCODE_RETURN, NO}},
    {"f10", "Weapons", "F10: crew, open fire / cease fire", {SDL_SCANCODE_F10, NO}},
    {"f9", "Weapons", "F9: identify target", {SDL_SCANCODE_F9, NO}},
    {"x", "Stations", "X: pilot, look ahead", {SDL_SCANCODE_X, NO}},
    {"z", "Stations", "Z: pilot, look left", {SDL_SCANCODE_Z, NO}},
    {"c", "Stations", "C: pilot, look right", {SDL_SCANCODE_C, NO}},
    {"v", "Stations", "V: bow gun", {SDL_SCANCODE_V, NO}},
    {"n", "Stations", "N: midship gun", {SDL_SCANCODE_N, NO}},
    {"b", "Stations", "B: stern gun", {SDL_SCANCODE_B, NO}},
    {"comma", "Stations", ", : chase view", {SDL_SCANCODE_COMMA, NO}},
    {"m", "Stations", "M: map", {SDL_SCANCODE_M, NO}},
    {"slash", "Stations", "/ : damage report", {SDL_SCANCODE_SLASH, NO}},
    {"period", "Stations", ". : mission assignment", {SDL_SCANCODE_PERIOD, NO}},
    {"f1", "Crew", "F1: main switch (pilot), panel switch (guns)", {SDL_SCANCODE_F1, NO}},
    {"f2", "Crew", "F2: engines (pilot), panel switch (guns)", {SDL_SCANCODE_F2, NO}},
    {"f3", "Crew", "F3: panel switch", {SDL_SCANCODE_F3, NO}},
    {"f4", "Crew", "F4: pilot, reverse course", {SDL_SCANCODE_F4, NO}},
    {"f5", "Crew", "F5: pilot, branch left", {SDL_SCANCODE_F5, NO}},
    {"f6", "Crew", "F6: pilot, branch right", {SDL_SCANCODE_F6, NO}},
    {"f7", "Crew", "F7: pilot, slower", {SDL_SCANCODE_F7, NO}},
    {"f8", "Crew", "F8: pilot, faster", {SDL_SCANCODE_F8, NO}},
    {"space", "Game", "Space: continue (screens and menus)", {SDL_SCANCODE_SPACE, NO}},
    {"esc", "Game", "Esc: pause", {SDL_SCANCODE_ESCAPE, NO}},
    {"backspace", "Game", "Backspace (hold): fast forward", {SDL_SCANCODE_BACKSPACE, NO}},
    {"plus", "Game", "+ : time compression", {SDL_SCANCODE_EQUALS, NO}},
    {"minus", "Game", "- : control rate", {SDL_SCANCODE_MINUS, NO}},
    {"tab", "Game", "Tab: return to base", {SDL_SCANCODE_TAB, NO}},
    {"d", "Game", "D: detail level", {SDL_SCANCODE_D, NO}},
    {"s", "Game", "S: sound on / off", {SDL_SCANCODE_S, NO}},
    {"ctrl_q", "Game", "Ctrl+Q: quit the game", {SDL_SCANCODE_LCTRL, SDL_SCANCODE_Q}},
    {"f11", "Game", "F11: enhanced / original picture", {SDL_SCANCODE_F11, NO}},
    {"f12", "Game", "F12: screenshot", {SDL_SCANCODE_F12, NO}},
    {"y", "Keys", "Y (answers)", {SDL_SCANCODE_Y, NO}},
    {"key_n", "Keys", "N (answers; the midship gun in a mission)", {SDL_SCANCODE_N, NO}},
    {"a", "Keys", "A", {SDL_SCANCODE_A, NO}},
    {"e", "Keys", "E", {SDL_SCANCODE_E, NO}},
    {"q", "Keys", "Q", {SDL_SCANCODE_Q, NO}},
    {"1", "Keys", "1", {SDL_SCANCODE_1, NO}},
    {"2", "Keys", "2", {SDL_SCANCODE_2, NO}},
    {"3", "Keys", "3", {SDL_SCANCODE_3, NO}},
};
constexpr int ACTIONS_N = int(sizeof ACTIONS / sizeof ACTIONS[0]);

std::string trim(const std::string &s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

} // namespace

const char *control_name(int c) { return c >= 0 && c < CONTROL_COUNT ? CONTROLS[c].name : "?"; }
const char *control_id(int c) { return c >= 0 && c < CONTROL_COUNT ? CONTROLS[c].id : "?"; }
int action_count() { return ACTIONS_N; }
const Action &action(int i) { return ACTIONS[i]; }

int action_index(const std::string &id)
{
    for (int i = 0; i < ACTIONS_N; i++)
        if (id == ACTIONS[i].id) return i;
    return -1;
}

// The defaults, for the Xbox layout: the left stick and the D-pad move; A fires and selects, B
// continues; Y the map, X the crew's fire at will; the right stick the gun stations and the chase
// view; the bumpers the pilot's looks left and right, the right stick's click ahead; the right trigger
// fast forward, the left trigger time compression; View the damage report, Menu the pause, the left
// stick's click the mission assignment.
void controls_defaults(ControllerMap &m)
{
    for (int &a : m.action) a = -1;
    auto set = [&](int c, const char *id) { m.action[c] = action_index(id); };
    set(PAD_A, "enter");
    set(PAD_B, "space");
    set(PAD_X, "f10");
    set(PAD_Y, "m");
    set(PAD_LB, "z");
    set(PAD_RB, "c");
    set(PAD_LT, "plus");
    set(PAD_RT, "backspace");
    set(PAD_BACK, "slash");
    set(PAD_START, "esc");
    set(PAD_LS, "period");
    set(PAD_RS, "x");
    set(PAD_UP, "up");
    set(PAD_DOWN, "down");
    set(PAD_LEFT, "left");
    set(PAD_RIGHT, "right");
    set(LS_UP, "up");
    set(LS_DOWN, "down");
    set(LS_LEFT, "left");
    set(LS_RIGHT, "right");
    set(RS_UP, "v");
    set(RS_DOWN, "b");
    set(RS_LEFT, "n");
    set(RS_RIGHT, "comma");
    m.deadzone = 50;
}

bool controls_load(const std::string &path, ControllerMap &m)
{
    controls_defaults(m);
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k == "deadzone") {
            const int d = std::atoi(v.c_str());
            m.deadzone = d < 10 ? 10 : d > 90 ? 90 : d;
            continue;
        }
        for (int c = 0; c < CONTROL_COUNT; c++)
            if (k == CONTROLS[c].id) m.action[c] = v == "none" ? -1 : action_index(v);
    }
    return true;
}

bool controls_save(const std::string &path, const ControllerMap &m)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "# Gunboat: the controller's mapping (edit with gunboat_controller). Each control presses one of\n"
           "# the game's keys; none = nothing. deadzone: percent of a stick's travel that presses a direction.\n\n";
    out << "deadzone = " << m.deadzone << "\n";
    for (int c = 0; c < CONTROL_COUNT; c++)
        out << CONTROLS[c].id << " = " << (m.action[c] >= 0 ? ACTIONS[m.action[c]].id : "none") << "\n";
    return bool(out);
}

std::string controls_path()
{
    char *dir = SDL_GetPrefPath("", "Gunboat");
    std::string p = dir ? std::string(dir) + "controller.ini" : std::string("controller.ini");
    SDL_free(dir);
    return p;
}

} // namespace gb
