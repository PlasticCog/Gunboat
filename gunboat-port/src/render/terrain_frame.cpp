// The terrain pass of the frame (render3d.md §3, simulation.md §10): the sky and water, the
// projection of both vertex groups when the view moved, the shore contact and its response, the
// group A draw order and the group B primitives.
#include "game/sim.hpp"
#include "mem.hpp"
#include "platform/card.hpp"
#include "render/render.hpp"
#include "symbols.hpp"

namespace gb {

// 0919:7158 terrain_frame (render3d.md §3, simulation.md §10): terrain_setup; unless the scene is
// rebuilt or the camera, the view heading or the horizon changed since the last projection, the
// projection and the contact test are skipped. Otherwise: the view saved, the contact of the last
// projection kept in D900, both groups projected (collecting the contact candidates), and outside
// the chase view the candidates tested. A new contact (nonzero, not the previous one) reverses the
// speed to -16 (+16 when reversing); at the Mare Island pilot station on colour 6 the boat is lost
// next frame; else, with RNG bits 1-3 clear in missions after 1, colour 0Eh damages a waterjet
// (RNG bit 0 picks which) and colour 6 the hull: condition 3 -> 1, 0/1 -> 2 with the message
// 17h/18h or 1, nothing at 2. Then the draw order (reset on a rebuild), its sort, group B.
void terrain_frame()
{
    terrain_setup();
    const bool same = ds_u8(DS_scene_rebuild) == 0 && ds_u16(DS_camera_qx) == ds_u16(DS_saved_camera_qx) &&
                      ds_u16(DS_camera_qy) == ds_u16(DS_saved_camera_qy) &&
                      u16(ds_u8(DS_view_heading) << 8 | ds_u8(DS_view_heading_fraction)) ==
                          ds_u16(DS_saved_view_heading) &&
                      ds_u8(DS_horizon_row) == ds_u8(DS_saved_horizon);
    if (!same) {
        terrain_save_view();
        ds_u8(DS_contact_previous) = ds_u8(DS_contact);
        ds_u8(DS_contact) = 0;
        ds_u16(DS_contact_candidate_a) = 0xFFFF;
        ds_u16(DS_contact_candidate_b) = 0xFFFF;
        project(0, ds_u16(DS_group_a_count));
        project(0x200, u16(ds_u16(DS_group_b_count) + 0x200));
        if (ds_u8(DS_chase_view) == 0) {
            shore_contact_test(ds_u16(DS_contact_candidate_a));
            if (ds_u8(DS_contact) == 0) shore_contact_test(ds_u16(DS_contact_candidate_b));
            const u8 contact = ds_u8(DS_contact);
            if (contact != 0 && contact != ds_u8(DS_contact_previous)) {
                ds_u8(DS_speed) = ds_s8(DS_speed) < 0 ? 0x10 : 0xF0;
                const u8 colour = ds_u8(DS_contact_colour) & 0x3F;
                if (ds_u8(DS_station) == 1 && ds_u8(DS_region) == 3 && colour == 6) {
                    ds_u8(DS_boat_lost_pending) = 1;  // byte tests of station and region
                } else {
                    const u8 rng = ds_u8(DS_rng_state);
                    if ((rng & 0x0E) == 0 && ds_u16(DS_mission_number) > 1) {
                        u16 bx = rng & 1;  // BH = rng & 0Eh = 0
                        u8 message = u8(bx + 0x17);
                        bool damage = colour == 0x0E;
                        if (colour == 6) {
                            message = 1;
                            bx = 6;  // D510, the hull
                            damage = true;
                        }
                        if (damage) {
                            u8 &condition = ds_u8(u16(DS_waterjet_condition + bx));
                            const u8 c = condition & 3;
                            if (c != 2) {
                                condition = c > 2 ? 1 : 2;
                                show_message_page0(message);
                            }
                        }
                    }
                }
            }
        }
    }
    if (ds_u8(DS_scene_rebuild) != 0) order_reset();
    order_sort();
    if (u8(ds_u16(DS_video_mode)) == 0x0D) {  // EGA (7268): map mask 0Fh, write mode 0, set/reset
        card_out16(0x3C4, 0x0F02);            // on every plane, function replace
        card_out16(0x3CE, 0x0005);
        card_out16(0x3CE, 0x0F01);
        card_out16(0x3CE, 0x0003);
    }
    draw_group_b();
}

} // namespace gb
