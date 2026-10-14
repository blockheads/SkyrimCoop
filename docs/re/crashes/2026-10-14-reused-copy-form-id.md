# 2026-10-14: host crash voicing a Cow (a deleted copy's form id reused)

**Symptom:** the host crashed in `party-fast-travels` (14:48:13): access violation reading `0x10` in
engine function 39372, from `Actor::SpeakSound` (`SkyrimBridge::ActorSpeakSound`,
`CharacterService::OnNotifyDialogue`), on `"Cow" [0xFF0045B4]`. The host was playing the client's voice
line on its copy of the client's character, whose form id the engine had given to a Cow.

## Mechanism

1. The host travelled to the river (`cow`) while the client was still in Riverwood, in the same
   worldspace. Outdoors the host counted the client as "with" it by worldspace, so its arrival spawned the
   client's copy (`FF0045B4`) at the client's position in Riverwood: an unloaded cell.
2. A copy was a created, temporary reference: the engine deleted it with that cell. The client then
   arrived; `RunPresence` saw `ActorExists(FF0045B4)` and moved it every 2 s; it never got 3D.
3. The engine gave `FF0045B4` to a new reference, a Cow. `ActorExists` was true again: we moved the Cow,
   forced its values, and played the client's voice line on it (a cow has no voice: the crash). Dropping
   the copy would also have deleted the Cow (`CancelServerAssignment` deleted any temporary actor at the
   form id).

## Fix

- A copy is the actor at its form id only while it is flagged a copy (`IsActorRemote`): `RunPresence`
  checks every frame and drops or respawns a copy that is gone; `CancelServerAssignment` deletes or
  un-flags only a copy. The form allocation hook (`HookFormAllocate`, Memory.cpp) gives every new actor a fresh extension, so a
  reused form id never reads as a copy.
- Players' copies no longer go with a cell at all: persistent, moved with the party, never respawned on
  arrival (`docs/design/copy-contract.md`, "Players' copies travel with the party").

`party-fast-travels` 8 of 8 since.
