# Lock order: what the checker's reports mean

`BSCore/LockOrder.cpp` hooks the engine's `BSReadWriteLock` calls (68233/68234, unlock 68239/68240) and reports two
threads taking the same two locks in opposite orders, the first time it sees it. A report is history, not a wait.
`[ours]` means our DLL is below an engine frame on that stack; pass-through hooks are excluded with
`LockOrder::IgnoreFrames` (main loop, VM update, `HookUpdateAI`, `HookCheckForNewPackage`, `HookApplyMovement`'s
engine path, `HookSendEvent`, the alias name hooks around 24011 and 24012, the 3D job's ends `HookEndLoad3D` and
`HookFinishLoad3D` since 2026-11-23: 13023 is on the Havok world pattern below, which read as ours once it was hooked;
`LiveMenuHooks`' `UpdatePlayer` on 36581 too: a guest whose grid shifted by itself detaches cells under it, 19130 >
`DetachHavok` 19717 > 12355 > 77803, the list-first side of that pattern, seen in `guest-leash`). Its list once held 8 of the 10 hooks and dropped the
rest silently, so the engine's own orders read as ours and failed `party-buys-house-host`: the alias walk 39213 > 37172 > 12721 >
11737 (`[ours]`: its frames were pass-through) against `ClearRefAlias` > 12047 > 12719. Now 32, a critical log past that, and
a harness unit counting the calls.

Each hook's code is measured, not written (2026-11-22): `IgnoreFrames(&Hook)` takes the range from the hook's own unwind
entry (`RtlLookupFunctionEntry`, `RUNTIME_FUNCTION` Begin to End). The written sizes had drifted both ways in the
2026-11-16 build: `HookUpdateAI` was 0x5F against 0x50 (its call returns at +0x59, so its frame read as ours), and four
were long enough to cover the next function's start (`RunTasksInStretch`, `UpdateInStretch`, `HookScriptCalled`,
`JudgedIsTrue`), hiding our own frames there. A hook with destructors keeps its cleanup funclets in later entries; they run
only while an exception unwinds, never on a lock stack. A hook with no entry starting at it (a leaf, or one inlined
into another's entry) is a critical log and `lock_order.unmeasured_hooks`, which fails every lock audit. A report tagged
`[ours]` names the frame that tagged it (`ours from backtrace #NN`): a hook missing from the list shows there, which
measuring cannot catch (`HookNativeCalled`, 2026-11-20).

## A lock freed and used again (2026-11-23)

Pairs are kept by the two locks' addresses. An extra data list is freed and allocated again all the time, so a pair seen
on a dead list read as the pair of the new list at its address: `alias-item-guest-drops` and `party-item-moves-host`
reported the boot's quest alias fill (`TESQuest` 25083 under `coc`, a created reference's list copied into a container's)
against a drop much later (a new reference's list), both the engine's copies of extra data, which lock source then
destination (`ExtraDataList__MoveExtrasFrom` 11591 `0x14015A2F0`, `ExtraDataList__CopyExtrasFrom` 11590 `0x14015A0B0`, read in IDA).
Each lock now has a generation in the pair's key (`LockOrderHistory::Key`), bumped after `ExtraDataList__Destroy`
(11572, which the thunk 11584 and the deleting destructor 418612 call): a list at a reused address starts with no pairs.
Generations are kept by the lock's own address (`LockGenerations`), so destroying one list never moves another live
lock's (review of PR #118: a shared slot had erased a live lock's history).
Other objects holding locks are not tracked (none seen this way). ClientTests `[locks]`.

What that left (measured the same day, the report now naming each side's stretches): a quest's own reference (never
freed) picked up and later dropped copies its list into the inventory's and back, both in our update but on different
worker threads (436 and 440). Our update is one job a frame, finished before the next starts (above), so two of its
orders never overlap: `KeptApartByFrame` now keeps our update against our update apart too, and `StretchOverlap` checks
the claim each run (a second thread entering our update while one is inside turns `stretches_overlapped` true, and
nothing is kept apart for the rest of the run). `alias-item-guest-drops` and `party-item-moves-host` pass.

## Orders the frame keeps apart

A report needs two threads, not two threads at once. `BSReadWriteLock` spins with no timeout, so a real overlap of an inversion would hang for good; whether one
can happen is a question about the frame (read in IDA 1.7.104, 2026-11-07):

1. `HookMainLoop` (36564's entry) drains `MainLoopTasks` on the main thread: `GuestWorldState`'s quest updates run here.
2. Only then does `Main::Update` release the frame's job lists (`SetJobTaskEvent`, 69514; `BSThreadStagesManager`). `VM update` (`Job_VM_update` 39074) is entry 25 of the job
   table, and `World::Update` runs inside it, on a job thread (`HookVMUpdate`).
3. The render step (36555) waits, in job mode 1 (`JobListManager +108`, IDA's misnamed `coop_menuMode`), on event 2 from `Job_UI`, entry 33: after `Ragdoll signal/sync` 26,
   `JobListEnd` 30 and `Quests/Scenes update sync` 32. So the update has finished before the next frame's step 1.

So a main loop task and our update do not overlap. Measured with a temporary counter at step 1, through respawns, cell changes, a mount, a house purchase and rejoins: our
update was running 0 times in 32,700 frames (16,500 host, 16,200 guest). Not read: what the other job modes do. A save's freeze runs `ProcessRegisteredUpdates` too, and
`HookVMUpdate` skips our update while `isFrozen`. An inversion between our update and a job in the same stretch of the table has no such protection.

### How the checker tells them apart

Each thread carries the stretch of the frame it is in (`LockOrder::FrameStretch`): main loop tasks while `HookMainLoop` drains
`MainLoopTasks` (`RunTasksInStretch`), our update while `HookVMUpdate` runs `World::Update` (`UpdateInStretch`), elsewhere otherwise.
Every observed order (an edge, per pair of locks) keeps the set of stretches it was ever taken in, not only the first stack's.
An inversion is "kept apart by the frame" (`LockOrderAttribution::KeptApartByFrame`, ClientTests `[locks]`) only when this side's stretch is
one of the two and the reverse order was seen in the other one alone; elsewhere on either side, or the reverse also seen elsewhere, is a risk.

- The report is still logged, with `kept apart by the frame:` before the kind. The harness logs it and does not fail on it.
- It is reported apart from the risk: the same call sites met later from elsewhere make their own report, which fails as before.
- The live overlap check is unchanged: a kept-apart pair that ever overlaps is reported live and fails.
- The frame's claim is checked every run, not trusted: step 3 is read for job mode 1 only, and its "the update has finished" leans on
  the sync entries' names (inferred) and the measured count. Each stretch counts its threads inside it (`StretchOverlap`); if main loop
  tasks start while our update runs, or the other way round, a critical log says so, `lock_order.stretches_overlapped` turns true, and
  nothing is kept apart for the rest of the run. The harness then fails a kept-apart report as `[ours]`.
- An edge's history is OR'ed, also while it is being published, so a sighting from elsewhere at that moment is not lost.
- The helpers are out of line so what they run stays ours, apart from the pass-through `HookMainLoop` and `HookVMUpdate`.

The case that needed it (bugs.md, 2026-11-07, `guest-rides-host-horse` after a respawn in the same boot), read in IDA 1.7.104:

- Main loop tasks: `GuestEngine::ClearFills` > `BGSRefAlias::ClearRefAlias` 24012 > `TESObjectREFR::RemoveAliasInstance` 19653 >
  `ExtraDataList::RemoveAliasInstance` 12047, which writes the reference's extra-data list lock (list +0x18), then 12719 the alias array's (+0x28).
- Our update: `DeathService::OnRespawnArrival` > `PlayerCharacter::Revive` > `Actor::CastPermanentMagic` 38753 > 12720 (the alias visit, the
  array's read lock) > `CastAliasSpellVisitor` 38846 > `PermanentMagicFunctor` 34464 > `MagicCaster::FindTargets` > 38786 > `Actor::IsGhost`
  37275, a read of the same list lock (actor +0x88). Read in IDA: 12047, 19653, 12719, 12720, 34464, 37275 and the call from 24012;
  the rest of each chain is the duo's symbolized stacks.

On the duo (2026-11-07, `death-spectate-guest` then `guest-rides-host-horse`, one boot, both passed), the guest showed the add side of the
same pair: `GuestEngine::ApplyFills` > `TESQuest::ForceRefIntoAlias` 25052 > `BGSRefAlias::InitializeRefAlias` 24011 > `TESObjectREFR::AddAliasInstance`
19652 (read) > `ExtraDataList::AddAliasInstance` 12046 (list, write) > `ExtraAliasInstanceArray::AddSorted` 12718 (array, write), from
`RunTasksInStretch`, against the respawn's 12720 > 37275: reported `kept apart by the frame`. The respawn's own report, earlier in the
run, stayed a risk: that reverse order was also taken by the duo's start `coc` on the main thread outside the tasks (the reviewed
`lock_order_known-1.7.104.txt` entry for it).

## Reports read

The 2026-11-04 playtest's reports (12 on the host, read in IDA 1.7.104) are all the engine's own orders, none live:

- Havok world (`bhkWorld` 77851) against a reference's extra-data list: trigger events reach the VM inside the world's
  write lock; `Job_IO`'s motion-type change takes them the other way.
- Path arbiter (90873) against the combat manager job's lock.
- A reference's extra-data list against its alias array: `CastPermanentMagic`'s alias walk (38846) against
  `ExtraDataList::AddAliasInstance` (12046). Our respawn calls it, as the engine does from `Load3D` and `Set3D`.
- Quest and alias locks during the wipe's load.

2026-11-16, `party-buys-house-guest` on the host (read in IDA 1.7.104, each named that day):
`ExtraAliasInstanceArray::FindAliasPackageThatCanRun` 12721 (array lock, read) > `TESPackage::CanRunFor` 29619 (once per
day, `kOncePerDay` 0x400, then schedule and conditions) > `ExtraDataList::HasRunOncePack` (11737, `0x140165460`, the list
lock for read, looking for the package in `ExtraRunOncePacks`, type 0x1B) against `ClearRefAlias` > 12047 (list, write) > 12719 (array, write). The engine's own
pair again, under an AI job and the guest's alias clears. It read `[ours]` only because `HookUpdateAI`'s frame (+0x59)
lay past its written 0x50-byte size; measured since 2026-11-22 (above).

2026-11-22, with measured hook sizes (agent/hook-sizes), one boot each of `party-buys-house-guest`, `party-buys-house-host`,
`host-dies-guest-still-fought` and `host-guard-arrest-jail`:

- The alias/list pair above now reads engine-only on the host (`party-buys-house-guest` passed).
- The Havok world against a reference's extra-data list, at fast travel's cell load, in all four. Read in IDA 1.7.104
  and named that day: 12000 `ExtraDataList::SetActionRef` (`ExtraAction`, type 0x0D), 11837
  `ExtraDataList::SetMultiBoundRef` (type 0x60), 77803 `bhkWorldObject::SetProperty`, each under the lock the report
  names, and where 13676 takes the cell's world lock (+0x1b5bfa). The frames between are the duo's symbolized stacks.
  - World first: `bhkWorld::Step` 77851 holds the world's write lock (taken at +0x104ad57) while its callbacks reach the
    list lock: 26053 > 26036 > 12000, or 61410 > 25850 > 11968 > 12329. In the third pattern the world lock is
    `TESObjectCELL` 13676's (+0x1b5bfa, cell vfunc 0x140, +0x430), held through 77921 > 77851 > 61410 > 25849 > 11837.
  - List first: `ExtraDataList::RemoveExtraData` 12355 > 11576 > 19932 > 77803 on the main thread, or `Job_IO`'s
    reference 3D (13023 > 13355 > 19742 > 11979 > 19535 > 77883 > 19538) > `bhkRigidBody::SetMotionType` 78077.
  - Only pass-through hooks on the stacks, so they warn and fail nothing. Not added to the reviewed list: that would
    also hide a later `[ours]` report at the same sites. The `Job_IO` side runs beside the frame, so nothing keeps it
    apart: none was live.
- On the guest (`party-buys-house-host`), two reviewed load pairs read `[ours]`, rightly: `GuestEngine::ApplyFills` >
  `QuestEvents::HookForceRef`, and `CharacterService::OnCharacterSpawn` > `HookMoveTo` > 56626 > `HookForceState`, each
  against fast travel's alias fills (25083 > 24011). The new `ours from` line named those two frames.
- Not seen in these runs: the pair "After the host dies" named (12720 > 37275 against 12047 > 12719), and
  `host-guard-arrest-jail`'s guest pair of 2026-11-05 (`+0x10587a6 > +0x16a695` against `+0x17014d > +0x1058a00`, the
  same Havok/list family, which frame made it ours unread).
