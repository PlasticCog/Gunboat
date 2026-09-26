// Timers (platform.md §1): the menu timer (install, restore, interrupt), the host's timer
// interrupt dispatch, and the BIOS tick wait.
#include "platform/platform.hpp"

#include "host.hpp"
#include "mem.hpp"
#include "sound/sound.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr u16 MENU_ISR_OFF = 0x0CE2;  // 121b:0ce2
constexpr u16 SFX_ISR_OFF = 0x00A3;   // 12ed:00a3
constexpr u16 SFX_ISR_FILE_SEG = 0x12ED;

FarPtr int8_vector() { return mem_far(0, 8 * 4); }

} // namespace

// PORT: the timer interrupt. The host calls this at the PIT rate; the INT 8 vector in mem[]
// decides which handler runs, as it does on the real machine.
void timer_interrupt() { run_int8_handler(int8_vector()); }

// PORT: runs the INT 8 handler at `vector`: one of the game's, the resident Ad Lib driver's (it
// hooks INT 8 when installed, sound/adlib_driver.cpp), or else the BIOS's.
// PORT: the host is told which handler runs (host_speaker_effects: the speaker changes of the effects
// driver's handler can be played on AdLib by the presentation layer); nothing in mem[] changes.
void run_int8_handler(FarPtr vector)
{
    const bool sfx = vector.seg == seg_of(SFX_ISR_FILE_SEG) && vector.off == SFX_ISR_OFF;
    const bool outer = host_speaker_effects(sfx);
    if (vector.seg == seg_of(CSSEG_menu_timer_old_vector) && vector.off == MENU_ISR_OFF) menu_timer_isr();
    else if (sfx) sfx_timer_isr();
    else if (adlib_is_clock_isr(vector)) adl_clock_isr();
    else bios_tick();  // the BIOS handler (its INT 1Ch hook is not used)
    host_speaker_effects(outer);
}

// 121b:0c9a timer_install (platform.md §1): saves the INT 8 vector in menu_timer_old_vector, points
// INT 8 at menu_timer_isr (DOS 3508h / 2508h) and sets the PIT to 3400h (89.63 Hz).
void timer_install()
{
    const FarPtr old = int8_vector();
    seg_u16(CSSEG_menu_timer_old_vector, CS_menu_timer_old_vector) = old.off;
    seg_u16(CSSEG_menu_timer_old_vector, u16(CS_menu_timer_old_vector + 2)) = old.seg;
    mem_far_set(0, 8 * 4, {MENU_ISR_OFF, seg_of(CSSEG_menu_timer_old_vector)});
    host_set_timer(PIT_DIV_MENU, timer_interrupt);
}

// 121b:0cc4 timer_restore (platform.md §1): the PIT back to 18.2 Hz, INT 8 back to the saved
// vector, then the speaker music reset (speaker_music_reset 1b37:0002).
void timer_restore()
{
    host_set_timer(PIT_DIV_BIOS, timer_interrupt);
    mem_far_set(0, 8 * 4,
                {seg_u16(CSSEG_menu_timer_old_vector, CS_menu_timer_old_vector),
                 seg_u16(CSSEG_menu_timer_old_vector, u16(CS_menu_timer_old_vector + 2))});
    speaker_music_reset();
}

// 121b:0ce2 menu_timer_isr (platform.md §1): the tick counter, the music, and every fifth
// interrupt the old INT 8 handler (the BIOS clock; the EOI is then its job).
void menu_timer_isr()
{
    ds_u16(DS_tick_counter)++;
    ds_u8(DS_menu_timer_divider)++;
    music_tick();
    speaker_music_tick();
    if (ds_u8(DS_menu_timer_divider) >= 5) {
        ds_u8(DS_menu_timer_divider) = 0;
        run_int8_handler({seg_u16(CSSEG_menu_timer_old_vector, CS_menu_timer_old_vector),
                          seg_u16(CSSEG_menu_timer_old_vector, u16(CS_menu_timer_old_vector + 2))});
    }
}

// 15d4:0007 bios_wait_ticks (platform.md §8): waits until at least n BIOS ticks (INT 1Ah) have
// passed; the elapsed count is formed from the low words, with the original's signed compares and
// its wrap-around case. Returns 0. Each poll pumps the host.
u16 bios_wait_ticks(s16 n)
{
    const u16 start = u16(bios_ticks());
    if (n <= 0) return 0;
    for (;;) {
        host_pump();
        const u16 now = u16(bios_ticks());
        u16 elapsed;
        if (s16(start) > s16(now)) elapsed = u16(0xFFFF - start + now + 1);
        else elapsed = u16(now - start);
        if (!(s16(elapsed) < n)) return 0;
    }
}

} // namespace gb
