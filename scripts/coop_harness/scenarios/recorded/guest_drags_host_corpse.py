"""A guest's player grabs a host wolf's corpse and drags it back: the corpse lies where the guest left it on both games (piece 10e: a dead body is the mover's until it rests). A hit on a corpse needs nothing: it is forwarded to the host and moves neither game's body.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261121-192314-guest-drags-host-corpse.jsonl, 19 calls (scripts/check_scenarios.py)
NAME = 'guest-drags-host-corpse'
TAGS = ['death', 'physics']
DOC = "A guest's player grabs a host wolf's corpse and drags it back: the corpse lies where the guest left it on both games (piece 10e: a dead body is the mover's until it rests). A hit on a corpse needs nothing: it is forwarded to the host and moves neither game's body."


def run(ctx):
    try:
        ctx.spawn_host_npc(base='00023ABE', restrained=True)
        ctx.find('Wolf', 'wolf')
        ctx.kill(target='wolf')
        ctx.ragdoll_lies_as_owner(owner='', actor='wolf', other='client', copy='wolf')
        ctx.go_to('client', 'wolf', 70.0, False, False)
        ctx.turn_to(instance='client', face='wolf')
        ctx.turn_to(instance='client', pitch=70)
        ctx.grab(instance='client', target='wolf')
        ctx.carry(instance='client', held='wolf', buttons=['back'], seconds=2)
        ctx.hold_key('client', 'e', 0.1)
        # Edited: the drag's rest, once the host's corpse lies still (an expect at the let go came too soon).
        ctx.ragdoll_lies_as_owner(owner='', actor='wolf', other='client', copy='wolf')
    finally:
        ctx.unstage()
