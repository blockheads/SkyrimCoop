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

