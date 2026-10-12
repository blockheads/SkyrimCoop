#!/usr/bin/env python3
"""meh321's skyrimae.relib: the offset of every Address Library ID in each AE version.

    scripts/re/relib.py 0x606DF0            which ID sat at this RVA, in which versions
    scripts/re/relib.py --id 37448          the ID's RVA in every version

Use it to turn an old hardcoded AE address (upstream Tilted Online used them until 2022) into its ID.
Format, reverse-engineered (no docs): u32 format, header, u8 length + "SkyrimSE.exe", u32 version count;
per version: u32 n + n u32 (1.6.317.0), u8, u64 image base, u32 count, count x (u64 id, u32 rva), u32
trailer. Versions after 1.6.640 use another layout (non-zero trailer); parsing stops there.
Fetch the file with scripts/re/fetch_references.sh.
"""
import argparse
from pathlib import Path
import struct

RELIB = Path(__file__).resolve().parents[2] / "reference/AddressLibraryDatabase/skyrimae.relib"


def load(path=RELIB):
    """{"1.6.317.0": {id: rva}} for every version the simple layout covers."""
    data = path.read_bytes()
    offset = 0x1E
    count = struct.unpack_from("<I", data, offset)[0]
    offset += 4
    versions = {}
    for _ in range(count):
        parts = struct.unpack_from("<I", data, offset)[0]
        version = struct.unpack_from(f"<{parts}I", data, offset + 4)
        offset += 4 + 4 * parts
        _base, entries = struct.unpack_from("<QI", data, offset + 1)
        offset += 13
        versions[".".join(map(str, version))] = dict(struct.iter_unpack("<QI", data[offset:offset + 12 * entries]))
        offset += 12 * entries
        trailer = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        if trailer:
            break
    return versions


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rva", nargs="?", help="RVA or absolute address (0x140... is accepted)")
    parser.add_argument("--id", type=int, help="print this ID's RVA in every version")
    args = parser.parse_args()
    versions = load()
    if args.id is not None:
        for version, ids in versions.items():
            print(f"{version:12} {ids.get(args.id, 0):#x}")
        return
    rva = int(args.rva, 0)
    rva -= 0x140000000 if rva >= 0x140000000 else 0
    for version, ids in versions.items():
        hits = [ident for ident, offset in ids.items() if offset == rva]
        print(f"{version:12} {', '.join(map(str, hits)) or '-'}")


if __name__ == "__main__":
    main()
