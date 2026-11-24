"""Traps (docs/re/systems/traps.md): who a trap touched and hurt, in each game."""
import time


class TrapActions:
    def floor_under(ctx, instance, at, above=120.0, below=300.0):
        """The floor under `at` [x, y, z] in that game, as the character controller meets it (a ray down, layer 30):
        its point, or a failure naming what the ray found (nothing, a wall, a ceiling)."""
        x, y, z = at
        start = z + above
        for _ in range(6):  # a surface well above `at` (a beam's top) is passed under: the floor nearest `at`
            for _ in range(10):  # a frame's picks can be spent: the next frame's
                reply = ctx.game(instance).rpc({"cmd": "ray", "from": [x, y, start], "to": [x, y, z - below], "layer": 30}) or {}
                if reply.get("ok"):
                    break
                time.sleep(0.05)
            ray = reply.get("ray") or {}
            # A trigger (a plate's volume: characters pass it) or what no one stands on (a tripwire's rope): looked past.
            if ray.get("hit") and (ray.get("layer") == 12 or ray["normal"][2] < 0.7):
                start = ray["point"][2] - 0.5
                continue
            if not ray.get("hit") or ray["point"][2] < z + 60:
                break
            start = ray["point"][2] - 8
        if not ray.get("hit"):
            raise ctx.failure(f"[{instance or 'host'}] no floor within {below:.0f} below {at}: {reply}")
        if ray["normal"][2] < 0.7:
            raise ctx.failure(f"[{instance or 'host'}] what is under {at} is no floor (normal {ray['normal']}, ref {ray.get('ref', 0):X})")
        return ray["point"]

    def put_player(ctx, instance, at, gap=0.0):
        """That game's player at `at`: [x, y, z] on the floor a ray finds under it (fails if none, never a blind setpos),
        or a reference (hex string) it then stands `gap` units from on a floor the plugin checked with rays, facing it.
        Returns once it stands there, not falling."""
        if isinstance(at, str):
            ctx.player_command(instance, {"cmd": "place", "target": int(at, 16), "distance": gap or 1.0})
            return
        floor = ctx.floor_under(instance, at)
        ctx.player_command(instance, {"cmd": "place", "position": [floor[0], floor[1], floor[2] + 2.0]})
        time.sleep(0.5)
        where = (ctx.actor_place(instance, 0x14) or {}).get("position") or [0, 0, -1e9]
        if abs(where[2] - floor[2]) > 60:
            raise ctx.failure(f"[{instance or 'host'}] put on the floor at {floor}, it is at {where}: it fell")

    def trap_watch(ctx, host_at=None, guest_at=None, seconds=6.0, trap=None, full=True):
        """Both players placed (a place left out: where it is), then `seconds` watched: each game's trap contacts
        (the engine's TESTrapHitEvent, by target) and each player's health before, lowest and after.
        `trap` (hex): only its contacts."""
        games = {"host": "", "guest": "client"}
        if full:
            for instance in games.values():
                ctx.console_on(instance, "player.restoreav health 1000")
            time.sleep(0.5)
        since = {g: ctx.engine_events(i)[1] for g, i in games.items()}
        health = {g: [ctx.player_health(i)] for g, i in games.items()}
        for game, at in (("host", host_at), ("guest", guest_at)):
            if at:
                ctx.put_player(games[game], at)
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            for g, i in games.items():
                health[g].append(ctx.player_health(i))
            time.sleep(0.1)
        seen = {}
        for g, i in games.items():
            seen[g] = {}
            for e in ctx.engine_events(i, since[g])[0]:
                if e["kind"] == "trap_hit" and (trap is None or e["trap"] == int(trap, 16)):
                    key = f"{e['trap']:X} on {e['target']:X}"
                    row = seen[g].setdefault(key, {"start": 0, "stop": 0, "hit": 0, "velocity": e["velocity"], "motion": e["motion"]})
                    row[("start", "stop", "hit")[e["type"]]] += 1
        return {"contacts": seen, "health": {g: [round(v[0], 1), round(min(v), 1), round(v[-1], 1)] for g, v in health.items()}}

    def trap_rounds(ctx, who, at, away, rounds=4, seconds=6.0, health=1000, holder_at=None, gap=0.0, use=None, pull=None, walk_on=None):
        """`walk_on` (a plate, hex): `who` walks onto it from `away` along the navmesh instead of being put on `at`. `pull` (a lever, hex): the other player walks up and pulls it once `who` stands at `at`. `who` (host or guest) put on `at` (a trap's plate) and back to `away`, `rounds` times, with `health` points: the
        hits it took each time (each loss of health between two looks)."""
        instance = "" if who == "host" else "client"
        other = "client" if instance == "" else ""
        ctx.console_on(instance, f"player.setav health {health}")
        # No regeneration while watched: a small hit landing during a walk (a dart's 0.8) is still there to read.
        ctx.console_on(instance, "player.modav healrate -0.7")
        try:
            results = []
            for _ in range(rounds):
                ctx.put_player(instance, away)
                if holder_at:  # off the trigger: it steps on again each round
                    ctx.put_player(other, [away[0] + 80, away[1], away[2]])
                time.sleep(3.0)
                ctx.console_on(instance, f"player.restoreav health {health}")
                time.sleep(0.5)
                if walk_on:  # along the navmesh, as go_to: a line by input caught on the room (191-615 units short)
                    ctx.reach(instance, walk_on, 60.0, False)  # on the plate, not at its centre
                else:
                    ctx.put_player(instance, at, gap)
                if holder_at:  # the other player holds the trap's trigger
                    ctx.put_player(other, holder_at)
                since = {g: ctx.engine_events(g)[1] for g in ("", "client")}
                last, hits = ctx.player_health(instance), []
                samples = [last]  # health before, after the pull, and the lowest: a miss explains itself
                if use:  # a lever: `who` walks up and pulls it, by real input
                    ctx.name_handle("trap_lever", {"host": int(use, 16), "guest": int(use, 16)})
                    ctx.use(instance, "trap_lever")
                if pull:  # beside it first (a path from afar may not exist); its darts may land while the pull is confirmed
                    ctx.put_player(other, pull, 120.0)
                    ctx.name_handle("trap_lever", {"host": int(pull, 16), "guest": int(pull, 16)})
                    ctx.use(other, "trap_lever")
                end = time.monotonic() + seconds
                if use or pull:  # what landed meanwhile
                    now = ctx.player_health(instance)
                    samples.append(now)
                    if now < last - 0.5:
                        hits.append(round(last - now, 1))
                    last = now
                while time.monotonic() < end:  # a burn drains a little each frame: its loss adds up to a hit of 0.5 or more
                    now = ctx.player_health(instance)
                    if now < last - 0.5:
                        hits.append(round(last - now, 1))
                        last = now
                    elif now > last:
                        last = now
                    time.sleep(0.03)
                struck = lambda g: [f"{e['target']:X} by {e['cause']:X}" for e in ctx.engine_events(g, since[g])[0]
                                    if e["kind"] == "hit" and e["target"] in (0x14, ctx.other_player_form(g))][:8]
                kinds = lambda g: [f"{e['target']:X}:{('start', 'stop', 'hit')[e['type']]}" for e in ctx.engine_events(g, since[g])[0]
                                   if e["kind"] == "trap_hit"]
                place = (ctx.actor_place(instance, 0x14) or {}).get("position")
                samples.append(min(samples + [ctx.player_health(instance)]))
                results.append({"samples": samples, "hits": hits, "host": kinds("")[:12], "guest": kinds("client")[:12], "ended": place, "struck": {"host": struck(""), "guest": struck("client")}})
            ctx.put_player(instance, away)
        finally:
            ctx.console_on(instance, "player.modav healrate 0.7")
        ctx.console_on(instance, "player.setav health 100")
        return results

    def expect_trap_hurts(ctx, who, at, away, rounds=4, holder_at=None, gap=0.0, use=None, seconds=6.0, pull=None, health=1000, least=None, walk_on=None):
        """`who` stepping on a trap's plate `rounds` times is hurt every time (or in `least` of them: a trap firing at
        random), on its own game (trap_rounds); fails naming
        the rounds it was not, with each game's contacts then."""
        results = ctx.trap_rounds(who, at, away, rounds, seconds=seconds, health=health, holder_at=holder_at, gap=gap, use=use, pull=pull, walk_on=walk_on)
        missed = [i for i, r in enumerate(results) if not r["hits"]]
        if rounds - len(missed) < (rounds if least is None else least):
            raise ctx.failure(f"[{who}] the trap hurt it in {rounds - len(missed)} of {rounds} rounds; missed {missed}: "
                              f"{[results[i] for i in missed]}")
        return results

    def world_projectiles(ctx, instance, reset=False):
        """How many projectiles of the world's objects (a dart trap) that game sent (the host) or launched (a guest)."""
        counters = (ctx.player_command(instance, {"cmd": "counters", "reset": reset}) or {}).get("counters") or {}
        return {name: c["count"] for name, c in counters.items() if name.startswith("world projectiles")}

    def other_player_form(ctx, instance):
        """That game's copy of the other player (0: none)."""
        try:
            return ctx.other_player(ctx.actors_on(instance)) or 0
        except Exception:
            return 0

    def player_effects(ctx, instance):
        """That game's player's active effects: spell, effect, caster, magnitude, elapsed of duration."""
        return [{k: (f"{v:X}" if isinstance(v, int) and k in ("spell", "effect", "caster") else v) for k, v in e.items()}
                for e in ctx.player_snapshot(instance).get("effects") or []]
