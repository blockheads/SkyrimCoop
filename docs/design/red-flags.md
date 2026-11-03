# The sync red flags: plan (2026-11-02)

Status: **1 done (2026-11-02); 2 fixed (uncommitted); 3-5 open.** Five issues found reviewing actor and physics sync (the 2026-11-02 handoff, item 6),
looked at in code on 2026-11-02. Each is small on its own. Two are measurements that decide whether code changes at
all. Work them in the order below, each through the bug-fix flow (CLAUDE.md) where it changes behaviour.

Before starting, read:

- CLAUDE.md;
- memories `sync-model-decisions`, `bug-fix-flow`, `duo-reservation`, `net-check-seldom`, `no-commit-without-ask`;
- `docs/design/copy-clock.md` (clocks and send rates);
- `docs/design/checked-actions.md` (the harness's checked actions: `ctx.act`, `ctx.act_both`, `wait_until_settled`).

```
order   flag                                   kind        risk
1       actions stamped with the raw clock      fix         low
2       the host's tick clamp                   measure     none, then maybe a fix
3       SaveAnimationGraphState per action      measure     none, then maybe a fix
4       two rest paths for a body               fix         medium
5       a guest's movement sent reliably        fix         medium (needs net_check, the user's OK)
```

## 1. Actions on the frame clock

```
owner's game, each frame
  movement snapshot.Tick = World::GetFrameTick()     smooth: steps by the frame (FrameClock, S10)
  action.Tick            = World::GetTick()          raw: the session time as read, jumps 27 then 6 ms   <- here
copy's game: InterpolationSystem plays both on one timeline (CopyClock); an action plays when the copy's
  playback tick passes action.Tick, so a raw stamp lands a frame or two off its movement
```

- **Fix:** `Code/client/Games/Animation.cpp:53` `action.Tick = World::Get().GetFrameTick();`. Check every other place
  that stamps an action or replay event (`grep -rn "GetTick()" Code/client/Games/Animation* Code/client/Systems`).
- **`CopyEase`:** `CharacterService.cpp:1452` (`currentTick = m_transport.GetTick()`) only has to be consistent with
  itself. Leave it, or move it to the copy clock if the change is trivial.
- **Test:**
  - ClientTests if a pure helper takes the tick;
  - otherwise a GameTests flow: an owner's action and its movement in one frame replay together on the copy. The
    fake's session time can be made to jump the way Skyrim's does (S10).
- **Duo:** `copy-jumps-with-owner`, `copy-strafes-with-host`, `copy-strafes-with-guest`, `host-kill-move`.
- **Done (2026-11-02).** The hook and the fake's actions stamp with `GetFrameTick()`; `FrameClock::Now` is atomic
  (read in the animation jobs). The fake learned S10 (`FakeEngine::FrameMs`: steady engine frames, our update 0 or
  11 ms into each). GameTests "A player's actions play on its copy where its movement was when it acted", 12
  actions at 600 units/s (10 a frame), where the copy's movement stood at each action's stamp against where its owner
  acted: raw stamp -2 to +7 units (15 ms spread), frame stamp -7.6 to -6.7 (under 1.5 ms).
- **Open:** a steady 11 ms early. An action the engine performs before our update is stamped with the previous
  update's frame tick, its movement with this one's. Even, so unseen; stamping with the coming frame needs to know
  whether our update ran this frame (an engine frame counter). None of the four scenarios measures the offset.
- **Duo (2026-11-02), with and without the fix:** `copy-jumps-with-owner`, `copy-strafes-with-guest` pass;
  `host-kill-move` fails on both after its kill move, on the wolf copy's `ai_state.head_track` (a session id the owner
  lacks); `copy-strafes-with-host`'s stutter is flaky on both (2/8 without, 2/10 with: the open S10 item).

## 2. The host's tick clamp: measure first

```
HostCharacters::OnMovement (HostCharacters.cpp:52)
  notify.Tick = min(acRequest.Tick, m_network.GetTick())     "never in the future"
  a guest's clock a little ahead -> its stamps flattened to the host's now -> uneven gaps -> copy jitter?
```

- **Measure:** count clamps, and how far, per guest (a log line every 10 s with the count and the largest cut).
  Run `copy-strafes-with-guest` and a 2-minute `coop_test.py play --control guest` or walking scenario.
- **If it clamps often** (more than a few percent, or by more than one frame), drop the clamp and rely on
  `CopyClock`, which never plays ahead (`copy-clock.md`). Test: HostAuthorityTests (stamps pass through) and a
  GameTests flow where a guest's clock runs ahead and its copy still moves evenly.
- **If it rarely clamps,** record the numbers in `copy-clock.md` and leave it.
- **Measured (2026-11-02, a counter in `OnMovement`, not committed):** about 2.5 minutes of
  `copy-strafes-with-guest`, `party-stacks-on-arrival`, `party-uses-door`, `guest-bump-moves-host` (3 runs each, all
  pass). The guest: 1170 of 4857 snapshots clamped (24%; 30% in walking windows), mean cut 8.9 ms, largest 179 (100-106
  through most of the run); 673 gaps (14%) changed by more than 1 ms. The host's own route: 64 of 4812 (1.3%),
  mean 41 ms, largest 139, nearly all around loads. The host's time is its last update's, up to a frame stale,
  so a guest stamping its frame's time is often ahead of it.
- **Fixed (2026-11-02):** the clamp is gone; `OnMovement` passes the owner's stamp on. HostAuthorityTests "Movement:
  a guest's stamp reaches the other games as it sent it even ahead of the host's time"; GameTests "A guest's player
  moves evenly on the host's game though its clock runs ahead": steady frames, the two games' updates interleaved, the
  guest's clock synchronized 60 ms ahead (in-process delivery takes 2-4 frames, so less never arrives ahead). Without
  the fix 62 of 92 snapshots reached the copy with a cut stamp (more often than Skyrim's 24%) and 30 of 179 frames
  stepped over a third off; with it, none. Why Skyrim's guest runs ahead is not known (the guest logs neither its
  ping nor its syncs); the ping's frame waits are the guess.
  Duo: `copy-strafes-with-guest` 8 of 9 with the fix (one run a single frame off), 6 of 6 without in the same
  session; `party-uses-door` 3 of 3.

## 3. `SaveAnimationGraphState` on every player action: measure first

```
Animation.cpp:89  pActor->SaveAnimationGraphState(action.GraphState)   on each accepted action, owner's game thread
                  ~7.9 KB for DefaultMale ("Graph state DefaultMale: loaded true, read 7863 of 7863 bytes")
```

- **Measure the cost:** a profiler zone around the call (`Diagnostics/Profile.h`, Tracy:
  `coop_test.py profile --stacks`). Look at the time per call and per second in combat, and the bytes sent per
  action (`MessageTrail`).
- **Measure input-to-motion delay:** the frames from a press to the player's own motion, as host, as guest, and in
  vanilla (`coop_test.py launch --vanilla`, `player_trace.on`). The checked actions give a frame-accurate press: an
  `act` with `buttons` and a watch of the player's position (`z`, or `distance` from its start), with `series`.
- **Decide from the numbers:**
  - Over about 0.5 ms a call, or a guest delay vanilla doesn't have: save it only when a copy needs it (the action
    has no target event and the copy's clip can't follow), or move it off the action's frame.
  - Otherwise record the numbers in `copy-clock.md` and leave it.

## 4. One rest path

```
host: a body comes to rest
  HostPhysics -> NotifyBodyRests -> guest BodyService::OnRests -> BodyBridge::Rest            (path A, between frames)
  HostWorldState move row (kMove) -> GuestWorldState -> GuestEngine::PlaceAtHostRest
                                     -> HoldBodies, later FreePlaced                          (path B, mid-frame)
seen 2026-11-02 for one ladle: "BodyService: placed the owner's rest of 32AFF" AND
                               "GuestWorldState: applied ... move 32AFF"
```

- **Why it matters:** path B writes a loaded body mid-frame, which is the class of the green flashes (S2) and of lost
  contacts. Two writers for one pose can also disagree.
- **Target:** a loaded reference with dynamic bodies takes its rest from path A only. Path B keeps the save record
  and references that aren't loaded (or have no 3D) here, where A can't reach.
- **Files:**
  - `Code/client/Bridges/GuestEngine.cpp:531`: `kMove` goes to `PlaceAtHostRest`;
  - `Code/client/Bridges/GuestEngineBodies.cpp` (`PlaceAtHostRest`, `HoldBodies`, `FreePlaced`);
  - `Code/client/Services/GuestService.cpp:118`;
  - the host side in `Services/Host/HostWorldState*`;
  - `docs/design/world-state-sync.md` (move rows) and `physics-sync.md`.
- **Bug-fix flow:**
  - a scenario that shows both paths firing for one rest, with a check that the guest's body is written once, in the
    scene-write phase. `BodyTrace` and the change log show writes.
  - GameTests: the fake counts both writers (`FakeEngine::Body::SceneWritesDuringJobs` and the rest counters exist).
- **Duo:** `world-changes-reach-guest` (the world oracle), `clutter-rests-at-any-frame-rate`, the physics set,
  `late-join-gets-cell-state`.

## 5. A guest's movement goes unreliable

```
guest: TransportService::SendPacket (TransportService.cpp:173)  every request kReliable          <- here
host:  HostCharacters::OnMovement (:72)                          relay kUnreliable, kReliable if any actor in
                                                                 the batch has an action
network: Net::Server and Client, 2 ENet channels: 0 reliable, 1 unreliable (Server.cpp:56, 228-240)
         unreliable is on its own channel, so a lost reliable packet doesn't hold it up
```

- **Why it matters:** about 30 reliable movement messages a second. On loss, every later one waits for the resend, so
  copies freeze and jump: a candidate for "lagging out when moving side to side" (S10, Fabien's notes).
- **Fix:**
  - The guest sends `ReferencesMoveRequest` unreliably unless it carries action events, the host's own rule. A
    `SendPacket` overload with flags, or a per-opcode choice.
  - Do the same for `BodyFramesRequest` (held frames). Its release stays reliable (`BodyReleaseRequest`).
  - Check that `Client.cpp` maps the flags to the same two channels.
- **Ordering:** copies replay movement and actions by tick, not by arrival, so splitting channels is safe. Check that
  an unreliable snapshot older than the newest is dropped (`InterpolationSystem`) rather than played back.
- **Tests:**
  - TPTests or ClientTests for the flag choice;
  - GameTests through `UdpRelay` (it can drop packets): with 5% loss, a guest's copy on the host moves without the
    stalls the reliable path shows.
- **Duo:** the movement and copy scenarios. **`scripts/net_check.sh` only with the user's OK** (memory
  `net-check-seldom`). It's the tier that shows the gain.

## Where things stand (2026-11-02)

- **Done the same day:**
  - `3b8124d0`: copies step through `Actor::Move`, and a held-still body is not at rest;
  - `6883e9b7`: checked actions;
  - `6b0300c9`: exclusive holds;
  - `826eec44`: the throw scenario's ready gate.
  - Next list: `docs/progress.md`, "Next (2026-11-02)".
- **Known flaky on committed code** (don't chase while doing this):
  - `actors-walk-after-arrival`: the host's own NPCs jump;
  - `knockdown-lies-as-host`: about 5 in 7;
  - the `head_track` desync before a baseline (`docs/bugs.md`).
- **After this plan:** the handoff's cleanup (item 7), then the explainer docs (item 8).
