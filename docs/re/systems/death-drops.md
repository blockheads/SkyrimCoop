# A dead actor's dropped weapon

Read in IDA on 1.7.104, 2026-11-05 (names recorded in the database, `docs/re/journal/1.7.104/agent-dropped-weapon-sync.tsv`);
measured on the duo the same day. Scenario `dead-guest-sword-no-theft`, GameTests "A guest dies holding a sword in an inn".

## The engine path (read)

```text
Actor::KillImpl 37896
  Actor::GetDroppableDrawnWeapon 37523   a WEAP in hand, drawn (weapon state >= 3), not a quest object
  vfunc 0xC5 false (Actor 38297: true; Character 40292: the knock byte), or iDeathDropWeaponChance and limits
  -> Actor::DropHeldItemsOnDeath 37320   shield, ammo/torch, then each hand's drawn weapon:
       vfunc 0xCB DropObject (the player's 40454, a character's 37520)
       the reference the drop made: owner = the player's base (7) when the actor is dead   (SetOwner 20198)
       TESObjectREFR::AdoptHeldNodeAs3D 19706: the hand's own weapon node becomes its 3D
```

- **The 3D is the hand's node.** 19706 hangs the node the actor held (root `Weapon  (00012EB7)`, children
  `IronLongSword:0`, blood nodes) under the cell, layer 32, dynamic. A drop made from the base gets its model
  (`IronLongSword01`). Same item, same body, different node names: nothing a player sees.
- **0x100000 is the dropper's bookkeeping** (read). `PlayerCharacter::DropObject` (40454), finding no 3D on the
  reference it made (the death drop's 3D comes after it returns), queues the handle (player +0x608) and sets form flag
  0x100000 (`TESObjectREFR::SetDroppedBeforeItsLoad` 19550, its only caller). Its one reader is
  `TESObjectREFR::Load3D` (19727): as the reference's 3D loads it places the reference from its model's offset. The
  model-made twin on another game never passes 40454's no-3D branch, so only the dropper's game has it.
- **The reference's position stays where the hand was** (measured; inferred from what was read). On the duo the
  dropper's reference kept the hand's position (z 7049) while its body rested 71 units lower, both games' bodies at the
  same place. Of what was read, only 40454's 3D branch moves a dropped reference to its 3D, and the death drop has no
  3D then; 19706 hands it the node without moving the reference. Not read: whatever else might move it later.
- **`changes` 0x400** (`kItemExtraData`, CommonLibSSE-NG `TESObjectREFR::ChangeFlags`; measured): the dropper's
  reference records it (0x446), the twin made from the base does not (0x46), after either game's death drop. Which
  call records it was not traced. It is that game's save record of its own extra list; what the extra data holds is
  compared through the item (`Stacks`) and the owner field.
- **The owner is the player** whatever the cell (read): 37320's `SetOwner(player base)` (20198) for a dead actor (the
  dying player is dead by then). A reference with no owner reads as another's: `TESObjectREFR::GetOwner` (20194)
  falls back to a linked door's owner, the encounter zone's, then the cell's. `PlayerCharacter::PickUpObject` (40533)
  reads it and calls `Actor::StealAlarm` (37422, `crime.md`) for anyone not an owner.

## In a session (measured)

- One sword on each game, the same body: the dying guest's game makes its reference and asks the host
  (`DropItemRequest`), the host's game makes the party's, the guest's becomes its twin (`dropped-items.md`). Either
  player picks it up, it is gone from both; nothing left after a respawn or a cell reload (both checked, 2026-11-05).
  The host's own death drop goes out as its created reference with its owner row.
- **The bug: the owner did not travel with a guest's drop.** The host made the party's sword from the base, unowned. In
  an owned cell (the Bannered Mare) the host taking a dead guest's sword was a theft (a theft warning, then bounty).
  The drop now carries its reference's owner (`DropItemRequest::Owner`) and the host's reference is made with it.
- Flags 0x100000, the 3D's node names, the reference's stale position and its `changes` bit 0x400 (the extra list) are
  each game's own: the world comparison reads a held-node drop at its body and skips them (`comparisons.held_node_view`).
