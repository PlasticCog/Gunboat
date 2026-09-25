"""Validate reverse_engineering/symbols.csv and export it for the other tools.

symbols.csv is the single, hand-maintained name list for GB.EXE:
  kind     func (code, SSSS:OOOO) | global (DGROUP, DS:xxxx) | cglobal (code-segment data, SSSS:OOOO)
  address  SSSS:OOOO with the segment as stored in the file, or DS:xxxx
  name     C identifier, unique
  type     C++ signature or data type, when known
  owner    subsystem spec (RE_GUIDE.md): platform, video, game_flow, world, simulation, render3d,
           hud, sound, runtime, engine (0919 functions not yet assigned)
  source   td3-match | codex | index | spec:<name> | ...
  notes    free text

usage: symbols.py            validate, then write reverse_engineering/out/symbols_ghidra.txt
                             (lines for tools/ghidra/ApplySymbols.java; Ghidra segment = file + 1000)
"""
import csv, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RE = os.path.join(ROOT, 'reverse_engineering')
KINDS = {'func', 'global', 'cglobal'}
OWNERS = {'platform', 'video', 'game_flow', 'world', 'simulation', 'render3d', 'hud', 'sound',
          'runtime', 'engine', ''}


def load():
    with open(os.path.join(RE, 'symbols.csv'), newline='', encoding='utf-8') as fh:
        return list(csv.DictReader(fh))


def main():
    rows = load()
    index = json.load(open(os.path.join(RE, 'map', 'gb_functions.json')))
    starts = {f['start'] for f in index['functions']}
    errors, warnings = [], []
    names, addrs = {}, {}
    for n, r in enumerate(rows, 2):
        kind, addr, name = r['kind'], r['address'].lower(), r['name']
        where = 'line %d (%s %s)' % (n, addr, name)
        if kind not in KINDS:
            errors.append('%s: unknown kind %r' % (where, kind))
        if not re.fullmatch(r'[A-Za-z_]\w*', name):
            errors.append('%s: name is not a C identifier' % where)
        if kind == 'global':
            if not re.fullmatch(r'ds:[0-9a-f]{4}', addr):
                errors.append('%s: global address must be DS:xxxx' % where)
        elif not re.fullmatch(r'[0-9a-f]{4}:[0-9a-f]{4}', addr):
            errors.append('%s: address must be SSSS:OOOO' % where)
        elif kind == 'func' and addr not in starts:
            warnings.append('%s: not a function start in map/gb_functions.json' % where)
        if r['owner'] not in OWNERS:
            errors.append('%s: unknown owner %r' % (where, r['owner']))
        if name in names:
            errors.append('%s: name also used on line %d' % (where, names[name]))
        if (kind, addr) in addrs:
            errors.append('%s: address also named on line %d' % (where, addrs[(kind, addr)]))
        names[name], addrs[(kind, addr)] = n, n
    for w in warnings:
        print('warning:', w)
    for e in errors:
        print('error:', e)
    if errors:
        sys.exit(1)

    out = os.path.join(RE, 'out')
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'symbols_ghidra.txt'), 'w', newline='\n') as fh:
        for r in rows:
            addr = r['address'].upper()
            if r['kind'] == 'global':
                fh.write('global %s %s\n' % (addr, r['name']))
            else:
                seg, off = addr.split(':')
                fh.write('%s %04X:%s %s\n' % ('func' if r['kind'] == 'func' else 'global',
                                              int(seg, 16) + 0x1000, off, r['name']))
    funcs = sum(1 for r in rows if r['kind'] == 'func')
    print('%d symbols OK (%d functions = %d%% of %d indexed); wrote out/symbols_ghidra.txt'
          % (len(rows), funcs, 100 * funcs // len(starts), len(starts)))


if __name__ == '__main__':
    main()
