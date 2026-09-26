// Bridge entries: the CGA (mode 4; Hercules too) twins of the renderer's and the view copies' VGA
// routines (render/mode_cga.cpp, hud/views_cga.cpp): the registers as their VGA twins' entries.
#include "bridge.hpp"
#include "hud/hud.hpp"
#include "mem.hpp"
#include "render/modes.hpp"

using namespace gb;

BRIDGE(blit_rows_cga) { blit_rows_cga(u8(r.ax), r.si); }
BRIDGE(spotlight_beam_cga) { spotlight_beam_cga(r.es, r.bx, r.dx); }
BRIDGE(sky_water_cga) { r.di = sky_water_cga(r.es, r.ax, u8(r.bx), u8(r.cx)); }
BRIDGE(water_marks_cga) { water_marks_cga(r.es, r.ax, r.bx, r.cx, r.dx, r.di); }
BRIDGE(span_cga_a) { span_cga_a(r.es); }
BRIDGE(span_cga_b) { span_cga_b(r.es); }

// the view copies run with ES = the destination and DS = the source segment (the harness calls them
// with DS = DGROUP)
BRIDGE(view_copy_1_cga) { r.si = view_copy_1_cga(r.es, DGROUP); }
BRIDGE(view_copy_2_cga) { r.si = view_copy_2_cga(r.es, DGROUP); }
BRIDGE(view_copy_3_cga) { r.si = view_copy_3_cga(r.es, DGROUP); }
BRIDGE(view_copy_4_cga) { r.si = view_copy_4_cga(r.es, DGROUP); }
BRIDGE(view_copy_5_cga) { r.si = view_copy_5_cga(r.es, DGROUP); }
BRIDGE(view_copy_6_cga) { r.si = view_copy_6_cga(r.es, DGROUP); }
BRIDGE(view_copy_7_cga) { r.si = view_copy_7_cga(r.es, DGROUP); }
BRIDGE(view_copy_8_cga) { r.si = view_copy_8_cga(r.es, DGROUP); }
BRIDGE(view_copy_head_cga)
{
    const DiSi p = view_copy_head_cga(r.es, DGROUP);
    r.di = p.di;
    r.si = p.si;
    r.cx = 0;
}
