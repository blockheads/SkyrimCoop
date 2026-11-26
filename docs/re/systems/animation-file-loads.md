# Animation file loads

How an actor's clip files get loaded, and why the lock a queued load names must outlive the load. Read in
IDA on 1.7.104, 2026-11-17, for the guest's freeze in the 2026-11-16 playtest.

## The queue

`AnimationFileManagerSingleton` (the pointer at 401579) loads clip files for whoever asks:

| Step | ID | What |
|---|---|---|
| Queue a load | 63969 / 63968 | Allocates an entry (0x68): `+0x38` the set's kind, `+0x48`/`+0x50` the callback and its argument, `+0x58` **the requester's lock**, `+0x60` live. Pushes `{priority, entry}` on the pending heap (64045) under the manager's lock (`+0x0C`). Returns the entry: the requester's handle. |
| The queue | | `+0x68` pending (a BSTArray kept as a max-heap by priority, size `+0x78`), `+0x80` the load under way, `+0x88` the finished loads (size `+0x98`). Typed in `engine/AnimationFileManager.h`. |
| Its job | 63980 | `Job_Animation_files`' update, every frame in the job list. It ends in 63997. |
| Callbacks | 63997 | First pass: **takes every finished live entry's `+0x58` lock** (`BSSpinLock::Lock`), with no manager lock held. Then the manager's lock, each live entry's callback, then it unlocks the locks of the entries **still live**, and drops the finished list. |
| Cancel | 63971 → 63986 | Under the manager's lock: drops the load under way, takes the entry off the pending heap (64014), or clears a finished entry's live byte so 63997 skips it. It compares pointers only, so a stale handle is harmless. |

Callers (`coop_LoadEventClips` 43621 and its variants 43622, 43624) pass their own lock. The engine's own owners
cancel before that lock goes:

- `MovementControllerNPC`'s destructor (41692) takes its selector's lock, cancels its load (`+0x1A8`), then frees itself.
- `AIProcess`'s action load (`AnimationSets +0x60`) is cancelled by 39258 when an actor leaves high or middle-high
  (`Actor::MoveToMiddleHigh`). The instant interaction's (`+0x80`) is cancelled by 39798 (`MoveToLow`, `MoveToMiddleLow`).
- Duo, 2026-11-17: an engine load (kind 2) and our action load (kind 3, kept at `+0x60`) queued under a copy's process
  lock were both gone the moment the copy was deleted.

## The freeze it explains

If a load is still queued when its lock's memory is freed, 63997 later takes a lock in freed memory. Freed memory
that still reads `{0, 0}` is taken, written and left. Memory that was reused reads as held by another thread, so
63997 spins forever. The frame waits for that job (`Main::Update` 36555 waits on job event 2), and the game freezes
with no crash.

The guest's freeze of 2026-11-16 (Josh's game, `logs/tp_client.log` of `SkyrimCoop-player-2026-11-17-1910-2fe.zip`):

1. The host's temporary NPCs were removed, and the guest deleted their copies at 21:38:56.002.
2. The next frame never ended. The watchdog's stacks: the main thread in 36555's `WaitForJobTask(0, 2)`, and one job
   thread in `BSSpinLock::Lock` called from 63997 (`0x140be9853`, an entry's lock).

Before the fix, the copies' movement idle sets (`ActionClips`, kind 47, which have no process slot) were loaded with
the copy's `MiddleProcess::animationLock` (`+0x1E8`). The handle was thrown away, so nothing cancelled the load when
the copy was deleted.

## What we do

A cancel does not make the lock safe to free (Codex's review of PR #71). 63997's first pass may already have read the
entry's lock pointer, or taken the lock, before the cancel clears `+0x60`. It still takes the lock, then skips the
unlock because the entry is no longer live. Freeing the lock after the cancel is a use after free; keeping it leaves it
held forever. The engine's own cancels share this window. We stay out of it by never cancelling.

`HeldClipLoads` (`sync/animation/HeldClipLoads.h`) holds a copy's movement sets:

- **Its own lock**, which outlives the copy.
- **Each queued load's callback reference.** The queue keeps only the pointer (63968 writes it to `+0x40` without
  taking a reference), so it is released at the holder's next request, not inside the engine's call.
- **No cancel.** A newer load leaves the older one queued, and only the newest load's set is kept (each load is
  numbered).
- **A gone copy's holder** is freed at a later request, once none of its loads is queued. Its last load's callback ran
  under its lock, and the sweep takes that lock after 63997's unlock, so the job no longer points at it.

`GameTests`: "A copy deleted while its movement idle's clips load leaves no load on its lock" (the fake's loads check
their lock, `FakeLoadLock`). `UnitTests`: `[held-clip-loads]`.

Duo: `copy_deleted_while_clips_load`. The harness's `clip_loads` holds the guest's 63980, so the copy's turn load
stays queued while the copy is deleted. It then lists each queued load with the actor whose process lock it holds.
Whether a freed lock hangs depends on reuse, so the scenario checks that no live load names a gone actor's lock.

### The freeze itself, with the heap quarantine (duo, 2026-11-17)

The quarantine (`SKYRIMCOOP_HEAP_QUARANTINE`, verb `heap_quarantine` before `duo_start`) fills freed blocks with
`0xDB`, so a lock in freed memory always reads as held. The same steps (`hold_clip_loads`,
`copy_goes_while_turn_loads`, release the hold):

- **Before the fix:** the guest froze. The watchdog's stacks are the playtest's: the main thread at `+0x6568f8`, a job
  thread in `BSSpinLock::Lock` (`+0x1b4e0a`) from 63997 (`+0xbe9858`).
- **With the first fix** (a cancel before release): three copies deleted with their turn load queued, no stall; the
  queued load named no actor's lock. That build still had the cancel race above, which the duo's hold cannot reach
  (the job is stopped while the copy goes); the no-cancel version is checked by the GameTests flow (a second copy's
  request while the gone one's load is queued) and a threaded probe of 63997's order under ASan.
- **With the no-cancel fix,** quarantine on: the scenario passes, and two copies deleted while each had a turn load
  queued (two loads left, neither cancelled) were released with no stall; the guest's main loop ran on.

## A request under a graph's lock (2026-11-22)

Read in IDA on 1.7.104; the deadlock measured on the duo (`guest-ivarstead-jail-mouse`, the hang watchdog's stacks).

```text
63997 (Job_Animation_files, 63980)        takes every finished load's caller lock (+0x58: a process's animationLock)
                                          then the manager's lock (+0xC), and runs every callback holding both
  a callback: Actor::OnItemEquipped 37373 -> 37974 -> SetGraphVariableInt 32886 -> 32932: the graph manager's lock

63358 BSAnimationGraphManager::Update     holds the graph manager's lock (+0xA0) through the graph's update
  our HookUpdate -> GraphStreams::Replay -> ActionClips::Request
    the process's animationLock, then 43621 -> 63968: the manager's lock (+0xC)
```

Both of our orders cross 63997's: the graph lock before the animation lock, and before the manager's. The two jobs
run at once (measured: `RunOneActorAnimationUpdateJob` 41453 threads beside 63980's). The engine's own callers of
43621 are 39625 (the action's), 39291 (an instant interaction's) and 41694 (a movement idle's) (read). Whether any
of them can run under 63358 is not established: their callers were not walked up to a graph update, so the engine
may hold the same inversion.

- **Measured:** thread 436 in 63358 for copy `A8C9C` waited in `ActionClips::Request` for the animation lock;
  thread 440 in 63997 held it and waited in 32932 for the graph lock, owner 436 (`waits for spin lock 0x27add630:
  owner thread 436`). Our equipment reconcile had equipped the copy just before (the equip's clip load is the
  callback). Four more animation threads waited in 63968 for the manager's lock.
- **The fix** (`sync/animation/DeferredClips.h`): the stream's request is queued for the frame's start
  (`MainLoopTasks`), where no job runs; the replay already waits on the load's flag, which now exists before the
  load does. A request whose actor is no longer a copy at the drain (released as the session ended) is dropped:
  the action's load would cancel that actor's own (39258). Tests: GameTests "A copy deleted while its movement idle's clips load leaves no load on its lock" (the
  fake counts requests made inside a graph update: `ClipRequestsUnderGraphLock`).
- The lock-order checker sees none of this: it tracks `BSReadWriteLock`, and these are `BSSpinLock`s.

