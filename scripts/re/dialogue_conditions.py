#!/usr/bin/env python3
"""Counts dialogue lines whose conditions read an actor's AI state, by topic category.

A copy runs no AI, so these conditions answer differently on a copy than on its owner's actor until the copy
carries its owner's AI state (docs/design/copy-contract.md). Menu topics are the "Topic" category.

    scripts/re/dialogue_conditions.py <Data dir> [plugin.esm ...]   (default: the base game and its DLC)
"""
import collections
import struct
import sys
import zlib

# Condition functions reading AI state (CommonLibSSE-NG TESCondition.h FUNCTION_DATA::FunctionID).
AI_STATE = {
    45: 'GetDetected', 49: 'GetSleeping', 61: 'GetAlarmed', 63: 'GetAttacked', 66: 'GetShouldAttack',
    91: 'GetIsAlerted', 107: 'GetKnockedState', 110: 'GetCurrentAIPackage', 141: 'IsTalking',
    143: 'GetCurrentAIProcedure', 159: 'GetSitting', 160: 'GetFurnitureMarkerID', 161: 'GetIsCurrentPackage',
    162: 'IsCurrentFurnitureRef', 163: 'IsCurrentFurnitureObj', 232: 'GetCombatTarget', 233: 'GetPackageTarget',
    242: 'GetUnconscious', 244: 'GetRestrained', 246: 'GetIsUsedItem', 248: 'IsScenePlaying',
    249: 'IsInDialogueWithPlayer', 289: 'IsInCombat', 323: 'GetCombatState', 590: 'IsInScene',
    644: 'IsInFurnitureState', 700: 'IsCommandedActor',
}
CATEGORIES = {0: 'Topic', 1: 'Favor', 2: 'Scene', 3: 'Combat', 4: 'Favors', 5: 'Detection', 6: 'Service', 7: 'Misc'}
RUN_ON_ACTOR = (0, 2)  # Subject (the speaker), Reference
DEFAULT_PLUGINS = ['Skyrim.esm', 'Update.esm', 'Dawnguard.esm', 'HearthFires.esm', 'Dragonborn.esm']


def subrecords(data):
    i, big = 0, None
    while i + 6 <= len(data):
        kind, size = data[i:i + 4], struct.unpack_from('<H', data, i + 4)[0]
        i += 6
        if kind == b'XXXX':
            big = struct.unpack_from('<I', data, i)[0]
            i += size
            continue
        if big is not None:
            size, big = big, None
        yield kind, data[i:i + size]
        i += size


def scan(buf, start, end, topic, plugin, state):
    i = start
    while i < end:
        kind, size = buf[i:i + 4], struct.unpack_from('<I', buf, i + 4)[0]
        if kind == b'GRUP':
            label, group_type = struct.unpack_from('<II', buf, i + 8)
            scan(buf, i + 24, i + size, label if group_type == 7 else topic, plugin, state)  # 7: a topic's lines
            i += size
            continue
        flags, form_id = struct.unpack_from('<II', buf, i + 8)
        data = buf[i + 24:i + 24 + size]
        if flags & 0x40000:
            data = zlib.decompress(data[4:])
        if kind == b'DIAL':
            for sub, sub_data in subrecords(data):
                if sub == b'DATA' and len(sub_data) >= 2:
                    state['category'][form_id & 0xFFFFFF] = CATEGORIES.get(sub_data[1], sub_data[1])
        elif kind == b'INFO':
            category = state['category'].get(topic & 0xFFFFFF, '?')
            state['lines'][category] += 1
            found = set()
            for sub, sub_data in subrecords(data):
                if sub == b'CTDA' and len(sub_data) >= 24:
                    function, run_on = struct.unpack_from('<H', sub_data, 8)[0], struct.unpack_from('<I', sub_data, 20)[0]
                    if function in AI_STATE and run_on in RUN_ON_ACTOR:
                        found.add((category, AI_STATE[function]))
                        state['example'].setdefault((category, AI_STATE[function]), f'{plugin} INFO {form_id:08X}')
            for key in found:
                state['hits'][key] += 1
        i += 24 + size


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    state = {'category': {}, 'lines': collections.Counter(), 'hits': collections.Counter(), 'example': {}}
    for plugin in sys.argv[2:] or DEFAULT_PLUGINS:
        buf = open(f'{sys.argv[1]}/{plugin}', 'rb').read()
        scan(buf, 0, len(buf), 0, plugin, state)
    print('dialogue lines by topic category:', dict(state['lines']))
    for category in sorted({c for c, _ in state['hits']}):
        print(f'{category} ({state["lines"][category]} lines): AI-state conditions on the speaker or a reference')
        for (cat, function), count in sorted(state['hits'].items(), key=lambda item: -item[1]):
            if cat == category:
                print(f'  {count:5d}  {function:24s} e.g. {state["example"][(cat, function)]}')


if __name__ == '__main__':
    main()
