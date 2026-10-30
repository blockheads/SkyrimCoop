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
    ACTIVATE_KEY,
    ALCHEMY_LAB,
    CRAFT_KEY,
    FORM_DELETED,
    FORM_DISABLED,
    HEIRLOOM,
    INGREDIENT_NAMES,
    IRON_SWORD,
    ITEM_LISTS,
    LISTS_SHOWN,
    TAKE_ALL_KEY,
)


class InventoryActions:
    def actor_state(ctx, instance, form_id):
        _game = ctx.game(instance)
        return ((_game.read_status() or {}).get("world") or {}).get(str(form_id)) or {}


    def menu_var(ctx, instance, menu, path):
        _game = ctx.game(instance)
        return (_game.rpc({"cmd": "menu_var", "menu": menu, "path": path}) or {}).get("value")


    def item_differences(ctx, a, b, a_name, b_name):
        """Where two {item: count} differ, as 'item: 2 (host) vs 0 (client)'."""
        return "; ".join(f"{item:X}: {a.get(item, 0)} ({a_name}) vs {b.get(item, 0)} ({b_name})"
                         for item in sorted(set(a) | set(b)) if a.get(item, 0) != b.get(item, 0))


    def take_all(ctx, instance, chest):
        """The player opens the chest (activates it) and takes everything (R), which closes it."""
        who = f"[{instance or 'host'}]"
        ctx.place_before(instance, chest, 100)
        ctx.activate_on(instance, chest)
        ctx.wait_for(f"{who} the chest's menu shows its lists",
                 lambda: ctx.menu_var(instance, "ContainerMenu", ITEM_LISTS + ".currentState") in LISTS_SHOWN, 5)
        ctx.tap_key(instance, TAKE_ALL_KEY)
        ctx.wait_for(f"{who} the menu closed", lambda: ctx.takes_input(instance), 5)


    def item_stacks(ctx, instance, holder=0x14):
        """A loose item's own count and extra data, or each stack with extra data a container or actor holds."""
        return ctx.player_command(instance, {"cmd": "item_stacks", "object": holder})["stacks"]


    def is_heirloom(ctx, stack):
        """The stack is HEIRLOOM's sword, every piece of its extra data kept."""
        return (stack["item"] == IRON_SWORD and stack["name"] == HEIRLOOM["name"] and stack["enchantment"] == HEIRLOOM["enchantment"]
                and abs(stack["health"] - HEIRLOOM["health"]) < 0.01 and abs(stack["charge"] - HEIRLOOM["charge"]) < 1
                and stack["enchant_charge"] == HEIRLOOM["enchant_charge"])


    def heirlooms(ctx, instance, holder=0x14):
        return sum(stack["count"] for stack in ctx.item_stacks(instance, holder) if ctx.is_heirloom(stack))


    def loose_swords(ctx, instance):
        """The iron swords lying in that game's loaded cells that it made at run time (0xFF ids), by id; a deleted or
        disabled one (picked up) is not lying there."""
        return {ref: fields for ref, fields in ctx.world_state(instance).items()
                if fields["base"] == IRON_SWORD and ref >> 24 == 0xFF and not fields["flags"] & (FORM_DELETED | FORM_DISABLED)}


    def describe_stacks(ctx, stacks):
        return "; ".join(f"{s['count']}x {s['item']:X} health {s['health']:.2f} ench {s['enchantment']:X} ({s['enchant_charge']}, "
                         f"charge {s['charge']:.0f}) name '{s['name']}'" for s in stacks) or "none"


    def new_loose_sword(ctx, instance, before):
        """The one iron sword lying in that game that was not there before, else None (two fail: a duplicate)."""
        made = set(ctx.loose_swords(instance)) - before
        if len(made) > 1:
            raise ctx.failure(f"[{instance or 'host'}] {len(made)} new iron swords lie in its world, not one: "
                              + ", ".join(f"{ref:X}" for ref in sorted(made)))
        return next(iter(made), None)


    def put_back_heirlooms(ctx, swords_before, held, heirlooms_held):
        """Clean-up: the players carry what they did before (the heirlooms removed as their own stacks), and no sword made
        here is left lying."""
        for name in ("", "client"):
            if (extra := ctx.heirlooms(name) - heirlooms_held[name]) > 0:
                ctx.player_command(name, {"cmd": "store", "item": IRON_SWORD, "count": extra, "container": 0})
            if (extra := (ctx.item_count(name, 0x14, IRON_SWORD) or 0) - held[name]) > 0:
                ctx.console_on(name, f"player.removeitem {IRON_SWORD:X} {extra}")
            for ref in set(ctx.loose_swords(name)) - swords_before[name]:
                ctx.console_on(name, f"{ref:X}.disable")
                ctx.console_on(name, f"{ref:X}.markfordelete")


    def heirloom_start(ctx):
        """What a dropped-items scenario puts back: the loose swords lying, the swords and heirlooms each player carries."""
        return ({name: set(ctx.loose_swords(name)) for name in ("", "client")},
                {name: ctx.item_count(name, 0x14, IRON_SWORD) or 0 for name in ("", "client")},
                {name: ctx.heirlooms(name) for name in ("", "client")})


    def recipe_of(ctx, instance, form):
        """A player-made potion or poison's recipe id in that game (the same in every game), else None."""
        _game = ctx.game(instance)
        reply = _game.rpc({"cmd": "recipe", "form": form}) or {}
        return reply.get("id") if reply.get("ok") else None


    def brewed(ctx, instance, holder=0x14):
        """The player-made potions and poisons the holder has in that game, by recipe: {recipe id: count}."""
        counts = collections.Counter()
        for item, count in ctx.container_items(instance, holder).items():
            if item >> 24 == 0xFF and (recipe := ctx.recipe_of(instance, item)) is not None:
                counts[recipe] += count
        return counts


    def form_of_recipe(ctx, instance, recipe, holder=0x14):
        """That game's own form for the recipe among what the holder has, else None."""
        return next((item for item in ctx.container_items(instance, holder) if item >> 24 == 0xFF and ctx.recipe_of(instance, item) == recipe), None)


    def loose_brewed(ctx, instance, recipe):
        """The loose player-made potion of that recipe lying in that game's loaded cells, else None."""
        for ref, fields in ctx.world_state(instance).items():
            if fields["base"] >> 24 == 0xFF and not fields["flags"] & (FORM_DELETED | FORM_DISABLED) and ctx.recipe_of(instance, fields["base"]) == recipe:
                return ref
        return None


    def brew(ctx, instance, ingredients):
        """That game's player brews at QASmoke's alchemy lab as a player does: E on each ingredient in the lab's list, R
        creates. The made potion's form id."""
        tag = "[host]" if instance == "" else "[guest]"
        for item in ingredients:
            ctx.console_on(instance, f"player.additem {item:08X} 1")
        before = ctx.brewed(instance)
        ctx.open_crafting_station(instance, ALCHEMY_LAB, "alchemy lab")
        try:
            for item in ingredients:
                name = INGREDIENT_NAMES[item]
                names = [str(ctx.crafting_entries(instance, f"entryList.{i}.text") or "")
                         for i in range(int(ctx.crafting_entries(instance, "entryList.length") or 0))]
                target = next((i for i, text in enumerate(names) if text.startswith(name)), None)
                if target is None:
                    raise ctx.failure(f"{tag} {name} is not among the ingredients: {', '.join(names)}")
                ctx.select_crafting_entry(instance, target)
                ctx.tap_key(instance, ACTIVATE_KEY)
            ctx.wait_for(f"{tag} the chosen ingredients make something (Create)", lambda: ctx.menu_var(instance, "Crafting Menu", "_root.Menu._bCanCraft"), 5)
            ctx.tap_key(instance, CRAFT_KEY)
            made = ctx.wait_for(f"{tag} its player carries the brewed potion",
                            lambda: next((recipe for recipe, count in ctx.brewed(instance).items() if count > before[recipe]), None), 10)
            return ctx.form_of_recipe(instance, made)
        finally:
            ctx.leave_crafting_station(instance, "alchemy lab")


    def guest_locks_taken(ctx, since):
        """The guests' lock proposals the host's game took since that trail number (its log trail)."""
        lines, _ = ctx.log_trail("", since)
        return [line["text"] for line in lines if line["text"].startswith("HostEngine: took a guest's lock")]


    def form_save(ctx, instance, form, **fields):
        """The form's save data in that game (FormState::Save), as the harness's form_save gives it."""
        return ctx.player_command(instance, {"cmd": "form_save", "form": form, **fields})

