# A leveled copy's base: never a permanent record (2026-11-23)

Playtest 2026-11-22, the guest's crash 3 (`docs/re/crashes` has no separate file; evidence below): access violation in
15678 on the background loader, walking a temporary leveled base's `faceNPC` chain (+0x1F0) into a non-pointer.

## The engine path (1.7.104, read in IDA, names recorded in the journal)

A placed leveled actor (an ACHR whose base is an NPC with a TPLT leveled list, `LvlAnimalCanyonPredator` 422B3) is
resolved by `TESLevCharacter__ResolveForReference` (14374): it rolls the list (14752), builds a new temporary NPC
(the factory, a copy of the current base, then `CopyFromTemplateForms` 24720 with the pick), records
`ExtraLeveledCreature {originalBase, templateBase = pick}` (20231 -> 11812) and sets it as the base (19785/37466).

Two paths roll it again, and both destroy the current base without checking it is a temporary:

- `Character__RerollLeveledBase_37323`: from the cell's respawn loop (19231), the `ResetReference` native and
  console `RecycleActor`, ref alias init, quest start, Papyrus `Reset`.
- `Actor::ResetReference_37292`: from the cell reset functor (19286, from TESObjectCELL 19084).

Each sets the original back, clears the extra, then hands the old base to `GarbageCollector__DestroyForm_36460`, which
removes it from the form map and deletes it (or queues that), whatever it is. The engine assumes a leveled actor's
current base is the temporary it built. Only `ExtraDataList::LoadGame`'s (12095) ExtraLeveledCreature case (0x140177F9C) checks
`IsTemporaryFormID` first.

## What went wrong

The copy code gave a placed leveled copy whose game had picked another NPC the owner's face record itself
(`SetActorBaseFormId(EncWolf 23ABE)`). The guest's log:

1. 03:40:13.76: copy `DC8E3` takes the EncWolf record (the swap succeeded; a breadcrumb, no error).
2. 03:40:18.34: our `MoveActorTo(DC8E3)` into cell 95F5: the engine re-rolls it (ResetInventory, two `SetLevel` from
   `CopyFromTemplateForms`, InitLeveledItems) and collects its current base: EncWolf is destroyed.
3. From 03:40:18.85: `GetById(23ABE)` finds nothing: "Failed to apply owner's NPC base 0:23ABE" for four copies.
4. The leveled list and temporary bases built from EncWolf still point at it; `FF0010BD`'s faceNPC reads EncWolf's
   reused memory (`0x3F1416AA0000086D`) and 15678 faults (03:40:26.3).

Inferred from the order of events (the dump holds no heap): which link held the freed pointer is not read, but every
symptom follows from step 2.

## The fix

`NpcSnapshot::ChooseBaseApply`: a leveled reference (`IsActorLeveled`, an ExtraLeveledCreature with an original) whose
pick differs is built again, the engine's way: `SkyrimBridge::RelevelActor` calls 14375 (`TESNPC::BuildLeveled`) with
the original and the owner's pick, records the pick in the extra and sets the new temporary base, as LoadGame's case 45
does. A later reset destroys only that temporary. The old temporary is left (not collected): a precaution, not read or
measured; the crash itself was the background loader (QueuedCharacter 13108 -> Load3D 37177) reading a base another
thread changed, and when its last reader is done is not known. Each correction after a guest re-roll leaves one such
form (`docs/bugs.md`). Other actors still take the owner's base.

Measured on the duo (`guest-releveled-copy-keeps-pick`, heap history on): at level 50 the guest's `RecycleActor` gave its
copy of `DC8E3` a Skeever; the host's announcement made it a new temporary (`FF000E8B`) with EncWolf as face and pick; a
second guest re-roll and announcement left EncWolf alive, no double free. Without the fix, GameTests `[copy-level]` "A
leveled copy whose game picked another NPC keeps the owner's pick a temporary base" fails four checks, with the
playtest's own log line.

Not settled: on the first duo attempt the host crashed teleporting to `DC8E3` (job thread executing a heap address under
the animation file manager, 63997), unrelated to this path; the second boot ran clean.
