#pragma once
// Sound (sound.md): the effects driver (segment 12ed, PC speaker path; Tandy parked), the music
// sequencer and its device back ends for the PC speaker (1ace, 1af5, 1b37), and the device detection
// (1ace:00e8, 1af5, 1b5f). One function per original function. Assembly routines take the registers
// they read as parameters (di = the voice offset 0, 2, 4, 6 of the effects driver) and return what
// their callers use (a carry flag as bool).
//
// Hardware (hw.cpp, PORT): the PC speaker (PIT channel 2, port 61h bits 0-1) becomes host_speaker();
// the effects timer's PIT channel 0 becomes host_set_timer(); the modelled machine has no MPU-401, no
// AdLib driver, no Game Blaster / Sound Blaster and no Tandy sound chip (sound.md §3.4).
#include "mem.hpp"
#include "types.hpp"

namespace gb {

// ---- effects (sfx.cpp), segment 12ed
void engine_sound_on();                 // 12ed:0000  far
void sfx_play_far(u16 id);              // 12ed:0018  far C
void sfx_play(u16 ax);                  // 12ed:0025  far, AX = effect id
void engine_sound_off();                // 12ed:0063  far
void sfx_timer_isr();                   // 12ed:00a3  the INT 8 handler's body (with 12ed:00b1)
void sfx_timer_tick();                  // 12ed:00eb
void sfx_secondary_step(u16 di);        // 12ed:0284
void sfx_channel_transfer(u16 di);      // 12ed:02b8
void sfx_channel_step(u16 di);          // 12ed:02f4
void sfx_channel_start(u16 di);         // 12ed:0302
void sfx_channel_run(u16 di);           // 12ed:033d
void sfx_parse_command(u16 di);         // 12ed:03c4  far
void sfx_note_on(u16 ax, u16 di);       // 12ed:0472  AL = note 1..12, AH = octave shift
void sfx_note_duration(u16 di);         // 12ed:0523
void sfx_tandy_envelope();              // 12ed:0586  (Tandy only)
void sfx_control(u16 ax, u16 di);       // 12ed:0655  AH = sub-command, AL = the low nibble (unused)
void sfx_cmd_legato(u8 al);             // 12ed:06d1
void sfx_cmd_tempo_shift(u16 ax);       // 12ed:06ec  AL = percent, AH = 1 (+) or 2 (-)
void sfx_cmd_tempo(u8 al);              // 12ed:0726
void sfx_cmd_envelope(u8 al, u16 di);   // 12ed:0732  (Tandy)
void sfx_cmd_volume(u8 al, u16 di);     // 12ed:0747  (Tandy)
void sfx_cmd_raw_length(u8 al);         // 12ed:074e
void sfx_raw_pitch(u16 ax, u16 di);     // 12ed:0752  AH = octave shift
void sfx_speaker_init();                // 12ed:0800
void sfx_silence();                     // 12ed:089d
void sfx_install();                     // 12ed:08c0
void sfx_remove();                      // 12ed:08f3

// ---- the engine noise (engine_sound.cpp), segment 0919
void engine_sound_update();             // 0919:3cc9  rewrites effect 6 from the throttles

// ---- music (music.cpp), segments 1ace and 1af5
void music_play(FarPtr stream, u16 loop);   // 1ace:000e  far C
void music_silence();                       // 1ace:005c  far C
void music_resume();                        // 1ace:00b4  far (unreached)
void sound_detect(u16 mask, FarPtr cms_buf, FarPtr adlib_buf);  // 1ace:00e8  far C
void music_tick();                          // 1af5:0006  far C (menu timer)
u16 mpu_command(u16 cmd);                   // 1af5:0192  AX: 0 = no acknowledge
u16 mpu_reset();                            // 1af5:01be  AX
u16 adlib_load_bin(FarPtr timbres);         // 1af5:0238  AX = the last read's result
u16 adlib_driver_present();                 // 1af5:0290  AX: 0 = no driver
void adlib_driver_init();                   // 1af5:02b0  parked (INT 65h)
void adlib_call(u16 a, u16 b, u16 c);       // 1af5:02b9  parked (INT 65h)
void cms_driver_init();                     // 1af5:0311  parked (CMS.DRV)
void speaker_note_on(u16 ch, u16 note, u16 vel);  // 1af5:033f  far C
void speaker_note_off(u16 ch);                    // 1af5:0394  far C
void speaker_program(u16 ch, u16 program);        // 1af5:03b8  far C (does nothing)
u16 mus_open(u16 name_ds);                  // 1af5:03bd  the DOS handle, or FFFFh
void mus_close(u16 fh);                     // 1af5:03d0
u16 mus_read(u16 fh, FarPtr buf, u16 n);    // 1af5:03dc  bytes read, or 0 on an error

// ---- speaker music (speaker_music.cpp), segment 1b37
void speaker_music_reset();                            // 1b37:0002  far
void speaker_voice_start(u16 script_ds, u16 priority, u16 voice);  // 1b37:003c  far C
void speaker_voice_play(u16 script_ds);                // 1b37:00a6  far C (unreached)
void speaker_music_tick();                             // 1b37:00c0  far (menu timer)
// The script operations (called through the table speaker_script_ops): AL = the command's high
// nibble, DI = the voice record offset, ES:SI = the script pointer. Return the carry flag (true:
// the voice is done for this tick).
bool spk_op_end();                                     // 1b37:01e3
bool spk_op_set(u8 al, u16 di, FarPtr &si);            // 1b37:01e5
bool spk_op_wait(u16 di, FarPtr &si);                  // 1b37:01f3
bool spk_op_return(u16 di, FarPtr &si);                // 1b37:01fb
bool spk_op_add(u8 al, u16 di, FarPtr &si);            // 1b37:0218
bool spk_op_branch(u8 al, u16 di, FarPtr &si);         // 1b37:0226
bool spk_op_compare(u8 al, u16 di, FarPtr &si);        // 1b37:0266
bool spk_op_poke(FarPtr &si);                          // 1b37:027a

// ---- Creative detection (cms_detect.cpp), segment 1b5f
u16 cms_detect();                        // 1b5f:000e  AX: 0 none, bit 0 CMS, 5 SB DSP, +2 OPL at base+8
bool cms_opl_wait(u8 al);                // 1b5f:00b3  carry: time-out
void cms_opl_write(u16 ax);              // 1b5f:00d4  AL = register, AH = value
void cms_opl_delay();                    // 1b5f:00e7
bool cms_dsp_read(u8 &al);               // 1b5f:00f8  carry: time-out
bool cms_dsp_write(u8 al);               // 1b5f:0116  carry: time-out

// ---- the machine's sound hardware (hw.cpp). PORT: hardware state, not game state (sound.md §3.4).
void spk_gate(bool on);          // in al,61h / and al,0FCh or or al,3 / out 61h,al
void spk_pit2_mode();            // out 43h,0B6h: channel 2, low then high byte, square wave
void spk_pit2_divisor(u16 d);    // out 42h,low / out 42h,high
void tandy_out(u16 port, u8 v);  // ports C0h/C1h: parked, not modelled
void tandy_enable();             // in al,61h / or al,60h / out 61h,al: bits 5-6, no speaker change
u8 no_device_in(u16 port);       // an absent device's port: FFh
void no_device_out(u16 port, u8 v);
[[noreturn]] void sound_parked(const char *what);  // a parked device path was reached: fatal
// The speaker hardware's state, for the differential tests (bridge_sound.cpp).
void spk_hw_state(u16 *divisor, u8 *gate_bits);
void spk_hw_set(u16 divisor, u8 gate_bits);

} // namespace gb
