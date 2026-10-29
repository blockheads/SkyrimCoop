#!/usr/bin/env python3
"""Build the bundled character-creation room from Skyrim's installed static scenery.

No quests, scripts, actors, doors or interactive objects are copied. Only the base game's
shack shell, decoration and lighting are referenced; Skyrim.esm remains the only master.
Run with the path to Skyrim.esm; output is GameFiles/Skyrim/SkyrimCoopStart.esp.
"""
import argparse
from pathlib import Path
import struct
import zlib
import sys
import math

sys.path.insert(0, str(Path(__file__).resolve().parent / "re"))
from dialogue_conditions import subrecords

CELL = 0x01000800
SOURCE_CELL = 0x00050F1E  # AbandonedShackInterior: small, lit, with a native COC marker.


def records(data, start=0, end=None, cell=0):
    end = len(data) if end is None else end
    while start < end:
        kind, size = struct.unpack_from('<4sI', data, start)
        if kind == b'GRUP':
            label, group_type = struct.unpack_from('<II', data, start + 8)
            yield from records(data, start + 24, start + size, label if group_type == 6 else cell)
            start += size
            continue
        flags, form = struct.unpack_from('<II', data, start + 8)
        body = data[start + 24:start + 24 + size]
        if flags & 0x40000:
            body = zlib.decompress(body[4:])
        yield kind, form, flags, cell, list(subrecords(body))
        start += size + 24


def field(kind, data):
    return kind + struct.pack('<H', len(data)) + data


def record(kind, form, fields, flags=0):
    body = b''.join(field(k, v) for k, v in fields)
    return struct.pack('<4sIIIIHH', kind, len(body), flags, form, 0, 44, 0) + body


def group(label, kind, body):
    return struct.pack('<4sI4siHHI', b'GRUP', len(body) + 24, label, kind, 0, 0, 0) + body


def build(master):
    scenery = {}
    source = None
    references = []
    for kind, form, flags, cell, fields in records(master):
        if kind in (b'STAT', b'LIGH'):
            scenery[form] = kind
        elif kind == b'CELL' and form == SOURCE_CELL:
            source = fields
        elif kind == b'REFR' and cell == SOURCE_CELL:
            references.append((flags, fields))
    if source is None:
        raise ValueError('Skyrim.esm has no expected source room')
    # Leave out location, ownership, encounters, music, water and story data.
    fields = [(b'EDID', b'SkyrimCoopCharacterCreation\0'), (b'FULL', b'Create your character\0'),
              (b'DATA', b'\x01\x00')]
    fields += [(k, v) for k, v in source if k in (b'XCLW', b'XCLF')]
    # Even ambient light makes the chosen face readable; no dependency on the source's light template.
    lighting = bytearray(dict(source)[b'XCLL'])
    lighting[:3] = bytes((80, 80, 80))
    for offset in range(40, 64, 4):
        lighting[offset:offset + 3] = bytes((80, 80, 80))
    lighting[-4:] = bytes(4)
    fields.append((b'XCLL', bytes(lighting)))
    refs = []
    marker = False
    for flags, fields_in in references:
        values = dict(fields_in)
        base = struct.unpack('<I', values.get(b'NAME', b'\0' * 4))[0]
        if base not in scenery or flags & (0x20 | 0x800) or b'XESP' in values:
            continue
        marker |= base == 0x32
        safe = [(k, v) for k, v in fields_in if k in (b'NAME', b'DATA', b'XSCL', b'XLIG')]
        if base == 0x32:
            # Face a closed wall rather than the original room's interactive doorway.
            position = bytearray(values[b'DATA'])
            struct.pack_into('<f', position, 20, struct.unpack_from('<f', position, 20)[0] + math.pi / 2)
            safe = [(k, bytes(position) if k == b'DATA' else v) for k, v in safe]
        refs.append(record(b'REFR', CELL + len(refs) + 1, safe))
    if not marker:
        raise ValueError('source room has no enabled COC marker')
    count = len(refs) + 1
    if count >= 0x800:
        raise ValueError('room exceeds light-plugin form range')
    header = record(b'TES4', 0, [
        (b'HEDR', struct.pack('<fII', 1.71, count, 0x800 + count)),
        (b'CNAM', b'SkyrimCoop\0'),
        (b'SNAM', b'Invite-only character creation. No story quests or scripts.\0'),
        (b'MAST', b'Skyrim.esm\0'), (b'DATA', bytes(8)),
    ], flags=0x200)  # ESL flagged: no full plugin slot.
    children = group(struct.pack('<I', CELL), 6, group(struct.pack('<I', CELL), 9, b''.join(refs)))
    cells = group(b'CELL', 0, group(struct.pack('<I', 8), 2, group(struct.pack('<I', 4), 3, record(b'CELL', CELL, fields) + children)))
    return header + cells, count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('master', type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'GameFiles/Skyrim/SkyrimCoopStart.esp')
    args = parser.parse_args()
    data, count = build(args.master.read_bytes())
    args.output.write_bytes(data)
    print(f'{args.output}: {count} records, {len(data)} bytes')


if __name__ == '__main__':
    main()
