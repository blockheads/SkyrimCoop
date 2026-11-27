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

## A rebuilt base recorded as the reference's base change (2026-11-27)

Read in IDA on 1.7.104; the free and the stale use measured on the duo (the six mount scenarios, heap history, a
guard on 0x268-byte blocks, probes on 19529, 12095 and 24888).

```text
SkyrimBridge::RelevelActor -> TESObjectREFR::SetBaseObject
  SetObjectReference (19785 through 37466), then MarkChanged(CHANGE_REFR_BASEOBJECT 0x80)   <- ours; the engine's
                                                                                               leveled resolve never records it
the reference's changes are stored (a cell's unload: StoreChangesOnUnload 35570) and loaded again
  TESObjectCELL::InitItemImpl 18881 -> 18882 -> 35572 (the moved-reference map) -> LoadStoredChanges 35569
    -> Actor::LoadGame 37650 -> TESObjectREFR::LoadGame 19529
         0x80: LookupFormByID(recorded id) -> SetObjectReference      the recorded temporary, or a newer one that
                                                                      took its id (ids are reused at once)
         -> ExtraDataList::LoadGame 12095, ExtraLeveledCreature (0x140177F9C)
              current base temporary -> its destructor (vfunc 0), a new base from 14375
```

So a recorded temporary base reaches a second reference (the one the cell makes again, while the old one waits for
the collector or is held as a copy) and is destroyed there as that reference's own.

- **Measured** (crash `08-28-24`, guard on): the collector's job (36468 -> 36480 -> 40288 -> 37175/37176) deleting
  copy `105A35`'s old reference read its base, a freed guarded block: the TESNPC `FF000996` our `RelevelActor` built
  for it 26 s before, destroyed 30 ms before through the path above (`probe:24888 TESNPC FF000996`); 10 ms later a
  new TESNPC had the id `FF000996`.
- **Measured** (crash `08-07-23`, quarantine on): a held copy (`EBC51`) enabled at the session's end read its base,
  the NPC `RelevelActor` built for it 140 s before and the same path destroyed 131 s before, as the party travelled.
- **Read, not measured:** crash `07-44-25` (no heap tools): the background loader (13108 -> 37982) read copy `DC8DF`'s
  destroyed base 21 ms after our `RelevelActor` and `MoveActorTo` for it; and the 2026-11-27 night's second crash (a
  copy's base `TESNPC` with a null vtable). Both have this shape.
- **Not established:** which reference object each destroying load ran on (the probes' breadcrumbs had rotated past
  it); that it was a second reference follows from the engine code above, since 12095 sets the new base on the one it
  loads.

**The fix** (`engine/BaseChange.h`, `TESObjectREFR::SetBaseObject`): a temporary base is never recorded as the base
change, and an older record goes (`UnsetChanged`, 14609), as the engine's own leveled resolve leaves it; on load the
base is built again from the ExtraLeveledCreature. A permanent base (a leveled item's pick, `ReferenceStates`) is
still recorded. UnitTests `[base-change]` model 19529 and 12095: recording the temporary destroys it under the copy.
