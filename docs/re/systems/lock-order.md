# Lock order: what the checker's reports mean

`BSCore/LockOrder.cpp` hooks the engine's `BSReadWriteLock` calls (68233/68234, unlock 68239/68240) and reports two
threads taking the same two locks in opposite orders, the first time it sees it. A report is history, not a wait.
`[ours]` means our DLL is below an engine frame on that stack; pass-through hooks are excluded with
`LockOrder::IgnoreFrames` (main loop, VM update, `HookUpdateAI`, `HookCheckForNewPackage`, `HookApplyMovement`'s
engine path, `HookSendEvent`).

The 2026-11-04 playtest's reports (12 on the host, read in IDA 1.7.104) are all the engine's own orders, none live:

- Havok world (`bhkWorld` 77851) against a reference's extra-data list: trigger events reach the VM inside the world's
  write lock; `Job_IO`'s motion-type change takes them the other way.
- Path arbiter (90873) against the combat manager job's lock.
- A reference's extra-data list against its alias array: `CastPermanentMagic`'s alias walk (38846) against
  `ExtraDataList::AddAliasInstance` (12046). Our respawn calls it, as the engine does from `Load3D` and `Set3D`.
- Quest and alias locks during the wipe's load.
