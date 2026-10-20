#!/usr/bin/env python3
"""Spike drivers for docs/research/player-events.md, against a running duo with a test build.

  player_events.py activator on|off   Rebel's Cairn: the client activates the sword; OnActivate on each game
  player_events.py trigger on|off     Soljund's Sinkhole: the client enters the boss trigger; its OnUpdate chain
  player_events.py tile on|off [PLATE] Kolbjorn Barrow: the client steps on a pressure tile (a stored player)
  player_events.py probe BATCH        host-side counts of the player-singleton functions (0..2, reload between)
  player_events.py story [CELL]       the client's location change raised on the host for its copy

`on` turns on the player context (Code/client/TestHarness/PlayerEvents.cpp) with its trace; the trace goes to
each game's tp_client.log ("Player context:", "Story:"). The party returns to QASmoke afterwards.
"""
import argparse
import contextlib
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import coop_test as c  # noqa: E402

GAMES = ("", "client")
FUNCTIONS = Path(__file__).with_name("player_singleton_functions.txt")  # IDA xrefs to g_thePlayer (403521)
PROBE_BATCH = 430  # MinHook places about 540 hooks near the game per session


def elapsed(seconds):
    start = time.monotonic()
    c.wait_for(f"{seconds}s", lambda: time.monotonic() - start >= seconds, seconds + 5)


def copy_on_host():
    c.select_instance("")
    return next(iter(c.remote_players()))


def context(on):
    for game in GAMES:
        c.select_instance(game)
        c.rpc({"cmd": "player_context", "on": on, "trace": True})
        c.rpc({"cmd": "player_events_reset"})


def events(match):
    out = {}
    for game in GAMES:
        c.select_instance(game)
        found = c.rpc({"cmd": "player_events"}, timeout=10)
        out[game or "host"] = {"context_hits": found["context_hits"],
                               "events": [e for e in found["events"] if match(e)]}
    return out


def visit(cell, on, act, match):
    with contextlib.redirect_stdout(sys.stderr):
        c.both_go(cell)
        context(on)
        act()
        elapsed(6)
    result = events(match)
    with contextlib.redirect_stdout(sys.stderr):
        context(False)
        c.both_go("QASmoke")
    return result


def activator(on):
    sword, linker = 0xA05C0, 0xA05C4
    return visit("RebelsCairn01", on, lambda: c.player_command("client", {"cmd": "activate", "object": sword}),
                 lambda e: e["event"] == "OnActivate" and {sword, linker} & set(e["examples"]))


def trigger(on):
    boss_trigger = 0x6AEBE
    return visit("SoljundsSinkhole01", on, lambda: c.console_on("client", f"player.moveto {boss_trigger:X}"),
                 lambda e: boss_trigger in e["examples"])


def tile(on, plate=0x0401DC0F):
    # A plate stays in state InTrigger when the party leaves it: use another one (0x0401DC0F to 0x0401DC26).
    return visit("DLC2KolbjornBarrow01", on, lambda: c.console_on("client", f"player.moveto {plate:X}"),
                 lambda e: plate in e["examples"])


def probe(batch):
    """Per window: {function id: [calls, calls with the copy as an argument]} on the host."""
    ids = [int(line.split()[0]) for line in FUNCTIONS.read_text().splitlines()]
    ids = ids[batch * PROBE_BATCH:(batch + 1) * PROBE_BATCH]
    out = {}

    def window(label, run):
        with contextlib.redirect_stdout(sys.stderr):
            c.select_instance("")
            installed = c.rpc({"cmd": "player_probe", "ids": ids, "copy": copy_on_host()}, timeout=60)
            out.setdefault("_installed", {"probes": installed["probes"], "failed": installed["failed"]})
            try:
                run()
            except SystemExit:
                out[label + " (failed)"] = True
            c.select_instance("")
            out[label] = c.rpc({"cmd": "player_probe_read"}, timeout=10)["counts"]

    def scenario(name):
        return lambda: c.command_scenario(argparse.Namespace(names=[name], tag=None, hold=0, repeat=1, shuffle=False,
                                                             seed=None, fail_fast=False))

    def boss_trigger():
        c.both_go("SoljundsSinkhole01")
        c.select_instance("")
        c.rpc({"cmd": "player_probe", "copy": copy_on_host()})
        c.console_on("client", "player.moveto 6AEBE")
        elapsed(8)
        c.both_go("QASmoke")

    window("idle", lambda: elapsed(15))
    for name in ("melee-client-hits-host-npc", "client-melee-kill-credit", "client-spell-kill-credit"):
        window(name, scenario(name))
    window("trigger", boss_trigger)
    return out


def story(cell):
    with contextlib.redirect_stdout(sys.stderr):
        for game in GAMES:
            c.select_instance(game)
            c.rpc({"cmd": "story_trace", "on": True})
        c.both_go(cell)
        elapsed(3)
        c.select_instance("client")
        captured = c.rpc({"cmd": "story_trace"})["captured"]
        copy = copy_on_host()
        raised = [c.rpc({"cmd": "story_raise_location_change", "actor": copy, "old": old, "new": new})["event"]
                  for old, new in captured]
        elapsed(3)
        for game in GAMES:
            c.select_instance(game)
            c.rpc({"cmd": "story_trace", "on": False})
        c.both_go("QASmoke")
    return {"captured": captured, "copy": copy, "raised_events": raised}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="spike", required=True)
    for name in ("activator", "trigger", "tile"):
        sub.add_parser(name).add_argument("context", choices=("on", "off"))
    sub.choices["tile"].add_argument("plate", nargs="?", type=lambda v: int(v, 0), default=0x0401DC0F)
    sub.add_parser("probe").add_argument("batch", type=int)
    sub.add_parser("story").add_argument("cell", nargs="?", default="WhiterunBanneredMare")
    args = parser.parse_args()
    if args.spike == "probe":
        result = probe(args.batch)
    elif args.spike == "story":
        result = story(args.cell)
    elif args.spike == "tile":
        result = tile(args.context == "on", args.plate)
    else:
        result = globals()[args.spike](args.context == "on")
    print(json.dumps(result, indent=1))


if __name__ == "__main__":
    main()
