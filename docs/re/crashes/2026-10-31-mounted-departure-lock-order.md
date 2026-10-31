# Mounted departure: extra data vs havok world lock order (2026-10-31)

Family `17A479 > E82682 vs E85437 > {17928D, 16B61D, 165105}`, reported `[ours]` in `mounted-guest-leaves`,
`leveled-placed-npc-is-hosts` and other travels (about 3,500 reports in older logs).

## The two orders

- Main thread, travel: `Main::Update` (36564) -> `ProcessQueuedTravel` (40438) -> 40744 -> `TES::SetCurrentCell` ->
  cell detach -> `DetachHavok` (19717) -> `RemoveExtraData` (12355, the reference's extra data lock for write at
  `17A479`) -> `~ExtraHavok` (19932) -> 77803 `HavokWorldEnterWrite` (the world lock, at `E82682`).
- Worker: `UpdateExteriorWorldJob` (18870) -> bhkWorld step (77851, world lock for write at `E85432`) -> trap listener
  (26053) -> `TriggerEntry` (26036) reads or writes a reference's extra data (`17928D`, `16B61D`, `165105`).

Same objects on both sides: one reference's `ExtraDataList` lock and the exterior world's lock. Neither is freed: the
reference only loses its havok object.

## Why it never deadlocks

- The travel's own world step (`TES::sub_13291`, from 40438 and 40744) queues the job, then `JobList::Begin` +
  `Finish`: the main thread waits for it.
- The per-frame step (`Job_Cell_animations`, 39067 -> 13291) never held the world lock during a travel's havok
  removal: a temporary watch on every `E82682` ask counted 220 per game across five travel scenarios, 0 with the lock
  held by another thread. The checker's overlap counts for this family: 4 asks per game, 0 contended, 0 live.
- Our travel hook does not make the order: `HookProcessQueuedTravel` defers a frame or calls 40438 from the same caller
  on the same thread. A 2026-10-23 soak report has the pair with no travel hook on the stack.

Classification: engine order, history only. Evidence: `build/lock-probe-evidence/` of the lock-mounted-departure
worktree.

## The checker since

Every inverted ask now checks whether another thread holds the asked lock from the reverse order
(`LockOrderHistory::OverlapOf`). Per-family counts are in the snapshot (`lock_order.overlaps`,
`coop_test.py locks`); a live overlap is its own report ("live lock order inversion") that fails any scenario, ours
or the engine's, and the reviewed-patterns list never covers it.

Applied (from the v1-copy-desync work): `LockOrder::IgnoreFrames(&HookProcessQueuedTravel, 0x358)` in
`PlayerCharacter.cpp`, as for `HookMainLoop`: the pair is no longer reported `[ours]`.
