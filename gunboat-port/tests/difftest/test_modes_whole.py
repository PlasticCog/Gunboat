"""Whole runs on the other video cards (video.md §6): the title and its menu, the title's demo,
practice missions with a tour of the stations, and whole front-end runs, on the EGA, CGA, Tandy and
Hercules machines. They are the VGA tests of test_title, test_frame and test_front, run from the state
the original's start-up reaches on that machine: GUNBOAT.CFG (in a temporary copy of the game folder)
names the machine's video mode, and the card state after the start-up is where the runs begin. All
memory and the whole card state are compared."""
import contextlib
import pathlib
import shutil
import struct
import tempfile

import mission_states
import test_frame
import test_front
import test_title
from gbdiff import DS_BASE, GAME_DIR, put8, put16
from test_modes import after_original, machine

CFG_MODE = {'ega': 0x0D, 'cga': 0x04, 'tandy': 0x09, 'hercules': 0x0C}


def game_dir(h):
    """A copy of the game folder whose GUNBOAT.CFG chooses the machine's video mode."""
    dirs = h.__dict__.setdefault('_mode_game_dirs', {})
    if h.machine not in dirs:
        d = pathlib.Path(tempfile.mkdtemp(prefix='gb_%s_' % h.machine))
        for f in GAME_DIR.iterdir():
            if f.is_file():
                shutil.copy2(f, d / f.name)
        cfg = next(p for p in d.iterdir() if p.name.upper() == 'GUNBOAT.CFG')
        data = bytearray(cfg.read_bytes())
        struct.pack_into('<H', data, 0, CFG_MODE[h.machine])
        cfg.write_bytes(data)
        dirs[h.machine] = d
    return dirs[h.machine]


def main_state(h):
    """test_title.main_state on this machine: config_load, kbd_install and mem_alloc_all run by the
    original, the card state carried from one to the next (h.card_state is the result's)."""
    h.card_state = None
    m = h.fresh_memory()
    data = (h.dos.dir / 'DATAC.DAT').read_bytes()
    m[DS_BASE + 0x027C:DS_BASE + 0x027C + len(data)] = data
    put16(m, 0x0082, 0x00FF)
    put8(m, 0xF398, 1)
    m = after_original(h, m, 'config_load')
    put16(m, 0x0082, 0)
    m = after_original(h, m, 'kbd_install')
    return after_original(h, m, 'mem_alloc_all')


@contextlib.contextmanager
def on_machine(h):
    """The VGA tests' state builders replaced by this machine's, in the machine's game folder."""
    saved = (test_title.main_state, test_front.main_state, mission_states.main_state, mission_states.run_original)
    test_title.main_state = test_front.main_state = mission_states.main_state = main_state
    mission_states.run_original = after_original
    h.set_game_dirs(str(game_dir(h)))
    try:
        yield
    finally:
        test_title.main_state, test_front.main_state, mission_states.main_state, mission_states.run_original = saved
        h.set_game_dirs(str(GAME_DIR))
        h.card_state = None


def title(h, rng, scale):
    n = 0
    with on_machine(h):
        for keys in ([0x0D], [0x99, 0, 0, 0x0D], [0x98, 0x96, 0x96, 0x92, 0x0D], [0x44]):
            test_title.menu(h, keys)
            n += 1
    return n


def missions(h, rng, scale):
    with on_machine(h):
        return test_frame.test_mission_run(h, rng, scale)


def front(h, rng, scale):
    with on_machine(h):
        return test_front.test_front_end(h, rng, scale)


TESTS = []
for _machine in ('ega', 'cga', 'tandy', 'hercules'):
    for _kind, _fn in (('title', title), ('missions', missions), ('front_end', front)):
        _test = machine(_machine)((lambda f: lambda h, rng, scale: f(h, rng, scale))(_fn))
        _test.__name__ = 'test_%s_%s' % (_kind, _machine)
        globals()[_test.__name__] = _test
        TESTS.append(_test)
