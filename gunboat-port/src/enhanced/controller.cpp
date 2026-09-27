// Controller support in the game (controller.hpp): each control of the controller presses the key it is
// mapped to (controls.hpp), through the host as the keyboard does (host_key: the game's keyboard
// interrupt, the presentation layer's hotkeys). A stick's or trigger's direction presses its key past
// the dead zone and lets it go a little below it (so that it does not chatter at the edge); a key that
// two controls press (the D-pad and the left stick) is let go when both have let go of it.
#include "enhanced/controller.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include "enhanced/controls.hpp"
#include "host.hpp"

namespace gb {

namespace {

ControllerMap mapping;
bool pressed[CONTROL_COUNT];
int holding[SDL_SCANCODE_COUNT];  // how many controls hold each key

void press(int c, bool down)
{
    if (pressed[c] == down) return;
    pressed[c] = down;
    const int a = mapping.action[c];
    if (a < 0) return;
    const Action &act = action(a);
    if (down) {
        for (SDL_Scancode k : act.keys)
            if (k != SDL_SCANCODE_UNKNOWN && holding[k]++ == 0) host_key(int(k), true);
    } else {
        for (int i = 1; i >= 0; i--) {
            const SDL_Scancode k = act.keys[i];
            if (k != SDL_SCANCODE_UNKNOWN && holding[k] > 0 && --holding[k] == 0) host_key(int(k), false);
        }
    }
}

void release_all()
{
    for (int c = 0; c < CONTROL_COUNT; c++) press(c, false);
}

// A direction of an axis: pressed past `on`, let go below 70% of it.
void direction(int c, int value, int on)
{
    if (!pressed[c] && value > on) press(c, true);
    else if (pressed[c] && value < on * 7 / 10) press(c, false);
}

void on_event(const SDL_Event &ev)
{
    switch (ev.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const bool down = ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        int c = -1;
        switch (ev.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: c = PAD_A; break;
        case SDL_GAMEPAD_BUTTON_EAST: c = PAD_B; break;
        case SDL_GAMEPAD_BUTTON_WEST: c = PAD_X; break;
        case SDL_GAMEPAD_BUTTON_NORTH: c = PAD_Y; break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: c = PAD_LB; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: c = PAD_RB; break;
        case SDL_GAMEPAD_BUTTON_BACK: c = PAD_BACK; break;
        case SDL_GAMEPAD_BUTTON_START: c = PAD_START; break;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: c = PAD_LS; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: c = PAD_RS; break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: c = PAD_UP; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: c = PAD_DOWN; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: c = PAD_LEFT; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: c = PAD_RIGHT; break;
        default: break;
        }
        if (c >= 0) press(c, down);
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const int v = ev.gaxis.value, on = 32767 * mapping.deadzone / 100;
        switch (ev.gaxis.axis) {
        case SDL_GAMEPAD_AXIS_LEFTX: direction(LS_RIGHT, v, on); direction(LS_LEFT, -v, on); break;
        case SDL_GAMEPAD_AXIS_LEFTY: direction(LS_DOWN, v, on); direction(LS_UP, -v, on); break;
        case SDL_GAMEPAD_AXIS_RIGHTX: direction(RS_RIGHT, v, on); direction(RS_LEFT, -v, on); break;
        case SDL_GAMEPAD_AXIS_RIGHTY: direction(RS_DOWN, v, on); direction(RS_UP, -v, on); break;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: direction(PAD_LT, v, 16000); break;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: direction(PAD_RT, v, 16000); break;
        default: break;
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_REMOVED:
        release_all();
        break;
    default:
        break;
    }
}

// Developer aid: GB_PAD="<seconds>:<control>[/p|/r],..." presses the control named as in controller.ini
// ("dpad_down", "a", "left_stick_up") at that many seconds after start-up and lets it go 0.2 s later
// (/p: only presses, /r: only lets go), as controller events in SDL's queue: the whole way a real
// controller's presses take.
Uint32 SDLCALL pad_script(void *event, SDL_TimerID, Uint32)
{
    SDL_PushEvent(static_cast<SDL_Event *>(event));
    return 0;
}

void schedule(const SDL_Event &ev, double seconds)
{
    SDL_AddTimer(Uint32(seconds * 1000), pad_script, new SDL_Event(ev));  // (a few, kept to the end)
}

void script(const char *spec)
{
    const std::string s = spec;
    size_t at = 0;
    while (at < s.size()) {
        size_t end = s.find(',', at);
        if (end == std::string::npos) end = s.size();
        const std::string item = s.substr(at, end - at);
        at = end + 1;
        const size_t colon = item.find(':');
        if (colon == std::string::npos) continue;
        const double t = std::atof(item.c_str());
        std::string name = item.substr(colon + 1);
        char mode = 0;
        if (!name.empty() && (name.back() == 'p' || name.back() == 'r') && name.size() > 1 &&
            name[name.size() - 2] == '/') {
            mode = name.back();
            name.resize(name.size() - 2);
        }
        int c = -1;
        for (int k = 0; k < CONTROL_COUNT; k++)
            if (name == control_id(k)) c = k;
        if (c < 0) continue;
        static const SDL_GamepadButton BUTTONS[] = {
            SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_INVALID,
            SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START,
            SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_DPAD_UP,
            SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
        auto event = [&](bool down) {
            SDL_Event ev{};
            if (c < int(sizeof BUTTONS / sizeof BUTTONS[0]) && BUTTONS[c] != SDL_GAMEPAD_BUTTON_INVALID) {
                ev.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
                ev.gbutton.button = u8(BUTTONS[c]);
                ev.gbutton.down = down;
            } else {
                ev.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
                SDL_GamepadAxis axis = SDL_GAMEPAD_AXIS_LEFTX;
                int v = down ? 32767 : 0;
                switch (c) {
                case PAD_LT: axis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER; break;
                case PAD_RT: axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER; break;
                case LS_UP: axis = SDL_GAMEPAD_AXIS_LEFTY; v = -v; break;
                case LS_DOWN: axis = SDL_GAMEPAD_AXIS_LEFTY; break;
                case LS_LEFT: axis = SDL_GAMEPAD_AXIS_LEFTX; v = -v; break;
                case LS_RIGHT: axis = SDL_GAMEPAD_AXIS_LEFTX; break;
                case RS_UP: axis = SDL_GAMEPAD_AXIS_RIGHTY; v = -v; break;
                case RS_DOWN: axis = SDL_GAMEPAD_AXIS_RIGHTY; break;
                case RS_LEFT: axis = SDL_GAMEPAD_AXIS_RIGHTX; v = -v; break;
                default: axis = SDL_GAMEPAD_AXIS_RIGHTX; break;
                }
                ev.gaxis.axis = u8(axis);
                ev.gaxis.value = s16(std::max(-32768, std::min(32767, v)));
            }
            return ev;
        };
        if (mode != 'r') schedule(event(true), t);
        if (mode != 'p') schedule(event(false), mode == 'r' ? t : t + 0.2);
    }
}

} // namespace

void controller_install()
{
    controls_load(controls_path(), mapping);
    host_set_gamepad_handler(on_event);
    if (const char *spec = SDL_getenv("GB_PAD")) script(spec);
}

} // namespace gb
