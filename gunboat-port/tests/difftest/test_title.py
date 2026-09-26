"""The title and the main menu (game_flow.md §3): title_menu from the state main reaches, with keys.

Poll points: the fades (121b:07eb, 121b:0841), the dissolve (121b:0670), the sprite loop (00f2:03d0),
bios_wait_ticks (15d4:0019). Each gives one tick of both kinds (DS:08C0 and the BIOS clock) and the
next key of the case's list."""
from gbdiff import DS_BASE, put8, put16
from test_sound import sound
from test_video import run_original

POLLS = ['121b:07eb', '121b:0841', '121b:0670', '00f2:03d0', '15d4:0019']


def main_state(h):
    """The memory when main calls title_menu: the archive directory, config_load, kbd_install,
    mem_alloc_all, run by the original."""
    m = h.fresh_memory()
    data = (h.dos.dir / 'DATAC.DAT').read_bytes()
    m[DS_BASE + 0x027C:DS_BASE + 0x027C + len(data)] = data
    put16(m, 0x0082, 0x00FF)
    put8(m, 0xF398, 1)
    m = run_original(h, m, 'config_load')
    put16(m, 0x0082, 0)
    m = run_original(h, m, 'kbd_install')
    return run_original(h, m, 'mem_alloc_all')


# Polls before the menu loop reads keys (kbd_flush_key clears any earlier key): the fade-out at the
# start and the fade-in of the menu (16 each). In the loop, bios_wait_ticks(2) polls twice per
# iteration, and a movement key is taken every second iteration (the debounce).
BEFORE_MENU = 32


def spaced(keys):
    out = [0] * BEFORE_MENU
    for k in keys:
        out += [k, 0, 0, 0]
    return out


def menu(h, keys, first_run=0, raw=False):
    m = main_state(h)
    put8(m, 0xF398, first_run)
    keys = keys if raw else spaced(keys)
    with sound(h):
        h.check('title_menu', m, outputs=['ax'], tick=(POLLS, 'both', keys), max_insns=2_000_000_000,
                label='first run %d, keys %s' % (first_run, bytes(keys).hex()))


def test_menu_choices(h, rng, scale):
    cases = [
        [0x0D],                                        # report for duty
        [0x96, 0, 0, 0x0D],                            # right: gunnery practice
        [0x98, 0, 0, 0x0D],                            # down: grenade practice
        [0x99, 0, 0, 0x0D],                            # 3: pilot practice
        [0x98, 0x96, 0x96, 0x92, 0x0D],                # moves, then Enter
        [0x44],                                        # D: the demo
        [0x64],                                        # d
    ]
    for keys in cases:
        menu(h, keys)
    return len(cases)


def test_menu_idle_demo(h, rng, scale):
    menu(h, [])  # no key: the demo starts after 113h menu iterations
    return 1


def test_first_run(h, rng, scale):
    """The whole intro: COPY, ACCO, the sprite, TITLE1 with the music, TITLE2, the credits; a key
    ends the waits early (0 = no key at that tick)."""
    menu(h, [0] * 40 + [0x20] + [0] * 400 + [0x0D], first_run=1, raw=True)
    return 1


TESTS = [test_menu_choices, test_menu_idle_demo, test_first_run]
