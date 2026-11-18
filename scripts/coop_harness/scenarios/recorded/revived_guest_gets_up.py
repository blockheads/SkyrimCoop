"""The guest is knocked down, dies (a plain death, then a kill move), is revived each time (talking to an NPC after the first), and is knocked down again: its own body stands in the conversation, and it and the host's copy lie together and get up (knock state back to 0) after every revive.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261118-120016-revived-guest-gets-up.jsonl, 26 calls (scripts/check_scenarios.py)
# hand-written: the recording's death_state polls became waits, its copy id the live one.
from ...common import KILL_MOVE_RANDOM, LYDIA
from ...comparisons import body_problems, body_report

NAME = 'revived-guest-gets-up'
TAGS = ['death', 'physics']
DOC = "The guest is knocked down, dies (a plain death, then a kill move), is revived each time (talking to an NPC after the first), and is knocked down again: its own body stands in the conversation, and it and the host's copy lie together and get up (knock state back to 0) after every revive."


def down_and_revived(ctx, kill):
    generation = ctx.death_state(instance='client').get('generation', 0)
    old_copy = ctx.player_copy(instance='')
    kill()
    ctx.wait_for("[client] the guest is down", lambda: ctx.death_state(instance='client').get('down'), 10)
    ctx.wait_for("[client] the guest is revived beside the host", lambda: (state := ctx.death_state(instance='client'))
                 .get('generation', 0) > generation and not state.get('down'), 40)
    # A revive makes the guest's copy again (CharacterService::OnPlayerRevived).
    ctx.wait_for("[host] the guest's copy is made again", lambda: ctx.player_copy(instance='') not in (None, old_copy), 10)


def stands_in_conversation(ctx):
    # The first conversation after a death once knelt the guest's own body (docs/bugs.md, 2026-10-30).
    npc, _, copy = ctx.spawn_host_npc(LYDIA)
    try:
        ctx.talk_to('client', copy)
        body = ctx.actor_snapshot('client', 0x14)
        if body_problems(body):
            raise ctx.failure(f"[client] the revived guest's own body in its conversation: {body_report(body)}")
    finally:
        ctx.close_dialogues()
        ctx.remove_host_npc(npc)


def kill_move(ctx):
    ctx.console_on('client', 'player.restoreav health 10000')
    ctx.console_on('client', 'player.damageav health 97')
    ctx.place_before(instance='', target=ctx.player_copy(instance=''), gap=70)
    ctx.draw(instance='')
    ctx.attack(instance='')


def run(ctx):
    try:
        ctx.player_knocked_down_together(instance='client')
        down_and_revived(ctx, lambda: ctx.console_on('client', 'player.damageav health 10000'))
        stands_in_conversation(ctx)
        ctx.player_knocked_down_together(instance='client')
        ctx.console_on('', 'set KillMoveRandom to 100')
        ctx.give_sword(instance='')
        down_and_revived(ctx, lambda: kill_move(ctx))
        ctx.sheathe(instance='')
        ctx.player_knocked_down_together(instance='client')
        if awake := [body for body in ctx.ragdoll_of(instance='client', actor=0x14) if body['in_world']]:
            raise ctx.failure(f"[client] the revived guest's ragdoll is still in the world: {len(awake)} bodies")
    finally:
        ctx.console_on('', f'set KillMoveRandom to {KILL_MOVE_RANDOM}')
        ctx.console_on('client', 'player.restoreav health 10000')
        ctx.unstage()
