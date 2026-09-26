"""Merge reverse_engineering/spec/<owner>_symbols.csv into reverse_engineering/symbols.csv.

A spec file has the columns kind,address,name,type,notes. Its entries win: an existing symbol at
the same (kind, address) is renamed (the old name is kept in notes as "was <old>"), owner becomes
the spec's owner (the file name before _symbols.csv) and source "spec:<owner>". Type and notes
from the spec replace empty fields and are prepended to existing notes. New symbols are added.
Refuses to write when two addresses would end up with the same name.

usage: merge_symbols.py            (then run symbols.py to validate and export)
"""
import csv, glob, os, re, sys

RE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELDS = ['kind', 'address', 'name', 'type', 'owner', 'source', 'notes']


def key(kind, address):
    return (kind, address.lower())


def sort_key(r):
    a = r['address'].lower()
    if a.startswith('ds:'):
        return (2, int(a[3:], 16))
    seg, off = (int(x, 16) for x in a.split(':'))
    return (0 if r['kind'] == 'func' else 1, seg * 16 + off)


def main():
    path = os.path.join(RE, 'symbols.csv')
    with open(path, newline='', encoding='utf-8') as fh:
        rows = {key(r['kind'], r['address']): r for r in csv.DictReader(fh)}
    before = {k: dict(r) for k, r in rows.items()}
    added = 0
    for spec in sorted(glob.glob(os.path.join(RE, 'spec', '*_symbols.csv'))):
        owner = os.path.basename(spec)[:-len('_symbols.csv')]
        with open(spec, newline='', encoding='utf-8') as fh:
            for s in csv.DictReader(fh):
                k = key(s['kind'], s['address'])
                address = s['address'].upper() if s['kind'] == 'global' else s['address'].lower()
                old = rows.get(k)
                if old is None:
                    rows[k] = dict(kind=s['kind'], address=address, name=s['name'],
                                   type=s.get('type', ''), owner=owner, source='spec:' + owner,
                                   notes=s.get('notes', ''))
                    added += 1
                    continue
                notes = old['notes']
                if old['name'] != s['name'] and 'was %s' % old['name'] not in notes:
                    notes = ('was %s | ' % old['name'] + notes) if notes else 'was %s' % old['name']
                if s.get('notes') and s['notes'] not in notes:
                    notes = s['notes'] + (' | ' + notes if notes else '')
                new = dict(old, name=s['name'], owner=owner, notes=notes,
                           type=s.get('type') or old['type'])
                if 'spec:' + owner not in old['source']:
                    new['source'] = old['source'] + '+spec:' + owner
                if new != old:
                    rows[k] = new
    seen = {}
    for r in rows.values():
        if r['name'] in seen:
            sys.exit('name %s used at %s and %s' % (r['name'], seen[r['name']], r['address']))
        seen[r['name']] = r['address']
    with open(path, 'w', newline='', encoding='utf-8') as fh:
        w = csv.DictWriter(fh, FIELDS, lineterminator='\n')
        w.writeheader()
        for r in sorted(rows.values(), key=sort_key):
            w.writerow({f: r.get(f, '') for f in FIELDS})
    changed = sum(1 for k, r in rows.items() if k in before and r != before[k])
    print('%d symbols: %d added, %d updated' % (len(rows), added, changed))


if __name__ == '__main__':
    main()
