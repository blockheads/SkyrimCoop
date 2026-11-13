"""The guest's player swings a sword at a ladle while the host's frames move it: on the guest the played ladle keeps its mass (nudged onto the host's frames, physics-sync.md "One rule"), so the swing knocks it, the host's ladle is knocked alike, and both come to lie at one place. Before, a played body was keyframed, massless, and the swing moved nothing in either game (docs/bugs.md, "A guest's swing at a moving object").

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261113-143100-guest-swing-moving-object.jsonl, 4 calls (scripts/check_scenarios.py)
NAME = 'guest-swing-moving-object'
TAGS = ['combat', 'objects', 'physics']
DOC = 'The guest\'s player swings a sword at a ladle while the host\'s frames move it: on the guest the played ladle keeps its mass (nudged onto the host\'s frames, physics-sync.md "One rule"), so the swing knocks it, the host\'s ladle is knocked alike, and both come to lie at one place. Before, a played body was keyframed, massless, and the swing moved nothing in either game (docs/bugs.md, "A guest\'s swing at a moving object").'


def run(ctx):
    try:
        ctx.stage({'objects': [{'name': 'ladle', 'base': '000319E5'}], 'items': [{'who': 'guest', 'item': '00012EB7', 'equip': True}]})
        ctx.stage({'players': {'host': {'near': 'ladle', 'gap': 300}, 'guest': {'near': 'ladle', 'gap': 70}}})
        ctx.swing_at_moving(name='ladle', game='guest')
    finally:
        ctx.unstage()
