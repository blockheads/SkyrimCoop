# A copy's hand spells after a move (2026-11-04)

Seen on the duo (late save `LateAfterDragonslayerBackinSkyrim`, Riverwood): the host's player holds Healing (12FCC)
in the left hand; the guest's copy of that player held it after its spawn (`SetMagicEquipment`, EquipSpellBySlot
38896), and after the party travelled (`Copy ... is not where its owner is: moved there`, MoveTo) its hands were empty:
`AIProcess::equippedObjects` [0, 0] and Papyrus `GetEquippedSpell(0)` None on the copy, 12FCC on the host. The move
rebuilds the copy's equipment (its outfit's equips, refused by the copy's equip hook) and drops its selected spells.

Nothing put them back: the owner sends its equipment every 3 s (`RequestEquipmentSnapshot`, with
`CurrentMagicEquipment`), but the copy compared only worn items. `InventoryService::OnNotifyEquipmentSnapshot` now
also compares the hand spells, and a difference takes the existing reset (`ActorResetEquipment`: unequip all, the
owner's spells, then the worn set). Shouts are not compared: not seen dropped.

Tests: scenario `copy-keeps-spell-after-party-travel`; GameTests "A player's copy holds its owner's spell after party
travel" (the fake drops an NPC's hand spells on a cell change); ClientTests `[hand-spell]`.

## Selected, not held (2026-11-07)

Still seen after the fix above: the riverwood-day-one flow (both copies, after doors), and on the duo after the party's
`coc WhiterunOrigin` from Riverwood (default boot, both players' Healing): each copy's left hand
(`AIProcess::equippedObjects[0]`, `state.hands`) empty, its selected spell (`Actor::selectedSpells[0]` +0x1C8; Papyrus
`GetEquippedSpell(0)`, 54683, reads it) still 12FCC. No reconcile ran: the selected spells matched the owner's.

A hand spell lives in two places, read in IDA:

- `EquipSpell` (38928) puts the spell in the hand (`ActorProcess::AddEquippedObject` 39192: the process's equip list
  and `equippedObjects` +0xF0 left / +0xF8 right) only when the actor has a process (`Actor+0xF8`); it selects it
  (`Actor::RevertSelectedSpell` 38768 > `Actor::SelectSpell` 38826) either way.
- `Actor::Set3D(null)` (37178), on an actor that had 3D, reverts each hand's selected spell: both places cleared.
- Loading 3D again re-equips nothing.

So a copy whose 3D unloads in a trip, and whose reconcile (or spawn's `SetMagicEquipment`) lands before the new place
loads it, keeps the spell selected and an empty hand, and the reconcile then reads it as matching. The reconcile now
also reads a loaded copy's hands: a hand the owner holds a spell in that holds nothing takes the reset
(`EquipmentRepair::HandHolds`). A hand holding a weapon over a stale selection is left alone, so it cannot reset every 3 s.

Tests: scenario `copy-holds-spell-after-trip`; GameTests "A player's copy whose 3D comes back holds its owner's spell,
not only selects it" (the fake learns both places: `HeldSpells`, `EquipSpells`, `Unload3D`); ClientTests `[hand-spell]`.
Readout: `mod.selected_spells` beside `state.hands`.
