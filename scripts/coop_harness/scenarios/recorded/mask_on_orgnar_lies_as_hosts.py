"""The host's game sets a mask on Orgnar's back as he leans over his counter and wipes it: his animated arm nudges it for half a minute before it settles on the bar. The guest's mask must move as the host's does (no frame's jump over 2 units, no more turn-backs than the host's and 3, no further travelled than twice the host's and 10 units). The playtest of 2026-11-23: the guest's snapped about once a second while he moved.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-162259-mask-on-moving-npc.jsonl, 5 calls (scripts/check_scenarios.py)
NAME = 'mask_on_orgnar_lies_as_hosts'
TAGS = ['objects', 'own-checks', 'physics', 'scene']
DOC = "The host's game sets a mask on Orgnar's back as he leans over his counter and wipes it: his animated arm nudges it for half a minute before it settles on the bar. The guest's mask must move as the host's does (no frame's jump over 2 units, no more turn-backs than the host's and 3, no further travelled than twice the host's and 10 units). The playtest of 2026-11-23: the guest's snapped about once a second while he moved."


def run(ctx):
    # hand-written: the export left out the failing verb (lies_on_npc) and the scene; the probes after it are dropped
    try:
        ctx.scene(at='coc RiverwoodSleepingGiantInn', pin=False)
        ctx.find('Orgnar', 'orgnar')
        ctx.lies_on_npc(npc='orgnar', ref='601A5', seconds=30, above=150, ahead=30)
    finally:
        ctx.unstage()
