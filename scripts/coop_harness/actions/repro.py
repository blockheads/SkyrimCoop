"""The repro loop's verbs (docs/design/repro-mcp.md): the duo's lock, named handles for what a repro stages,
and the recorder that turns a session's calls into a scenario. The MCP server (scripts/coop_mcp.py) calls these;
scenarios can too."""
import contextlib
import inspect
import json
import collections
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

from ..common import BOOT_FILE, GAME, ROOT, TEST_HOME, WINDOWS
from ..comparisons import flatten
from ..duo_lock import IDLE_MINUTES
from ..errors import TestFailure

RECORDINGS = TEST_HOME / "mcp"
SCENE_RADIUS = 4000.0  # units around the host player a scene names and looks at
STAGE_RANGE = 3000.0  # a stage spot further from the host player than this is not in the scene
SETTLE_SECONDS = 8.0  # copies catch up after an arrival
# What `observe` shows of an actor unless asked for everything.
BRIEF_FIELDS = ("identity.name", "placement.position", "placement.cell", "state.dead", "state.bleeding_out",
                "state.sit_sleep", "state.weapon_drawn", "state.knock", "values.health", "ai.combat_target",
                "ai.running_package", "ai.package_target", "ai.follow_target", "ai_state.package", "ai_state.in_combat",
                "animation.event")
# Shown, not compared: a copy runs no AI, and a place is compared as `apart`.
UNCOMPARED = ("ai.", "placement.position", "animation.")


def slug(name):
    """A display name as a handle: "Faendal" -> faendal, "Whiterun Guard" -> whiterun_guard."""
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def shared_actors(host, guest, here):
    """The NPCs both games show, nearest `here` first: {name, host, guest, distance}, paired by session id."""
    copies = {row["mod"]["session_id"]: int(form) for form, row in guest.items() if row["mod"]["session_id"] >= 0}
    shared = []
    for form, row in host.items():
        if row.get("player") or not (name := row["identity"].get("name")) or row["mod"]["session_id"] not in copies:
            continue
        shared.append({"name": name, "host": int(form), "guest": copies[row["mod"]["session_id"]],
                       "distance": round(math.dist(row["placement"]["position"][:2], here[:2]))})
    return sorted(shared, key=lambda actor: actor["distance"])


def current_copies(handles, host, guest):
    """Handles with the guest's copy as it is now: a copy's form id changes when its cell reloads, and the old id can
    go to another actor; its session id stays. A copy the guest doesn't show has no id there."""
    copies = {row["mod"]["session_id"]: int(form) for form, row in guest.items() if row.get("role") == "copy"}
    fresh = {}
    for name, ids in handles.items():
        fresh[name] = dict(ids)
        row = host.get(str(ids.get("host")))
        if row and row.get("role") == "own" and not row.get("player") and row["mod"]["session_id"] >= 0:
            fresh[name].pop("guest", None)
            if row["mod"]["session_id"] in copies:
                fresh[name]["guest"] = copies[row["mod"]["session_id"]]
    return fresh


def name_handles(actors):
    """Each actor's handle by its name: the nearest of a name takes it, the next name_2, and so on."""
    named = {}
    for actor in actors:
        base = handle = slug(actor["name"])
        count = 1
        while handle in named:
            count += 1
            handle = f"{base}_{count}"
        named[handle] = actor
    return named


def brief(row, full=False):
    """An actor's snapshot or a reference's row, flat: the fields a repro reads, places rounded."""
    fields = flatten(row or {})
    if "identity.name" in fields and not full:
        fields = {key: fields[key] for key in BRIEF_FIELDS if key in fields}
    return {key: [round(v) for v in value] if key.endswith("position") and isinstance(value, list) else value
            for key, value in fields.items()}


def changes(before, after):
    """What moved between two flat readings: {field: [was, is]}."""
    return {key: [before.get(key), after.get(key)] for key in sorted(set(before) | set(after))
            if before.get(key) != after.get(key)}


def game_differences(host, guest):
    """Fields a copy should hold as its owner does and doesn't: {field: [host, guest]}."""
    return {key: pair for key, pair in changes(host, guest).items() if not key.startswith(UNCOMPARED)}


def duo_home():
    """The main checkout's build/coop-test, where every worktree's duo lock lives (COOP_DUO_HOME overrides). Kept here, not
    duo_lock.home_of: mcp_reload keeps a running server's duo_lock module, which may predate it."""
    if home := os.environ.get("COOP_DUO_HOME"):
        return Path(home)
    common = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute", "--git-common-dir"],
                            capture_output=True, text=True).stdout.strip()
    return (Path(common).parent / "build/coop-test").resolve() if common else TEST_HOME


class ReproActions:
    # --- The duo's lock (coop_harness/duo_lock.py): none on Windows, one machine per duo there ---

    def duo_lock(ctx, name=None):
        if WINDOWS:
            return None
        if getattr(ctx, "DUO_LOCK", None) is None:
            from ..duo_lock import DuoLock
            ctx.DUO_LOCK = DuoLock(duo_home(), ROOT, name or os.environ.get("COOP_DUO_NAME") or ROOT.name,
                                   stop_command=[sys.executable, str(ROOT / "scripts/coop_test.py"), "stop"])
        return ctx.DUO_LOCK

    def duo_games_running(ctx):
        return any(ctx.prefix_processes(_game=game) for game in ctx.games)

    def duo_claim(ctx, owner, what, wait_seconds=0, keep_place=False, capped=False):
        """Holds the shared duo (or joins this checkout's hold), waiting up to wait_seconds in the queue. capped: an
        interactive hold, which the first in line takes after its cap (duo_lock.over_cap)."""
        from ..duo_lock import DuoBusy
        if not (lock := ctx.duo_lock(owner)):
            return "no lock on Windows"
        try:
            # mcp_reload keeps a running server's duo_lock module: one loaded before the cap takes no `capped`.
            extra = {"capped": capped} if "capped" in inspect.signature(lock.acquire).parameters else {}
            return lock.acquire(what, timeout=wait_seconds, keep_place=keep_place, **extra)
        except DuoBusy as error:
            raise ctx.failure(str(error)) from None

    def duo_free(ctx, owner, summary):
        """Stops the duo, checks none of its Skyrims is left and frees the hold with a log line. Only a queue place:
        leaves the queue. Refused otherwise unless this process holds it: never stop a duo someone else started."""
        lock = ctx.duo_lock(owner)
        if lock and lock.role == "queued":
            number = lock.poll_place()
            lock.leave()
            return f"left the queue, was number {number}" if number else "left the queue (the place had already lapsed)"
        if lock and lock.role != "holder":
            raise ctx.failure(f"not ours to stop: this session holds no duo ({lock.role or 'none'})")
        if lock and not lock.still_held():  # taken at its cap, or freed by this checkout's stop: the games are not ours
            lock.release()
            return "already freed (its cap, or this checkout's stop); games left alone"
        ctx.stop_duo()
        if left := ctx.our_skyrims():
            raise ctx.failure(f"{len(left)} of our Skyrim processes still running after stop: {left}")
        if lock:
            lock.release(f"{summary}. Stopped, 0 Skyrim processes, freed")
        return "freed"

    def duo_finish(ctx):
        """A command's end: a hold whose games still run goes to a keeper (scripts/duo.py keep), else it is freed."""
        lock = getattr(ctx, "DUO_LOCK", None)
        if not lock or not lock.role:
            return
        if lock.role == "holder" and ctx.duo_games_running():
            if lock.park([sys.executable, str(ROOT / "scripts/duo.py"), "keep"]):
                print("duo: games left running; a keeper holds the duo until `coop_test.py stop` "
                      f"(or {IDLE_MINUTES:.0f} idle minutes)", file=sys.stderr)
                return
        lock.release("Command ended")

    def our_skyrims(ctx):
        """The Skyrim processes running from this checkout's test game folder (another worktree's are theirs)."""
        folder = str(GAME).replace("/", "\\")
        listing = subprocess.run(["ps", "-eo", "pid,args"], capture_output=True, text=True).stdout.splitlines()
        return [line.split(None, 1)[0] for line in listing if "SkyrimSE.exe" in line and folder.lower() in line.lower()]

    # --- Handles: a repro's names for what it staged, per game ---

    def graph_watch(ctx, seconds=60.0, every=0.25, lasting=2.0, limit=10, ids=()):
        """Owner against copy, both read live, for NPCs on both games: the runs apart for `lasting` s or more (`scenario`
        runs this watch over each flow). `ids`: owner form ids; else the first `limit` pairs at each look."""
        wanted = {int(i, 16) if isinstance(i, str) else i for i in ids}
        watch = ctx.graph_watch_start(every, lasting, wanted, limit)
        watch["stop"].wait(seconds)
        watch["stop"].set()
        watch["thread"].join(10)
        apart, start = watch["apart"], watch["started"]
        return {"actors": [f"{owner:X}" for owner in sorted(watch["actors"])], "samples": apart.samples,
                "apart": [{**run, "actor": f"{run['actor']:X}", "at": round(run["at"] - start, 1)} for run in apart.finish(time.time())]}

    def hands_compared(ctx):
        """Each player's hands and worn items against its copy's on the other game: the process's hands and the worn marks
        are separate (docs/bugs-map.md, A copy's hands)."""
        def row(game, form):
            snapshot = ctx.actor_snapshot(game, form) or {}
            return {"hands": (snapshot.get("state") or {}).get("hands"), "worn": sorted(snapshot.get("equipment") or []),
                    "drawn": (snapshot.get("state") or {}).get("weapon_drawn")}
        return {"host": row("", 0x14), "host_copy": row("client", ctx.other_player(ctx.actors_on("client"))),
                "guest": row("client", 0x14), "guest_copy": row("", ctx.other_player(ctx.actors_on("")))}

    def handles(ctx):
        if not hasattr(ctx, "REPRO_HANDLES"):
            ctx.REPRO_HANDLES = {}
        return ctx.REPRO_HANDLES

    def name_handle(ctx, name, ids):
        """Names a staged thing: ids is {"host": form id, "guest": form id} (either may be missing)."""
        ctx.handles()[name] = {game: ids[game] for game in ("host", "guest") if ids.get(game)}
        return ctx.handles()[name]

    def graph_machines(ctx, who, name=""):
        """A handle's state machines on both games, active or not (harness graph_machines): the state each holds and
        the state its next activation enters (`start_override`), for machines whose name holds `name`."""
        def read(game, label):
            reply = ctx.game(game).rpc({"cmd": "graph_machines", "actor": ctx.resolve(who, label), "name": name}) or {}
            return {f"{m['behaviour']}/{m['name']}": {k: m[k] for k in ("active", "state", "start_override", "clone", "template_active", "template_state", "template_start") if k in m}
                    for m in reply.get("machines", [])}
        return {"host": read("", "host"), "guest": read("client", "guest")}

    def graph_machines_watch(ctx, who, name="", seconds=30.0, every=0.05):
        """graph_machines on both games for `seconds`: each change of a machine (appearing, gone, active, state, start
        override), stamped in seconds from the start and in wall-clock time (@, as the animation trace's)."""
        start, last, changes = time.time(), {}, []
        while time.time() - start < seconds:
            for game, machines in ctx.graph_machines(who, name).items():
                for key in set(machines) | {k for (g, k) in last if g == game}:
                    now = machines.get(key)
                    if last.get((game, key)) != now:
                        changes.append(f"{time.time() - start:6.2f} @{time.time():.2f} {game:5} {key.split('/')[-1]}: {now or 'gone'}")
                        last[(game, key)] = now
            time.sleep(every)
        return changes

    def graph_stream_check(ctx, seconds=60.0):
        """The graph input stream's live check (docs/design/graph-input-stream.md): every streamed copy's graph states,
        hashed after each update by its owner's update index, against its owner's, from both games for `seconds`.
        Fails at the first difference with both games' states then; returns each copy's updates compared."""
        host, guest = ctx.game(""), ctx.game("client")
        owners, copies, pending = {}, {}, []
        compared = collections.Counter()
        started = time.monotonic()

        def states(game, form, update):
            return (game.rpc({"cmd": "graph_stream", "states_at": {"form": form, "update": update}}, timeout=10) or {}).get("states")

        def step():
            for actor in (host.rpc({"cmd": "graph_stream", "take": True}, timeout=10) or {}).get("actors", []):
                if actor["role"] == "owner":
                    owner = owners.setdefault(actor["actor"], {"form": actor["form"], "hashes": {}})
                    owner["hashes"].update({update: value for update, value in actor.get("hashes", [])})
            for actor in (guest.rpc({"cmd": "graph_stream", "take": True}, timeout=10) or {}).get("actors", []):
                if actor["role"] != "copy":
                    continue
                copies[actor["actor"]] = {key: actor[key] for key in ("form", "on_stream", "last")}
                pending.extend((actor["actor"], actor["form"], update, value) for update, value in actor.get("hashes", []))
            waiting = []
            for actor, form, update, value in pending:
                expected = owners.get(actor, {}).get("hashes", {}).get(update)
                if expected is None:
                    waiting.append((actor, form, update, value))
                elif expected != value:
                    owner_states = states(host, owners[actor]["form"], update) or []
                    copy_states = states(guest, form, update) or []
                    raise ctx.failure(f"copy {form:X} (actor {actor}) left its owner's graph at update {update} after "
                                      f"{compared[actor]} equal: owner only {sorted(set(owner_states) - set(copy_states))}, "
                                      f"copy only {sorted(set(copy_states) - set(owner_states))}")
                else:
                    compared[actor] += 1
            pending[:] = waiting
            return time.monotonic() - started >= seconds or None

        ctx.wait_for(f"[both] {seconds:.0f} s of streamed copies' graphs against their owners'", step, seconds + 30,
                     explain=lambda: f"copies {copies}, compared {dict(compared)}")
        return {"compared": {f"{copies.get(actor, {}).get('form', 0):X}": count for actor, count in compared.items()},
                "copies": {f"{value['form']:X}": value for value in copies.values()},
                "unmatched": len(pending), "owners": len(owners)}

    def name_owner(ctx, name, owner):
        """Names an NPC by its owner's form id on the host (hex string or number) and its copy on the guest: `find`
        names only NPCs near the host."""
        owner = int(owner, 16) if isinstance(owner, str) else owner
        copy_form = next((pair[1] for pair in ctx.copy_pairs().values() if pair[0] == owner), None)
        if copy_form is None:
            raise ctx.failure(f"{owner:X} has no owner on the host with a copy on the guest, both in 3D")
        return ctx.name_handle(name, {"host": owner, "guest": copy_form})

    def resolve(ctx, value, game):
        """A form id in that game for a handle name, "player", or a number (or hex string) as given."""
        game = "guest" if game in ("client", "guest") else "host"
        if isinstance(value, int):
            return value
        if value == "player":
            return 0x14
        if value in ctx.handles():
            ids = ctx.handles()[value]
            if game not in ids:
                raise ctx.failure(f"{value!r} has no form id in the {game}'s game")
            return ids[game]
        try:
            return int(value, 16)
        except (TypeError, ValueError):
            raise ctx.failure(f"unknown handle {value!r} (known: {', '.join(ctx.handles()) or 'none'})") from None

    # --- Retrying from a checkpoint: the host's save, the party loads with it ---

    def checkpoint(ctx, name="mcp-checkpoint"):
        """The host saves the party where it stands (`save <name>`): `retry` comes back here in seconds, not a new duo."""
        path = ctx.save_game("", name)
        ctx.wait_for("the checkpoint and its co-save are written", ctx.saves_written(path), 20)
        ctx.REPRO_CHECKPOINT = name
        return {"saved": name}

    def retry(ctx, name=None):
        """The host loads the checkpoint and the party goes back to it (the guest loads with the host). Waits for both
        games' `game_loaded` event, each showing the other player and taking input; handles follow the new copies."""
        name = name or getattr(ctx, "REPRO_CHECKPOINT", None)
        if not name:
            raise ctx.failure("no checkpoint to retry from: checkpoint first")
        started, games = time.monotonic(), ("", "client")
        since = {game: ctx.engine_events(game)[1] for game in games}
        ctx.console_on("", f"load {name}")
        loaded = lambda game: any(e["kind"] == "game_loaded" for e in ctx.engine_events(game, since[game])[0])
        ctx.wait_for(f"both games load the checkpoint {name}", lambda: all(loaded(game) for game in games), 120)
        ctx.wait_for("each game shows the other player again", ctx.both_see_each_other, 60)
        ctx.wait_for("both games take input", lambda: all(ctx.takes_input(game) for game in games), 30)
        ctx.refresh_handles()
        return {"seconds": round(time.monotonic() - started, 1)}

    # --- The recorder ---

    def record_start(ctx, name):
        RECORDINGS.mkdir(parents=True, exist_ok=True)
        ctx.REPRO_RECORDING = RECORDINGS / f"{time.strftime('%Y%m%d-%H%M%S')}-{name}.jsonl"
        ctx.REPRO_RECORDING.write_text("")
        if save := ctx.booted_save():  # a late save's flow: the test needs the duo booted from it
            ctx.record_call("needs_save", {"save": save}, True)
        if scene := getattr(ctx, "REPRO_PLACE", None):  # the scene it was set in
            ctx.record_call("scene", scene, True)
        for spec in getattr(ctx, "REPRO_SCENE", []):  # the scene as staged so far: the test starts from it too
            ctx.record_call("stage", {"spec": spec}, True)
        return str(ctx.REPRO_RECORDING)

    def booted_save(ctx):
        """The save the running duo was booted from (its boot file), or None."""
        with contextlib.suppress(OSError, ValueError):
            return json.loads((ctx.game("").control / BOOT_FILE).read_text()).get("save")
        return None

    def needs_save(ctx, save):
        """The duo was booted from `save`: a recorded late-save flow starts where it was recorded."""
        if ctx.booted_save() != save:
            raise ctx.failure(f"this flow needs the duo booted from {save}: `coop_test.py duo --save {save} --keep-running`")

    def record_call(ctx, tool, arguments, ok, result=None):
        """One tool call, as the recorder keeps it (the JSON lines a scenario is exported from)."""
        path = getattr(ctx, "REPRO_RECORDING", None)
        if not path:
            return
        entry = {"time": time.time(), "tool": tool, "args": arguments, "ok": ok}
        if result is not None:
            entry["result"] = result
        with path.open("a") as file:
            file.write(json.dumps(entry, default=str) + "\n")

    # --- The world frozen and stepped (TestHarness world_freeze / step, FrameStepper) ---

    def world_clock(ctx, instance):
        reply = ctx.player_command(instance, {"cmd": "world_clock"})
        if not reply or not reply.get("ok"):
            raise ctx.failure(f"[{instance or 'host'}] no world clock: {reply}")
        return reply

    def freeze_world(ctx, frozen=True):
        """Stops (or restarts) the world's update in both games: time, AI, the player, projectiles."""
        for instance in ("", "client"):
            reply = ctx.player_command(instance, {"cmd": "world_freeze", "frozen": frozen})
            if not reply or not reply.get("ok"):
                raise ctx.failure(f"[{instance or 'host'}] world_freeze refused: {reply}")
        return {"host": ctx.world_clock("")["world_frames"], "guest": ctx.world_clock("client")["world_frames"]}

    def step_world(ctx, frames=1, seconds=10.0):
        """In a frozen world, both games run `frames` world updates and freeze again."""
        for instance in ("", "client"):
            reply = ctx.player_command(instance, {"cmd": "step", "frames": frames})
            if not reply or not reply.get("ok"):
                raise ctx.failure(f"[{instance or 'host'}] step refused: {reply}")
        clocks = ctx.wait_for(f"both games ran {frames} frames", lambda: (c := {i: ctx.world_clock(i) for i in ("", "client")})
                              and all(not x["steps_left"] for x in c.values()) and c, seconds)
        return {"host": clocks[""]["world_frames"], "guest": clocks["client"]["world_frames"]}

    # --- stage: one call sets up a scene ---

    def name_players(ctx):
        """The players' handles: host_player and guest_player, in both games."""
        ctx.name_handle("host_player", {"host": 0x14, "guest": ctx.player_copy("client")})
        ctx.name_handle("guest_player", {"guest": 0x14, "host": ctx.player_copy("")})

    def staged(ctx):
        if not hasattr(ctx, "REPRO_STAGED"):
            ctx.REPRO_STAGED = []
        return ctx.REPRO_STAGED

    def stage(ctx, spec):
        """Sets up a scene: {players: {host|guest: {near, gap, bearing}}, npcs: [{name, base | ref, frozen}],
        objects: [{name, base | ref, by}], items: [{who, item, count, equip}]}. Returns the handles."""
        ctx.name_players()
        if not hasattr(ctx, "REPRO_SCENE"):
            ctx.REPRO_SCENE = []
        ctx.REPRO_SCENE.append(spec)
        for index, npc in enumerate(spec.get("npcs", [])):
            spot = ctx.scene_spot(index)
            if "ref" in npc:
                host = int(str(npc["ref"]), 16) if isinstance(npc["ref"], str) else npc["ref"]
                copy = ctx.stage_host_npc(host, spot)
            else:
                base = npc["base"] if isinstance(npc["base"], str) else f"{npc['base']:X}"
                host, _, copy = ctx.spawn_host_npc(base, restrained=npc.get("frozen", True), goal=spot)
                ctx.staged().append(("npc", host))
            if npc.get("frozen", True):
                ctx.freeze("", host)
            ctx.name_handle(npc["name"], {"host": host, "guest": copy})
        for item in spec.get("items", []):
            game = "client" if item.get("who") in ("guest", "client") else ""
            code = item["item"] if isinstance(item["item"], str) else f"{item['item']:X}"
            ctx.console_on(game, f"player.additem {code} {item.get('count', 1)}")
            if item.get("equip"):
                ctx.console_on(game, f"player.equipitem {code}")
        for game, place in spec.get("players", {}).items():
            instance = "client" if game in ("guest", "client") else ""
            ctx.place_before(instance, ctx.resolve(place["near"], game), place.get("gap", 120), place.get("bearing"))
        for thing in spec.get("objects", []):
            if "ref" in thing:  # a plugin's reference: the same id in both games
                ref = int(str(thing["ref"]), 16) if isinstance(thing["ref"], str) else thing["ref"]
                ctx.name_handle(thing["name"], {"host": ref, "guest": ref})
                continue
            ctx.name_handle(thing["name"], ctx.place_object(thing["base"]))
        return ctx.handles()

    def scene_spot(ctx, index=0, gap=250.0):
        """Where a staged NPC stands: the stage while the party is home, else ahead of the host player on a floor it
        can see (rays). None of that near the players fails: a stage far off is never what a repro means."""
        here = ctx.my_position("")
        spot = ctx.stage_spot()
        if math.dist(spot[:2], here[:2]) <= STAGE_RANGE:
            return spot
        heading = ctx.player_snapshot("")["placement"]["rotation"][2]
        reach = gap + 100.0 * index
        x, y = here[0] + math.sin(heading) * reach, here[1] + math.cos(heading) * reach
        chest = [here[0], here[1], here[2] + 100.0]
        floor = (ctx.player_command("", {"cmd": "ray", "from": [x, y, here[2] + 200.0], "to": [x, y, here[2] - 500.0]}) or {}).get("ray") or {}
        if not floor.get("hit"):
            raise ctx.failure(f"no floor {reach:.0f} units ahead of the host at {[round(v) for v in here]}: turn, or stage elsewhere")
        goal = [x, y, floor["point"][2]]
        sight = (ctx.player_command("", {"cmd": "ray", "from": chest, "to": [x, y, goal[2] + 100.0]}) or {}).get("ray") or {}
        if sight.get("hit"):
            raise ctx.failure(f"something stands between the host and the spot {reach:.0f} units ahead: {sight}")
        return goal

    def twin_in(ctx, instance, before, host_row, seconds=10):
        """The reference new in that game since `before` with the same base, nearest the other game's: its twin."""
        def find():
            fresh = {ref: row for ref, row in ctx.world_state(instance).items() if ref not in before}
            near = sorted((math.dist(row["position"][:2], host_row["position"][:2]), ref) for ref, row in fresh.items()
                          if row.get("base") == host_row.get("base") and math.dist(row["position"][:2], host_row["position"][:2]) < 300)
            return near[0][1] if near else None
        return ctx.wait_for(f"[{instance or 'host'}] a twin of {host_row.get('id', 0):X}", find, seconds)

    def place_object(ctx, base):
        """The host places a new object at its player's feet; returns its id and its twin's in the guest's game."""
        code = base if isinstance(base, str) else f"{base:X}"
        before = {game: set(ctx.world_state(game)) for game in ("", "client")}
        ctx.console_on("", f"player.placeatme {code} 1")
        made = ctx.wait_for("[host] the new object", lambda: set(ctx.world_state("")) - before[""], 5)
        host = next(iter(made))
        ctx.staged().append(("object", host))
        guest = ctx.twin_in("client", before["client"], ctx.world_state("", [host])[host])
        ctx.staged().append(("guest-object", guest))
        return {"host": host, "guest": guest}

    def drop_item(ctx, game, item, name):
        """That game's player gets one `item` and drops it (the harness's plain drop); names it and its twin."""
        instance = "client" if game in ("guest", "client") else ""
        other = "" if instance else "client"
        item = item if isinstance(item, int) else int(item, 16)
        ctx.console_on(instance, f"player.additem {item:X} 1")
        ctx.wait_for(f"[{instance or 'host'}] the player carries {item:X}", lambda: (ctx.item_count(instance, 0x14, item) or 0) > 0, 5)
        before = set(ctx.world_state(other))
        dropped = ctx.player_command(instance, {"cmd": "drop", "item": item, "plain": True})["object"]
        ctx.staged().append(("object" if not instance else "guest-object", dropped))
        row = ctx.wait_for(f"[{instance or 'host'}] the dropped {item:X}", lambda: ctx.world_state(instance, [dropped]).get(dropped), 5)
        twin = ctx.twin_in(other, before, row)
        ctx.staged().append(("object" if other == "" else "guest-object", twin))
        mine, theirs = ("guest", "host") if instance else ("host", "guest")
        return ctx.name_handle(name, {mine: dropped, theirs: twin})

    def wait_rest(ctx, name, game="host", seconds=15):
        """Waits until that game's body of the handle sleeps (at rest)."""
        instance = "client" if game in ("guest", "client") else ""
        ref = ctx.resolve(name, game)
        ctx.wait_for(f"[{instance or 'host'}] {name} rests", lambda: (b := ctx.body_of(instance, ref)) and not b["awake"], seconds)
        position = ctx.body_of(instance, ref)["position"]
        if not hasattr(ctx, "REPRO_RESTS"):
            ctx.REPRO_RESTS = {}
        ctx.REPRO_RESTS[(name, "guest" if instance else "host")] = position
        return {"position": position}

    def unstage(ctx):
        """Removes what stage made (objects, spawned NPCs) in both games and thaws the world."""
        with contextlib.suppress(TestFailure):
            ctx.freeze_world(False)
        ctx.unpin_world()
        for kind, ref in reversed(ctx.staged()):
            game = "client" if kind == "guest-object" else ""
            # An item a player picked up is gone already.
            if kind in ("object", "guest-object") and ref not in ctx.world_state(game, [ref]):
                continue
            ctx.console_on(game, f"{ref:X}.disable")
            ctx.console_on(game, f"{ref:X}.markfordelete")
        ctx.staged().clear()
        ctx.handles().clear()
        ctx.REPRO_SCENE = []

    # --- scene: a place with the world held still, checked where the repro starts ---

    def pin_world(ctx):
        """Holds the world for a repro that plays: the host's clock stopped (so schedules stay) and its weather forced;
        the guest follows. `world_freeze` is the frame-level tool: it stops the players too."""
        if getattr(ctx, "REPRO_PIN", None) is None:
            ctx.REPRO_PIN = ctx.calendar("")["scale"]
        ctx.console_on("", f"fw {ctx.game_state('').get('weather'):X}")
        ctx.console_on("", "set timescale to 0")
        ctx.wait_for("[guest] its clock stops with the host's", lambda: abs(ctx.calendar("client").get("scale", 1)) < 0.001, 5)

    def unpin_world(ctx):
        if (scale := getattr(ctx, "REPRO_PIN", None)) is not None:
            ctx.console_on("", f"set timescale to {scale:g}")
            ctx.REPRO_PIN = None

    def scene_start(ctx):
        """Where the two games stand once copies had time to catch up: the clock and weather must agree; what else
        differs lasts, {key: message}, and is the scene's own start: real bugs, shown, not this repro's."""
        ctx.wait_for("the guest follows the host's weather and calendar", lambda: not ctx.world_sync_problems(), SETTLE_SECONDS)
        ended = time.monotonic() + SETTLE_SECONDS
        while (differ := ctx.lasting_desyncs()) and time.monotonic() < ended:
            time.sleep(0.2)
        return differ

    def scene(ctx, save=None, at=None, pin=True):
        """Starts a repro: on the duo booted from `save`, the party goes to `at` (a console line such as coc Riverwood)
        and the world is pinned. What differs here is returned as `disagree`; from now on only new differences fail
        (`expect agree`, the runner's end check), as a baseline's known world differences don't. Names the NPCs around."""
        if save:
            ctx.needs_save(save)
        if at:
            ctx.go_far("", at)
            ctx.arrives_with_party("client")
        ctx.name_players()
        if pin:
            ctx.pin_world()
        differ = ctx.scene_start()
        ctx.SCENE_KNOWN = set(differ)
        ctx.REPRO_PLACE = {"save": save, "at": at, "pin": pin}
        named = ctx.name_nearby()
        return {"cell": ctx.player_cell(""), "time": ctx.calendar("").get("hour"), "named": named, "disagree": list(differ.values())}

    def forget_scene(ctx):
        """Back at the blank baseline: a recording starts from there, and nothing is known to differ."""
        ctx.REPRO_PLACE, ctx.SCENE_KNOWN, ctx.REPRO_SEEN = None, set(), {}

    def worlds(ctx):
        return tuple((ctx.game(instance).read_status() or {}).get("world") or {} for instance in ("", "client"))

    def nearby_shared(ctx):
        host, guest = ctx.worlds()
        return [actor for actor in shared_actors(host, guest, ctx.my_position("")) if actor["distance"] <= SCENE_RADIUS]

    def name_nearby(ctx):
        """Handles for the NPCs around, by name: {handle: distance}."""
        named = name_handles(ctx.nearby_shared())
        for handle, actor in named.items():
            ctx.name_handle(handle, actor)
        return {handle: actor["distance"] for handle, actor in named.items()}

    def find(ctx, name, handle=None):
        """Names the nearest NPC both games show whose name holds `name`: replays by name, as ids differ between runs."""
        actors = ctx.nearby_shared()
        match = next((actor for actor in actors if name.lower() in actor["name"].lower()), None)
        if not match:
            raise ctx.failure(f"no {name!r} near the host: {sorted({actor['name'] for actor in actors})}")
        handle = handle or slug(match["name"])
        ctx.name_handle(handle, match)
        return {"handle": handle, "name": match["name"], "distance": match["distance"]}

    # --- watch: what is going on, without a throwaway script ---

    def look(ctx, instance):
        """That game around its player: cell, menus, what the player is doing, the actors near it, nearest first."""
        status = ctx.game(instance).read_status() or {}
        world = status.get("world") or {}
        me = world.get("20") or {}
        here = (me.get("placement") or {}).get("position") or [0.0, 0.0, 0.0]
        around = []
        for form, row in world.items():
            place = row["placement"]["position"]
            if form != "20" and math.dist(place[:2], here[:2]) <= SCENE_RADIUS:
                around.append({"name": row["identity"].get("name"), "id": f"{int(form):X}", "role": row.get("role"),
                               "distance": round(math.dist(place[:2], here[:2])), "dead": row["state"].get("dead"),
                               "package": f"{row['ai'].get('running_package') or 0:X}",
                               "combat_target": f"{row['ai'].get('combat_target') or 0:X}"})
        return {"cell": (me.get("placement") or {}).get("cell"), "position": [round(v) for v in here],
                "menus": sorted(set(status.get("menus") or []) - {"HUD Menu"}), "health": (me.get("values") or {}).get("health"),
                "character_state": (status.get("player") or {}).get("character_state"),
                "nearby": sorted(around, key=lambda actor: actor["distance"])[:12]}

    def refresh_handles(ctx):
        """Follows the copies of what the handles name (`current_copies`); a repro's reads and verbs start with this."""
        ctx.handles().update(current_copies(ctx.handles(), *ctx.worlds()))
        if "host_player" in ctx.handles():  # a player's copy is new after a death and respawn
            with contextlib.suppress(TestFailure):
                ctx.name_players()

    def read_handle(ctx, name, full=False):
        """Each game's flat reading of a handle: an actor's snapshot, or a reference's row."""
        shown = {}
        for game, instance in (("host", ""), ("guest", "client")):
            if ref := ctx.handles().get(name, {}).get(game):
                row = ctx.actor_row(instance, ref) or ctx.world_state(instance, [ref]).get(ref)
                shown[game] = brief(row, full)
        return shown

    def watch(ctx, names, full=False):
        """What changed in each handle since the last look ({field: [was, is]}; everything the first time), how far
        apart the two games have it, and which fields they hold differently."""
        if not hasattr(ctx, "REPRO_SEEN"):
            ctx.REPRO_SEEN = {}
        ctx.refresh_handles()
        seen = {}
        for name in names:
            if name not in ctx.handles():
                raise ctx.failure(f"unknown handle {name!r} (known: {', '.join(ctx.handles()) or 'none'})")
            shown, before = ctx.read_handle(name, full), ctx.REPRO_SEEN.get(name)
            entry = {game: changes(before[game], row) if before and game in before else row for game, row in shown.items()}
            if len(shown) == 2:
                entry["differs"] = game_differences(shown["host"], shown["guest"])
                places = [row.get("placement.position") or row.get("position") for row in shown.values()]
                if all(places):
                    entry["apart"] = round(math.dist(places[0], places[1]))
            ctx.REPRO_SEEN[name] = shown
            seen[name] = entry
        return seen

    def spec_step(ctx, step):
        """One shared-spec step (Code/tests/specs, scenarios/specs.run_step): a session of these exports as a spec."""
        from ..scenarios import specs
        return specs.run_step(ctx, {"name": "repro", "steps": [step]}, 0, step)

    def record_export(ctx, name, doc, tags=("recorded",)):
        """The recording as scenarios/recorded/<name>.py, and as Code/tests/specs/<name>.json when it is all spec steps."""
        entries = [json.loads(line) for line in ctx.REPRO_RECORDING.read_text().splitlines() if line]
        module = name.replace("-", "_")
        path = ROOT / "scripts/coop_harness/scenarios/recorded" / f"{module}.py"
        path.write_text(export_scenario(entries, name, doc, tags, recorded=ctx.REPRO_RECORDING.name))
        written = {"scenario": str(path)}
        if spec := export_spec(entries, name, doc, tags):
            spec_path = ROOT / "Code/tests/specs" / f"{name}.json"
            spec_path.write_text(json.dumps(spec, indent=2) + "\n")
            written["spec"] = str(spec_path)
        else:  # out of Code/tests/specs: every file there is a test
            draft, written["leftovers"] = spec_draft(entries, name, doc)
            draft_path = RECORDINGS / f"{name}.spec-draft.json"
            draft_path.write_text(json.dumps(draft, indent=2) + "\n")
            written["spec_draft"] = str(draft_path)
        return written

    # --- observe and expect ---

    def observe_one(ctx, name):
        """What each game shows of a handle: an actor's status entry, or a reference's world-state row."""
        shown = {}
        for game, instance in (("host", ""), ("guest", "client")):
            ref = ctx.handles().get(name, {}).get(game)
            if not ref:
                continue
            actor = ctx.actors_on(instance).get(str(ref))
            if actor is None and ref == 0x14:
                actor = ctx.local_player(instance)
            if actor is None:  # this game's own actors (the host's NPCs)
                actor = (ctx.local_player(instance).get("local_actors") or {}).get(str(ref))
            shown[game] = actor if actor is not None else ctx.world_state(instance, [ref]).get(ref)
        return shown

    def observe_handles(ctx, names):
        """{name: {host, guest, apart}}: apart is the distance between the two games' places of it."""
        seen = {}
        for name in names:
            shown = ctx.observe_one(name)
            places = [where(row) for row in shown.values() if row]
            entry = {game: row for game, row in shown.items()}
            if len(places) == 2 and all(places):
                entry["apart"] = round(math.dist(places[0], places[1]), 2)
            seen[name] = entry
        return seen

    def expect(ctx, name, check, within=5.0, game=None, value=None, of=None, field=None, seconds=0.0):
        """The correct behaviour, checked now: "same_place" (the two games within `within` units), "at" (that game's
        place within `within` of `value`), "at_rest_of" (that game's place within `within` of where the `of` game's body
        rested, as wait_rest saw it), "stage" (quest `name` at stage value[0] in both games), "items" (container `name`
        holds the same in both games), "crosshair" (that game's player, or each, faces `name` in first person and the engine's
        crosshair takes it within `seconds`), "actor" (`field` of the snapshot equals `value` in `game` or both, within `seconds`;
        value "set": anything but nothing; no value: the two games equal), "dialogue" (the menus agreed), "agree" (no desync of
        `name`, or anywhere with name "all"), "graph" (owner's and copy's graphs read live for `seconds`: never apart for
        `within` seconds or more, no gap between readings of both as long, and the owner entering a state named like `field`
        at least `value` times, or with no `field` its graph changing that often: a quiet run proves nothing)."""
        if check == "items":
            held = {g: ctx.container_items(i, ctx.resolve(name, g)) for g, i in (("host", ""), ("guest", "client"))}
            if held["host"] != held["guest"]:
                raise ctx.failure(f"{name}: holds differently: " + ctx.item_differences(held["host"], held["guest"], "host", "guest"))
            return {"items": len(held["host"])}
        if check == "actor":
            return ctx.expect_actor(name, field, game, value, seconds)
        if check == "crosshair":
            return ctx.expect_crosshair(name, game, seconds or 2.0)
        if check == "stays":
            return ctx.expect_actor(name, field, game, value, seconds, stays=True)
        if check == "graph":
            owner, seconds = ctx.resolve(name, "host"), seconds or 60.0
            watch = ctx.graph_watch_start(0.25, within, {owner}, None, field, strict=True)
            watch["stop"].wait(seconds)
            watch["stop"].set()
            watch["thread"].join(10)
            if error := watch["state"]["error"]:
                raise ctx.failure(f"{name}: the graph watch stopped: {error}")
            first, last, reads = watch["seen"].get(owner, (0.0, 0.0, 0))
            failed = watch["failed"].get(owner, 0)
            # Read through the interval (a second's slack), once a second at least: a pair lost early is no pass.
            if reads < max(1.0, seconds) or last - first < seconds - 1.0:
                raise ctx.failure(f"{name}: owner and copy both read {reads} times over {last - first:.1f} of {seconds:g} s "
                                  f"({failed} failed)")
            if (gap := watch["gaps"].get(owner, 0.0)) >= within:
                raise ctx.failure(f"{name}: {gap:.1f} s without a reading of both graphs ({failed} failed): a run apart could hide there")
            changed, entered = watch["changes"].get(owner, 0), watch["entered"].get(owner, 0)
            if field and entered < int(value or 1):
                raise ctx.failure(f"{name}: the owner entered {field} {entered} times, fewer than {int(value or 1)}: nothing to follow")
            if not field and value and changed < int(value):
                raise ctx.failure(f"{name}: the owner's graph changed {changed} times, fewer than {int(value)}: nothing to follow")
            if runs := watch["apart"].finish(time.time()):
                raise ctx.failure(f"{name}: owner and copy graphs apart for {within:g} s or more: " + " | ".join(
                    f"{run['seconds']:.1f} s, owner {run.get('owner_only')}, copy {run.get('copy_only')}" for run in runs[:3]))
            return {"samples": watch["apart"].samples, "owner_changes": changed, **({"entered": entered} if field else {}), "failed": failed}
        if check == "dialogue":
            if problems := ctx.check_dialogue_trace():
                raise ctx.failure("the menus differ in a conversation: " + " | ".join(problems[:6]))
            return {"agree": True}
        if check == "agree":
            ids = [f"{ref:08X}" for ref in ctx.handles().get(name, {}).values()]
            problems = [p for p in ctx.check_desync(known=getattr(ctx, "SCENE_KNOWN", ())) if name == "all" or any(ref in p for ref in ids)]
            if problems:
                raise ctx.failure(f"{name}: the games disagree: " + " | ".join(problems[:8]))
            return {"agree": True}
        if check == "stage":  # `name` a quest's editor id, `value` [stage]: both games at it
            stages = {}
            for label, instance in (("host", ""), ("guest", "client")):
                printed = " ".join(ctx.console_on(instance, f"getstage {name}") or [])
                found = re.search(r">>\s*(-?[\d.]+)", printed)
                stages[label] = float(found.group(1)) if found else None
            if any(stage != float(value[0]) for stage in stages.values()):
                raise ctx.failure(f"{name}: not at stage {value[0]:g} in both games: {stages}")
            return {"stages": stages}
        shown = ctx.observe_one(name)
        if check == "at_rest_of":
            game = "guest" if game in ("guest", "client") else "host"
            other = "guest" if of in ("guest", "client") else "host"
            rest = getattr(ctx, "REPRO_RESTS", {}).get((name, other))
            if rest is None:
                raise ctx.failure(f"{name}: no rest seen in the {other}'s game (wait_rest first)")
            body = ctx.body_of("client" if game == "guest" else "", ctx.resolve(name, game))
            place = body and body["position"]
            if place is None or math.dist(place, rest) > within:
                off = f"{math.dist(place, rest):.1f}" if place else "?"
                raise ctx.failure(f"{name}: the {game}'s is not where the {other}'s rested at {[round(v) for v in rest]}: "
                                  f"at {[round(v) for v in place] if place else None}, {off} off (at most {within})")
            return {"off": round(math.dist(place, rest), 2)}
        if check == "same_place":
            places = {g: where(row) for g, row in shown.items() if row}
            if len(places) < 2 or None in places.values():
                raise ctx.failure(f"{name}: not shown in both games ({sorted(places)})")
            apart = math.dist(places["host"], places["guest"])
            if apart > within:
                raise ctx.failure(f"{name}: the guest's is {apart:.1f} units from the host's (at most {within}): "
                                  f"host {[round(v) for v in places['host']]}, guest {[round(v) for v in places['guest']]}")
            return {"apart": round(apart, 2)}
        if check == "at":
            place = where(shown.get(game or "host") or {})
            if place is None or math.dist(place, value) > within:
                raise ctx.failure(f"{name}: in the {game or 'host'}'s game at {place}, not within {within} of {value}")
            return {"at": place}
        raise ctx.failure(f"unknown check {check!r}")


    def expect_crosshair(ctx, name, game, seconds=2.0):
        """In `game` (or each), the player faces `name` in first person and the crosshair takes it (CrosshairPickData,
        written by PickCrosshairReference 40620): what a player needs for its name and Activate prompt."""
        games = [("guest", "client")] if game in ("guest", "client") else [("host", "")] if game else [("host", ""), ("guest", "client")]
        ctx.refresh_handles()
        taken = {}
        for label, instance in games:
            ref = ctx.resolve(name, label)
            camera = lambda: ctx.player_command(instance, {"cmd": "camera"})
            if camera().get("state") in (8, 9):  # third person: the view key, as a player
                ctx.hold_key(instance, "f", 0.1)
                ctx.wait_for(f"[{label}] first person", lambda: camera().get("state") == 0, 5)
            ctx.act(instance, {"name": f"aims at {name}", "ready": ["takes_input"], "turn": {"face": ref, "rate": 180},
                               "until": ["aimed"], "seconds": 3})
            with contextlib.suppress(TestFailure):
                ctx.wait_for(f"[{label}] the crosshair on {name}", lambda: camera().get("crosshair") == ref, seconds)
            seen = camera()
            if seen.get("crosshair") != ref:
                looking = seen.get("looking") or {}
                hit = f"{looking.get('ref') or 0:X} at {looking.get('distance', 0):.0f} units" if looking.get("hit") else "nothing"
                raise ctx.failure(f"[{label}] the crosshair is on {seen.get('crosshair') or 0:X}, not {name} ({ref:X}); "
                                  f"the camera's ray hits {hit}")
            taken[label] = f"{ref:X}"
        return {"crosshair": taken}

    def expect_actor(ctx, name, field, game, value, seconds=0.0, stays=False):
        """`field` of a handle's snapshot (ai.follow_target, state.dead) as expected within `seconds`, or, with `stays`,
        all through them (read every 0.2 s); see `expect`."""
        games = [("guest", "client")] if game in ("guest", "client") else [("host", "")] if game else [("host", ""), ("guest", "client")]
        ctx.refresh_handles()
        read = lambda: {label: flatten(ctx.actor_row(instance, ctx.resolve(name, label))).get(field) for label, instance in games}
        def wrong(got):
            if value is None:
                return len(set(map(repr, got.values()))) > 1
            return not all(got.values()) if value == "set" else any(seen != value for seen in got.values())
        ended, started = time.monotonic() + seconds, time.monotonic()
        expected = 'the same in both' if value is None else repr(value)
        if stays:  # the first reading, every 0.2 s, and one at the deadline
            while True:
                if wrong(got := read()):
                    raise ctx.failure(f"{name}: {field} became {got} {time.monotonic() - started:.1f} s in, expected {expected} "
                                      f"all through {seconds:g} s")
                if time.monotonic() >= ended:
                    return {field: got}
                time.sleep(min(0.2, max(0.0, ended - time.monotonic())))
                ctx.refresh_handles()
        while wrong(got := read()):
            if time.monotonic() >= ended:
                raise ctx.failure(f"{name}: {field} is {got}, expected {expected}")
            time.sleep(0.2)
        return {field: got}


def where(row):
    """A shown thing's place: an actor's actual or position, a reference's position."""
    if not row:
        return None
    return row.get("actual") or row.get("position")


# Recorded tool -> its scenario line; None: not part of the test (looking, the duo, the recorder).
_INSTANCE = {"host": "", "guest": "client", "client": "client", "": ""}


# Verbs that only read: left out of an exported scenario.
READ_VERBS = {"witness", "witness_mood", "party_bounty", "bounty_total"}

# Harness commands that only read: left out of an exported scenario.
_READS = {"status", "dialogue", "camera", "world_clock", "counters", "papyrus_result", "path_request", "path_probe", "screen", "item_count"}


def _lines(say):
    """Dialogue lines as `pick` takes them: a number is an index."""
    return [int(line) if str(line).isdigit() else line for line in say]


def _line(tool, args):
    game = repr(_INSTANCE.get(args.get("game", "host"), ""))
    match tool:
        case "scene":
            return f"ctx.scene(save={args.get('save') or None!r}, at={args.get('at') or None!r}, pin={args.get('pin', True)!r})"
        case "find":
            return f"ctx.find({args['name']!r}, {args.get('handle') or None!r})"
        case "go_to":
            return f"ctx.go_to({game}, {args['target']!r}, {args.get('within', 100.0)!r}, {args.get('straight', False)!r}, {args.get('by_input', False)!r})"
        case "talk":
            return f"ctx.talk({game}, {args['target']!r}, {_lines(args.get('say') or [])!r})"
        case "use":
            return f"ctx.use({game}, {args['target']!r})"
        case "take":
            return f"ctx.take({game}, {args['target']!r})"
        case "fight":
            return f"ctx.fight({game}, {args['target']!r})"
        case "world_freeze":
            return f"ctx.freeze_world({args.get('frozen', True)!r})"
        case "step":
            return f"ctx.step_world({args.get('frames', 1)!r})"
        case "stage":
            return f"ctx.stage({args['spec']!r})"
        case "name_handle":
            return f"ctx.name_handle({args['name']!r}, {{'host': {args.get('host', 0)!r}, 'guest': {args.get('guest', 0)!r}}})"
        case "needs_save":
            return f"ctx.needs_save({args['save']!r})"
        case "console":
            if re.match(r"(\S+\.)?(get|is)\w*\b", args["line"], re.I):  # a read: looking, not part of the test
                return None
            return f"ctx.console_on({game}, {args['line']!r})"
        case "harness":
            if args["request"].get("cmd") in _READS:
                return None
            return f"ctx.player_command({game}, {args['request']!r})"
        case "act":
            return f"ctx.act({game}, {args['spec']!r})"
        case "press":
            return f"ctx.hold_key({game}, {args['key']!r}, {args.get('seconds', 0.1)!r})"
        case "mouse":
            return f"ctx.mouse_on({game}, {args.get('action', 'click')!r}, {args.get('button', 1)!r})"
        case "expect":
            extra = "".join(f", {key}={args[key]!r}" for key in ("within", "game", "value", "of", "field", "seconds") if args.get(key) not in (None, ""))
            return f"ctx.expect({args['name']!r}, {args['check']!r}{extra})"
        case "drop":
            return f"ctx.drop_item({args['game']!r}, {args['item']!r}, {args['name']!r})"
        case "wait_rest":
            return f"ctx.wait_rest({args['name']!r}, {args.get('game', 'host')!r})"
        case "arrives_with_party":
            return f"ctx.arrives_with_party({game}, {args.get('within', 3000.0)!r})"
        case "travel":
            return f"ctx.go_far({game}, {args['command']!r})"
        case "leave_dialogue":
            return "ctx.close_dialogues()"
        case "equip":
            return f"ctx.equip_weapon({game}, {int(args.get('item') or '0', 16)!r})"
        case "kill_move_exchange":
            return "ctx.kill_move_exchange({g: ctx.other_player(ctx.actors_on(g)) for g in ('', 'client')})"
        case "menu":
            return f"ctx.menu({game}, {args['name']!r}, {args.get('show', True)!r})"
        case "spec_step":
            return f"ctx.spec_step({args['step']!r})"
        case "checkpoint":
            return f"ctx.checkpoint({args.get('name', 'mcp-checkpoint')!r})"
        case "retry":
            return f"ctx.retry({args.get('name') or None!r})"
        case "verb":
            if args["name"] in READ_VERBS:
                return None
            verb_args = {**(args.get("args") or {})}
            if "instance" in verb_args:
                verb_args["instance"] = _INSTANCE.get(verb_args["instance"], verb_args["instance"])
            return f"ctx.{args['name']}(" + ", ".join(f"{key}={value!r}" for key, value in verb_args.items()) + ")"
        case "rounds":
            body = [line for name, given in ((args.get("reset"), args.get("reset_args")), (args["call"], args.get("args")))
                    if name and (line := _called(name, given or {}))]
            if not body:
                return None
            return f"for _ in range({args.get('count', 10)!r}):\n" + "\n".join(f"    {line}" for line in body)
    return None


# Tools that only look or run the duo: never part of a test (a name in `rounds` that is neither these nor a replayed
# tool is a Session verb).
LOOKS = {"jitter", "duo_start", "vanilla_start", "duo_wait", "duo_stop", "status", "look", "terrain", "observe", "why", "press_to_motion", "profile",
         "counters", "network", "job_wait", "quest_walk", "screenshot", "record_export", "record_start", "unstage",
         "scene_reset", "verbs", "rounds", "trace_start", "trace_read", "trace_mark", "trace_stop", "log", "mcp_reload", "kill_move_check"}


def _called(name, args):
    """A call `rounds` repeats as its scenario line: a replayed tool's, else a verb's."""
    if (line := _line(name, args)) or name in LOOKS:
        return line
    return _line("verb", {"name": name, "args": args})


def _is_expect(entry):
    """A check, kept even when it failed (a failing expect is the bug): the tool, or an expect* verb through `verb`."""
    return entry["tool"] == "expect" or (entry["tool"] == "verb" and entry["args"].get("name", "").startswith("expect"))


def _replayed(entry):
    """A call's arguments as it ran: `rounds` replays the rounds it ran (it may have stopped at `until`)."""
    ran = (entry.get("result") or {}).get("ran") if isinstance(entry.get("result"), dict) else None
    return {**entry["args"], "count": ran} if entry["tool"] == "rounds" and ran is not None else entry["args"]


def export_scenario(entries, name, doc, tags, recorded=None):
    """A recorded session as a scenario module: its calls in order (failed ones left out, except expects: a failing
    expect is the bug), in a try that unstages and thaws the world. `recorded` (the session's file) stamps it."""
    lines = [line for entry in entries if entry["ok"] or _is_expect(entry)
             if (line := _line(entry["tool"], _replayed(entry)))]
    if not lines:
        raise ValueError("nothing recorded to replay")
    if any(re.search(r"'(host|guest)_player'", line) for line in lines):  # named by scene_reset, which isn't recorded
        lines.insert(0, "ctx.name_players()")
    body = "\n".join("\n".join(f"        {part}" for part in line.split("\n")) for line in lines)
    if any(entry["tool"] == "needs_save" for entry in entries):  # the runner's saved-story fixture
        tags = [*tags, "late-save"]
    if any(entry["tool"] == "scene" and (entry["args"].get("save") or entry["args"].get("at")) for entry in entries):
        tags = [*tags, "scene"]  # away from the baseline: the flow checks its own start (registry.py)
    stamp = f"# recorded: {recorded or 'unnamed session'}, {len(entries)} calls (scripts/check_scenarios.py)\n"
    return (f'"""{doc}\n\nRecorded with the repro MCP (docs/design/repro-mcp.md)."""\n{stamp}'
            f"NAME = {name!r}\nTAGS = {sorted(set(tags))!r}\nDOC = {doc!r}\n\n\n"
            f"def run(ctx):\n    try:\n{body}\n    finally:\n        ctx.unstage()\n")


def export_spec(entries, name, doc, tags):
    """A recorded session made only of spec steps, as a shared spec (Code/tests/specs): None when a call isn't one."""
    replayed = [entry for entry in entries if _line(entry["tool"], entry["args"])]
    if not replayed or any(entry["tool"] != "spec_step" for entry in replayed):
        return None
    return {"name": name, "doc": doc, "tags": sorted(set(tags)), "steps": [entry["args"]["step"] for entry in replayed]}


def _spec_steps(tool, args):
    """A recorded call as shared-spec steps, None when it has none (the fake has no walking, talking or menus)."""
    match tool:
        case "spec_step":
            return [args["step"]]
        case "stage":
            spec = args["spec"]
            if spec.get("players") or spec.get("objects"):
                return None
            steps = [{"do": "stage_npc", "ref": npc["ref"], "as": npc["name"]} if "ref" in npc else
                     {"do": "spawn_npc", "base": npc["base"], "as": npc["name"]} for npc in spec.get("npcs", [])]
            steps += [{"do": "add_items", "game": item.get("who", "host"), "ref": "0x14", "item": f"0x{item['item']}"
                       if isinstance(item["item"], str) else hex(item["item"]), "count": item.get("count", 1)} for item in spec.get("items", [])]
            return steps
        case "expect" if args.get("check") == "agree":
            return [{"expect": "actors agree"}]
    return None


def spec_draft(entries, name, doc):
    """As much of a recording as the shared specs can say, and a line for each call they can't: step 2 of the bug-fix
    flow starts from this; a flow on the fake is written by hand from what is left."""
    steps, leftovers = [], []
    for entry in entries:
        if not (entry["ok"] or _is_expect(entry)) or not (line := _line(entry["tool"], _replayed(entry))):
            continue
        if (mapped := _spec_steps(entry["tool"], entry["args"])) is None:
            leftovers.append(f"no spec step: {line}")
        else:
            steps += mapped
    return {"name": name, "doc": doc, "steps": steps}, leftovers


class PressTiming:
    """Frames and ms from a real key press to the player's own motion: our games through the animation trace, the
    vanilla stand-in through its player_trace (red flag 3's measurement, kept as a verb)."""

    def press_to_motion(ctx, instance, key="w", presses=5, hold=0.5, vanilla=False):
        env, window = ctx.game_window(instance)
        pressed_at = []
        def press_all(between):
            for _ in range(presses):
                between()
                subprocess.run(["xdotool", "windowfocus", window], env=env, check=True, stderr=subprocess.DEVNULL)
                subprocess.run(["xdotool", "keydown", key], env=env, check=True, stderr=subprocess.DEVNULL)
                pressed_at.append(time.time())
                time.sleep(hold)
                subprocess.run(["xdotool", "keyup", key], env=env, check=True, stderr=subprocess.DEVNULL)
        if vanilla:
            game = ctx.game(instance)
            switch, trace = game.control / "player_trace.on", game.control / "player_trace.txt"
            trace.unlink(missing_ok=True)
            switch.write_text("")
            time.sleep(0.6)  # the stand-in checks the switch every 250 ms
            press_all(lambda: time.sleep(1.0))
            time.sleep(1.0)
            switch.unlink(missing_ok=True)
            points = [(int(us) / 1e6, (float(x), float(y))) for us, x, y, _, _ in
                      (line.split() for line in trace.read_text().splitlines())]
        else:
            from ..objects import AnimTrace
            with AnimTrace(ctx, f"press-{instance or 'host'}", {instance: {0x14: "me"}}, video=False) as recorded:
                press_all(lambda: (f := ctx.game_frame(instance)) and ctx.wait_for(
                    "standing still", lambda: ctx.game_frame(instance) > f + 50, 5))
                frame = ctx.game_frame(instance)
                ctx.wait_for("a last second", lambda: ctx.game_frame(instance) > frame + 60, 5)
            points = [(e["time"], tuple(e["pos"][:2])) for e in recorded.entries if e["kind"] == "pos" and e["who"] == "me"]
        rows = []
        for pressed in pressed_at:
            before = [p for p in points if p[0] <= pressed]
            after = [p for p in points if p[0] > pressed]
            if not before or not after:
                continue
            moved = next((i for i, p in enumerate(after) if math.dist(p[1], before[-1][1]) > 0.5), None)
            rows.append({"frames": None if moved is None else moved + 1,
                         "ms": None if moved is None else round((after[moved][0] - pressed) * 1000)})
        return rows


class RefSteps:
    """How references move frame by frame, on our games or the vanilla stand-in: walking or stepped by position
    writes (owners-move-without-walking's vanilla control)."""

    def ref_steps(ctx, refs, seconds=12.0, instance="", vanilla=False, arrive=None):
        """How each reference in `refs` (form IDs) moved over the next `seconds`, frame by frame, on that game: the
        distance, the frames it moved in, and its jumps (over 10 units in one frame, under 200: no teleport). A walk
        moves a little every frame; an actor stepped by the virtual path follower jumps every few hundred ms.
        vanilla=True reads the vanilla stand-in's ref_trace (vanilla_start), else the animation trace. With `arrive`
        (a cell) the trace starts first, then the party (or the stand-in's player) goes there by coc."""
        def go():
            if arrive and vanilla:
                ctx.console_on(instance, f"coc {arrive}")
            elif arrive:
                ctx.both_go(arrive)
        if vanilla:
            game = ctx.game(instance)
            switch, trace = game.control / "ref_trace.on", game.control / "ref_trace.txt"
            trace.unlink(missing_ok=True)
            def rows():
                lines = trace.read_text().splitlines() if trace.exists() else []
                return [line.split() for line in lines[:-1]]  # the last line may be half written
            try:
                switch.write_text(" ".join(f"{ref:X}" for ref in refs))
                go()
                ctx.wait_for("[vanilla] the stand-in traces the references", lambda: rows(), 60)
                first = int(rows()[0][0])
                ctx.wait_for(f"[vanilla] {seconds}s traced", lambda: int(rows()[-1][0]) - first >= seconds * 1e6, seconds + 10)
            finally:
                switch.unlink(missing_ok=True)
            points = collections.defaultdict(list)
            for us, ref, x, y, _ in rows():
                if int(us) - first <= seconds * 1e6:
                    points[int(ref, 16)].append((float(x), float(y)))
        else:
            from ..objects import AnimTrace
            with AnimTrace(ctx, f"ref-steps-{instance or 'host'}", {instance: {ref: f"{ref:X}" for ref in refs}},
                           video=False) as recorded:
                go()
                started = time.monotonic()
                ctx.wait_for(f"{seconds}s traced", lambda: time.monotonic() - started >= seconds, seconds + 5)
            points = collections.defaultdict(list)
            for e in recorded.entries:
                if e["kind"] == "pos" and e.get("game", instance) == instance:
                    points[int(e["who"], 16)].append(tuple(e["pos"][:2]))
        result = {}
        for ref, places in points.items():
            steps = [math.dist(a, b) for a, b in zip(places, places[1:])]
            steps = [s for s in steps if s < 200.0]
            result[f"{ref:08X}"] = {"travelled": round(sum(steps)), "frames": len(steps),
                                    "moving_frames": sum(s > 0.05 for s in steps),
                                    "jumps": sum(10.0 < s for s in steps),
                                    "biggest": round(max(steps, default=0.0), 1)}
        return result
