"""Actions for the co-op harness."""
import argparse
import collections
import contextlib
import math
import os
import re
import socket
import subprocess
import time
import uuid
from ..common import (
    COMPANIONS_HOUSEKEEPING,
    STORM_CALL_BOLT,
    WEREWOLF_BEAST_RACE,
)


class MagicActions:
    def script_var(ctx, instance, form, script, name, value=None):
        """A script variable or auto property on that form (harness script_var), set first when a value is given."""
        request = {"cmd": "script_var", "object": form, "script": script, "var": name}
        if value is not None:
            request["value"] = value
        response = ctx.player_command(instance, request)
        return response["value"]


    def copy_race_on_host(ctx):
        """The race the host shows for the client's character (its copy is respawned on a change of form)."""
        _game = ctx.game('')
        copy = next(iter(ctx.other_players(_game=_game)), None)
        return ctx.race_of("", copy) if copy else None


    def prepare_beast_form(ctx, instance):
        # These QA characters receive the power directly, bypassing the Companions' original-race setup.
        race = ctx.race_of(instance, 0x14)
        if race == WEREWOLF_BEAST_RACE:
            raise ctx.failure(f"[{instance or 'host'}] Beast Form fixture is already a werewolf")
        ctx.player_command(instance, {"cmd": "script_var", "object": COMPANIONS_HOUSEKEEPING,
                                  "script": "CompanionsHousekeepingScript", "var": "PlayerOriginalRace", "form": race})
        ctx.script_var(instance, COMPANIONS_HOUSEKEEPING, "CompanionsHousekeepingScript", "PlayerIsWerewolfVirgin", 0)


    def storm_call(ctx, instance="", bolts=3):
        """That game's player shouts three-word Storm Call outdoors: magicProjectileStormScript strikes from an activator
        in the sky (Spell.RemoteCast, docs/re/systems/projectiles.md), on the caster's game for its own effect and on the
        host's for the NPCs it hit. Fails unless each game shows every bolt the other's engine launched (Diagnostics
        counters `projectile E4CB5`, its engine's, against `projectile E4CB5 relaunched` on the other)."""
        words, shout, spell = (0x6029A, 0x6029B, 0x6029C), 0x7097D, 0x1860D
        own, copied = f"projectile {STORM_CALL_BOLT:X}", f"projectile {STORM_CALL_BOLT:X} relaunched"
        for word in words:
            ctx.console_on(instance, f"player.teachword {word:X}")
            ctx.console_on(instance, f"player.unlockword {word:X}")
        ctx.console_on(instance, f"player.addshout {shout:X}")
        ctx.console_on(instance, f"player.equipshout {shout:X}")
        ctx.console_on(instance, "player.setav shoutrecoverymult 0")
        ctx.wait_for("Storm Call is equipped", lambda: ctx.local_player(instance).get("selected_power") == shout, 5)
        def read():
            return {(game, name): ctx.counter(game, name) for game in ("", "client") for name in (own, copied)}
        before = read()
        def since():
            now = read()
            return {key: now[key] - before[key] for key in now}
        _, events_since = ctx.engine_events(instance)
        frame = ctx.game_frame(instance)
        ctx.press(instance, "shout")
        try:
            ctx.wait_for("the shout button is held through a full three-word charge",
                         lambda: ctx.game_frame(instance) >= frame + 120, 5)
        finally:
            ctx.release(instance, "shout")
        ctx.wait_for("the three-word Storm Call is cast",
                     lambda: any(e["kind"] == "spell_cast" and e.get("caster") == 0x14 and e.get("spell") == spell
                                 for e in ctx.engine_events(instance, events_since)[0]), 5)
        ctx.wait_for(f"[{instance or 'host'}] the storm strikes {bolts} times", lambda: since()[(instance, own)] >= bolts, 30)
        def summary(counts):
            return {f"{game or 'host'} {'relaunched' if name == copied else 'own'}": n for (game, name), n in counts.items()}
        struck = since()  # each game's copies of these arrive after them
        def each_shows_the_others():
            now = since()
            return now[("", copied)] >= struck[("client", own)] and now[("client", copied)] >= struck[("", own)]
        ctx.wait_for(f"each game shows the bolts the other's engine struck ({summary(struck)})", each_shows_the_others, 5)
        return summary(since())
