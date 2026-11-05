# Crime: theft, witnesses, bounty

Read on Steam 1.7.104 in IDA, 2026-11-04 (names and comments in the database, exported to `ida_notes_1.7.104.tsv`).
How the party shares this is in [shared-party-bounty.md](../../design/shared-party-bounty.md); this page is the engine.

## Theft: StealAlarm (37422)

`Actor::StealAlarm(criminal, ref, item, count, total, owner, allowWarning)`, called by `PickUpObject` (37521) for an
owned loose item and by the container menu on close (`item` null, `total` the menu's value).

```
criminal must be the real player (a teammate is folded into it), else return
value = count * item gold value, else total, else 1
crime = Crime::ctor (42502): type, victim handle +0x08, criminal handle +0x0C, item +0x18
ProcessLists__AddPlayerCrime (41349): kept only if criminal handle == the player's handle
ownerActor = ProcessLists__FindStolenOwnerActor (41335):
    a high-process actor matching the owner (NPC base / leveled base, or faction member)
    when highest detection of the player (41408) + weight factor > 0
no owner actor: player's unwitnessed stolen value (+0x428 map, 40650), return
seen = RequestDetectionLevel(ownerActor, criminal, 3) > 0
if seen && allowWarning && first theft today (criminal +0xE8 bit 0x8000 unset):
    Actor__StartStealWarning (37556): a package (type 34) going to g_thePlayer, no bounty; return
37369 (not read: a process call, topic 38)
if seen: Actor__StartAlarm(ownerActor, crime) (37554)
witness loop (ProcessLists__GatherCrimeWitnesses 38094, all high actors):
    a witness that detects the criminal (37764, priority 3) in the shared crime faction -> witnessed
if witnessed:
    Character__ReportCrimeAddBounty (37679): bounty = 37680(victim, type, faction, value),
        criminal->ModCrimeGold (vfunc 0xB6) and the "N Bounty added" HUD line; only for the player
    ActorProcess__AlertNearbyToCrime (39361) -> 39695: guards in the faction StartCombat(guard, g_thePlayer)
        or StartAlarm(guard, crime)
else: unwitnessed stolen value only
```

The bounty is written only by 37679, and only when an owner or witness detected the criminal at that instant.

## Who the reactions target

Every reaction aims at the crime's criminal, which StealAlarm requires to be the real player:

- `Actor__StartAlarm` (37554) reads the criminal from crime +0x0C: an assault (39359) when the witness is aggressive
  toward it (or it is attack-on-sight), otherwise an alarm package (type 21) whose location and target are that
  handle.
- `Actor__StartStealWarning` (37556) builds its package on `g_thePlayer`.
- 39695 starts guards' combat on `g_thePlayer` directly.

So a guest's theft judged on the host (the bridge passes the host's player as the criminal, `CrimeBridge::Judge`) sends
every reaction to the host's player, wherever the guest stands.

## Detection is on request: a witness sees only what it was asked about lately

`RequestDetectionLevel` (37764) = `ActorKnowledge__GetFreshDetectionState` (42702) on the witness's knowledge of the
target (39515, created on first use):

- The knowledge's `DetectionState` (+0x20) is filled only by a later detection pass; a request (42712) queues one
  and stamps +0x44.
- Priority 3 (every crime check) accepts a state at most 2 s old (table `0x1420B8AB8`: {refresh 1 s, max 2 s}).
  Older or missing: it queues an update and answers -1000, unseen, for this call.

StealAlarm judges once, synchronously, so a witness whose state of the criminal is older than 2 s cannot catch it. On
the host the checks are redirected to the guest's copy (`HookProcessKnowledge`). Who keeps the player's state fresh in
vanilla was not traced: 41408's callers include the lock menu, `FindStolenOwnerActor` and `Actor::sub_40593`, none
proved per-frame, and Hulda's state of the far host read 4.1 s old once.

## The bounty map moved on 1.7.104 (the playtest's missing bounty)

`PlayerCharacter::GetCrimeGoldValue` (40641) reads the map at +0x3F8 (capacity +0x3FC, entries +0x418), and GoToPrison
(40655) the jail faction at +0x728 and sentence at +0x730: 1.6.1170's offsets plus `SkyrimRuntime::PlayerShift`.
`CrimeEngine::Player` still had the 1.6 values, so `ReadGold` read 8 bytes off and found nothing. The engine wrote the
bounty; the host never saw it change and sent no `NotifyCrimeState`. The fix applies the shift.

Proof: with the shift, the host read a bounty of 8 that earlier thefts had already written and the guest received it;
from 0, a guest's chest theft through the menu made it 1 in both games. With the shift removed, `guest-theft-witnessed`
failed with `{}` in both. Those runs also asked every frame (below), and the scenario then accepted a leftover bounty.

Every "no bounty" read before that fix was blind, so none of them shows a theft went unwitnessed.

## Measured on the duo (2026-11-04)

Harness `crime knowledge` (39763) reads a witness's state without asking for one.

- Bannered Mare, nobody asking: Hulda knew the guest's copy with no state at all, later read it 4.7 s old while her
  state of the nearby host was 0.5 s old.
- Asking for every copy each frame (41408, tried, since removed) held her state of the copy at 0.3 s.
- Not measured: a theft with the offset fix and nobody asking (`docs/bugs.md`).
- A value 200 chest theft raised the bounty by 1, and one of value 100 by 1 (8 to 9). Not explained.
- `SetPlayerReportCrime` (+0xBEC bit 2) ends StealAlarm when clear. Whether the duo's characters start with it clear
  was not read: `report_crimes` logs it now.
- Harness: a frozen NPC stops detecting (its age only grows). After a hot reload, states of existing copies went stale
  and `Judge` logged nothing until a save load; suspected, not proved: the copies lost their player flag.

## What SkyrimCoop does about it

- `CrimeEngine::Player` applies `PlayerShift` (above).
- Not fixed: reactions still target the host's player (`docs/bugs.md`, The guest's aggro goes to the host).

## IDs

| ID | Name (ours) | What |
|---|---|---|
| 37422 | Actor::StealAlarm | the theft judgment above |
| 41335 | ProcessLists__FindStolenOwnerActor | owner actor in high process |
| 41349 | ProcessLists__AddPlayerCrime | keeps only the player's crimes |
| 42502 | Crime__ctor | crime record, criminal +0x0C, faction +0x60, bounty +0x58 |
| 37679 | Character__ReportCrimeAddBounty | writes the bounty (ModCrimeGold 0xB6), HUD line |
| 37554 | Actor__StartAlarm | witness's alarm or assault on the criminal |
| 37556 | Actor__StartStealWarning | first-theft warning package to g_thePlayer |
| 39361, 39695 | ActorProcess__AlertNearbyToCrime, ProcessLists__AlertCrimeResponders | guards respond |
| 40650, 40652 | PlayerCharacter__AddUnwitnessed/WitnessedStolenValue | stolen-value stats, +0x428 |
| 37764 | Actor::RequestDetectionLevel | fresh detection level or -1000 |
| 42702, 42712 | ActorKnowledge__GetFreshDetectionState, request | freshness rule |
| 39763 | HighProcessData__FindKnowledge | knowledge lookup without creating (harness `knowledge`) |
| 41408 | ProcessLists::RequestHighestDetectionLevelAgainstActor | asks every high actor about one target |
| 40641, 40655 | PlayerCharacter::GetCrimeGoldValue, GoToPrison | where the player's crime offsets were read |
| 55578 | papyrus::Game::SetPlayerReportCrime | +0xBEC bit 2; StealAlarm and the other alarms return early when clear |
