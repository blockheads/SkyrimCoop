"""A fight must respond to both players, regardless of who hosts or lands the first hit."""
import json

from ..common import ROOT, FIREBOLT, SPELL_GAP, WOLF
from ..objects import GodMode


def late_damage(ctx, first="", hide_host=False):
    second = "" if first else "client"
    records = []
    npc = None
    label = "host-first" if not first else "guest-first-hidden-host" if hide_host else "guest-first"
    try:
        with GodMode(ctx):
            if hide_host:
                ctx.console_on("", "player.setav invisibility 1")
            npc, _, copy = ctx.spawn_host_npc(WOLF, restrained=True)
            for command in ("setav health 5000", "setav healrate 0", "setav aggression 0", "setav confidence 4", "setav speedmult 30"):
                ctx.console_on("", f"{npc:X}.{command}")
            for game, target, bearing in (("", npc, 90), ("client", copy, 270)):
                ctx.give_spell(game, FIREBOLT)
                ctx.draw(game)
                ctx.place_before(game, target, SPELL_GAP, bearing=bearing)
            ctx.console_on("", f"{npc:X}.stopcombat")
            ctx.console_on("", f"{npc:X}.setrestrained 0")
            targets = {"": 0x14, "client": ctx.client_on_host()}

            def record(step):
                host = ctx.host_world(npc)
                shown = ctx.actor_state("client", copy)
                row = {"step": step, "health": ctx.host_npc_health(npc), "target": host.get("ai", {}).get("combat_target"),
                       "native_combat": ctx.player_command("", {"cmd": "combat_targets", "actor": npc}),
                       "guest_combat": ctx.player_command("client", {"cmd": "combat_targets", "actor": copy}),
                       "guest_target": shown.get("ai_state", {}).get("combat_target"), "host_npc": host,
                       "host_player": ctx.player_snapshot(""), "guest_on_host": ctx.actor_state("", targets["client"])}
                records.append(row)
                return row

            record("ready")
            before = ctx.host_npc_health(npc)
            ctx.cast_spell(first, npc if not first else copy, FIREBOLT)
            ctx.hit_lands(npc, before, f"{first or 'host'}'s first hit", 4)
            record("first hit")
            try:
                ctx.wait_for(f"the enemy initially targets the first attacker ({first or 'host'})",
                             lambda: ctx.host_world(npc).get("ai", {}).get("combat_target") == targets[first], 5)
                if hide_host:
                    ctx.console_on("", "player.setav invisibility 0")
                for hit in range(4):
                    before = ctx.host_npc_health(npc)
                    ctx.cast_spell(second, npc if not second else copy, FIREBOLT)
                    ctx.hit_lands(npc, before, f"{second or 'host'}'s later hit {hit + 1}", 4)
                    record(f"later hit {hit + 1}")
                ctx.wait_for(f"the enemy responds to {second or 'host'} dealing the greater damage",
                             lambda: ctx.host_world(npc).get("ai", {}).get("combat_target") == targets[second], 8)
                ctx.wait_for("the guest sees the enemy's new target too", lambda:
                             ctx.actor_state("client", copy).get("ai_state", {}).get("combat_target") ==
                             (0x14 if second else ctx.player_copy("client")), 5)
                start = ctx.game_frame("")
                def stable():
                    actual = ctx.host_world(npc).get("ai", {}).get("combat_target")
                    if actual != targets[second]:
                        raise ctx.failure(f"the enemy flipped away from the higher-damage player: {actual}")
                    return ctx.game_frame("") >= start + 300
                ctx.wait_for("the new target survives native reevaluation for 300 frames", stable, 15)
            finally:
                record("finished")
    finally:
        folder = ROOT / "build/aggro-evidence"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / f"{label}.json").write_text(json.dumps(records, indent=2))
        if npc is not None:
            ctx.remove_host_npc(npc)
        if hide_host:
            ctx.console_on("", "player.setav invisibility 0")
