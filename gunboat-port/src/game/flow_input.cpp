// Keys for the game flow (game_flow.md §3, simulation.md §3.1): input_read_key, the demo script,
// wait_key.
#include "game/flow.hpp"

#include "host.hpp"
#include "mem.hpp"
#include "platform/platform.hpp"
#include "game/pending.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

// 08e1:006e demo_next_key (simulation.md §3.1): the recorded demo. A real key ends it (the mission
// quits through station 9). Otherwise the script (count, key) pairs at 08e1:0008 + demo_script_pos
// (code-segment data) return `count` zeros, then the key; keys from E0h set held_keys instead. The
// pair 00 00 restarts the script at 08e1:0008.
void demo_next_key(u8 *key)
{
    *key = 0;
    if (ds_u8(DS_isr_key_code) != 0) {
        ds_u16(DS_demo_mode) = 0;
        ds_u8(DS_isr_key_code) = 0;
        ds_u16(DS_station) = 9;
        return;
    }
    *key = 0;
    if (ds_u8(DS_demo_countdown) != 0) {
        ds_u8(DS_demo_countdown)--;
        return;
    }
    *key = ds_u8(DS_demo_next);
    const u16 bx = u16(ds_u16(DS_demo_script_pos) + 8);
    const u8 count = seg_u8(CSSEG_demo_script, bx);
    u8 k = seg_u8(CSSEG_demo_script, u16(bx + 1));
    ds_u16(DS_demo_script_pos) = u16(bx + 2 - 8);
    ds_u8(DS_demo_countdown) = count;
    if (k >= 0xE0) {
        ds_u8(DS_held_keys) = u8(k - 0xE0);
        k = 0;
    }
    ds_u8(DS_demo_next) = k;
    if (count == 0 && k == 0) ds_u16(DS_demo_script_pos) = 0;
}

// 0000:07a8 input_read_key (game_flow.md §3): the next key into *key (0 = none): the demo's, the
// keyboard's (isr_key_code) or the joystick's. Handles Ctrl+Q (quit), E, S (sound on/off) and Esc
// (pause until the next Esc), and clicks for keys. key_code gets the key too.
void input_read_key(u16 *key)
{
    u8 c = 0;
    *key = 0;
    if (ds_u16(DS_demo_mode) != 0 && ds_u8(DS_disk_prompt) == 0) {
        demo_next_key(reinterpret_cast<u8 *>(key));
        if (ds_u16(DS_sound_muted) == 0 && u8(*key) != 0) sfx_play_far(0x0C);
    } else {
        if (ds_u8(DS_isr_key_code) != 0) {
            *key = ds_u8(DS_isr_key_code);
            ds_u8(DS_isr_key_code) = 0;
        }
        c = u8(*key);
        if (c == 0) {
            // the joystick
            if (ds_u16(DS_joystick) != 0) {
                u8 dir = 0;
                joystick_read(1, reinterpret_cast<u8 *>(key), &dir);
                c = 0;
                ds_u8(DS_joystick_bits) = 0;
                const u16 button = u8(*key);
                if (button == 0x0D) ds_u8(DS_joystick_bits) = u8(ds_u8(DS_joystick_bits) + 0x10);
                switch (dir) {  // jump table 0000:08C0
                case 0x47: ds_u8(DS_joystick_bits) = u8(ds_u8(DS_joystick_bits) + 5); c = 0x91; break;
                case 0x48: ds_u8(DS_joystick_bits)++; c = 0x92; break;
                case 0x49: ds_u8(DS_joystick_bits) = u8(ds_u8(DS_joystick_bits) + 9); c = 0x93; break;
                case 0x4B: ds_u8(DS_joystick_bits) = u8(ds_u8(DS_joystick_bits) + 4); c = 0x94; break;
                case 0x4D: ds_u8(DS_joystick_bits) = u8(ds_u8(DS_joystick_bits) + 8); c = 0x96; break;
                case 0x4F: ds_u8(DS_joystick_bits) = u8(ds_u8(DS_joystick_bits) + 6); c = 0x97; break;
                case 0x50: ds_u8(DS_joystick_bits) = u8(ds_u8(DS_joystick_bits) + 2); c = 0x98; break;
                case 0x51: ds_u8(DS_joystick_bits) = u8(ds_u8(DS_joystick_bits) + 0x0A); c = 0x99; break;
                default: break;
                }
                if (button == 0) *key = c;
            }
        } else {
            // the keyboard
            if ((c & 0xDF) == 'Q' && ds_u8(DS_ctrl_held) != 0) quit_to_dos();
            if ((c & 0xDF) == 'E') ds_u8(DS_e_toggle) ^= 1;
            if ((c & 0xDF) == 'S') {
                sfx_play_far(0x0C);
                ds_u8(DS_sound_muted) ^= 1;
                if (ds_u16(DS_sound_muted) == 1) music_stop();
            }
            const bool quiet = (c >= 0x91 && c <= 0x99) || c == 0x0D;  // moves and Enter
            if (!quiet || ds_u16(DS_phase) != 3) {
                sfx_play_far(0x0C);
                if (ds_u16(DS_sound_muted) == 0) sfx_play_far(0);
            }
            if (c == 0x80 && ds_u16(DS_phase) == 3 && s16(ds_u16(DS_station)) >= 1 && s16(ds_u16(DS_station)) < 5)
                show_message_far(0x33);  // "Raid suspended."
            // Esc pauses until the next Esc (the original busy-waits on isr_key_code; the port
            // pumps the host meanwhile)
            while (c == 0x80) {
                host_pump();
                *key = 0;
                if (ds_u8(DS_isr_key_code) != 0) {
                    *key = ds_u8(DS_isr_key_code);
                    ds_u8(DS_isr_key_code) = 0;
                }
                if (u8(*key) == 0x80) {
                    if (ds_u16(DS_phase) == 3 && s16(ds_u16(DS_station)) >= 1 && s16(ds_u16(DS_station)) < 5)
                        show_message_far(0x34);
                    c = 0;
                    *key = 0;
                    ds_u8(DS_clock_subsecond) = u8(ds_u16(DS_tick_counter));
                    if (ds_u16(DS_sound_muted) == 0) sfx_play_far(0);
                } else if ((u8(*key) & 0xDF) == 'Q' && ds_u8(DS_ctrl_held) != 0) {
                    quit_to_dos();
                }
            }
        }
    }
    *key &= 0x00FF;
    ds_u8(DS_key_code) = u8(*key);
}

// 00f2:0e7a wait_key (game_flow.md §3): waits up to n - 1 BIOS ticks for a key (n = 0: forever;
// n = 1: returns at once). Returns the key, or n if none came. random() runs on every poll.
u16 wait_key(u16 n)
{
    u16 i = 1;
    while (i != n) {
        bios_wait_ticks(1);
        u16 key = random();
        input_read_key(&key);
        if (key != 0) return u8(key);
        if (n != 0) i++;
    }
    return n;
}

// 00f2:103e kbd_flush_key: forgets the last key.
void kbd_flush_key() { ds_u8(DS_isr_key_code) = 0; }

} // namespace gb
