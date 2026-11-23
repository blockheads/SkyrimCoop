"""An NPC holding a torch in each hand on the host (2026-11-22 playtest: Winterhold guards) holds one in each hand on the guest's copy too. An equip holds a torch left only (17619); a forced torch worn right is held right only by a 3D load's worn init (24736), so the copy's repair holds it there the same way (Actor::HoldInHand). docs/re/systems/hand-slots.md.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-002529-guard-torch-each-hand.jsonl, 43 calls (scripts/check_scenarios.py); trimmed to the Faralda
# steps (the guard's bow took its hands back), the hands' values added to the expects
NAME = 'npc-torch-each-hand'
TAGS = ['hands', 'inventory', 'scene']
DOC = "An NPC holding a torch in each hand on the host (2026-11-22 playtest: Winterhold guards) holds one in each hand on the guest's copy too. An equip holds a torch left only (17619); a forced torch worn right is held right only by a 3D load's worn init (24736), so the copy's repair holds it there the same way (Actor::HoldInHand). docs/re/systems/hand-slots.md."


def run(ctx):
    try:
        ctx.scene(save=None, at='coc WinterholdExterior01', pin=True)
        faralda = ctx.resolve('faralda', 'host')
        ctx.freeze(instance='', actor='faralda', frozen=True)
        ctx.console_on('', '1C1A5.additem 1d4ec 2')
        ctx.console_on('', '1C1A5.equipitem 1d4ec')
        # 38001: an equipped torch is marked worn left (its slot is LeftHand, 17619) and held left.
        ctx.expect('faralda', 'actor', within=5.0, game='host', field='worn_left', value=[0x1D4EC], seconds=5.0)
        # The state the guards were in: a forced torch in each hand, as their 3D load holds them (24736).
        ctx.player_command('', {'cmd': 'hold_in_hand', 'actor': faralda, 'item': 0x1D4EC, 'hand': 1})
        # Measured: the item change reruns the worn init, holding only the forced torch (right); the plain one comes off.
        ctx.console_on('', '1C1A5.additem f 1')
        ctx.expect('faralda', 'actor', within=5.0, field='state.hands', seconds=10.0)
        ctx.player_command('', {'cmd': 'hold_in_hand', 'actor': faralda, 'item': 0x1D4EC, 'hand': 0})
        # An item change declares the owner's items; measured, the host's engine reruns its worn init then (37525 calls 24725).
        ctx.console_on('', '1C1A5.additem f 1')
        ctx.expect('faralda', 'actor', within=5.0, game='host', field='state.hands', value=[0x1D4EC, 0x1D4EC], seconds=10.0)
        ctx.expect('faralda', 'actor', within=5.0, field='state.hands', seconds=10.0)
    finally:
        ctx.unstage()
