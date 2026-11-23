# Hand slots: what fills an actor's two hands

An actor's hands are `AIProcess::equippedObjects` (+0xF0 left, +0xF8 right; `state.hands` in the harness). The worn
marks on the inventory's extra lists (`ExtraWorn` 0x16 right, `ExtraWornLeft` 0x17 left; harness `worn_left`) are
separate, and either can change without the other. Each claim says whether it was read in IDA (1.7.104) or measured.

## The equip: `ActorEquipManager::EquipObject` 38894 -> `coop_RealEquip` 38929 (read)

```text
38894 EquipObject(actor, item, list, count, slot, ...)
  list already worn (11696 IsWorn either) -> nothing at all
  no slot -> 38911's default (RightHand for a weapon)
  -> 38929: slot = item's own slot (BGSEquipType, vfunc +0x20)
       passed slot wins: 38926 (race flag bit 30, a weapon, LeftHand or RightHand passed)
                         or the own slot has parents and lacks UseAllParents
       -> ActorProcess::AddEquippedObject 39192: each hand the slot covers (BGSEquipSlot::Covers 23605)
       -> Character::MarkEquippedWorn 38001 -> Actor::AddWornItem 38004 -> 16066:
            worn left = (slot == LeftHand); forceEquip adds ExtraCannotWear
```

- A light's slot is always default object 19, LeftHand (`TESObjectLIGH::GetEquipSlot` 17619). LeftHand has no parents,
  so no passed slot wins: an equip holds a torch in the left hand only, marked WornLeft. Measured 2026-11-23 (Faralda,
  `equipitem 1d4ec`): `worn_left` [1D4EC], hands [torch, 0]. This corrects copy-equipment-after-rebuild.md, which said
  ExtraWorn.
- A two-hander's own slot is BothHands, covering both hands. Passing no slot is RightHand (38911), which 38926 keeps
  for a race with flags bit 30 (read). Measured against that: a console `equipitem` of a bow (no slot) filled both
  hands on the guest's player and on a Winterhold guard (2026-11-23). The copy repair passes BothHands (0x13F45)
  anyway: 38926 accepts only LeftHand or RightHand, so 38929 keeps it (read, not measured).
- What counts as two-handed is the weapon's equip slot covering both hands (`TESObjectWEAP::equipSlot` +0xE8, from
  14218 and 14216; `BGSEquipSlot::Covers` 23605), the same thing 39192 fills hands by. Its animation type is not
  used: `DummyWarhammer` (0x20154) has animation type 1 and slot BothHands (Skyrim.esm, as read in review).

## A 3D load: `TESNPC::InitWornOn3D` 24725 -> 24736 (read)

Per worn list, 24736 holds a weapon worn left in the left hand and one worn right in the **right hand slot**, never the
weapon's own (a bow would be `[0, bow]`). A light worn left goes to the biped shield slot and the left hand; a light
worn right goes to biped slot 32 and the right hand. It sets the process's hands only for lists with ExtraCannotWear
(forced). It never clears a hand.

Sampled 2026-11-23: a bow copy read `[bow, bow]` after the harness's `reset_3d` (`DoReset3D(false)` 40255, whose
false branch can queue the model update) and after a guest-side disable/enable. Neither shows that 24725 ran, so this
does not rule out a 3D load as the writer.

## Where `[0, bow]` comes from (measured)

`rejoined-guest-sees-rider`, 2026-11-23, with the repair's log line ("hands [..] hold less than its worn set"): after
the guest rejoins, Faendal's copy (1348C) is at `[0, 13985]` at its first repair, 40 ms after its spawn
(`NotifyCharacterSpawn`), against his owner's `[13985, 13985]`. That is the state at the first repair; it does not
say which step between the rejoin and the repair wrote it. Not read. Candidates: 39430 (calls 39192), 24725's
visitor, or an equip with no slot (38911's RightHand, kept by 38926).

The old repair checked one hand per item, so it passed `[0, bow]`. It now checks every hand an item is held in
(`EquipmentRepair::HandsOf`). A two-hander takes both hands unless something else worn holds the left: Aela's owner
held `[torch, bow]` (inventory-changes.md, measured), so the bow then takes the right only. The worn marks don't say
more than that; an owner whose two-hander really holds only the right would need its hands sent. In the same run the
copy was repaired (logged reset and re-equip), and the scenario passed (2 of 2 failed on dev `aa723513`).

## Torches on guards (measured)

The 2026-11-22 sighting was guards' copies with one torch where the host showed one in each hand. At 21:00 in
Winterhold on 2026-11-23, two real guards held `[torch, 0]` and `[torch, sword]`, the torch marked WornLeft, and the
copies agreed all through 30 s. No vanilla equip puts a torch in the right hand (17619 above). No guard with two
torches was seen, so the sighting is not reproduced. A torch worn right in the right hand stays a reading of 24736.
