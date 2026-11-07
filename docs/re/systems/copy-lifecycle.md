# What happens to an actor going away, and to its copy

Read in IDA on 1.7.104 unless it says measured (the duo, 2026-11-06). Searched `reference/plugins` and the web: no mod or
write-up covers a speaker losing its process. Closest: SkyrimSoulsRE's `AutoCloseManager` closes a menu whose target is
disabled or marked for deletion. `UpdateInDialogue`'s signature is CommonLib's (`Actor.h`, `DialogueResponse*, bool`).

## How an actor loses its process or its form

```
Disable (19801)
  leaves the process list (RemoveFromLevelList 41291)
  has a package: EndInterruptPackage (37474)
    IsTalkingToPC (39347): EndDialogue (35294), which clears the conversation (35274)
  frees the AIProcess inline: currentProcess (+0xF8) = null

marked deleted, each frame in ProcessLists::UpdateTempChangeList (41294)
  no source file and not persistent (a temporary reference): GarbageCollector::Add
    process and handle go together
  a source file, or persistent (a plugin's reference): MoveToLow + ReleaseProcess (37563)
    the handle stays valid, and NOTHING ends its conversation

UpdateProcessLevel (37501)
  below high, not persistent, its cell not loaded: asks to be freed; UpdateTempChangeList collects it
```

`ReleaseProcess` (37563) and `Disable` write a null to `currentProcess`; a scan for other writers was not read to the end.

- **Measured:** a temporary or a disabled speaker ends its conversation cleanly (guest talking to placed Vilkas, the
  host's `disable`: menu closes, no crash).
- **Measured:** a placed speaker marked deleted with no disable faults: within 30 ms on the guest (its own
  `markfordelete`), and on the host at once too (`placed-speaker-deleted-mid-conversation`, the guest talking):
  `AIProcess::GetLipData` (39373, `mov rdx,[rcx+0x10]`; crash tools print 39372+0x40) from `UpdateInDialogue`
  (37541+0x89) from `MenuTopicManager::Update` (35289+0xA8). The 2026-10-29 crash had this stack, on Lydia's copy
  flagged deleted; guests stopped deleting placed references on 2026-10-30. That her copy was a placed one is inferred.
- **Read:** the engine's own "no process" exit in `UpdateInDialogue` (37541+0x79A) does not help: after `EndDialogue` it
  reads the null process again (37541+0x812). Only the caller could check, and 35289 does not.

Our answer: `HookUpdateInDialogue` skips the update while online and the speaker has no process;
`DialogueBridge::ClearOrphaned` treats that speaker as gone, so `DialogueService` clears it and closes the menu.

Out of a session the hook changes nothing, so solo play should still fault the same way: read, not run. It is the
engine's own path with no code of ours in it, and it needs a script to delete a placed NPC mid-conversation.

## A reference's place when it is not loaded

- **Measured:** a persistent reference outdoors out of the loaded cells has no parent cell, only a worldspace and a
  position (Olava `1A699`, Skulvar `1A69C`: `cell` 0, `worldspace` 60 or 107119). `ReadWhereabouts` read that as "no
  whereabouts", and the guest's copy stayed where it stood with no owner (`held-copy-goes-with-owner`, the entry's message).
- **Measured:** a plugin's non-persistent reference that follows to a cell the guest has not loaded stops existing:
  `TESForm::GetById` finds nothing (guard `D8C56`, sent to cell `9A26`; the host's stayed, with 3D). That it was
  `UpdateProcessLevel` that freed it is inferred from the read above; nothing traced the call. The plugin brings the
  reference back when its cell next loads.
- **Measured:** the host's discovery does drop a moved guard (`Actor discovery: D8C56 removed`, a second after the
  move). The map's guess that it stayed "loaded" on the host was wrong: the host's game decides what is loaded, and
  discovery needed no change.

## A held copy: where the engine loads, frees or enables it (read 2026-11-07)

Read in IDA on 1.7.104, no mod covers it (searched `reference/plugins`: none hides a reference from its own game).
Three kinds of point, and the disabled flag (form flag `0x800`) decides each:

```
loads its 3D
  TESObjectREFR::Load3D (19727, vfunc 0x6A; Actor::Load3D 37177 calls it first)
    first test: deleted (0x20) or disabled (0x800) -> null. Cell attach, Enable and MoveTo all load through it

frees it
  ProcessLists::UpdateTempChangeList (41294), each frame
    disabled: RemoveFromLevelList + ReleaseProcess (37563); the form stays
    else UpdateProcessLevel (37501): not persistent, below high, its cell not loaded -> GarbageCollector::Add
      (also when a fade-out ends on a non-persistent one)
  TESObjectREFR::Enable (19800): an actor enabled in a cell not loaded goes through 37501 too
  a cell's unload: TESDataHandler 13721 -> TESObjectCELL 19113: each reference its functor (1402D00D0) picks gets
    StoreChangesOnUnload, then GarbageCollector::Add, disabled or not; a persistent one stays (measured, below)

enables it again
  TESObjectREFR::Enable (19800): returns unless disabled; SetDisabled(0), a new AIProcess, Load3D if loaded,
    then its enable children (20270). Callers: Papyrus Enable (EnableFunctor 56110), the main thread's queued
    enables (21919), an actor's reset (37292: in-game flag 8), an ash pile, ClearDestruction, the children loop.
    MoveTo_Impl (56626) disables and enables a non-actor for the move, never an actor
  TESForm::SetDisabled (14646) raw: InitItemImpl and InitLoadGame (a reference made from its file or a save) and
    the enable-parent sync 20271 they run, which sets each child's flag with no Enable
```

So a held copy is kept by making it persistent (19597, as players' copies are) for the rest of the session, its own
flag back at the session's end, and hidden by disabling it: no 3D, and no per-frame free either. What can show it
again is an enable: `HookEnable` (19800) returns for a form id in `HiddenCopies`, and `HookSetDisabled` (14646) keeps its flag set
(it returns true, the flag's state). Our own `SkyrimBridge::SetActorHidden` adds the id before the disable and removes
it before the enable. Out of a session nothing is hidden, so neither hook changes anything.

- **Not established (2026-11-07, the duo):** with `Cast<Actor>` and `Actor::GetExtension` in the `SetDisabled` hook,
  two boots of a fresh worktree's prefix failed to host; with no hooks, the `Enable` hook alone, or `SetDisabled`
  passing straight through, later boots hosted. Those first boots may as well have been the fresh prefix's
  Anniversary box (progress.md, 2026-11-07, PR #34). `SetDisabled` also runs from `InitItemImpl` and `InitLoadGame` on
  forms being loaded, so the hooks read only `formID` (+0x14) and look it up either way.

- **Read, not run:** that a held copy's enable comes from the host's cell answer (the guest's own disable is a change
  its cell request sends back, the host's enabled row the answer) or from an enable parent's first load. The fake
  refuses enables for a hidden copy as the hooks do; the hooks themselves are proven only in Skyrim.
- **Measured before (2026-11-06):** a placed guard's non-persistent reference that followed to a cell the guest had not
  loaded was gone (`GetById` null), and the host's next spawn of it found nothing to take.
- **Measured (2026-11-07):** disabled alone, the guard was gone all the same once the party travelled on (the harness's
  `actor_place` on the guest: none), so the cell's unload frees disabled references too (19113, read after). And a
  spawn that showed it and gave its flag back in one frame, its new cell still loading on the guest, lost it to 37501
  (`gone where its owner is not: dropped`): it stays persistent until the session ends. Persistent, the two passes
  of `leveled-placed-npc-is-hosts` in one session both find it.

## What a copy does (`CharacterService::OnRemoveCharacter`)

```
temporary reference          deleted
a game's reference           FollowOwner: kept and hidden on a guest (CopyHeldEvent, GuestCopies), then
                             where the owner's went; kept and disabled, not hidden, when the owner's was
                             disabled or deleted
dropped on an arrival        held, kept and hidden on a guest (DropCopies)
next spawn                   moved where its owner is, then shown, a copy (still kept)
session end                  a hidden one shown, a kept one's own persistence back
```

Hidden is for guests only: the host's own reference that a guest's mount was is the host's to own again.
`OnNotifyDismount` places a horse through `FollowOwner` too, its owner still there, so it is not hidden.

Tests: GameTests `[copies]` (the three flows at the end of `copy_flows.cpp`) and "A quest deletes a placed NPC a guest
talks to" (`dialogue_races.cpp`); ClientTests `SpeakerUpdate::Skipped`; the two scenarios named above and
`leveled-placed-npc-is-hosts`. The fake cannot fault: the dialogue hook itself is proven only by the scenario.
