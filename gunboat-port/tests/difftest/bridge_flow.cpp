// Bridge entries: the game flow (game_flow.md).
#include "bridge.hpp"
#include "game/flow.hpp"
#include "mem.hpp"
#include "platform/platform.hpp"

using namespace gb;

BRIDGE(bios_wait_ticks) { r.ax = bios_wait_ticks(s16(a[0])); }
BRIDGE(demo_next_key) { demo_next_key(&ds_u8(a[0])); }
BRIDGE(input_read_key) { input_read_key(&ds_u16(a[0])); }
BRIDGE(wait_key) { r.ax = wait_key(a[0]); }
BRIDGE(kbd_flush_key) { kbd_flush_key(); }
BRIDGE(print_records) { r.ax = print_records(a[0], a[1]); }
BRIDGE(print_text) { r.ax = print_text(a[0], a[1]); }
BRIDGE(screen_present) { screen_present(); }
BRIDGE(credits_text) { r.ax = credits_text(a[0]); }
BRIDGE(pal_apply_vga) { pal_apply_vga(); }
BRIDGE(pal_black_vga) { pal_black_vga(); }
BRIDGE(pal_fade_in_vga) { pal_fade_in_vga(); }
BRIDGE(pal_fade_out_vga) { pal_fade_out_vga(); }
BRIDGE(ega_pal_apply) { ega_pal_apply(); }
BRIDGE(ega_pal_init) { ega_pal_init(); }
BRIDGE(music_stop) { music_stop(); }
BRIDGE(menu_cursor_init) { menu_cursor_init(); }
BRIDGE(title_menu) { r.ax = title_menu(); }
BRIDGE(config_load) { config_load(); }
BRIDGE(music_start) { music_start(); }
