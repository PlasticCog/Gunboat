// The window's icon (icon.hpp).
#include "enhanced/icon.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>
#include <vector>

#include "enhanced/icon_image.hpp"
#include "host.hpp"
#include "platform/card.hpp"

namespace gb {

namespace {

constexpr int SIZE = 64;       // the icon made from the title: 64 x 64
constexpr int CROP_X = 40;     // the square of the 320 x 200 title it shows: the boat, its flag and
constexpr int CROP_W = 200;    // the GUNBOAT logo (x 40..239, every row)
bool grab;                     // the title screen is on: take the next frame

std::string cache_path()
{
    char *dir = SDL_GetPrefPath("", "Gunboat");
    std::string p = dir ? std::string(dir) + "title_icon.bmp" : std::string();
    SDL_free(dir);
    return p;
}

void set_icon(SDL_Surface *s)
{
    if (SDL_Window *w = host_window()) SDL_SetWindowIcon(w, s);
}

void set_own_icon()
{
    SDL_Surface *s = SDL_CreateSurfaceFrom(ICON_SIZE, ICON_SIZE, SDL_PIXELFORMAT_RGBA32,
                                           const_cast<u8 *>(ICON_RGBA), ICON_SIZE * 4);
    if (!s) return;
    set_icon(s);
    SDL_DestroySurface(s);
}

void on_title_shown() { grab = true; }

} // namespace

void icon_install()
{
    const std::string p = cache_path();
    SDL_Surface *kept = p.empty() ? nullptr : SDL_LoadBMP(p.c_str());
    if (kept) {
        set_icon(kept);
        SDL_DestroySurface(kept);
    } else {
        set_own_icon();
    }
    static bool hooked;
    if (!hooked) host_add_title_shown_observer(on_title_shown);
    hooked = true;
}

void icon_frame(const u32 *xrgb, int w, int h)
{
    if (!grab) return;
    grab = false;
    // The VGA title only (the other cards draw it in their few colours; their icon stays as it is).
    if (card_machine() != Machine::Vga || w != 320 || h != 200) return;
    SDL_Surface *s = SDL_CreateSurface(SIZE, SIZE, SDL_PIXELFORMAT_XRGB8888);
    if (!s) return;
    // each icon pixel: the average of its square of the title (box filter)
    for (int y = 0; y < SIZE; y++) {
        u32 *row = reinterpret_cast<u32 *>(static_cast<u8 *>(s->pixels) + size_t(y) * s->pitch);
        const int y0 = y * 200 / SIZE, y1 = std::max(y0 + 1, (y + 1) * 200 / SIZE);
        for (int x = 0; x < SIZE; x++) {
            const int x0 = CROP_X + x * CROP_W / SIZE, x1 = std::max(x0 + 1, CROP_X + (x + 1) * CROP_W / SIZE);
            u32 r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const u32 c = xrgb[yy * w + xx];
                    r += c >> 16 & 0xFF;
                    g += c >> 8 & 0xFF;
                    b += c & 0xFF;
                    n++;
                }
            row[x] = 0xFF000000u | (r / n) << 16 | (g / n) << 8 | b / n;
        }
    }
    set_icon(s);
    const std::string p = cache_path();
    if (!p.empty()) SDL_SaveBMP(s, p.c_str());
    SDL_DestroySurface(s);
}

} // namespace gb
