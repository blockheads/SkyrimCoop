#!/usr/bin/env python3
"""Reads an actor's behaviour graph state (BShkbAnimationGraph::SaveState, 63601) as named records.

  scripts/re/graph_snapshot.py [--instance client] [--actor 0x14] [--vars]   dump a running game's actor

The bytes come from the harness command `graph_state` (TESObjectREFR::SaveAnimationGraphState: 8 bytes of
ActorState flags, then per graph its project name and SaveState's output). The layout is in
docs/design/graph-replication.md; `parse_actor`/`parse_graph` are what the measurements there used.
"""
import argparse
import struct
import sys
from pathlib import Path


class Reader:
    def __init__(self, data):
        self.data, self.offset = data, 0

    def u8(self):
        value = self.data[self.offset]
        self.offset += 1
        return value

    def u16(self):
        (value,) = struct.unpack_from("<H", self.data, self.offset)
        self.offset += 2
        return value

    def u32(self):
        (value,) = struct.unpack_from("<I", self.data, self.offset)
        self.offset += 4
        return value

    def raw(self, size):
        value = self.data[self.offset:self.offset + size]
        self.offset += size
        return value

    def string(self):
        return self.raw(self.u16()).decode(errors="replace")


END_OF_VARIABLES = "$-NoMoreVariables-$"


def parse_graph(data):
    """One graph's SaveState bytes -> {section: records}. Raises if it does not consume them exactly."""
    r = Reader(data)
    out = {}

    def section(name, record):
        out[name] = [record() for _ in range(r.u32())]

    # Active state machines: "file.hkb/machine", an index, the state's name, two words. The root comes last.
    section("states", lambda: (r.string(), r.u32(), r.string(), r.u32(), r.u32()))
    section("s2", lambda: (r.string(), r.u32()))
    section("s3", lambda: (r.string(), r.u32()))
    # Variables that differ from the behaviour's defaults (sub_140BB88C0): name, kind (1 word, 0 quad), value.
    variables = []
    while (name := r.string()) != END_OF_VARIABLES:
        kind = r.u8()
        variables.append((name, kind, r.raw(4 if kind else 16).hex()))
    out["variables"] = variables
    section("s4", lambda: (r.string(), r.u8()))
    section("clips", lambda: (r.string(), r.string(), r.string(), r.u32(), r.u32(), r.u16(), r.u16(), r.u16(), r.u8(), r.u8()))
    section("s6", lambda: (r.string(), r.u32()))
    section("s7", lambda: (r.string(), r.u32()))

    def modifier():
        name, a, b = r.string(), r.u32(), r.u32()
        return (name, a, b, r.raw(16 * a).hex(), r.raw(16 * b).hex())

    section("s8", modifier)  # e.g. 0_Master.hkb/BSLookAtModifier
    section("s9", lambda: (r.string(), r.u8()))
    section("s10", lambda: (r.string(), r.raw(48).hex(), r.u32(), r.u32()))
    section("s11", lambda: (r.string(), r.raw(144).hex(), r.u32(), r.u8(), r.u8()))
    section("s12", lambda: (r.string(), r.string()))
    section("s13", lambda: (r.string(), r.u16(), r.u32(), r.u32(), r.u32(), r.u8(), r.u32()))
    if r.offset != len(data):
        raise ValueError(f"graph state: {len(data) - r.offset} bytes left over")
    return out


def parse_actor(data):
    """The harness's graph_state bytes -> (flags1, flags2, {project: parsed graph})."""
    flags1, flags2 = struct.unpack_from("<II", data, 0)
    offset, graphs = 8, {}
    while offset < len(data):
        (length,) = struct.unpack_from("<H", data, offset)
        project = data[offset + 2:offset + 2 + length].decode()
        offset += 2 + length
        (size,) = struct.unpack_from("<I", data, offset)
        graphs[project] = parse_graph(data[offset + 4:offset + 4 + size])
        offset += 4 + size
    return flags1, flags2, graphs


def fetch(instance, actor):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    import coop_test
    coop_test.select_instance(instance)
    response = coop_test.rpc({"cmd": "graph_state", "actor": actor})
    if not response or not response.get("ok"):
        raise SystemExit(f"no graph state: {response}")
    return bytes.fromhex(response["state"]), response["micros"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--instance", default="", help="'' the host, 'client' the second game")
    parser.add_argument("--actor", default="0x14", help="form id in that game")
    parser.add_argument("--vars", action="store_true", help="also list the variables")
    args = parser.parse_args()
    data, micros = fetch(args.instance, int(args.actor, 0))
    flags1, flags2, graphs = parse_actor(data)
    print(f"{len(data)} bytes, saved in {micros} us, flags1 {flags1:08x} flags2 {flags2:08x}")
    for project, graph in graphs.items():
        print(f"{project}: " + ", ".join(f"{k} {len(v)}" for k, v in graph.items() if v))
        for machine, _, state, *_ in graph["states"]:
            print(f"    {machine} -> {state}")
        if args.vars:
            for name, kind, value in graph["variables"]:
                print(f"    {name} = {value}{'' if kind else ' (quad)'}")


if __name__ == "__main__":
    main()
