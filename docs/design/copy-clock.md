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
an owner sends (CharacterService::RunLocalUpdates, a round every 33 ms):
  its player, its ridden mount, actors in combat (CombatState): every round   30/s
  every other actor: every third round                                        10/s

this game, on each snapshot (OnReferencesMoveRequest; BodyService's ragdoll frames):
  CopyClock::OnSnapshot(owner tick, now)      lateness = now - owner tick (latency, jitter, clock error)
  CopyPlayback::OnPoint(tick)                 that copy's own gap between snapshots

each frame, before UpdateEvent (World::Update):
  CopyClock::Advance(now)
    network = worst lateness of the last 3 s + 20 ms, at most 400 ms   rises at once, falls when a spike leaves
    p steered toward now - network: speed 0.85..1.1, never back, never ahead; > 1 s behind: jumps
  a copy's tick (InterpolationSystem::PlaybackTick, its CopyPlayback, once a frame whoever asks first):
    p - its interval (the 3 s mean, or the newest 4 gaps' if longer; changes by <= 10 % of a frame per frame)
    used by: its movement (RunRemoteUpdates), its actions (AnimationSystem::Update), its ragdoll (BodyService)
  a teleport snaps it (even back)

past the newest snapshot (it is late):
  movement goes on its way for up to one interval (100 ms at most), then holds; not while lying or knocked
  actions are never guessed: replayed when reached
```

A snapshot is seen at the frame after it arrives, so the measured lateness includes up to a frame.

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
  - one tick a frame.
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
