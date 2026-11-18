"""Solitude's execution: the guest steps onto the platform (SolitudeOpening stage 29 starts the platform guard's combat on Game.GetPlayer(), the guest's copy), then walks off; the host beside the guard is the nearest player and the party's fight is with every player, so the guard turns on the host (docs/re/systems/combat-targets.md, A fight with one player is the party's).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261117-072435-solitude-platform-guard.jsonl, 17 calls (scripts/check_scenarios.py)
NAME = 'guest-trips-platform-guard'
TAGS = ['combat', 'scene']
DOC = "Solitude's execution: the guest steps onto the platform (SolitudeOpening stage 29 starts the platform guard's combat on Game.GetPlayer(), the guest's copy), then walks off; the host beside the guard is the nearest player and the party's fight is with every player, so the guard turns on the host (docs/re/systems/combat-targets.md, A fight with one player is the party's)."


def run(ctx):
    try:
        ctx.name_players()
        ctx.scene(save=None, at='player.moveto 85373', pin=True)
        ctx.name_handle(name='platform_guard', ids={'host': 188881, 'guest': 188881})
        ctx.console_on('client', 'tgm')
        ctx.go_to('client', 'platform_guard', 120.0, False, False)  # the walk lets the lead-in reach stage 20
        ctx.go_to('', 'platform_guard', 150.0, False, False)  # the host beside the guard before the guest trips it
        # the platform's OpeningSceneInterruptTrigger (DAB6C): stage 29 starts the guard's combat on the guest's copy
        ctx.name_handle(name='platform_trigger', ids={'host': 895852, 'guest': 895852})
        ctx.go_to('client', 'platform_trigger', 60.0, False, False)
        ctx.go_to('client', 'beirand', 300.0, False, False)
        ctx.expect('platform_guard', 'actor', within=5.0, game='host', value='host_player', field='ai.combat_target', seconds=15.0)
    finally:
        ctx.unstage()
