# Game-test coverage destinations

The refactor keeps the gameplay coverage from `c755e622`. The [machine-readable ledger](../reports/2026-10-30-game-test-flow-coverage.json)
records 1,036 source flows, helpers, shared specs and trace objects, with setup/actions, original checks and destinations.
Its `native_flows` table maps every one of the 295 original Skyrim registrations to one of the 292 canonical flows.
Fake-engine and Skyrim checks remain separate entries.

## Consolidated lifecycles

| Previous checks | Destination | Preserved checkpoints |
|---|---|---|
| `equip-host-player`, `draw-host-player` | `player-equipment-host` | Equip, owner/copy equipment, input draw, copy drawn, input sheathe, copy sheathed, remove |
| `equip-client-player`, `draw-client-player` | `player-equipment-guest` | Same lifecycle in the opposite ownership direction |
| `equip-host-npc`, `repair-equipment-drift` | `npc-equipment-lifecycle` | Equip/remove a sword, strip the same NPC's copy, observe drift, wait for snapshot repair |
| C++ player equipment, personal items, drawn weapon cases | `Players carry, equip, swap, draw and remove their own items` | Personal lockpicks, consumption, sword/drop, right/left hands, sword swap, draw/sheathe fallback, armor/helmet changes, personal apple |
| C++ NPC item/equipment cases | `A host NPC carries gold and equips its sword` | Gold contents, sword equipment, copy refusing its own unsolicited equipment |
| C++ guest/host cutscene sections | Their existing named cases, now sequential | Internal holds before leaving, controls restored after leaving, host holds until its script ends |
| C++ map-marker sections | Its existing named case, now sequential | Correct worldspace/position before removing the marker from every game |

Superseded functions, registrations and draw-only helper implementations are removed. Removed names are rejected;
they occur here and in historical evidence only, with no compatibility aliases.

## Boundaries retained separately

Late join, first character creation, unloaded actors, announce-before-change ordering, rest/resurrection ordering,
transport loss, and failures retain their fresh fixtures. The outfit regression still starts with the outfit being
removed, then put back without an item change. Drift repair remains a deliberate copy-only disturbance. Native
player actions still go through input dispatch.

All ten JSON specs remain unchanged. They now register individually as `spec:<name>` in Catch2 and Skyrim, with
their tags. Runtime discovery replaces the aggregate Catch2 case and its sections. Native tests with internal
holds, explicit world-change recording, two world cutscenes, or control-message ordering remain handwritten;
those checks extend beyond the corresponding JSON spec's observable contract.

No gameplay assertion was intentionally removed. A normalized audit of 1,590 original Python wait/check/failure
sites found every gameplay expression in the result. The two unmatched expressions are infrastructure messages
whose data source changed from the old dictionary/current-instance globals to the registry and `Game` handles.
Pure comparison algorithms retain their original expressions and tolerances. Repeated fixture handshake assertions
run fewer times because lifecycles share their setup.

See [the current testing guide](testing.md) for selection and execution, and [the verification report](../reports/2026-10-30-game-test-flows.md)
for measured setup counts, timing, controls and native results. The former feature/twin table is preserved as a
[historical snapshot](../reports/testing-history/2026-10-30-coverage-before-refactor.md), rather than maintained as a second guide.
