"""The last simulation and renderer functions under game_frame (simulation.md §1.2, render3d.md §5):
boat_motion, crew_pilot, incoming_fire, object_update, object_frame and the F4 key handler; later
also game_frame, mission_run and the station screens.

The states are the original's own: test_render.frame_states (missions at several frames, moved
boats, chase view, night, lights) and test_sim2's combat states (enemies alerted and shooting),
captured where the original's game_frame enters each routine (test_render.capture), then varied
where the routine branches."""
import struct

from gbdiff import DS_BASE, put8, put16
from test_render import BIG, capture, frame_states
from test_sim2 import combat, visible_state
from test_w2b import DivideStop

PICKS = [0, 1, 2, 3, 5, 8, 13]


def get8(m, off):
    return m[DS_BASE + off]


def get16(m, off):
    return struct.unpack_from('<H', m, DS_BASE + off)[0]


def states(h, rng, scale, combats=4):
    out = list(frame_states(h, rng, scale, variants=4))
    for i in range(combats):
        out.append(('combat %d' % i, combat(h, rng, i)))
    return out


def captured(h, rng, scale, target, caller='game_frame', picks=PICKS, combats=4):
    for label, m in states(h, rng, scale, combats):
        for k, (regs, words, snap) in enumerate(capture(h, m, target, picks, caller)):
            yield '%s, call %d' % (label, k), regs, words, snap


def test_boat_motion(h, rng, scale):
    n = 0
    for label, regs, words, snap in captured(h, rng, scale, 'boat_motion'):
        for variant in range(3):
            m = bytearray(snap)
            if variant == 1:
                put8(m, 0xB828, 0)                               # stopped: the mission stop test
            elif variant == 2:
                put8(m, 0xB828, rng.choice([1, 0x7F, 0x80, 0xF0, rng.randrange(256)]))
                put8(m, 0xB81E, rng.randrange(256))
            r = dict(regs, si=rng.randrange(0x10000) if variant else regs['si'])
            h.check('boat_motion', m, regs=r, outputs=['si'], label='%s v%d' % (label, variant), max_insns=BIG)
            n += 1
    return n


def test_crew_pilot(h, rng, scale):
    n = 0
    for label, regs, words, snap in captured(h, rng, scale, 'crew_pilot'):
        for _ in range(4):
            m = bytearray(snap)
            put16(m, 0x0070, rng.choice([0, 0, 1]))
            put16(m, 0x0086, rng.choice([1, 1, 2, 3, 4, 0x101]))
            put8(m, 0xD96B, rng.choice([0, 0, 1]))
            put8(m, 0xD514, rng.randrange(256))
            m[DS_BASE + 0x88] = rng.randrange(256)
            put8(m, 0xD520, rng.choice([0, 1, 2, 3]))
            put8(m, 0xB816, rng.randrange(256))
            put8(m, 0xB817, rng.randrange(256))
            h.check('crew_pilot', m, regs=regs, label=label, max_insns=BIG)
            n += 1
    return n


def test_incoming_fire(h, rng, scale):
    """The incoming shots: timers that run out now (1) or later, every weapon class and accuracy,
    missiles (class 5, bearing slots) against every speed difference, day and night, random draws."""
    n = 0
    for label, regs, words, snap in captured(h, rng, scale, 'incoming_fire'):
        for case in range(30 * scale):
            m = bytearray(snap)
            missiles = case % 2 == 0                             # every other case: missiles firing now
            for i in range(16):
                put8(m, 0xD71F + i, rng.choice([1, 1, 1, 0, 2]) if missiles else rng.choice([0, 0, 1, 1, 2, rng.randrange(256)]))
                d = rng.randrange(256)
                if rng.random() < (0.8 if missiles else 0.3):
                    d = 0xA0 | rng.randrange(32)                 # a missile
                put8(m, 0xD70E + i, d)
            put8(m, 0xD70D, rng.randrange(1, 17))
            put8(m, 0xD6F8, rng.randrange(256))
            put8(m, 0xD6F9, rng.randrange(256))
            put8(m, 0xB828, rng.choice([0, 0x10, 0x7F, 0x80, 0xF0, rng.randrange(256)]))
            put8(m, 0xB7FC, rng.randrange(2))
            m[DS_BASE + 0x88:DS_BASE + 0x8C] = bytes(rng.randrange(256) for _ in range(4))
            r = dict(regs, si=rng.choice([regs['si'], rng.randrange(0x10000)]))
            h.check('incoming_fire', m, regs=r, outputs=['si'], label=label, max_insns=BIG)
            n += 1
    return n


def test_object_update(h, rng, scale):
    """Ramming: the entry object_update picks (the last listed object >= 48h) moved within E0h and in
    front of the bow (or not), of every kind (mines, wrecks, the kinds it ignores), at speed and not,
    in both sort modes; and lists with no such entry."""
    n = 0
    kinds = [0, 0x11, 0x13, 0x13, 0x17, 0x21, 3, 4, 5, 6, 7, 0x1D, 0x1E, 0x1F, 0x20, 0x38, 0x39, 0x3C]
    for label, regs, words, snap in captured(h, rng, scale, 'object_update', caller='game_frame'):
        count = get16(snap, 0xB83D)
        picked = [i for i in range(count) if get16(snap, 0x523E + 2 * i) >= 0x48]
        for _ in range(12 * scale):
            m = bytearray(snap)
            put8(m, 0xD8BC, rng.choice([0, 0, 0, 1]))
            if picked and rng.random() < 0.9:
                i = picked[-1]
                o = get16(m, 0x523E + 2 * i)
                put16(m, 0x4C96 + 2 * i, rng.choice([0, 0x40, 0xE0, 0xE0, 0xE1, rng.randrange(0x100)]))
                # the bow's sector: bearing - view heading + hull heading - 20h < 50h (80h turned astern)
                rel = rng.choice([0, 0x27, 0x28, 0x4F, 0x50, rng.randrange(0x50), rng.randrange(256)])
                put8(m, 0x4E00 + i, (rel + get8(m, 0xD191) - get8(m, 0xB81E) + 0x20) & 0xFF)
                put8(m, 0xB95D + o, rng.choice(kinds))
            elif count and rng.random() < 0.5:
                for i in range(count):                               # temporary objects only
                    put16(m, 0x523E + 2 * i, rng.randrange(0x48) & 0xFFFE)
            speed = rng.choice([0, 5, 0x0E, 0x0F, 0x40, 0x80, 0xF1, 0xF1, rng.randrange(256)])
            put8(m, 0xB828, speed)
            if speed & 0x80 and picked:                              # astern: the sector turns by 80h
                put8(m, 0x4E00 + picked[-1], (get8(m, 0x4E00 + picked[-1]) + 0x80) & 0xFF)
            m[DS_BASE + 0x88] = rng.choice([0, 0x20, rng.randrange(256)])
            put16(m, 0xB503, rng.choice([0, 1, 2, 3]))
            h.check('object_update', m, regs=regs, label=label, max_insns=BIG)
            n += 1
    return n


def test_object_frame(h, rng, scale):
    n = 0
    for label, regs, words, snap in captured(h, rng, scale, 'object_frame'):
        # (the captured states only: forcing D8BC on a list that was not rebuilt, or off on one that
        # was, runs the original's list sort away)
        h.check('object_frame', snap, regs=regs, label=label, max_insns=BIG)
        m = bytearray(snap)                                          # the 1FEh clamp of group A
        put16(m, 0xD960, rng.choice([0x1FF, 0x200, 0x2FF]))
        h.check('object_frame', m, regs=regs, label=label + ' (group A clamp)', max_insns=BIG)
        n += 2
    return n


def test_key_f4(h, rng, scale):
    """F4 (reverse course) at the gun stations: the route state random around the real one (the
    current and the previous waypoint, missing or not), the message shown or suppressed."""
    n = 0
    for i in range(6):
        base = visible_state(h, rng) if i % 2 else combat(h, rng, i % 4)
        for _ in range(40 * scale):
            m = bytearray(base)
            put16(m, 0x0086, rng.choice([2, 3, 4]))
            put8(m, 0xB800, rng.choice([0, 1, 2]))
            put8(m, 0xD684, rng.randrange(2))
            put8(m, 0xD686, rng.choice([0, 1, 2, 3, 7, 0x1F, rng.randrange(256)]))
            put8(m, 0xD687, rng.choice([0, 1, 2, 3, rng.randrange(256)]))
            put8(m, 0xD688, rng.choice([0, 1, 4, 0x44, 0x80, 0xFF, rng.randrange(256)]))
            cell = get16(m, 0xD689)
            put16(m, 0xD689, rng.choice([cell, cell, 0, 1, rng.randrange(0x40), rng.randrange(0x1100)]))
            with DivideStop(h) as d:  # a cell of 4352 or more: R6003 on both sides
                d.check('key_f4_reverse_course', m, regs={'cx': rng.randrange(256)}, label='state %d' % i,
                        max_insns=BIG)
            n += 1
    return n


TESTS = [test_boat_motion, test_crew_pilot, test_incoming_fire, test_object_update, test_object_frame,
         test_key_f4]
