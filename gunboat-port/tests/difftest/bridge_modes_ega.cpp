// Bridge entries: the EGA (mode 0Dh) twins of the renderer (render/mode_ega.cpp) and of the view
// copies (hud/views_ega.cpp). The harness runs the near view copies with DS = DGROUP.
#include "bridge.hpp"
#include "hud/hud.hpp"
#include "mem.hpp"
#include "render/modes.hpp"

using namespace gb;

BRIDGE(blit_rows_ega) { blit_rows_ega(u8(r.ax), r.si); }
BRIDGE(ega_gc_setup) { ega_gc_setup(); }
BRIDGE(spotlight_beam_ega) { spotlight_beam_ega(r.es, r.bx, r.dx); }
BRIDGE(sky_water_ega) { r.di = sky_water_ega(r.es, r.ax, u8(r.bx), u8(r.cx)); }
BRIDGE(water_marks_ega) { water_marks_ega(r.es, r.ax, r.bx, r.cx, r.dx, r.di); }
BRIDGE(span_ega_a) { span_ega_a(r.es); }
BRIDGE(span_ega_b) { span_ega_b(r.es); }

BRIDGE(view_copy_1_ega) { r.si = view_copy_1_ega(r.es, DGROUP); }
BRIDGE(view_copy_2_ega) { r.si = view_copy_2_ega(r.es, DGROUP); }
BRIDGE(view_copy_3_ega) { r.si = view_copy_3_ega(r.es, DGROUP); }
BRIDGE(view_copy_4_ega) { r.si = view_copy_4_ega(r.es, DGROUP); }
BRIDGE(view_copy_5_ega) { r.si = view_copy_5_ega(r.es, DGROUP); }
BRIDGE(view_copy_6_ega) { r.si = view_copy_6_ega(r.es, DGROUP); }
BRIDGE(view_copy_7_ega) { r.si = view_copy_7_ega(r.es, DGROUP); }
BRIDGE(view_copy_8_ega) { r.si = view_copy_8_ega(r.es, DGROUP); }
BRIDGE(view_copy_head_ega)
{
    const DiSi p = view_copy_head_ega(r.es, DGROUP);
    r.di = p.di;
    r.si = p.si;
    r.cx = 0;
}
