// One pass of the mission's simulation and drawing (simulation.md §1.2): game_frame.
#include "game/sim.hpp"

#include "hud/hud.hpp"
#include "mem.hpp"
#include "platform/gfx.hpp"
#include "render/render.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

void set_draw_page(u16 page)
{
    ds_u16(DS_draw_page) = page;
    gfx_set_draw_page(s16(page));
}

// The number of passes - 1 of a group: 1, 2 or 4 passes by the time compression (B7F1), 4 while
// DA39 is set. MOV AL, 1 / SHL AL, CL / DEC AL: the count is masked to 5 bits (286 and later).
u8 passes_minus_one()
{
    const u8 cl = ds_u8(DS_fast_forward_key) != 0 ? 2 : ds_u8(DS_time_compression);
    const u8 n = u8(cl & 0x1F);
    return u8(u8(n < 8 ? 1u << n : 0u) - 1);
}

} // namespace

// 0919:8930 game_frame (simulation.md §1.2): the frame counter and the screen shake; on the 3D
// stations the engine sound, the terrain window and the view heading of the station (the pilot's
// look direction turns it by 28h), the world group (missiles, incoming fire, sinking, enemies,
// messages) once per time-compression pass, the boat's destruction, the message line, then the
// boat group (reloads, motion, bob, propulsion, the crew pilot, the clock) once per pass, the
// gunners, the pilot's instruments and the lamps' blinking, and on page 1 the flash, the terrain,
// the objects, the projectiles and the muzzle flashes. SI and ES are kept: SI is the caller's,
// passed on to the routines that save it (caller_si); AX flows from the gunners into the gauges.
void game_frame(u16 si)
{
    ds_u16(DS_frame_counter)++;
    screen_shake_step();
    if (u8(ds_u16(DS_station)) >= 5) return;
    engine_sound_update();
    terrain_cells_update();
    const u16 bx = ds_u16(DS_station);
    u8 al = ds_u8(DS_chase_heading);
    if (ds_u8(DS_chase_view) == 0) {
        al = ds_u8(u16(DS_heading - 1 + bx));  // heading[station - 1]
        if (u8(bx) == 1) {
            const u8 cl = u8(ds_u16(DS_look_direction));
            if (cl != 1) {
                al = u8(al - 0x28);
                if (cl != 0) al = u8(al + 0x50);
            }
        }
    }
    ds_u8(DS_view_heading) = al;
    al = ds_u8(u16(DS_heading_fraction - 1 + bx));
    ds_u8(DS_view_heading_fraction) = al;
    ds_u8(DS_view_heading_low) = u8((al >> 3) | (al << 5));  // ror al, 3
    u8 n = passes_minus_one();
    do {  // the world group
        si = missile_update(si);
        incoming_fire(si);
        sinking_update();
        enemy_update(si);
        message_sequencer();
    } while (s8(--n) >= 0);
    if (ds_u8(DS_boat_lost_pending) != 0) {
        ds_u8(DS_boat_lost_pending) = 0;
        boat_destroyed();
        ds_u8(DS_chase_heading) = u8(ds_u8(DS_chase_heading) + 0x80);
    }
    message_line_draw();
    if (u8(ds_u16(DS_station)) < 5) {  // a key handler may have left the 3D stations
        n = passes_minus_one();
        do {  // the boat group
            reload_tick();
            si = boat_motion(si);
            camera_pitch_bob();
            propulsion();
            crew_pilot();
            mission_clock_tick();
        } while (s8(--n) >= 0);
        const AxSi g = crew_gunners(si);
        si = g.si;
        u16 ax = jet_marker(g.ax);
        ax = throttle_needles(ax);
        panel_blink(ax);
        set_draw_page(1);
        palette_flash();
        terrain_frame();
        object_frame();
        projectile_tick(si);
        muzzle_flash_tick();
    }
    set_draw_page(0);
}

} // namespace gb
