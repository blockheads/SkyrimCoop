"""/killmove 100: a guest's single swing at a healthy wolf (not a killing blow) is a kill move, and the wolf dies.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-162413-killmove-odds-every-swing.jsonl, 14 calls (scripts/check_scenarios.py)
# hand-written: the recording's detours dropped (a first kill, three probe swings), its form id resolved, wolves thawed,
# and each half traced (the attacker's kill move, as frozen-wolf-kill-move does)
from ...objects import AnimTrace

NAME = 'killmove-odds-every-swing'
TAGS = ['dev-commands', 'kill-move']
DOC = "/killmove 100: a guest's single swing at a healthy wolf (not a killing blow) is a kill move, and the wolf dies."


def one_swing_kill_move(ctx, game, wolf):
    """`game`'s player swings once at a 1000-health wolf: its idle tree picks a kill move, not the plain swing."""
    seen = ctx.resolve(wolf, game)
    # Staged frozen, then thawed: the host refuses a kill move on an NPC with no AI (HostGame::CanPair).
    ctx.freeze('', wolf, False)
    ctx.console_on('', f"{ctx.resolve(wolf, ''):X}.setav health 1000")
    # The attacker's game judges the swing (ShouldAttackKill 21744) on its own view of the wolf: no swing kills it.
    ctx.wait_for(f"[{game or 'host'}] {wolf} shows 1000 health",
                 lambda: ((ctx.actor_snapshot(game, seen).get("values") or {}).get("health") or 0) >= 1000, 5)
    with AnimTrace(ctx, f"killmove-odds-{wolf}", {game: {0x14: "player", seen: "wolf"}}, video=False) as trace:
        ctx.fight(game, wolf, 1, False)
        trace.wait_state(game, "player", "kill_move", True, 3)
    ctx.expect(wolf, 'actor', within=5.0, game='host', value=True, field='state.dead', seconds=8.0)


def run(ctx):
    try:
        ctx.name_players()
        ctx.stage({'players': {'guest': {'near': 'host_player', 'gap': 150}}, 'npcs': [{'name': 'wolf2', 'base': '23ABE'}], 'items': [{'who': 'guest', 'item': '139B9', 'count': 1, 'equip': True}]})
        ctx.say(instance='client', text='/killmove 100')
        ctx.say(instance='client', text='/god on')
        one_swing_kill_move(ctx, 'client', 'wolf2')
        # The guest's /killmove reached the host's game: its own player's swing is one too.
        ctx.unstage()
        ctx.stage({'npcs': [{'name': 'wolf3', 'base': '23ABE'}], 'items': [{'who': 'host', 'item': '139B9', 'count': 1, 'equip': True}]})
        ctx.say(instance='', text='/god on')
        one_swing_kill_move(ctx, '', 'wolf3')
    finally:
        ctx.say(instance='client', text='/reset')  # every game's odds back to the game's own
        ctx.unstage()
