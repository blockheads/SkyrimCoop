# Hand slots: what fills an actor's two hands

An actor's hands are `AIProcess::equippedObjects` (+0xF0 left, +0xF8 right; `state.hands` in the harness). The worn
marks on the inventory's extra lists (`ExtraWorn` 0x16 right, `ExtraWornLeft` 0x17 left) are separate. Two writers fill
the hands, and they follow different rules. A copy runs both, so it can hold a weapon or torch differently from its
owner. This was the shared cause of the hand-slot cases under "A copy without its weapon" (bugs.md): a bow at
`[0, bow]` and a guard's two torches shown as one. Whether it is what the 2026-11-06 report saw (a weapon's 3D gone) is
inferred, not observed.

Read in IDA (1.7.104) unless marked measured.

## The equip: `ActorEquipManager::EquipObject` 38894 -> `coop_RealEquip` 38929

```text
38894 EquipObject(actor, item, list, count, slot, ...)
  list already worn (11696 IsWorn either) -> nothing at all
  -> 38929: slot = item's own slot (BGSEquipType, vfunc +0x20)
       passed slot wins: 38926 (race flag bit 30 CanDualWield, a weapon, a hand slot)
                         or the own slot has parents and lacks UseAllParents
       -> ActorProcess::AddEquippedObject 39192: each hand the slot covers (BGSEquipSlot::Covers 23605)
       -> Character::MarkEquippedWorn 38001 -> Actor::AddWornItem 38004 -> 16066:
            worn left = (slot == LeftHand); forceEquip adds ExtraCannotWear
```

- A light's slot is always default object 19, LeftHand (`TESObjectLIGH::GetEquipSlot` 17619). LeftHand has no parents,
  so no passed slot wins: **an equip holds a torch in the left hand only, marked WornLeft.** Measured 2026-11-23
  (Faralda, `equipitem 1d4ec`): `worn_left` [1D4EC], hands [torch, 0]. This corrects the older note in
  copy-equipment-after-rebuild.md that a native torch is marked ExtraWorn.
- A two-handed weapon's own slot is BothHands, which covers both hands: `[bow, bow]`.

## A 3D load: `TESNPC::InitWornOn3D` 24725 -> 24736

```text
24725(npc, actor, biped, force)
  no 3D yet (or forced): ResetWornToOutfit 418622 on a living actor (hands emptied, outfit equipped)
  InitWornVisitor -> 24736 per worn entry, per extra list:
    weapon: WornLeft -> AddEquippedObject(LeftHand); Worn -> AddEquippedObject(RightHand)
    light:  WornLeft -> biped shield slot + left hand; Worn -> biped slot 32 + right hand
    (the process's hands only for a list with ExtraCannotWear, a forced equip)
```

- **The hand slot, never the item's own:** a forced bow marked Worn is held `[0, bow]`.
- **A forced torch marked Worn is held in the right hand.** It is the only way to a torch in the right hand: no equip
  gives one.
- Our copies' equips are forced (`ActorEquipItem`, abForceEquip). So a copy's 3D load leaves its bow at `[0, bow]` and
  its torches where their marks say, while its owner's lists (not forced) keep the hands the equip gave. This is the
  `[0, bow]` writer that piece D could not find (rejoined-guest-sees-rider, elder-scroll-vision-reaches-party).
- Measured 2026-11-23: an `additem` on a host NPC holding a plain-equipped left torch and a forced right one left
  its hands [0, torch]. `Actor::AddObjectToContainer` 37525 calls 24725 (its body not read); only the forced list
  was held again.

## The copy's repair (InventoryService::ApplyWorn, EquipmentRepair)

- Each worn weapon or torch must be in every hand it is held in (`HandsOf`): its mark's hand, and both for a
  two-hander (`TESObjectWEAP::IsTwoHanded`, animation type 5-7 or 9 as 39811 tests it).
- A two-hander is equipped with BothHands passed: no slot becomes RightHand (38911), which 38926 keeps for a race with
  flags bit 30. BothHands is no hand slot, so 38929 keeps it and fills both hands. A torch marked left is equipped
  with no slot (LeftHand).
- A torch marked right: `Actor::HoldInHand` does what a 3D load does for one list. 16066 marks one more of the
  stack worn right, forced. 24736 runs on that list alone. Then `SetEquipFlag(1)` 39907 and `Update3DModel` 39395
  attach the biped's objects (bit 1 -> Actor 19743).
- Measured 2026-11-23 (`npc-torch-each-hand`): a host NPC holding a forced torch in each hand ends with its copy at
  [torch, torch]. The fake engine has the same facts (FakeEngine::Equip, InitWornHands, ActorHoldInHand), and two
  GameTests flows fail without the repair: "A guard's copy holds a torch in each hand as its owner does" (both join
  orders) and "A copy whose 3D load leaves its bow in one hand holds it in both again".

Not read: which vanilla path gave the playtest's host guards a torch marked right. The guards'
`[torch, torch]` on the host is taken as the playtest showed it, and staged by the harness's `hold_in_hand`.
