# Animation file loads

How an actor's clip files get loaded, and why a load must be cancelled before the lock it names goes away. Read in
IDA on 1.7.104, 2026-11-17, for the guest's freeze in the 2026-11-16 playtest.

## The queue

`AnimationFileManagerSingleton` (the pointer at 401579) loads clip files for whoever asks:

| Step | ID | What |
|---|---|---|
| Queue a load | 63969 / 63968 | Allocates an entry (0x68): `+0x38` the set's kind, `+0x48`/`+0x50` the callback and its argument, `+0x58` **the requester's lock**, `+0x60` live. Pushes `{priority, entry}` on the pending heap (64045) under the manager's lock (`+0x0C`). Returns the entry: the requester's handle. |
| The queue | | `+0x68` pending (a BSTArray kept as a max-heap by priority, size `+0x78`), `+0x80` the load under way, `+0x88` the finished loads (size `+0x98`). Typed in `Games/Skyrim/AnimationFileManager.h`. |
| Its job | 63980 | `Job_Animation_files`' update, every frame in the job list. It ends in 63997. |
| Callbacks | 63997 | For each finished live entry: **takes its `+0x58` lock** (`BSSpinLock::Lock`), then the manager's lock, runs the callback, releases the locks and drops the finished list. |
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

`HeldClipLoads` (`Games/Animation/HeldClipLoads.h`) holds a copy's movement set the way `MovementControllerNPC` holds
its own:

- Its own lock, which outlives the copy.
- The pending handle, cancelled when a new load replaces it.
- When a copy is gone, its holder is cancelled before it is released (swept at the next request).

`GameTests`: "A copy deleted while its movement idle's clips load leaves no load on its lock" (the fake's loads check
their lock, `FakeLoadLock`). `ClientTests`: `[held-clip-loads]`.

Duo: `copy_deleted_while_clips_load`. The harness's `clip_loads` holds the guest's 63980, so the copy's turn load
stays queued while the copy is deleted. It then lists each queued load with the actor whose process lock it holds.
Whether a freed lock hangs depends on reuse, so the scenario checks that no live load names a gone actor's lock.

### The freeze itself, with the heap quarantine (duo, 2026-11-17)

The quarantine (`SKYRIMCOOP_HEAP_QUARANTINE`, verb `heap_quarantine` before `duo_start`) fills freed blocks with
`0xDB`, so a lock in freed memory always reads as held. The same steps (`hold_clip_loads`,
`copy_goes_while_turn_loads`, release the hold):

- **Before the fix:** the guest froze. The watchdog's stacks are the playtest's: the main thread at `+0x6568f8`, a job
  thread in `BSSpinLock::Lock` (`+0x1b4e0a`) from 63997 (`+0xbe9858`).
- **With the fix:** three copies deleted with their turn load queued, no stall. The queued load named no actor's lock,
  and a second copy's request cancelled the first's pending load (one load left queued, not two).
