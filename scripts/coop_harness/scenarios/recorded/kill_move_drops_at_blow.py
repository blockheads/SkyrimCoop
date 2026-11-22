"""A guest's kill move on a host bandit: the victim drops at its clip's blow on both games (no pose hold first), nobody is left in the pair and the attacker walks on.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261122-143201-kill-move-drops-at-blow.jsonl, 2 calls (scripts/check_scenarios.py)
# hand-written: rest=False added, the lying bodies are kill-moves-by-weapon's check (bugs.md)
NAME = 'kill-move-drops-at-blow'
TAGS = ['copies', 'kill-move']
DOC = "A guest's kill move on a host bandit: the victim drops at its clip's blow on both games (no pose hold first), nobody is left in the pair and the attacker walks on."


def run(ctx):
    try:
        ctx.kill_move_on(attacker='client', base='0003DE53', weapon=77495, strict=True, rest=False)
    finally:
        ctx.unstage()
