"""A script on the host sends an animation event to the guest's character (Debug.SendAnimationEvent, a graph event on its argument, 55380): the guest's own player takes it (IsBleedingOut), not the host's, and the stop comes back the same way (docs/design/party-calls.md).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261107-183719-story-animation-event-reaches-guest.jsonl, 7 calls (scripts/check_scenarios.py)
NAME = 'story-animation-event-reaches-guest'
TAGS = ['cutscenes', 'scripts']
DOC = "A script on the host sends an animation event to the guest's character (Debug.SendAnimationEvent, a graph event on its argument, 55380): the guest's own player takes it (IsBleedingOut), not the host's, and the stop comes back the same way (docs/design/party-calls.md)."


def run(ctx):
    try:
        ctx.spec_step({'expect': 'graph bool', 'game': 'guest', 'name': 'IsBleedingOut', 'value': False, 'now': True})
        ctx.spec_step({'do': 'script', 'game': 'host', 'call': 'Debug.SendAnimationEvent', 'args': [{'form': 'guest-character'}, 'BleedOutStart']})
        ctx.spec_step({'expect': 'graph bool', 'game': 'guest', 'name': 'IsBleedingOut', 'value': True})
        ctx.spec_step({'expect': 'graph bool', 'game': 'host', 'name': 'IsBleedingOut', 'value': False, 'now': True})
        ctx.spec_step({'do': 'script', 'game': 'host', 'call': 'Debug.SendAnimationEvent', 'args': [{'form': 'guest-character'}, 'BleedOutStop']})
        ctx.spec_step({'expect': 'graph bool', 'game': 'guest', 'name': 'IsBleedingOut', 'value': False})
    finally:
        ctx.unstage()
