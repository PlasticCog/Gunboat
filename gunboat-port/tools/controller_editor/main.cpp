// gunboat_controller: the controller mapping of the Gunboat port (src/enhanced/controls.hpp).
//
// Each control of an Xbox-style controller (buttons, triggers, the directions of the sticks and of the
// D-pad) presses one of the game's keys; this tool chooses which, shows the controller with every
// control's key and lights up what is pressed, and saves controller.ini in the settings folder, which
// the game reads when it starts. Pressing a control selects its row.
//
// C++, SDL3 and Dear ImGui (vendor/imgui, MIT).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "enhanced/controls.hpp"
#include "enhanced/icon_image.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

using namespace gb;

namespace {

ControllerMap mapping, saved;
SDL_Gamepad *pad;
int selected = PAD_A;
bool scroll_to_selected;
bool live[CONTROL_COUNT];  // pressed now
std::string status;
Uint64 status_until;

void say(const std::string &s)
{
    status = s;
    status_until = SDL_GetTicksNS() + 4 * SDL_NS_PER_SECOND;
}

bool dirty()
{
    if (mapping.deadzone != saved.deadzone) return true;
    for (int c = 0; c < CONTROL_COUNT; c++)
        if (mapping.action[c] != saved.action[c]) return true;
    return false;
}

void save()
{
    const std::string p = controls_path();
    if (controls_save(p, mapping)) {
        saved = mapping;
        say("Saved " + p + ". The game uses it from its next start.");
    } else {
        say("Could not write " + p);
    }
}

// The key a control presses, short ("Enter", "F10", ","), from its action's label.
std::string key_of(int c)
{
    const int a = mapping.action[c];
    if (a < 0) return "-";
    std::string s = action(a).label;
    const size_t colon = s.find(':'), paren = s.find(" (");
    const size_t end = std::min(colon, paren);
    if (end != std::string::npos) s = s.substr(0, end);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// What is pressed now (the game's rules: a stick direction past the dead zone, a trigger past half).
void read_pad()
{
    for (bool &b : live) b = false;
    if (!pad) return;
    auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
    live[PAD_A] = btn(SDL_GAMEPAD_BUTTON_SOUTH);
    live[PAD_B] = btn(SDL_GAMEPAD_BUTTON_EAST);
    live[PAD_X] = btn(SDL_GAMEPAD_BUTTON_WEST);
    live[PAD_Y] = btn(SDL_GAMEPAD_BUTTON_NORTH);
    live[PAD_LB] = btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    live[PAD_RB] = btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    live[PAD_BACK] = btn(SDL_GAMEPAD_BUTTON_BACK);
    live[PAD_START] = btn(SDL_GAMEPAD_BUTTON_START);
    live[PAD_LS] = btn(SDL_GAMEPAD_BUTTON_LEFT_STICK);
    live[PAD_RS] = btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    live[PAD_UP] = btn(SDL_GAMEPAD_BUTTON_DPAD_UP);
    live[PAD_DOWN] = btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    live[PAD_LEFT] = btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    live[PAD_RIGHT] = btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    const int on = 32767 * mapping.deadzone / 100;
    auto axis = [&](SDL_GamepadAxis a) { return int(SDL_GetGamepadAxis(pad, a)); };
    live[PAD_LT] = axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000;
    live[PAD_RT] = axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
    live[LS_RIGHT] = axis(SDL_GAMEPAD_AXIS_LEFTX) > on;
    live[LS_LEFT] = -axis(SDL_GAMEPAD_AXIS_LEFTX) > on;
    live[LS_DOWN] = axis(SDL_GAMEPAD_AXIS_LEFTY) > on;
    live[LS_UP] = -axis(SDL_GAMEPAD_AXIS_LEFTY) > on;
    live[RS_RIGHT] = axis(SDL_GAMEPAD_AXIS_RIGHTX) > on;
    live[RS_LEFT] = -axis(SDL_GAMEPAD_AXIS_RIGHTX) > on;
    live[RS_DOWN] = axis(SDL_GAMEPAD_AXIS_RIGHTY) > on;
    live[RS_UP] = -axis(SDL_GAMEPAD_AXIS_RIGHTY) > on;
}

// ---- the controller, drawn

struct Spot {
    int control;
    float x, y;       // centre, in a 520 x 340 drawing
    float lx, ly;     // where its key is written (relative to the centre)
};
// the Xbox layout, in a W x H drawing
constexpr float W = 700, H = 340;
const Spot SPOTS[] = {
    {PAD_LT, 170, 14, -52, 0}, {PAD_RT, 530, 14, 52, 0}, {PAD_LB, 170, 44, -52, 0}, {PAD_RB, 530, 44, 52, 0},
    {PAD_Y, 540, 108, 22, -14}, {PAD_X, 510, 138, -22, 0}, {PAD_B, 570, 138, 22, 0}, {PAD_A, 540, 168, 22, 12},
    {PAD_BACK, 310, 132, 0, -24}, {PAD_START, 390, 132, 0, -24},
    {LS_UP, 170, 104, 0, -14}, {LS_DOWN, 170, 170, 0, 14}, {LS_LEFT, 136, 137, -12, 0}, {LS_RIGHT, 204, 137, 12, 0},
    {PAD_LS, 170, 137, 0, 0},
    {PAD_UP, 255, 208, 0, -18}, {PAD_DOWN, 255, 262, 0, 18}, {PAD_LEFT, 228, 235, -14, 0}, {PAD_RIGHT, 282, 235, 14, 0},
    {RS_UP, 445, 202, 0, -14}, {RS_DOWN, 445, 268, 0, 14}, {RS_LEFT, 411, 235, -12, 0}, {RS_RIGHT, 479, 235, 12, 0},
    {PAD_RS, 445, 235, 0, 0},
};

void draw_controller()
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float scale = std::max(0.5f, std::min(avail.x / W, (avail.y - 110) / H));
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("controller", ImVec2(W * scale, H * scale));
    const bool clicked = ImGui::IsItemClicked();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto P = [&](float x, float y) { return ImVec2(o.x + x * scale, o.y + y * scale); };
    const ImU32 body = IM_COL32(52, 56, 64, 255), edge = IM_COL32(95, 100, 112, 255);
    // the body: two grips and the middle
    dl->AddCircleFilled(P(195, 168), 105 * scale, body, 48);
    dl->AddCircleFilled(P(505, 168), 105 * scale, body, 48);
    dl->AddRectFilled(P(195, 70), P(505, 272), body, 40 * scale);
    dl->AddCircleFilled(P(165, 252), 72 * scale, body, 40);
    dl->AddCircleFilled(P(535, 252), 72 * scale, body, 40);
    dl->AddCircle(P(350, 100), 16 * scale, edge, 24, 2);  // the guide button (not used)
    // the controls
    for (const Spot &sp : SPOTS) {
        const int c = sp.control;
        const bool on = live[c], sel = c == selected;
        const ImU32 fill = on ? IM_COL32(255, 200, 60, 255) : sel ? IM_COL32(80, 130, 220, 255) : IM_COL32(30, 32, 38, 255);
        const ImVec2 at = P(sp.x, sp.y);
        float r = 13;
        switch (c) {
        case PAD_A: case PAD_B: case PAD_X: case PAD_Y: {
            const ImU32 ring = c == PAD_A ? IM_COL32(90, 200, 90, 255) : c == PAD_B ? IM_COL32(220, 70, 70, 255)
                             : c == PAD_X ? IM_COL32(80, 130, 230, 255) : IM_COL32(230, 200, 60, 255);
            dl->AddCircleFilled(at, r * scale, fill, 24);
            dl->AddCircle(at, r * scale, ring, 24, 2.5f);
            const char *t = c == PAD_A ? "A" : c == PAD_B ? "B" : c == PAD_X ? "X" : "Y";
            dl->AddText(ImVec2(at.x - 4, at.y - 7), ring, t);
            break;
        }
        case PAD_LB: case PAD_RB: case PAD_LT: case PAD_RT: {
            const float w = 44, h = c == PAD_LT || c == PAD_RT ? 22 : 14;
            dl->AddRectFilled(P(sp.x - w, sp.y - h / 2), P(sp.x + w, sp.y + h / 2), fill, 6 * scale);
            dl->AddRect(P(sp.x - w, sp.y - h / 2), P(sp.x + w, sp.y + h / 2), edge, 6 * scale);
            const char *t = c == PAD_LB ? "LB" : c == PAD_RB ? "RB" : c == PAD_LT ? "LT" : "RT";
            dl->AddText(ImVec2(at.x - 7, at.y - 7), IM_COL32(200, 200, 210, 255), t);
            r = 30;
            break;
        }
        case PAD_BACK: case PAD_START:
            r = 8;
            dl->AddCircleFilled(at, r * scale, fill, 16);
            dl->AddCircle(at, r * scale, edge, 16);
            break;
        case PAD_LS: case PAD_RS:
            r = 20;
            dl->AddCircleFilled(at, 30 * scale, IM_COL32(24, 26, 30, 255), 32);
            dl->AddCircleFilled(at, r * scale, fill, 32);
            dl->AddCircle(at, r * scale, edge, 32, 2);
            break;
        case PAD_UP: case PAD_DOWN: case PAD_LEFT: case PAD_RIGHT: {
            r = 11;
            dl->AddRectFilled(ImVec2(at.x - r * scale, at.y - r * scale), ImVec2(at.x + r * scale, at.y + r * scale), fill, 3);
            dl->AddRect(ImVec2(at.x - r * scale, at.y - r * scale), ImVec2(at.x + r * scale, at.y + r * scale), edge, 3);
            break;
        }
        default: {  // a stick's direction: a small triangle pointing out
            r = 7;
            const float dx = sp.lx > 0 ? 1.f : sp.lx < 0 ? -1.f : 0.f, dy = sp.ly > 0 ? 1.f : sp.ly < 0 ? -1.f : 0.f;
            const ImVec2 tip(at.x + dx * r * scale, at.y + dy * r * scale);
            const ImVec2 b1(at.x - dx * r * scale - dy * r * scale, at.y - dy * r * scale - dx * r * scale);
            const ImVec2 b2(at.x - dx * r * scale + dy * r * scale, at.y - dy * r * scale + dx * r * scale);
            dl->AddTriangleFilled(tip, b1, b2, on ? IM_COL32(255, 200, 60, 255) : sel ? IM_COL32(80, 130, 220, 255)
                                                                                : IM_COL32(150, 155, 165, 255));
            break;
        }
        }
        // the key it presses
        {
            const std::string k = key_of(c);
            const ImVec2 ts = ImGui::CalcTextSize(k.c_str());
            const float gx = sp.lx == 0 ? 0 : (sp.lx < 0 ? -1.f : 1.f) * (r * scale + 4) + sp.lx * scale;
            const float gy = sp.ly == 0 ? 0 : (sp.ly < 0 ? -1.f : 1.f) * (r * scale + 2) + sp.ly * scale;
            const float tx = at.x + gx, ty = at.y + gy;
            ImVec2 tp(tx - (sp.lx < 0 ? ts.x : sp.lx > 0 ? 0 : ts.x / 2), ty - (sp.ly < 0 ? ts.y : sp.ly > 0 ? 0 : ts.y / 2));
            dl->AddRectFilled(ImVec2(tp.x - 3, tp.y - 1), ImVec2(tp.x + ts.x + 3, tp.y + ts.y + 1),
                              sel ? IM_COL32(40, 70, 130, 230) : IM_COL32(15, 17, 22, 200), 3);
            dl->AddText(tp, on ? IM_COL32(255, 220, 90, 255) : IM_COL32(230, 232, 238, 255), k.c_str());
        }
        // a click on it selects its row
        if (clicked) {
            const float dx = mouse.x - at.x, dy = mouse.y - at.y;
            if (std::sqrt(dx * dx + dy * dy) <= (r + 4) * scale) {
                selected = c;
                scroll_to_selected = true;
            }
        }
    }
}

void ui(bool &quit_asked)
{
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("controller", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (ImGui::Button("Save")) save();
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        controls_load(controls_path(), mapping);
        saved = mapping;
        say("The saved mapping is back.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Defaults")) {
        controls_defaults(mapping);
        say("The default mapping (not saved yet).");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", controls_path().c_str());
    if (pad) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1), "Controller: %s. Press a control to select it.", SDL_GetGamepadName(pad));
    else ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "No controller connected: connect an Xbox controller (or any the system knows).");
    ImGui::Separator();

    ImGui::BeginChild("drawing", ImVec2(ImGui::GetContentRegionAvail().x * 0.58f, 0));
    draw_controller();
    ImGui::Spacing();
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("Stick dead zone", &mapping.deadzone, 10, 90, "%d%%");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far a stick must move to press its direction's key.");
    ImGui::TextWrapped("The controller presses the game's keys: every function of the keyboard can be put on a "
                       "control. The keyboard still works. The launcher is driven by the D-pad, A and B.");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("table", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("map", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Presses");
        ImGui::TableHeadersRow();
        for (int c = 0; c < CONTROL_COUNT; c++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(c);
            if (ImGui::Selectable(control_name(c), selected == c, ImGuiSelectableFlags_SpanAllColumns |
                                                                      ImGuiSelectableFlags_AllowOverlap))
                selected = c;
            if (selected == c && scroll_to_selected) {
                ImGui::SetScrollHereY(0.5f);
                scroll_to_selected = false;
            }
            if (live[c]) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "*");
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            const int a = mapping.action[c];
            if (ImGui::BeginCombo("##act", a >= 0 ? action(a).label : "(nothing)", ImGuiComboFlags_HeightLarge)) {
                if (ImGui::Selectable("(nothing)", a < 0)) mapping.action[c] = -1;
                const char *group = "";
                for (int i = 0; i < action_count(); i++) {
                    if (std::strcmp(group, action(i).group) != 0) {
                        group = action(i).group;
                        ImGui::SeparatorText(group);
                    }
                    if (ImGui::Selectable(action(i).label, a == i)) mapping.action[c] = i;
                    if (a == i) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (!status.empty() && SDL_GetTicksNS() < status_until) {
        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, ImGui::GetWindowHeight() - ImGui::GetFrameHeight()));
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1), "%s", status.c_str());
    }
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save();

    if (quit_asked) {
        ImGui::OpenPopup("Unsaved changes");
        quit_asked = false;
    }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save the changed mapping before leaving?");
        auto quit = [&] {
            saved = mapping;
            SDL_Event q{};
            q.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&q);
            ImGui::CloseCurrentPopup();
        };
        if (ImGui::Button("Save")) {
            save();
            quit();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save")) quit();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

} // namespace

int main(int argc, char **argv)
{
    const char *snapshot = nullptr;  // developer aid: --snapshot FILE saves the window's picture and exits
    for (int i = 1; i + 1 < argc; i++)
        if (!std::strcmp(argv[i], "--snapshot")) snapshot = argv[i + 1];
    controls_load(controls_path(), mapping);
    saved = mapping;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Gunboat - controller mapping", int(1060 * scale), int(640 * scale),
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window, &renderer)) {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    if (SDL_Surface *icon = SDL_CreateSurfaceFrom(ICON_SIZE, ICON_SIZE, SDL_PIXELFORMAT_RGBA32,
                                                  const_cast<u8 *>(ICON_RGBA), ICON_SIZE * 4)) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    io.FontGlobalScale = scale;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    bool quit_asked = false, prev[CONTROL_COUNT] = {};
    for (bool running = true; running;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            switch (ev.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (dirty()) quit_asked = true;
                else running = false;
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!pad) pad = SDL_OpenGamepad(ev.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (pad && SDL_GetGamepadID(pad) == ev.gdevice.which) {
                    SDL_CloseGamepad(pad);
                    pad = nullptr;
                }
                break;
            default:
                break;
            }
        }
        read_pad();
        for (int c = 0; c < CONTROL_COUNT; c++) {  // a control just pressed selects its row
            if (live[c] && !prev[c]) {
                selected = c;
                scroll_to_selected = true;
            }
            prev[c] = live[c];
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ui(quit_asked);
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 12, 16, 28, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        static int frames;
        if (snapshot && ++frames == 5) {
            if (SDL_Surface *shot = SDL_RenderReadPixels(renderer, nullptr)) {
                SDL_SaveBMP(shot, snapshot);
                SDL_DestroySurface(shot);
            }
            running = false;
        }
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (pad) SDL_CloseGamepad(pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
