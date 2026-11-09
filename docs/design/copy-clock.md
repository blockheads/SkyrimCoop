# The copies' clock

Status: **built 2026-10-28** (v1 checklist, "Adaptive playback"). A copy shows its owner as far behind as the line
needs, instead of a fixed 300 ms: on a good line a player's copy trails by about 60-80 ms and a 10/s NPC's by about
120 ms (it was 300 ms for all of them). Its movement, replayed actions and ragdoll stay on one timeline.

## What others do

- **Source / CS:GO** ([Valve](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking),
  [interpolation](https://developer.valvesoftware.com/wiki/Interpolation)): one interpolation time for every entity,
  `max(cl_interp, cl_interp_ratio / cl_updaterate)`, 2 snapshot intervals by default (100 ms at 20/s); a ratio of
  3-4 survives 1-2 lost packets; a short extrapolation (`cl_extrapolate_amount` 0.25 s) when snapshots run out.
- **Gaffer On Games** ([snapshot interpolation](https://gafferongames.com/post/snapshot_interpolation/)): a buffer of
  3x the send interval at 2-5 % loss (300 ms at 10/s, 150 ms at 30/s, 85 ms at 60/s); sending more often is the
  real lever; extrapolation looks wrong for physics.
- **Unity Netcode for Entities**
  ([ClientTickRate](https://docs.unity3d.com/Packages/com.unity.netcode@1.8/api/Unity.NetCode.ClientTickRate.html)):
  the interpolation time is steered, never jumped: time scale `0.85..1.1`, about 10 % of the error corrected per
  frame, jitter x 1.25, extrapolation capped at 20 ticks.
- **Overwatch** ([summary](https://edgegap.com/blog/game-backend-deep-dive-overwatch-2016-netcode-architecture-rollback)):
  time dilation (slightly faster or slower) instead of jumps.
- **Upstream** ([TiltedEvolution #921](https://github.com/tiltedphoques/TiltedEvolution/pull/921)): a clock per
  actor = its send interval + measured jitter on receive times, a brief extrapolation, teleports snap, the interval
  averaged over 3 s or the newest 4 gaps if longer (the stutter when an NPC leaves combat), 30/s for players, mounts
  and fights.

Ours takes Source's and Unity's one game-wide time (a player's hit and the NPC's stagger line up), adds upstream's
per-copy send interval (a 10/s NPC needs more buffer than a 30/s player) and Unity's steering.

## How it works

```text
an owner sends (CharacterService::RunLocalUpdates, a round every 33 ms by the frame clock, half a frame early:
at 60 fps every second frame):
  its player, its ridden mount, actors in combat (CombatState): every round   30/s
  every other actor: every third round                                        10/s

each frame, first (World::Update):
  FrameClock::Advance(session time as read, the engine's frame)
    steps by the engine's frame (BSTimer's realTimeDelta), leans 10 % a frame toward the session's time as read,
    jumps if 250 ms off; owners stamp their snapshots with it (World::GetFrameTick)

this game, on each snapshot (OnReferencesMoveRequest; BodyService's ragdoll frames):
  CopyClock::OnSnapshot(owner tick, now)      lateness = now - owner tick (latency, jitter, clock error)
  CopyPlayback::OnPoint(tick)                 that copy's own gap between snapshots

each frame, before UpdateEvent (World::Update):
  CopyClock::Advance(now = the frame clock, the engine's frame)
    network = worst lateness of the last 3 s + 20 ms, at most 400 ms   rises at once, falls when a spike leaves
    p steps by the engine's frame, steered toward now - network: speed 0.85..1.1, never back, never ahead;
    > 1 s behind: jumps
  a copy's tick (InterpolationSystem::PlaybackTick, its CopyPlayback, once a frame whoever asks first):
    p - its interval (the 3 s mean, or the newest 4 gaps' if longer; changes by <= 10 % of a frame per frame)
    used by: its movement (RunRemoteUpdates), its actions (AnimationSystem::Update), its ragdoll (BodyService)
  a teleport snaps it (even back)

past the newest snapshot (it is late):
  movement goes on its way for up to one interval (100 ms at most), then holds; not while lying or knocked
  the late snapshot: where the guess showed it less where it now goes, blended out over 100 ms (CopyCorrection)
  actions are never guessed: replayed when reached
```

A snapshot is seen at the frame after it arrives, so the measured lateness includes up to a frame.

**Why the engine's frame (S10, 2026-11-01).** The session's time (`TransportService::GetTick`: the host's last
`Server::Update`, a guest's `SynchronizedClock`) is wherever the network last updated it, and our update runs at a
different point of each frame (in the VM's update, after a varying amount of the engine's work). Read once a frame it
stepped 27 then 6 ms, or 39 then 2, while frames were a steady 16.4 ms. The clock stepped by those differences, and
owners stamped snapshots with them while their positions were the frame's: a copy strafing at 350 units/s moved 50
to 770 a frame (`copy-strafes-with-host`/`-guest`: 22-48 frames a third off its owner's steady move). Stepping by
the engine's frame (measured at the frame's start, steady) and stamping with the frame clock: none. The owner's own
movement matched vanilla throughout (the stand-in harness's player trace: 5.8 units a frame, a turn in two frames);
a copy turns over one snapshot interval later than its owner. Rounds were timed by the wall clock at our update, so at
60 fps they fell every third frame (49 ms, 20/s: 33 ms is just over two); timed by the frame clock, half a frame early,
every second (33 ms). A copy's turn takes 49-66 ms, was 66-82 (its owner's 33), and its animation's floats, sampled at
the nearest snapshot, are off by at most half as much. A stuttering frame or two still shows in some strafe runs at
either rate (open).

**Its animation turns with it.** A copy's graph floats match its owner's (`Direction` 0.25/0.75 for a strafe right
or left, `Speed`), but they were the current snapshot's while its movement already blended toward the next: at every
turn it slid the new way in its old strafe for a snapshot (22-33 ms on average, the trace's `floats`). It now takes the
nearest snapshot's floats (past halfway, the next one's: what moved the owner there; booleans and integers stay the
current one's, graphs react to their changes): about 10 ms. Tried and dropped: sending a snapshot at once when the
owner's graph steps (a frame, but the uneven gaps made copies stutter).

## Compared with other games (2026-11-02)

- **Buffered snapshot interpolation**, as Source (a fixed 100 ms "lerp", two update intervals so one lost packet
  is covered) and Glenn Fiedler's snapshot interpolation (enough delay to lose two packets). Ours adapts instead: the
  worst lateness of the last 3 s, a 20 ms margin and the copy's own send interval; a lost relay (the host relays
  movement unreliably) leaves a gap the short extrapolation covers.
- **A steered playback clock** (0.85-1.1 speed, a jump past 1 s), as adaptive jitter buffers do, so a delay change
  never jumps or reverses a copy.
- **Stamped at the frame**, not at the read: owners stamp snapshots and actions with the frame clock and copies step with the
  engine's frame, as engines stamp with their tick (S10, above).
- **Short extrapolation** (up to one interval, 100 ms at most; Source allows 250 ms), never while lying or knocked.
- **Linear, not Hermite.** Fiedler's Hermite spline (velocity in each snapshot) smooths speed across samples; a
  player's strafe reverses within two frames, which a curve would round off, and steady movement is already exact
  under linear blending. Left as an option if curved paths (turning at a run) show kinks.
- **No visual correction offset.** Unreal snaps a simulated proxy's capsule and eases its mesh offset back; copies
  are placed on the interpolated path every frame, so there is nothing to correct except a ragdoll's end
  (`CopyEase`).
- **Animation from the owner's graph.** Engines usually drive a remote character's locomotion from its replicated
  velocity; copies load their owner's graph variables instead (mod behaviours too), floats from the nearest snapshot
  so animation and movement turn together.
- **skymp** (another Skyrim multiplayer) extrapolates the newest position 0.2 s ahead and lets the engine glide there
  (`TranslateTo`), its locomotion animated by the engine (`KeepOffsetFromActor`): less delay, but paths overshoot
  and correct. Copies follow the owner's path instead.
- **In the frame** (`actor-movement.md`, "Where a copy's step falls in the frame"): the job table, read in IDA
  2026-11-09, lists `Job_Actor_movement` (39053), then `Job_VM_update` (39074; ours: goals set, placed copies placed,
  graphs loaded), then `Job_Cell_animations` (39067; Havok's step); that they run in that order within one frame is
  inferred from the list's sync entries, not read. A stepped copy reaches its goal the next frame, a placed one the
  same frame (a constant lag; a frame held at a Place-to-Step switch); an owner's snapshot reads the previous frame's
  movement.

Sources: [Source multiplayer networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking),
[Source interpolation](https://developer.valvesoftware.com/wiki/Interpolation),
[Snapshot interpolation](https://gafferongames.com/post/snapshot_interpolation),
[Unreal SmoothClientPosition](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UCharacterMovementComponent/SmoothClientPosition),
skymp's `skymp5-client/src/sync/movementApply.ts` (`reference/plugins/skymp`).

## Measuring smoothness (2026-11-09)

The MCP's `smoothness` tool (`coop_harness/smoothness.py`): the mover's player walks, runs, strafes and runs a curve by
real input while both games trace; per segment, each one's speed per engine frame (the trace's `dt`, BSTimer's
`realTimeDelta`: our update's wall clock is uneven, S10) against its neighbours' (median, p95, share of frames over
20 % off), turn-backs, the copy's fitted delay and distance off its owner's path, and the movement and body messages'
bytes/s on both games. The owner's own motion is the floor.

Baseline on the duo, then after the fixes below (copy frames over 20 % off, owner's in brackets):

```
                         before        after
walk, host moves         35 % (7 %)    7 % (7 %)
walk, guest moves        15 % (5 %)    3-6 % (4-7 %)
a standing snapshot      112 B         50 B
```

What the baseline showed and what changed:

- **Positions went in whole units, truncated.** A walking copy (84 units/s, 1.4 a frame) stepped 0.9 to 1.55 units a
  frame, and sideways not at all, then half a unit at once (the owner's y moved 0.05 a frame). Now quarter units,
  rounded, in the same 64 bits (`Vector3_NetQuantize`: x and y over 22 bits, ±524288; z over 20, ±131072). GameTests
  "A walking player's copy steps as evenly as its owner" (18 frames off with whole units); TPTests.
- **Physics on the frame clock** (S10 for bodies): `physics-sync.md`, "Following the host smoothly".
- **A late snapshot is blended, not jumped.** Past its newest snapshot a copy goes on its way; a snapshot lost at a
  turn made it overshoot and the late one put it back in one frame (a copy moving against its owner in the strafe
  trace). `CopyCorrection`: from where the guess showed it, over 100 ms; past 64 units (`CopyStep::kSnap`) or at a
  teleport it jumps. A teleport or a refreshed spawn (an arrival) drops the guess and correction with the points
  (`InterpolationSystem::Restart`; GameTests "A refreshed spawn drops a copy's guess and correction with its points"). ClientTests `[copy-correction]`; GameTests "Over a lossy line a strafing player's copy never jumps
  back where its guess overshot" (up to 23 units in a frame before).
- **Smaller snapshots.** The graph variables were diffed against nothing, so every integer and float went each time;
  now against zeros of their sizes (only those set travel; the reader already fills zeros), and `Direction` in 16 bits.
  A walking humanoid's snapshot is 64 bytes (TPTests), a standing one 50 on the duo.
- **What still costs:** a moving player's snapshots average 525 to 750 bytes (17 to 20 KB/s; 7.9 KB/s on the wire after
  Snappy), not for movement: each accepted action carries its whole graph state (12 to 15 KB raw, `red-flags.md`, 3).
  A 15-wolf fight: 76 KB/s on the wire to each guest (`wire_cost`). Parked with a plan: `docs/ideas.md`, "Graph state
  only when a copy needs it".
- `copy-strafes-with-*`'s stutter check measured speed at 60 fps; a watcher dropping to 43 fps read as a copy running
  40 % fast (most of its known flakiness). It now divides by each frame's recorded length. Still flaky after all of
  the above (`copy-strafes-with-host` 3 of 5 on the duo, `-guest` 3 of 3): the copy turns as often as its owner and
  never back and forth, but its delay drifts 0.13 to 0.31 s within one run (the clock steering), which one fitted delay
  misreads at the turns.

## Readouts

The harness status: `copy_clock` (`network_ms`, `delay_ms`), and per copy `behind_ms` and `send_interval_ms`.
`scripts/re/graph_compare.py` samples each copy `behind_ms` after its owner.

## Tests

- ClientTests `copy_clock_tests.cpp`, on a simulated line:
  - a good line plays a little behind;
  - a jittery line never runs out;
  - a spike raises the delay at once and is forgotten after the window;
  - a stall jumps ahead, never back;
  - the delay stays bounded;
  - leaving a fight (30/s to 10/s) steers smoothly;
  - a teleport snaps;
  - one tick a frame;
  - steady frames step the copies steadily however unevenly the session's time is read;
  - the frame clock steps with the frames and follows the session's time.
- GameTests `copy_playback_flows.cpp`:
  - the extrapolation and its limits;
  - a copy well under 300 ms behind in-process (121 ms for a 10/s copy);
  - over an 80 ms ±40 ms relay a walking copy never steps back (the clock settles at about 140 ms);
  - players and fights are sent 30/s, others 10/s.
- Skyrim (2026-10-28, the duo on one machine, games off-screen at about 21 fps): 16 of 19 movement, combat,
  knockdown, death and travel scenarios pass. The three failures are in [bugs.md](../bugs.md) and are not the clock:
  - `knockdown-lies-as-host` fails 0 of 4 on the old timing too;
  - `client-melee-kill-credit` and `party-fast-travels` passed when rerun.

  Measured from the harness status, the guest's copy of the host's player:
  - **Delay:** 135-150 ms behind (median), against 300 ms on the old timing. The clock's network term is about
    95 ms here, mostly the two games' 47 ms frames; a send round can only happen on a frame, so a "30/s" actor sends
    every 47 ms at that frame rate.
  - **Guest's line:** up 2.5-2.8 KB/s (1.2 on the old timing), down 7-9 KB/s (3.8; physics bursts to 130 KB/s in
    knockdowns either way). This compares different scenario sets, so the numbers are indicative only.

**A player's own motion (2026-11-02).** From a real key press the player moves 2 frames later, as host, as guest and
in vanilla (red-flags.md, 3); saving the graph state on each action costs 0.05-0.15 ms.
