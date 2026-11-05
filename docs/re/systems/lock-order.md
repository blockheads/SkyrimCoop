# Lock order: the engine's BSReadWriteLock and what the checker reports

The checker (`Code/client/Games/Skyrim/BSCore/LockOrder.cpp`) hooks the four `BSReadWriteLock` calls (68233 read,
68234 write, 68239/68240 unlock) and reports an inversion the first time two threads take the same two locks in
conflicting orders, whether or not this run's timing makes them wait. A report is history, not a wait: a lock pair
costs time only when the second thread finds the lock held (the checker's `GetOverlaps` counts those as
contended/live).

`[ours]` means our DLL is on the reporting thread's stack *below* an engine frame: our code called into engine code
that took the locks. A hook that only passes the engine's work through is registered with `LockOrder::IgnoreFrames`
so it does not count (the main loop, the VM update, `HookUpdateAI`, `HookCheckForNewPackage`, and since 2026-11-04
`HookApplyMovement`'s pass-through and `WorldEvents::HookSendEvent`).

## The 2026-11-04 playtest's reports (12 on the host, 13 on the guest)

The host's `report_count` is 12 (the triage's 17 is not in this evidence). Every report pairs engine call sites taking
engine locks; none takes a lock of ours. The guest's 14 patterns are the same families (7 shared with the host, the
others other sites of the same list, alias and quest locks, not read one by one), none tagged `[ours]`. Read in IDA (1.7.104):

| Pattern | Locks | Who |
|---|---|---|
| `bhkWorld::Func50` (77851, Havok world write) > `ExtraDataList::GetExtraData` read, vs `sub_1401700F0` (list write) > `bhkRigidBody::SetMotionType` (world write) | Havok world, a ref's extra-data list | engine: trigger events (`TESTriggerEnterEvent` sink > VM `SendEvent` > `GetUniqueIDs`) run inside the world's write lock; `sub_1401700F0` applies and removes extra type 60 under the list lock, from `Job_IO`, and reaches `SetMotionType` the other way. Tagged `[ours]` once only because `HookSendEvent` was on the stack |
| `MovementPathManagerArbiter::sub_90873` (write) > `sub_140818C50` read, vs `sub_140818570` (write) > `IMovementQueryPathingState::Func9` read | path arbiter, the lock at `sub_140818570`'s `a1+0x160` | engine: an actor's `ApplyMovement` (37356) path step vs the update the combat manager job reaches. Tagged `[ours]` only because `HookApplyMovement` was on the stack |
| `ExtraAliasInstanceArray::sub_12720` read > `Actor::IsGhost` read (the actor's list lock), vs `ExtraDataList::AddAliasInstance` (12046: list write) > `ExtraAliasInstanceArray::sub_12718` write | a ref's extra-data list, its alias array | engine: `Actor::CastPermanentMagic` walks alias spells (`CastAliasSpellVisitor`, 38846) with the alias array read-locked and its casts read the actor's list; `BGSRefAlias::InitializeRefAlias` takes them the other way. `[ours]` because our respawn called it (`DeathService::OnRespawnArrival` > `PlayerCharacter::Revive` > `CastPermanentMagic`), the same function the engine calls from `Actor::Load3D`, `Set3D` and `SwitchRace` |
| `ExtraAliasInstanceArray` (12720/12727/12728) vs `sub_140175910`, `sub_140165460`, `sub_140167450`, `TESQuest::sub_25049/25066/25083` | alias arrays, extra-data lists, quest locks | engine: quest alias fills during the wipe's load (11:37, thread 376) |

No report was live: no thread found the lock held from the reverse order, so none shows a thread waiting on an
inversion. They are not proved unrelated to stalls either. On the host none falls inside a stall; on the guest 8 of 13
fall inside one or within 50 ms of it (18:04:56-57, 18:11:38): job threads were doing cell and 3D attach work then, and
reports fire on first sight of a pair, so they mark load bursts, not the cause of a wait.

## What the checker itself costs

The hooks run on every engine lock call, player builds included. Per call: a thread-slot lookup, a scan of the
thread's held locks and an edge-table probe per held lock. A new caller pair captures one stack
(`RtlCaptureStackBackTrace`, slow under Wine) once; a report formats and flushes once per pattern. When probing
fails the 32 MB edge table is cleared on the thread that hit it (`table_resets`: 6 in the 8-minute playtest; a clear
measures 1-2 ms natively, so it explains no stall). Edges are keyed per lock object, so the table does fill: a simulation
of `LockOrderHistory::Key` runs out of probes only above 31% load, and the snapshot's `edges` counts since the last reset.
