// main and the ways out (game_flow.md §1).
#include "game/flow.hpp"

#include "host.hpp"
#include "mem.hpp"
#include "platform/platform.hpp"
#include "symbols.hpp"

namespace gb {

// 0000:021e quit_to_dos (game_flow.md §1).
// TODO(verify): placeholder until music_stop, gfx_free_page, gfx_set_mode, text_exit_clear,
// engine_sound_off and kbd_restore are ported; then this becomes the original sequence.
void quit_to_dos()
{
    mem_free_all();
    crt_exit(0);
}

// 0000:0276 fatal_exit (game_flow.md §1): messages DS:00A6 (1), DS:00CA (2), DS:00F4 (3).
// TODO(verify): placeholder, as quit_to_dos. PORT: the message goes to a message box.
void fatal_exit(s16 code)
{
    mem_free_all();
    const u16 msg = code == 1 ? 0x00A6 : code == 2 ? 0x00CA : code == 3 ? 0x00F4 : 0;
    if (msg) host_fatal("%s", ds_str(msg));
    crt_exit(code);
}

} // namespace gb
