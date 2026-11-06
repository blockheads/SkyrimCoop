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

## What a copy does (`CharacterService::OnRemoveCharacter`)

```
temporary reference          deleted
a game's reference           FollowOwner: where the owner's went, or disabled as it is
each frame on a guest (GuestCopies)
  held, with 3D here         hidden (disabled, HeldCopyComponent::Hidden): at removal or
                             when this game's loaded cells reach it later
each frame (RunPresence)
  collected by the engine    its entity goes
next spawn                   enabled again, a copy
session end                  a hidden one enabled, the flag cleared
```

Hidden is for guests only: the host's own reference that a guest's mount was is the host's to own again.
`OnNotifyDismount` places a horse through `FollowOwner` too, but its entity keeps a `RemoteComponent`, so it is not hidden.

Tests: GameTests `[copies]` (the three flows at the end of `copy_flows.cpp`) and "A quest deletes a placed NPC a guest
talks to" (`dialogue_races.cpp`); ClientTests `SpeakerUpdate::Skipped`; the two scenarios named above and
`leveled-placed-npc-is-hosts`. The fake cannot fault: the hook itself is proven only by the scenario.
