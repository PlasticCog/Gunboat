// The player's settings file (settings.hpp): "key = value" lines, unknown keys ignored.
#include "enhanced/settings.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gb {

namespace {

bool truthy(const char *v) { return !std::strcmp(v, "1") || !SDL_strcasecmp(v, "yes") || !SDL_strcasecmp(v, "on"); }

void trim(char *&b)
{
    while (*b == ' ' || *b == '\t') b++;
    char *e = b + std::strlen(b);
    while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
}

} // namespace

const char *aspect_name(Aspect a) { return a == Aspect::Square ? "square" : "4:3"; }

const char *filter_name(Filter f)
{
    switch (f) {
    case Filter::Nearest: return "nearest";
    case Filter::Smooth: return "smooth";
    case Filter::Crt: return "crt";
    default: return "sharp";
    }
}

const char *sound_name(Sound s) { return s == Sound::Adlib ? "adlib" : s == Sound::Speaker ? "speaker" : "auto"; }

std::string settings_path()
{
    char *dir = SDL_GetPrefPath("", "Gunboat");
    std::string p = dir ? std::string(dir) + "gunboat.ini" : std::string("gunboat.ini");
    SDL_free(dir);
    return p;
}

bool settings_load(Settings &s)
{
    std::FILE *f = std::fopen(settings_path().c_str(), "r");
    if (!f) return false;
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        char *eq = std::strchr(line, '=');
        if (line[0] == ';' || line[0] == '#' || !eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        trim(k);
        trim(v);
        if (!std::strcmp(k, "game_dir")) s.game_dir = v;
        else if (!std::strcmp(k, "launcher")) s.launcher = truthy(v);
        else if (!std::strcmp(k, "fullscreen")) s.fullscreen = truthy(v);
        else if (!std::strcmp(k, "window_scale")) s.window_scale = SDL_clamp(std::atoi(v), 1, 8);
        else if (!std::strcmp(k, "aspect")) s.aspect = !std::strcmp(v, "square") ? Aspect::Square : Aspect::Crt43;
        else if (!std::strcmp(k, "filter")) {
            s.filter = !std::strcmp(v, "nearest") ? Filter::Nearest
                       : !std::strcmp(v, "smooth") ? Filter::Smooth
                       : !std::strcmp(v, "crt")    ? Filter::Crt
                                                   : Filter::Sharp;
        }
        else if (!std::strcmp(k, "hires_view")) s.hires_view = truthy(v);
        else if (!std::strcmp(k, "smooth_motion")) s.smooth_motion = truthy(v);
        else if (!std::strcmp(k, "widescreen")) s.widescreen = truthy(v);
        else if (!std::strcmp(k, "sound")) {
            s.sound = !std::strcmp(v, "adlib") ? Sound::Adlib : !std::strcmp(v, "speaker") ? Sound::Speaker : Sound::Auto;
        }
        else if (!std::strcmp(k, "fps")) s.fps = SDL_clamp(std::atoi(v), 0, 1000);
    }
    std::fclose(f);
    return true;
}

bool settings_save(const Settings &s)
{
    std::FILE *f = std::fopen(settings_path().c_str(), "w");
    if (!f) return false;
    std::fprintf(f,
                 "; Gunboat port settings (written by the launcher)\n"
                 "game_dir = %s\n"
                 "launcher = %d\n"
                 "fullscreen = %d\n"
                 "window_scale = %d\n"
                 "aspect = %s\n"
                 "filter = %s\n"
                 "; enhancements (0 = as the original)\n"
                 "hires_view = %d\n"
                 "smooth_motion = %d\n"
                 "widescreen = %d\n"
                 "sound = %s\n"
                 "fps = %d\n",
                 s.game_dir.c_str(), s.launcher, s.fullscreen, s.window_scale, aspect_name(s.aspect),
                 filter_name(s.filter), s.hires_view, s.smooth_motion, s.widescreen, sound_name(s.sound), s.fps);
    return std::fclose(f) == 0;
}

} // namespace gb
