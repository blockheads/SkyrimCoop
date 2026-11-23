"""Playtest 2026-11-22 item 12: a trap's Rattles (DoCombatSpellApply on the guest's copy, a world cast) reaches the guest with both of Survival Mode's alternate effects; its stamina regeneration must stay at the host player's 2.5/s, not 0.

Recorded with the repro MCP (docs/design/repro-mcp.md)."""
# hand-written: from the recording 20261123-101215-trap_disease_guest_stamina.jsonl, its probes (casts that never
# land a disease) dropped, the copy's form id read instead of this run's, the host's own player added as the reference
# (player.addspell: the engine's add check picks one effect) and both diseases removed again
NAME = 'trap_disease_guest_stamina'
TAGS = ['magic', 'stamina']
DOC = "Playtest 2026-11-22 item 12: a trap's Rattles (DoCombatSpellApply on the guest's copy, a world cast) reaches the guest with both of Survival Mode's alternate effects; its stamina regeneration must stay at the host player's 2.5/s, not 0."

RATTLES = 0x10A24E  # TrapDiseaseRattles: StaminaRate -2.5, Survival Mode's override adds a second -2.5 for survival on


def run(ctx):
    copy = ctx.client_on_host()
    try:
        ctx.name_players()
        ctx.name_handle('guest_player', {'host': copy, 'guest': 0x14})
        ctx.name_handle('host_player', {'host': 0x14, 'guest': ctx.other_player(ctx.actors_on('client'))})
        # The host's own player: the engine adds only the effect for Survival Mode off.
        ctx.console_on('', f'player.addspell {RATTLES:X}')
        ctx.expect('host_player', 'actor', game='host', value=2.5, field='values.stamina_regen.rate', seconds=3.0)
        # A trap's script gives its disease on the victim (Actor.DoCombatSpellApply): on the guest's copy, a world cast.
        ctx.script_call(instance='', klass='Actor', function='DoCombatSpellApply',
                        args=[{'form': RATTLES}, {'form': copy}], on=copy)
        ctx.expect('guest_player', 'actor', game='guest', value=2.5, field='values.stamina_regen.rate', seconds=3.0)
        # Both variants take 2.5: the guest holds the very effect the host's engine picked for its own player.
        picked = lambda game: sorted(e['effect'] for e in ctx.player_snapshot(game).get('effects') or []
                                     if e.get('spell') == RATTLES)
        if picked('client') != picked(''):
            raise ctx.failure(f"the guest's Rattles effects {[hex(e) for e in picked('client')]}, "
                              f"the host player's {[hex(e) for e in picked('')]}")
    finally:
        ctx.console_on('', f'player.removespell {RATTLES:X}')
        ctx.console_on('client', f'player.dispel {RATTLES:X}')  # given as a combat spell: active, not in its list
        ctx.unstage()
