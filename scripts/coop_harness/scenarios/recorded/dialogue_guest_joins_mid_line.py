"""The host picks Andurs's third answer; while he answers, the guest talks to him (joins). The guest's copy goes on saying the line it already says: its joining menu must not start the NPC's line again over it (it did: ReplayGreeting on every menu opening, and Actor::SpeakSound stops nothing, so the two overlapped).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261116-225051-dialogue-guest-joins-mid-line.jsonl, 16 calls (scripts/check_scenarios.py); edited: the session's earlier stages dropped
NAME = 'dialogue-guest-joins-mid-line'
TAGS = ['dialogue']
DOC = "The host picks Andurs's third answer; while he answers, the guest talks to him (joins). The guest's copy goes on saying the line it already says: its joining menu must not start the NPC's line again over it (it did: ReplayGreeting on every menu opening, and Actor::SpeakSound stops nothing, so the two overlapped)."


def run(ctx):
    try:
        ctx.stage({'npcs': [{'name': 'andurs', 'base': '00013BA8', 'frozen': False}], 'players': {'host': {'near': 'andurs', 'gap': 110, 'bearing': 90}, 'guest': {'near': 'andurs', 'gap': 110, 'bearing': 270}}})
        ctx.talk('', 'andurs', [])
        ctx.dialogue_highlight(instance='', index=2)
        ctx.wait_takes_pick(instance='')
        ctx.dialogue_press(instance='')
        ctx.guest_replays_mark()
        ctx.talk('client', 'andurs', [])
        ctx.expect_no_line_replayed(seconds=3)
    finally:
        ctx.unstage()
