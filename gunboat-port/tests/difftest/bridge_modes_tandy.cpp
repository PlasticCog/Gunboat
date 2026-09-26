// Bridge entries: the Tandy (mode 9) twins of the renderer (render/mode_tandy.cpp) and of the view
// copies (hud/views_tandy.cpp). The view copies run with DS = DGROUP, as the harness calls them.
#include "bridge.hpp"
#include "hud/hud.hpp"
#include "mem.hpp"
#include "render/modes.hpp"

using namespace gb;

BRIDGE(blit_rows_tandy) { blit_rows_tandy(u8(r.ax), r.si); }
BRIDGE(spotlight_beam_tandy) { spotlight_beam_tandy(r.es, r.bx, r.dx); }
BRIDGE(sky_water_tandy) { r.di = sky_water_tandy(r.es, r.ax, u8(r.bx), u8(r.cx)); }
BRIDGE(water_marks_tandy) { water_marks_tandy(r.es, r.ax, r.bx, r.cx, r.dx, r.di); }
BRIDGE(span_tandy_a) { span_tandy_a(r.es); }
BRIDGE(span_tandy_b) { span_tandy_b(r.es); }

BRIDGE(view_copy_1_tandy) { r.si = view_copy_1_tandy(r.es, DGROUP); }
BRIDGE(view_copy_2_tandy) { r.si = view_copy_2_tandy(r.es, DGROUP); }
BRIDGE(view_copy_3_tandy) { r.si = view_copy_3_tandy(r.es, DGROUP); }
BRIDGE(view_copy_4_tandy) { r.si = view_copy_4_tandy(r.es, DGROUP); }
BRIDGE(view_copy_5_tandy) { r.si = view_copy_5_tandy(r.es, DGROUP); }
BRIDGE(view_copy_6_tandy) { r.si = view_copy_6_tandy(r.es, DGROUP); }
BRIDGE(view_copy_7_tandy) { r.si = view_copy_7_tandy(r.es, DGROUP); }
BRIDGE(view_copy_8_tandy) { r.si = view_copy_8_tandy(r.es, DGROUP); }
BRIDGE(view_copy_head_tandy)
{
    const DiSi p = view_copy_head_tandy(r.es, DGROUP);
    r.di = p.di;
    r.si = p.si;
    r.cx = 0;
}
