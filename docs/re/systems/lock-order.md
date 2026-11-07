# Lock order: what the checker's reports mean

`BSCore/LockOrder.cpp` hooks the engine's `BSReadWriteLock` calls (68233/68234, unlock 68239/68240) and reports two
threads taking the same two locks in opposite orders, the first time it sees it. A report is history, not a wait.
`[ours]` means our DLL is below an engine frame on that stack; pass-through hooks are excluded with
`LockOrder::IgnoreFrames` (main loop, VM update, `HookUpdateAI`, `HookCheckForNewPackage`, `HookApplyMovement`'s
engine path, `HookSendEvent`, the alias name hooks around 24011 and 24012). Its list once held 8 of the 10 hooks and dropped the
rest silently, so the engine's own orders read as ours and failed `party-buys-house-host`: the alias walk 39213 > 37172 > 12721 >
11737 (`[ours]`: its frames were pass-through) against `ClearRefAlias` > 12047 > 12719. Now 32, a critical log past that, and
a harness unit counting the calls.

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

## Reports read

The 2026-11-04 playtest's reports (12 on the host, read in IDA 1.7.104) are all the engine's own orders, none live:

- Havok world (`bhkWorld` 77851) against a reference's extra-data list: trigger events reach the VM inside the world's
  write lock; `Job_IO`'s motion-type change takes them the other way.
- Path arbiter (90873) against the combat manager job's lock.
- A reference's extra-data list against its alias array: `CastPermanentMagic`'s alias walk (38846) against
  `ExtraDataList::AddAliasInstance` (12046). Our respawn calls it, as the engine does from `Load3D` and `Set3D`.
- Quest and alias locks during the wipe's load.
