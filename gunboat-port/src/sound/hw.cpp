// The sound hardware of the modelled machine (sound.md §3.4). PORT: a model of the machine, not a
// translation. The Unicorn tests run the same model (tests/difftest/soundmodel.py); keep the two in
// step.
//
// PC speaker: PIT channel 2 (ports 42h/43h) and the gate bits 0-1 of port 61h. The speaker sounds
// when both gate bits are set. host_speaker(divisor, on) receives one call per completed divisor load
// (the high byte written to port 42h) and one per write to port 61h that changes bits 0-1; on = both
// bits set. The game always writes the control word B6h before a divisor and both divisor bytes in
// a row, so a divisor load is one call here.
//
// Absent devices: no MPU-401 (ports 330h/331h), no Game Blaster / Sound Blaster (220h..22Fh), no AdLib
// driver (the INT 65h vector is 0000:0000), no Tandy sound chip (C0h/C1h, parked): reads give FFh,
// writes do nothing.
#include "sound/sound.hpp"

#include "host.hpp"

namespace gb {

namespace {
// PORT: hardware state, not game state.
u16 pit2_divisor;   // the last divisor loaded into PIT channel 2
u8 port61_gate;     // port 61h bits 0-1
} // namespace

void spk_gate(bool on)
{
    const u8 bits = on ? 3 : 0;
    if (bits == port61_gate) return;
    port61_gate = bits;
    host_speaker(pit2_divisor, on);
}

void spk_pit2_mode() {}  // the control word only selects the mode; nothing to hear

void spk_pit2_divisor(u16 d)
{
    pit2_divisor = d;
    host_speaker(d, port61_gate == 3);
}

void tandy_out(u16, u8) {}  // PORT: the Tandy 3-voice sound chip is parked
void tandy_enable() {}      // PORT: bits 5-6 of port 61h (Tandy sound enable): no speaker change

u8 no_device_in(u16) { return 0xFF; }
void no_device_out(u16, u8) {}

void sound_parked(const char *what) { host_fatal("sound: %s is not ported (parked device path)", what); }

void spk_hw_state(u16 *divisor, u8 *gate_bits)
{
    if (divisor) *divisor = pit2_divisor;
    if (gate_bits) *gate_bits = port61_gate;
}

void spk_hw_set(u16 divisor, u8 gate_bits)
{
    pit2_divisor = divisor;
    port61_gate = gate_bits & 3;
}

} // namespace gb
