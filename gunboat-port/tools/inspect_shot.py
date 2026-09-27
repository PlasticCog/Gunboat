"""The game state of an F12 screenshot (shot_NNNN.mem, the port's memory image): where the boat is,
and the objects of the game's visible list, with their kind, flags (the damage bits 38h of the flags byte),
class and position. For bug reports: a picture says what looks wrong, this says what the game has.

    python tools/inspect_shot.py 3            # shot_0003.mem in the settings folder's Screenshots
    python tools/inspect_shot.py path/to/shot_0003.mem
"""
import os
import pathlib
import struct
import sys

DS = 0x2B73 * 16
# DGROUP offsets (reverse_engineering/symbols.csv)
STATION, REGION, MISSION = 0x0086, 0xB503, 0xB505
VISIBLE_COUNT, VISIBLE_OBJECT, VISIBLE_DISTANCE, VISIBLE_BEARING = 0xB83D, 0x523E, 0x4C97, 0x4E00
OBJECT_WORD, OBJECT_X, OBJECT_Y, OBJECT_CLASS = 0xB95D, 0xC12D, 0xC8FD, 0x541B
CAMERA_QX, CAMERA_QY = 0xD972, 0xD974

KINDS = {  # world.md §6.3 (region-dependent names joined by /)
    0x01: 'tank', 0x02: 'light tank', 0x03: 'APC', 0x04: 'sampan / skiff', 0x05: 'sampan / powerboat',
    0x06: 'junk / powerboat', 0x07: 'machine gun', 0x08: 'mortar nest', 0x09: 'enemy fortification (bunker)',
    0x0A: 'infantry', 0x0B: 'infantry', 0x0C: 'infantry', 0x0D: 'ammo cache / camp', 0x0E: 'huts / barracks',
    0x0F: 'huts / SAM / camp', 0x10: 'dock', 0x11: 'bridge base', 0x12: 'missile', 0x13: 'mine',
    0x14: 'infantry / truck / ASM launcher', 0x16: 'practice target', 0x17: 'helicopter', 0x18: 'downed helicopter',
    0x19: 'civilian', 0x1A: 'civilian', 0x1B: 'civilian hut', 0x1C: 'civilian house', 0x1D: 'civilian house',
    0x1E: 'civilian boat', 0x1F: 'civilian boat', 0x20: 'civilian dock', 0x21: 'civilian bridge base',
    0x22: 'car', 0x23: 'PBR', 0x24: 'ex-POW', 0x25: 'SEAL unit', 0x26: 'US infantry / DEA / barge',
    0x28: 'buoy', 0x29: 'buoy', 0x2A: 'tree', 0x2B: 'tree', 0x2C: 'tree', 0x2D: 'tree', 0x2E: 'stump',
    0x2F: 'buffalo / monument / statue', 0x30: 'burning rubble', 0x31: 'scorched tree',
    0x32: 'dead beast / rubble / statue', 0x33: 'bodies', 0x34: 'bodies', 0x35: 'bodies', 0x37: 'broken buoy',
    0x38: 'capsized boat', 0x39: 'rock',
}
REGIONS = {0: 'Vietnam', 1: 'Colombia', 2: 'Panama', 3: 'practice'}


def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else ''
    if arg.isdigit():
        base = pathlib.Path(os.environ.get('APPDATA', pathlib.Path.home() / '.local' / 'share')) / 'Gunboat'
        path = base / 'Screenshots' / ('shot_%04d.mem' % int(arg))
    else:
        path = pathlib.Path(arg)
    m = path.read_bytes()

    def u8(off):
        return m[DS + off]

    def u16(off):
        return struct.unpack_from('<H', m, DS + off)[0]

    region = u16(REGION)
    print('%s: region %d (%s), mission %d, station %d, camera %04X,%04X (quarter units)'
          % (path.name, region, REGIONS.get(region, '?'), u16(MISSION), u16(STATION) & 0xFF, u16(CAMERA_QX),
             u16(CAMERA_QY)))
    n = u16(VISIBLE_COUNT)
    print("%d objects in the game's visible list (its order; dist = distance class):" % n)
    print('  entry obj   kind  name                              flags dmg class  dist bearing  x      y')
    for e in range(min(n, 0xB5)):
        obj = u16(VISIBLE_OBJECT + 2 * e)
        kind, flags = u8(OBJECT_WORD + obj), u8(OBJECT_WORD + obj + 1)
        cls = u8(OBJECT_CLASS + 2 * kind)
        print('  %4d  %04X  %02X    %-33s %02X    %d   %02X    %4d  %3d     %04X   %04X'
              % (e, obj, kind, KINDS.get(kind, 'effect' if kind >= 0x3F else '?')[:33], flags, (flags >> 3) & 7, cls,
                 u8(VISIBLE_DISTANCE + 2 * e), u8(VISIBLE_BEARING + e), u16(OBJECT_X + obj), u16(OBJECT_Y + obj)))


if __name__ == '__main__':
    main()
