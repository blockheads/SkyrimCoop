#!/usr/bin/env python3
"""Lists the kill move idles in the game's idle tree: each IDLE whose event starts pa_Kill (or that sits under one), its
parent chain, animation event and conditions, as the idle tree picks them (docs/re/systems/kill-moves.md, "Every kill move").

    scripts/re/kill_moves.py <Data dir> [--json | --rows] [plugin.esm ...]   (default: the base game and its DLC)

--rows: the kill moves a player's normal swing can pick per victim, weapon and side, walked through the tree's
conditions (scripts/coop_harness/kill_moves.json, the kill-moves-every scenario's table).
"""
import json
import re
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dialogue_conditions import DEFAULT_PLUGINS, subrecords  # noqa: E402

HEADER = Path(__file__).resolve().parents[2] / 'reference/CommonLibSSE-NG/include/RE/T/TESCondition.h'
OPS = ['==', '!=', '>', '>=', '<', '<=']
RUN_ON = {0: '', 1: 'target.', 2: 'ref.', 3: 'combat target.', 4: 'linked.', 5: 'alias.', 6: 'package data.', 7: 'event data.'}
WEAPON_TYPES = {0: 'hands', 1: 'sword', 2: 'dagger', 3: 'war axe', 4: 'mace', 5: 'greatsword', 6: 'battleaxe/warhammer',
                7: 'bow', 8: 'staff', 9: 'spell', 10: 'shield', 11: 'torch', 12: 'crossbow'}


RACE_KEYWORDS = {}  # race editor id: its keywords' form ids, filled by load()


def function_names():
    names = {}
    for name, number in re.findall(r'\bk(\w+) = (\d+),', HEADER.read_text(errors='replace')):
        names.setdefault(int(number), name)
    return names


def records(buf, start, end, kind_wanted, out):
    i = start
    while i < end:
        kind, size = buf[i:i + 4], struct.unpack_from('<I', buf, i + 4)[0]
        if kind == b'GRUP':
            records(buf, i + 24, i + size, kind_wanted, out)
            i += size
            continue
        flags, form_id = struct.unpack_from('<II', buf, i + 8)
        data = buf[i + 24:i + 24 + size]
        if flags & 0x40000:
            data = zlib.decompress(data[4:])
        if kind in kind_wanted:
            out.append((kind, form_id, data))
        i += 24 + size


def load(data_dir, plugins):
    """Every IDLE and the editor IDs of the records conditions name, by load-order form id."""
    idles, names = {}, {}
    for index, plugin in enumerate(plugins):
        path = Path(data_dir) / plugin
        if not path.exists():
            continue
        buf = path.read_bytes()
        masters = [sub.rstrip(b'\0').decode() for kind, sub in subrecords(buf[24:24 + struct.unpack_from('<I', buf, 4)[0]]) if kind == b'MAST']
        def global_id(form_id):
            slot = form_id >> 24
            owner = masters[slot] if slot < len(masters) else plugin
            return (plugins.index(owner) << 24 | form_id & 0xFFFFFF) if owner in plugins else form_id
        found = []
        records(buf, 0, len(buf), {b'IDLE', b'RACE', b'KYWD', b'WEAP', b'GLOB', b'PERK'}, found)
        for kind, form_id, data in found:
            form_id = global_id(form_id)
            subs = list(subrecords(data))
            edid = next((d.rstrip(b'\0').decode(errors='replace') for k, d in subs if k == b'EDID'), '')
            names[form_id] = edid
            if kind == b'RACE':
                keywords = next((d for k, d in subs if k == b'KWDA'), b'')
                RACE_KEYWORDS[edid] = {global_id(k) for k in struct.unpack(f'<{len(keywords) // 4}I', keywords)}
            if kind != b'IDLE':
                continue
            idle = {'id': form_id, 'plugin': plugin, 'edid': edid, 'conditions': [], 'parent': 0, 'previous': 0, 'event': '', 'file': ''}
            for k, d in subs:
                if k == b'ENAM':
                    idle['event'] = d.rstrip(b'\0').decode(errors='replace')
                elif k == b'DNAM':
                    idle['file'] = d.rstrip(b'\0').decode(errors='replace')
                elif k == b'ANAM' and len(d) >= 8:
                    parent, previous = struct.unpack_from('<II', d)
                    idle['parent'], idle['previous'] = global_id(parent) if parent else 0, global_id(previous) if previous else 0
                elif k == b'CTDA' and len(d) >= 32:
                    op_flags, value, function, p1, p2, run_on = struct.unpack_from('<B3xfH2xIII', d)
                    idle['conditions'].append({'op': OPS[op_flags >> 5] if op_flags >> 5 < 6 else '?', 'or': bool(op_flags & 1),
                                               'value': value, 'function': function, 'p1': p1, 'p2': p2, 'run_on': run_on,
                                               'global_value': bool(op_flags & 4)})
            idles[form_id] = idle
    return idles, names


def describe(condition, functions, names):
    name = functions.get(condition['function'], f"fn{condition['function']}")
    p1 = condition['p1']
    arg = ''
    if name == 'GetEquippedItemType':
        arg = 'left' if p1 == 0 else 'right'
    elif p1:
        arg = names.get(p1) or f'{p1:08X}'
    value = f"{condition['value']:g}"
    if name == 'GetEquippedItemType':
        value = WEAPON_TYPES.get(int(condition['value']), value)
    return f"{RUN_ON.get(condition['run_on'], '?')}{name}({arg}) {condition['op']} {value}{' OR' if condition['or'] else ''}"


# What a row's swing looks like to the idle tree: the player (GetIsID 7) swings once, not sneaking, at the last hostile
# actor, standing level with it; the unread rest (GetRandomPercent, graph variables) may go either way.
WEAPONS = {'hands': (0, 0), 'dagger': (2, 0), 'sword': (1, 0), 'war axe': (3, 0), 'mace': (4, 0), 'greatsword': (5, 0),
           'battleaxe': (6, 0), 'warhammer': (6, 0), 'dual swords': (1, 1)}
FIXED = {'ShouldAttackKill': 1, 'IsWeaponMagicOut': 1, 'GetAttackState': 0, 'IsBlocking': 0, 'IsStaggered': 0,
         'GetPairedAnimation': 0, 'IsFurnitureAnimType': 0, 'GetGraphVariableInt': 0, 'IsSneaking': 0, 'GetDead': 0,
         'IsBleedingOut': 0, 'IsEssential': 0, 'IsLastHostileActor': 1, 'GetActorValuePercent': 1, 'GetIsFlying': 0,
         'GetSitting': 0, 'IsInKillMove': 0, 'GetActorValue': 0}


def evaluate(condition, row, functions, names):
    """True, False, or None (could be either) for one condition on this row."""
    name = functions.get(condition['function'], '')
    p1, target = condition['p1'], condition['run_on'] == 1
    if name == 'GetEquippedItemType' and not target:
        value = WEAPONS[row['weapon']][0] if p1 == 1 else WEAPONS[row['weapon']][1]
    elif name == 'GetIsRace' and target:
        value = int(names.get(p1) == row['race'])
    elif name == 'HasKeyword' and target:
        value = int(p1 in RACE_KEYWORDS.get(row['race'], ()))
    elif name == 'HasPerk' and not target:
        value = int(names.get(p1) in row['perks'])
    elif name == 'GetIsID':
        value = int(p1 == 7) if not target else 0
    elif name == 'GetRelativeAngle':
        value = 0 if row['behind'] else 180
    elif name == 'GetTargetHeight':
        value = row.get('height', 0)
    elif name == 'GetGlobalValue':
        value = 1 if names.get(p1) == 'KillMove' else None
    elif name in FIXED:
        value = FIXED[name]
    else:
        return None
    return {'==': value == condition['value'], '!=': value != condition['value'], '>': value > condition['value'],
            '>=': value >= condition['value'], '<': value < condition['value'], '<=': value <= condition['value']}[condition['op']]


def passes(idle, row, functions, names):
    """An idle's conditions: OR-flagged runs are groups, the groups AND together (TESCondition)."""
    result, group = True, []
    for condition in idle['conditions']:
        group.append(evaluate(condition, row, functions, names))
        if condition['or']:
            continue
        value = True if True in group else (None if None in group else False)
        result = False if value is False or result is False else (None if value is None or result is None else True)
        group = []
    return result


def children(idles):
    """Each idle's children in the tree's order (ANAM's previous sibling)."""
    by_parent = {}
    for idle in idles.values():
        by_parent.setdefault(idle['parent'], []).append(idle)
    ordered = {}
    for parent, siblings in by_parent.items():
        after = {idle['previous']: idle for idle in siblings}
        ids = {idle['id'] for idle in siblings}
        line = [idle for idle in siblings if idle['previous'] not in ids][:1]
        while line and len(line) < len(siblings) and after.get(line[-1]['id']):
            line.append(after[line[-1]['id']])
        ordered[parent] = line + [idle for idle in siblings if idle not in line]
    return ordered


def reachable(node, row, tree, functions, names):
    """(the kill move events this node's subtree can pick, whether it surely picks one). A node whose children all fail
    passes on to its next sibling (inferred, not read: the pick 37147 was not reversed for this)."""
    events, sure = [], False
    for child in tree.get(node, []):
        passed = passes(child, row, functions, names)
        if passed is False:
            continue
        if tree.get(child['id']):
            found, taken = reachable(child['id'], row, tree, functions, names)
        else:
            found, taken = ([child['event']] if child['event'] else []), True
        events += [event for event in found if event not in events]
        if taken and passed:
            sure = True
            break
    return events, sure


VICTIMS = [  # (name, a base to spawn, its race)
    ('bandit', '0003DE53', 'NordRace'), ('draugr', '0001FB8F', 'DraugrRace'), ('falmer', '00023A9A', 'FalmerRace'),
    ('wolf', '00023ABE', 'WolfRace'), ('troll', '00023ABA', 'TrollRace'), ('sabre cat', '00023AB5', 'SabreCatRace'),
    ('bear', '00023A8B', 'BearBrownRace'), ('spider', '00023AAA', 'FrostbiteSpiderRace'),
    ('spriggan', '00023AB9', 'SprigganRace'), ('hagraven', '00023AB0', 'HagravenRace'), ('giant', '00023AAE', 'GiantRace'),
    ('centurion', '00023A96', 'DwarvenCenturionRace'), ('skeever', '00023AB7', 'SkeeverRace'),
    ('chaurus', '000A5600', 'ChaurusRace'), ('dwarven spider', '00023A98', 'DwarvenSpiderRace'),
]


def rows(idles, names):
    """Every (victim, weapon, side) whose normal swing at one hit from death can be a kill move, with the events it can pick."""
    functions = function_names()
    tree = children(idles)
    root = next(idle['id'] for idle in idles.values() if idle['edid'] == 'NonMountedCombatRight')
    out = []
    for victim, base, race in VICTIMS:
        for weapon in WEAPONS:
            for behind in (False, True):
                for perks in ((), ('SavageStrike', 'DevastatingBlow')):
                    row = {'victim': victim, 'base': base, 'race': race, 'weapon': weapon, 'behind': behind, 'perks': perks}
                    events = [e for e in reachable(root, row, tree, functions, names)[0] if e.lower().startswith('pa_kill')]
                    if perks and out and out[-1]['events'] == events and out[-1]['weapon'] == weapon and out[-1]['victim'] == victim \
                            and out[-1]['behind'] == behind:
                        continue  # the perks add nothing here
                    if events:
                        ids = {name: form_id for form_id, name in names.items()}
                        out.append({**row, 'perks': [f'{ids[perk]:08X}' for perk in perks], 'events': events})
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args:
        sys.exit(__doc__)
    idles, names = load(args[0], args[1:] or DEFAULT_PLUGINS)
    functions = function_names()
    def chain(idle):
        out, seen = [], set()
        while idle and idle['id'] not in seen:
            seen.add(idle['id'])
            out.append(idle['edid'])
            idle = idles.get(idle['parent'])
        return list(reversed(out))
    kill = [i for i in idles.values() if i['event'].lower().startswith('pa_kill')
            or any(idles.get(p, {}).get('event', '').lower().startswith('pa_kill') for p in [i['parent']])
            or 'killmove' in i['edid'].lower()]
    rows = []
    for idle in sorted(kill, key=lambda i: chain(i)):
        path = chain(idle)
        rows.append({'id': f"{idle['id']:08X}", 'edid': idle['edid'], 'event': idle['event'], 'path': ' > '.join(path[:-1]),
                     'conditions': [describe(c, functions, names) for c in idle['conditions']]})
    if '--rows' in sys.argv:
        json.dump(globals()['rows'](idles, names), sys.stdout, indent=1)
        return
    if '--json' in sys.argv:
        json.dump(rows, sys.stdout, indent=1)
        return
    for row in rows:
        print(f"{row['id']} {row['edid']}  [{row['event']}]  under {row['path']}")
        for condition in row['conditions']:
            print(f"    {condition}")


if __name__ == '__main__':
    main()
