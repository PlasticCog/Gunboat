// The launcher (launcher.hpp), drawn with SDL's built-in 8x8 font.
#include "enhanced/launcher.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

#include "host.hpp"
#include "mem.hpp"

namespace gb {

namespace {

enum Item { FOLDER, VIDEO, PRESET, VIEW, MOTION, DISTANCE, WIDE, ASPECT, FILTER, DISPLAY, SOUND, EFFECTS, SHOW, PLAY, QUIT, ITEMS };

const char *const LABELS[ITEMS] = {"Game folder", "Video card", "Preset",  "3D view", "Motion",      "Draw distance",
                                   "Widescreen",  "Picture", "Scaling", "Display",     "Music",   "Sound effects",
                                   "This screen", "Play",    "Quit"};

const char *const HELP[ITEMS][3] = {
    {"The folder with the original game's files: by default the folder Game next to gunboat.exe",
     "(Game/README.md lists the files). Enter: choose another folder.", ""},
    {"The graphics card the game runs on, as in the original's setup: VGA (256 colours), EGA or",
     "Tandy (16 colours), CGA (4 colours) or Hercules (monochrome). The enhancements below work",
     "with VGA only; the other cards show the original's picture with the scaling chosen here."},
    {"Original: the picture exactly as the DOS game drew it.",
     "Enhanced: every enhancement below. The game itself is the same either way;",
     "the enhancements only change how its frames are shown. In the game, F11 switches."},
    {"High resolution: the 3D view drawn again at the window's resolution from the",
     "game's own terrain and objects; the cockpit, gauges and screens stay the original art.",
     "Original: the 320 x 200 view."},
    {"Smooth: 60 frames per second in the 3D view, drawn between the game's frames",
     "(the game still runs at its own rate, so the view is one game frame behind).",
     "Original: the view changes with the game's frames (15 per second)."},
    {"Extended: the terrain, scenery and objects beyond the 3 x 3 cells around the boat that the",
     "game draws, out to 5 cells, behind its own: islands and shores on the horizon. The game",
     "itself is unchanged (what it sees and hits). Original: only the game's cells."},
    {"Wide cockpit: in a window wider than the picture, the cockpit art is widened to its",
     "edges where it has the least detail (the centre stays as drawn). Extended world: the",
     "world continues beside the cockpit instead. Off: black borders. (3D stations only.)"},
    {"4:3: the 320 x 200 picture stretched to 4:3 as on a VGA monitor (tall pixels).",
     "Square pixels: 16:10, every pixel square.", ""},
    {"Sharp pixels: crisp at any window size. Nearest: plain pixel copies (uneven at",
     "some sizes). Smooth: filtered. CRT scanlines: sharp pixels with dark lines",
     "between the rows."},
    {"Window or full screen. Alt+Enter switches in the game.", "", ""},
    {"AdLib: the music on the AdLib (OPL2), through the game's ADLIB.COM driver.",
     "PC speaker: the music on the speaker. Auto: AdLib when the game folder has ADLIB.COM.",
     "(The sound effects are the next setting.)"},
    {"PC speaker: the effects as in the original. AdLib: each effect plays its original notes",
     "on an FM instrument instead (an AdLib of its own, with or without the AdLib music).",
     "Each effect has its own instrument, built into the game."},
    {"No: start the game directly next time (gunboat --launcher shows this screen again).", "", ""},
    {"Start the game with these settings (they are saved).", "", ""},
    {"Leave without starting the game.", "", ""},
};

struct GameCheck {
    std::string dir;
    bool ok = false;
    bool adlib = false;
    std::string why;
} game;

std::mutex picked_mutex;
std::string picked_dir;
bool picked = false;

void SDLCALL folder_picked(void *, const char *const *files, int)
{
    if (!files || !files[0]) return;
    std::lock_guard<std::mutex> lock(picked_mutex);
    picked_dir = files[0];
    picked = true;
}

std::string find_file(const std::string &dir, const char *name)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec)) {
        const std::string n = entry.path().filename().string();
        if (n.size() != std::strlen(name)) continue;
        bool same = true;
        for (size_t i = 0; same && i < n.size(); i++) same = std::tolower(u8(n[i])) == std::tolower(u8(name[i]));
        if (same) return entry.path().string();
    }
    return {};
}

void recheck(const std::string &dir)
{
    if (game.dir == dir && !game.dir.empty()) return;
    game.dir = dir;
    game.why.clear();
    game.adlib = !find_file(dir, "ADLIB.COM").empty();
    game.ok = launcher_check_game_dir(dir, game.why);
}

std::string value_of(const Settings &s, int item)
{
    switch (item) {
    case FOLDER: return s.game_dir.empty() ? "Game: " + default_game_dir() : s.game_dir;
    case VIDEO:
        switch (s.video) {
        case Video::Ega: return "EGA (16 colours)";
        case Video::Tandy: return "Tandy (16 colours)";
        case Video::Cga: return "CGA (4 colours)";
        case Video::Hercules: return "Hercules (monochrome)";
        default: return "VGA (256 colours)";
        }
    case PRESET:
        if (!s.any_enhancement()) return "Original";
        if (s.all_enhancements()) return "Enhanced";
        return "Custom";
    case VIEW: return s.hires_view ? "High resolution" : "Original (320 x 200)";
    case MOTION: return s.smooth_motion ? "Smooth (60 fps)" : "Original (the game's frames)";
    case DISTANCE: return s.far_view ? "Extended" : "Original (3 x 3 cells)";
    case WIDE:
        return s.widescreen == Wide::Cockpit ? "Wide cockpit"
               : s.widescreen == Wide::World ? "Extended world"
                                             : "Off (4:3 with borders)";
    case ASPECT: return s.aspect == Aspect::Square ? "Square pixels (16:10)" : "4:3 (VGA monitor)";
    case FILTER:
        switch (s.filter) {
        case Filter::Nearest: return "Nearest";
        case Filter::Smooth: return "Smooth";
        case Filter::Crt: return "CRT scanlines";
        default: return "Sharp pixels";
        }
    case DISPLAY: return s.fullscreen ? "Full screen" : "Window";
    case SOUND:
        if (s.sound == Sound::Adlib) return "AdLib";
        if (s.sound == Sound::Speaker) return "PC speaker";
        return game.adlib ? "Auto (AdLib)" : "Auto (PC speaker: no ADLIB.COM)";
    case EFFECTS: return s.effects == Effects::Adlib ? "AdLib (FM instruments)" : "PC speaker (original)";
    case SHOW: return s.launcher ? "Show at start" : "Skip next time";
    default: return "";
    }
}

// One step of an item's value (dir = +1 / -1).
void change(Settings &s, int item, int dir)
{
    switch (item) {
    case PRESET:
        if (s.all_enhancements()) s.set_original();
        else s.set_enhanced();
        break;
    case VIDEO: s.video = Video((int(s.video) + 5 + dir) % 5); break;
    case VIEW: s.hires_view = !s.hires_view; break;
    case MOTION: s.smooth_motion = !s.smooth_motion; break;
    case DISTANCE: s.far_view = !s.far_view; break;
    case WIDE: s.widescreen = Wide((int(s.widescreen) + 3 + dir) % 3); break;
    case ASPECT: s.aspect = s.aspect == Aspect::Crt43 ? Aspect::Square : Aspect::Crt43; break;
    case FILTER: s.filter = Filter((int(s.filter) + 4 + dir) % 4); break;
    case DISPLAY:
        s.fullscreen = !s.fullscreen;
        host_set_fullscreen(s.fullscreen);
        break;
    case SOUND: s.sound = Sound((int(s.sound) + 3 + dir) % 3); break;
    case EFFECTS: s.effects = s.effects == Effects::Adlib ? Effects::Speaker : Effects::Adlib; break;
    case SHOW: s.launcher = !s.launcher; break;
    default: break;
    }
}

void text(SDL_Renderer *r, float x, float y, const std::string &str, u8 cr, u8 cg, u8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderDebugText(r, x, y, str.c_str());
}

// The path shortened from the left to n characters.
std::string fit(const std::string &s, size_t n)
{
    if (s.size() <= n) return s;
    return "..." + s.substr(s.size() - (n - 3));
}

} // namespace

bool launcher_check_game_dir(const std::string &dir, std::string &why)
{
    const std::string exe = find_file(dir, "GB.EXE");
    if (exe.empty()) {
        why = "No GB.EXE in this folder.";
        return false;
    }
    ExeInfo info;
    if (!mem_load_exe(exe, info, why)) return false;
    return true;
}

bool launcher_run(Settings &s)
{
    SDL_Window *win = host_window();
    SDL_Renderer *r = host_renderer();
    if (!win || !r) return true;
    SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderVSync(r, 1);
    SDL_Gamepad *pad = nullptr;
    int sel = PLAY;
    std::string message;
    Uint64 message_until = 0;
    recheck(game_dir_of(s));
    if (!game.ok) sel = FOLDER;
    float scale = 2;
    const float row0 = 64, row_h = 13, value_x = 16 + 15 * 8;
    auto item_y = [&](int i) { return row0 + i * row_h + (i >= PLAY ? 8 : 0); };
    auto item_at = [&](float y) {  // the item under a window y, or -1
        y /= scale;
        for (int i = 0; i < ITEMS; i++)
            if (y >= item_y(i) - 3 && y < item_y(i) - 3 + row_h) return i;
        return -1;
    };

    auto activate = [&](int item, int dir) -> int {  // 1 play, -1 quit, 0 stay
        if (item == PLAY) {
            recheck(game_dir_of(s));
            if (game.ok) return 1;
            sel = FOLDER;
            message = game.why.empty() ? "Choose the folder with GB.EXE first." : game.why;
            message_until = SDL_GetTicksNS() + 4 * SDL_NS_PER_SECOND;
            return 0;
        }
        if (item == QUIT) return -1;
        if (item == FOLDER) {
            if (dir == 0) SDL_ShowOpenFolderDialog(folder_picked, nullptr, win, game_dir_of(s).c_str(), false);
            return 0;
        }
        change(s, item, dir == 0 ? 1 : dir);
        return 0;
    };

    for (;;) {
        {
            std::lock_guard<std::mutex> lock(picked_mutex);
            if (picked) {
                picked = false;
                // the Game folder itself is remembered as the default (it moves with the program)
                s.game_dir = picked_dir == default_game_dir() ? std::string() : picked_dir;
                recheck(game_dir_of(s));
                if (game.ok) sel = PLAY;
            }
        }
        int result = 0;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT: result = -1; break;
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.key == SDLK_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
                    if (!ev.key.repeat) {
                        s.fullscreen = !host_fullscreen();
                        host_set_fullscreen(s.fullscreen);
                    }
                    break;
                }
                switch (ev.key.scancode) {
                case SDL_SCANCODE_UP: sel = (sel + ITEMS - 1) % ITEMS; break;
                case SDL_SCANCODE_DOWN:
                case SDL_SCANCODE_TAB: sel = (sel + 1) % ITEMS; break;
                case SDL_SCANCODE_LEFT: if (sel != PLAY && sel != QUIT) result = activate(sel, -1); break;
                case SDL_SCANCODE_RIGHT: if (sel != PLAY && sel != QUIT) result = activate(sel, 1); break;
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_KP_ENTER:
                case SDL_SCANCODE_SPACE: result = activate(sel, 0); break;
                case SDL_SCANCODE_ESCAPE: result = -1; break;
                default: break;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION: {
                const int row = item_at(ev.motion.y);
                if (row >= 0) sel = row;
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                const int row = item_at(ev.button.y);
                if (row >= 0) {
                    sel = row;
                    result = activate(row, ev.button.button == SDL_BUTTON_RIGHT ? -1 : 0);
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!pad) pad = SDL_OpenGamepad(ev.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                switch (ev.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_UP: sel = (sel + ITEMS - 1) % ITEMS; break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: sel = (sel + 1) % ITEMS; break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: if (sel != PLAY && sel != QUIT) result = activate(sel, -1); break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: if (sel != PLAY && sel != QUIT) result = activate(sel, 1); break;
                case SDL_GAMEPAD_BUTTON_SOUTH:
                case SDL_GAMEPAD_BUTTON_START: result = activate(sel, 0); break;
                case SDL_GAMEPAD_BUTTON_EAST: result = -1; break;
                default: break;
                }
                break;
            default: break;
            }
            if (result) break;
        }
        if (result) {
            if (pad) SDL_CloseGamepad(pad);
            SDL_SetRenderScale(r, 1, 1);
            return result > 0;
        }

        int ow, oh;
        SDL_GetRenderOutputSize(r, &ow, &oh);
        scale = std::max(1.0f, std::floor(std::min(ow / (78.0f * 8), oh / (48.0f * 8)) * 2) / 2);
        SDL_SetRenderScale(r, 1, 1);
        SDL_SetRenderDrawColor(r, 12, 16, 28, 255);
        SDL_RenderClear(r);
        SDL_SetRenderScale(r, scale * 3, scale * 3);
        text(r, 16 / 3.0f, 12 / 3.0f, "GUNBOAT", 255, 196, 64);
        SDL_SetRenderScale(r, scale, scale);
        text(r, 16 + 8 * 8 * 3 + 16, 16, "Accolade, 1990", 200, 200, 210);
        text(r, 16 + 8 * 8 * 3 + 16, 28, std::string("The faithful C++ / SDL3 port, version ") + GB_VERSION, 130, 140, 160);
        for (int i = 0; i < ITEMS; i++) {
            const float y = item_y(i);
            if (i == sel) {
                SDL_SetRenderDrawColor(r, 40, 56, 96, 255);
                const SDL_FRect bar = {8, y - 3, float(ow / scale - 16), row_h};
                SDL_RenderFillRect(r, &bar);
            }
            const bool on = i == sel;
            if (i == PLAY || i == QUIT) {
                const bool can = i == QUIT || game.ok;
                text(r, 16, y, i == PLAY ? "> Play" : "  Quit", on ? 255 : 220, on ? 230 : (can ? 220 : 110), on ? 120 : (can ? 220 : 110));
                continue;
            }
            text(r, 16, y, LABELS[i], on ? 255 : 170, on ? 230 : 180, on ? 120 : 200);
            const size_t room = size_t(std::max(10.0f, (ow / scale - value_x - 16) / 8));
            std::string v = value_of(s, i);
            if (i != FOLDER) v = "< " + v + " >";
            text(r, value_x, y, fit(v, room), 240, 240, 245);
        }
        // Status of the game folder.
        const float status_y = item_y(ITEMS - 1) + row_h + 10;
        if (!message.empty() && SDL_GetTicksNS() < message_until) text(r, 16, status_y, message, 255, 120, 100);
        else if (game.ok) text(r, 16, status_y, std::string("Game found: ") + fit(game.dir, 60), 120, 220, 140);
        else text(r, 16, status_y, game.why.empty() ? "Choose the game folder." : game.why, 255, 150, 110);
        // What the selected item does.
        const float help_y = status_y + 18;
        SDL_SetRenderDrawColor(r, 60, 70, 100, 255);
        const SDL_FRect line = {16, help_y - 6, float(ow / scale - 32), 1};
        SDL_RenderFillRect(r, &line);
        for (int k = 0; k < 3; k++) text(r, 16, help_y + 4 + k * 11, HELP[sel][k], 190, 196, 210);
        text(r, 16, float(oh / scale - 14),
             "Up/Down choose  Left/Right change  Enter select  Esc quit", 120, 130, 150);
        static bool saved;  // developer aid: GB_SNAPSHOT_DIR gets the launcher's first picture
        if (!saved) {
            saved = true;
            if (const char *dir = SDL_getenv("GB_SNAPSHOT_DIR")) {
                if (SDL_Surface *shot = SDL_RenderReadPixels(r, nullptr)) {
                    SDL_SaveBMP(shot, (std::string(dir) + "/launcher.bmp").c_str());
                    SDL_DestroySurface(shot);
                }
            }
        }
        SDL_RenderPresent(r);
    }
}

} // namespace gb
