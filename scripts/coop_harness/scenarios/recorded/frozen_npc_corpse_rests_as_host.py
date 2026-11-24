"""A host NPC staged frozen by the harness (freeze: kProcessMe off, restrained) is killed by the console on a hillside outside Helgen: its body must come to rest on the host and the guest's copy must lie as it, bone by bone. Before: the frozen NPC's death never ran past its first frame on the host (its ragdoll keyframed, drifting into the hill), so no rest reached the guest and the copy lay where its own physics left it (golden-claw-guest, arvels-journal-guest-loots).

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# recorded: 20261123-211025-frozen-npc-dies-as-host.jsonl, 11 calls (scripts/check_scenarios.py)
NAME = 'frozen-npc-corpse-rests-as-host'
TAGS = ['death', 'harness', 'late-save', 'physics', 'own-checks']  # outside Helgen: Ralof walks
DOC = "A host NPC staged frozen by the harness (freeze: kProcessMe off, restrained) is killed by the console on a hillside outside Helgen: its body must come to rest on the host and the guest's copy must lie as it, bone by bone. Before: the frozen NPC's death never ran past its first frame on the host (its ragdoll keyframed, drifting into the hill), so no rest reached the guest and the copy lay where its own physics left it (golden-claw-guest, arvels-journal-guest-loots)."


def run(ctx):
    # Edited after export: the failing check the recording dropped, the kill by handle, the exploratory calls cut.
    try:
        ctx.needs_save('LateOutsideHelgen')
        ctx.stage({'npcs': [{'name': 'bandit', 'base': '0001A765', 'frozen': True}]})
        ctx.console_on('', f"{ctx.resolve('bandit', ''):X}.kill")
        ctx.ragdoll_lies_as_owner('', 'bandit', 'client', 'bandit')
    finally:
        ctx.unstage()
