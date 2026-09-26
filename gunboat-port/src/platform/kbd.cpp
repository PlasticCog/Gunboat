// The keyboard (platform.md §2): the game's INT 9 handler and its installation, segment 121b. The
// Test Drive III port's platform/kbd.c (MIT) is the model; Gunboat's build has no Ctrl table, no
// Space bit, its own NumLock mapping and a Backspace flag, and keeps the BIOS shift and lock state
// where the BIOS does (0040:0017, 0040:0018, 0040:0097 in mem[]).
#include "platform/platform.hpp"

#include "mem.hpp"
#include "symbols.hpp"

namespace gb {

namespace {

constexpr u16 KBD_ISR_OFF = 0x0A9C;  // 121b:0a9c

u8 &bda17() { return mem_u8(0x40, 0x17); }
u8 &bda18() { return mem_u8(0x40, 0x18); }
u8 &bda97() { return mem_u8(0x40, 0x97); }

// The last byte the ISR wrote to port 60h (the LED command EDh, then the LED bits); -1 = none.
// Hardware state, not game state (PORT): the keyboard answers each with FAh (kbd_byte).
int kbd_sent = -1;

void send_to_keyboard(u8 b) { kbd_sent = b; }

} // namespace

// 121b:0a4d kbd_install (platform.md §2): clears the ISR state, saves the INT 9 vector (DOS 3509h)
// in kbd_old_vector and points INT 9 at kbd_isr (DOS 2509h). PORT: port 61h bit 7 is not modelled.
void kbd_install()
{
    ds_u8(DS_kbd_prefix_state) = 0;
    ds_u8(DS_kbd_typematic) = 0;
    ds_u8(DS_kbd_last_scancode) = 0;
    ds_u8(DS_isr_key_code) = 0;
    ds_u8(DS_held_keys) = 0;
    ds_u16(DS_kbd_old_vector) = mem_u16(0, 9 * 4);
    ds_u16(u16(DS_kbd_old_vector + 2)) = mem_u16(0, 9 * 4 + 2);
    mem_u16(0, 9 * 4) = KBD_ISR_OFF;
    mem_u16(0, 9 * 4 + 2) = seg_of(CSSEG_scancode_table);
}

// 121b:0a88 kbd_restore: INT 9 = the saved vector.
void kbd_restore()
{
    mem_u16(0, 9 * 4) = ds_u16(DS_kbd_old_vector);
    mem_u16(0, 9 * 4 + 2) = ds_u16(u16(DS_kbd_old_vector + 2));
}

bool kbd_isr_installed()
{
    return mem_u16(0, 9 * 4) == KBD_ISR_OFF && mem_u16(0, 9 * 4 + 2) == seg_of(CSSEG_scancode_table);
}

// 121b:0a9c kbd_isr (platform.md §2): one byte from port 60h (sc). Keeps the prefix state, Ctrl and
// Backspace flags, the shift and lock bits, the translated key code isr_key_code, the last scan
// code, and the held-key bits held_keys (up 1, down 2, left 4, right 8, Enter 10h).
void kbd_isr(u8 sc)
{
    u8 ah = sc, al, bl;
    // port 61h acknowledge pulse: nothing to model
    if (ds_u8(DS_kbd_prefix_state) == 0xED) {  // the byte after the LED command (normally FAh)
        al = u8((bda17() >> 4) & 7);
        bda97() = u8((bda97() & 0xF8) | al);
        send_to_keyboard(al);
        ds_u8(DS_kbd_prefix_state) = 0;
    }
    al = ah;
    if (al >= 0xE0) {
        if (al == 0xE0 || al == 0xE1) ds_u8(DS_kbd_prefix_state) = al;
        return;  // FAh etc.: ignored
    }
    al &= 0x7F;
    if (al == 0x1D || al == 0x61) ds_u8(DS_ctrl_held) = (ah & 0x80) ? 0 : 1;
    if (al == 0x2A || al == 0x36) {  // shift keys
        const u8 bit = al == 0x2A ? 2 : 1;
        if (ds_u8(DS_kbd_prefix_state) == 0xE0) {  // the grey keys' fake shifts
            ds_u8(DS_kbd_prefix_state) = 0;
            return;
        }
        if (!(ah & 0x80)) bda17() |= bit;
        else bda17() &= u8(~bit);
    }
    if (ds_u8(DS_kbd_prefix_state) == 0xE1) {  // Pause: E1 1D 45 / E1 9D C5
        if (al == 0x1D) return;
        if (al != 0x45) {
            ds_u8(DS_kbd_prefix_state) = 0;
            return;
        }
        ah++;
        al++;  // 45h -> 46h, then as E0 46h
        ds_u8(DS_kbd_prefix_state)--;
    }
    if (ds_u8(DS_kbd_prefix_state) == 0xE0) {  // grey keys -> pseudo scan codes
        al = u8(al - 0x1C);
        if (al >= 2) {
            al = u8(al - 0x17);
            if (al >= 6) al = u8(al - 0x0D);
        }
        al = seg_u8(CSSEG_scancode_e0_table, u16(CS_scancode_e0_table + al));  // cs: xlat
        if (al == 0) {
            ds_u8(DS_kbd_prefix_state) = 0;
            return;
        }
        ah = u8((ah & 0x80) | al);
        ds_u8(DS_kbd_prefix_state) = 0;
    }
    // typematic filter
    if (!(ah & 0x80)) {
        if (al == ds_u8(DS_kbd_typematic)) return;
        ds_u8(DS_kbd_typematic) = ah;
    } else if (al == ds_u8(DS_kbd_typematic)) {
        ds_u8(DS_kbd_typematic) = ah;
    }
    // lock keys
    const u8 lock = al == 0x3A ? 0x40 : al == 0x45 ? 0x20 : al == 0x46 ? 0x10 : 0;
    if (lock) {
        if (!(ah & 0x80)) {
            if (!(bda18() & lock)) {
                bda18() |= lock;
                bda17() ^= lock;
                send_to_keyboard(0xED);
                ds_u8(DS_kbd_prefix_state) = 0xED;
            }
        } else {
            bda18() &= u8(~lock);
        }
    }
    if (!(ah & 0x80)) {  // translate make codes
        const u16 table = (bda17() & 3) ? CS_scancode_shift_table : CS_scancode_table;
        al = seg_u8(CSSEG_scancode_table, u16(table + ah));  // cs: xlat
        if ((bda17() & 0x20) && al >= 0x10) {  // NumLock
            if (al <= 0x19) al = u8(al + 0x20);
            if (al == 0x1E) al = u8(al + 0x10);
        }
        if ((bda17() & 0x40) && al >= 0x41 && al <= 0x7A && !(al >= 0x5B && al < 0x61)) al ^= 0x20;  // CapsLock
        ds_u8(DS_isr_key_code) = al;
    }
    ds_u8(DS_kbd_last_scancode) = ah;
    bl = ah & 0x7F;
    if (bl == 0x0E) ds_u8(DS_fast_forward_key) = (ah & 0x80) ? 0 : 1;  // Backspace
    al = ds_u8(DS_held_keys);
    if (bl == 0x60 || bl == 0x1C) {  // Enter, keypad Enter
        al |= 0x10;
        if (ah & 0x80) al &= 0xEF;
    } else {
        if (bl == 0x29) bl = 0x48;  // ` acts as Up
        if (bl == 0x2B) bl = 0x4B;  // \ acts as Left
        bl = u8(bl - 0x47);
        if (bl & 0x80) return;
        if (bl <= 0x0A) bl = seg_u8(CSSEG_kbd_keypad_dir, u16(CS_kbd_keypad_dir + bl));
        bl = u8(bl - 0x1F);
        if (bl & 0x80) return;
        if (bl >= 8) return;
        al |= seg_u8(CSSEG_kbd_dir_or, u16(CS_kbd_dir_or + bl));
        if (ah & 0x80) al &= seg_u8(CSSEG_kbd_dir_or, u16(CS_kbd_dir_or + 8 + bl));
    }
    ds_u8(DS_held_keys) = al;
    // out 20h, 20h (EOI)
}

// PORT: the keyboard controller. A byte from the host goes to INT 9: to kbd_isr while it is the
// installed handler, otherwise to the BIOS (whose key buffer the VGA path never reads: dropped).
// After a byte whose ISR run sent something to the keyboard (the LED command, the LED bits), the
// keyboard answers with an ACK FAh, as the real one does.
void kbd_byte(u8 b)
{
    if (!kbd_isr_installed()) return;
    kbd_sent = -1;
    kbd_isr(b);
    for (int guard = 0; kbd_sent >= 0 && guard < 4; guard++) {
        kbd_sent = -1;
        kbd_isr(0xFA);
    }
}

// PORT: the host lost the keyboard focus: a Shift released outside the window would stay down.
void kbd_focus_lost() { bda17() &= u8(~0x03); }

} // namespace gb
