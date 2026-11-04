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
