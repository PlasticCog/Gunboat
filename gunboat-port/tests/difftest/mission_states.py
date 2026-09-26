"""Mission memory states for the differential tests of the mission's functions (simulation,
render3d, hud, world): the original runs main's way into mission_run in Unicorn and is stopped when
it enters game_frame for the k-th time; the memory there is the state a test starts from.

    m = mission_state(h, practice=1)                 # gunnery practice, at the first game_frame
    m = mission_state(h, region=0, mission=3, frames=40, keys=[...])
    m = mission_state(h, region=1, mission=5, rank=9, station=1, night=True)

frames = the number of game_frame calls the original completes before the stop (0 = the state
after mission_run's set-up, when it calls game_frame the first time). keys: one key per mission
loop pass (0 = none), delivered into isr_key_code when the loop reads its start tick. States are
cached per harness (the set-up takes about a second in Unicorn).

The station screens and mission_load wait on fades and BIOS ticks: the poll points are those of
test_front.POLLS plus the mission loop's own (MISSION_POLL, where it reads DS:08C0 at the top of
each pass: one tick per pass, like the port's host_pump there)."""
import struct

from unicorn import UC_HOOK_CODE

from gbdiff import DS_BASE, Mismatch, lin, put8, put16, seg_of
from test_sound import sound
from test_title import main_state
from test_video import run_original

POLLS = ['121b:07eb', '121b:0841', '15d4:0019']
MISSION_POLL = '05bd:0000'   # replaced by the real address below (the loop's "loop_start_ticks = DS:08C0")

GAME_FRAME = (0x0919, 0x8930)


def _find_mission_poll():
    """The address in mission_run's loop where it reads DS:08C0 (mov ax,[08C0]) into its start
    tick: a poll point that runs once per loop pass."""
    from gbdiff import load_image
    m = load_image()
    base = lin(seg_of(0x05BD), 0)
    code = bytes(m[base + 0x000A:base + 0x0500])
    at = code.find(bytes([0xA1, 0xC0, 0x08]))
    if at < 0:
        raise RuntimeError('mission_run: no read of DS:08C0 found')
    return '05bd:%04x' % (0x000A + at)


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def _base(h, practice, region, mission, rank, station, weapons, engines, sea, night, targets):
    """main's memory when it calls mission_run: the start-up done by the original, then the variables
    the title menu (practice) or the front end (a campaign mission) set."""
    m = main_state(h)
    if practice is not None:                           # title_menu's Enter on a practice item
        put16(m, 0xB503, 3)
        put16(m, 0xB505, 1 if practice == 1 else 2 if practice == 2 else 0)
        put16(m, 0xB509, 0x18 + (1 if practice == 1 else 2 if practice == 2 else 0))
        put8(m, 0xB804, 0)
        put8(m, 0xB805, 1)
        put8(m, 0xB806, 0)
        put8(m, 0xB807, 0)
        put16(m, 0xB4FF, 3 if practice == 3 else 1)
        put16(m, 0xF110, practice)
        put16(m, 0x0086, {1: 2, 2: 4}.get(practice, 1))
    else:                                              # the front end's choices
        put16(m, 0xB503, region)
        put16(m, 0xB505, mission)
        put16(m, 0xB509, 8 * region + mission)
        put8(m, 0xB50D, rank)
        bow, stern, midship = weapons
        put8(m, 0xB804, bow)
        put8(m, 0xB806, stern)
        put8(m, 0xB807, midship)
        put8(m, 0xB805, engines)
        put16(m, 0xB4FF, sea)
        put16(m, 0xF110, 0)
        put8(m, 0xB803, targets)
        put8(m, 0xB7FC, 0 if night else 1)
        put16(m, 0x0086, station)
    put8(m, 0xF398, 0)
    put16(m, 0x0082, 3)
    return run_original(h, m, 'gfx_set_copy_page', [2])


def mission_state(h, practice=None, region=0, mission=1, rank=1, station=1, weapons=(0, 0, 1), engines=1,
                  sea=1, night=False, targets=0, frames=0, keys=()):
    """The memory when the original enters game_frame for the (frames + 1)-th time (see the module
    docstring). Returns a fresh bytearray."""
    global MISSION_POLL
    if MISSION_POLL == '05bd:0000':
        MISSION_POLL = _find_mission_poll()
    key = (practice, region, mission, rank, station, tuple(weapons), engines, sea, night, targets, frames,
           tuple(keys))
    cache = h.__dict__.setdefault('_mission_states', {})
    if key not in cache:
        m = _base(h, practice, region, mission, rank, station, weapons, engines, sea, night, targets)
        count = [0]
        at = lin(seg_of(GAME_FRAME[0]), GAME_FRAME[1])

        def on_frame(uc, address, size, _):
            if count[0] == frames:
                h.orig.fail('stop')
            count[0] += 1
        hook = h.orig.uc.hook_add(UC_HOOK_CODE, on_frame, None, at, at)
        # keys: one per loop pass, at the loop's tick read (the port pumps the host there)
        with sound(h):
            h.set_tick((POLLS + [MISSION_POLL], 'both', [0] + list(keys)))
            try:
                h.orig.set_memory(m)
                file_seg, off, far = h.sym.func('mission_run')
                h.orig.call(file_seg, off, far, {}, (), max_insns=4_000_000_000)
                raise Mismatch('mission_run returned before game_frame call %d' % (frames + 1))
            except Mismatch as e:
                if str(e) != 'original: stop':
                    raise
            finally:
                h.orig.uc.hook_del(hook)
                h.set_tick(None)
        cache[key] = bytes(h.orig.memory())
    return bytearray(cache[key])
