# 2026-10-23: guest crash voicing a copy with no process (fixed)

**Symptom:** the guest crashed about a second after both games teleported to Riverwood (`player.moveto`): access
violation reading `0x10` at `0x1406E34C0` (`AIProcess::GetLipData`, 39373 in `ida_notes.tsv`), from `Actor::SpeakSound` (37542+0xD4),
`SkyrimBridge::ActorSpeakSound`, `CharacterService::OnNotifyDialogue`, on the copy of a host bandit (`0001EBA1`).
Crash log `prefix-client/.../SKSE/crash-2026-10-23-20-11-01.log`.

**Cause (IDA):** 37542 reads the actor's process (`currentProcess`, +0xF8) and passes it to `GetLipData` unchecked. The
host's bandit said a line while the guest's game was still loading its reference: no process yet. The same engine
function crashed on 2026-10-14 for another reason (`2026-10-14-reused-copy-form-id.md`, a reused form id).

**Fix:** `OnNotifyDialogue` voices a copy only when it has 3D (an actor with 3D has a process); a line said before
its copy loads is not heard. The fake engine counts a voice line on an actor without 3D as this crash
(`FakeEngine::SpokeWithout3D`); GameTests `The host's NPC speaks while the guest's copy has no 3D yet` fails without
the fix.
