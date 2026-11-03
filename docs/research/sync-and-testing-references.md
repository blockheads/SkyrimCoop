# How others sync physics and test games (2026-11-02)

Research from one session. It started with a playtest bug and went on to how other projects handle the same
problems. It records what we read, what we take, and what is parked. The code was read where it is noted; the rest
is from articles and docs, linked at the end.

## The bug that started it

```
the user, as the guest: grabbed an object, carried it about, held it against an NPC
  -> it slowly sank into her
short scripted scenarios (a swing into her, a bucket on her head) all passed
```

- **Cause** (`docs/re/systems/actor-movement.md`): copies skip `Actor::ApplyMovement` (37356) and are warped every
  frame by `ForcePosition` (`Actor::SetPosition` 37309). So Havok's character step never runs for a copy, and the
  step is the only thing that pushes bodies out of a capsule.
- **Reproduced by** `guest-holds-into-npc`, which carries the bucket about, then presses it against her for 30 s.
  - The guest failed 2 of 2: the bucket got 1.8 to 2.6 units from the copy's axis, and her capsule was shoved 43 to
    61 units off.
  - `host-holds-into-npc` passed 2 of 2, holding steady at 19.7.
- **Lesson:** our scenarios were too short to show it. Sustained pressure did.

## Test tooling (parked, not dropped)

**The idea.** An MCP-like layer, where Claude drives the duo as players: walk, turn, grab, look around, compare
both games. A bug gets recreated interactively, then turned into a scenario, then into a fast test. That's one
layer above the scenarios. No Skyrim MCP fits:

- SkyLink AI (Nexus 175682) reads game state but sends no player input and drives one game only;
- SkyrimNet exposes NPC AI state;
- Mutagen and the like edit plugin files.

**The gripe that stopped the first plan:** "you say an action, it happens, other state changes, and the thing you
wanted to happen didn't." Our actions are open loop: Python sends an input and hopes. Meanwhile the game runs on
between calls, and a failed intent is never reported. In this session alone:

- a walk into Lydia measured the carrier's own capsule, not the bucket;
- a sudden turn drops the held object;
- `place` leaves the player in the air for a few frames;
- NPCs walk off to greet.

**How others handle it.** An action is a goal the game itself checks every frame, and it ends with an explicit
outcome.

| Project | What it does |
|---|---|
| OpenMW integration tests (code read) | The test is a Lua coroutine in the player script: set controls, `coroutine.yield()` one frame, check. It doesn't advance while the world is paused. |
| Minecraft GameTest | `succeedWhen` re-checks every tick until a timeout. Each test runs in its own loaded structure. |
| Unreal automation | Latent commands (`FWaitUntil`, `FUntilCommand`) poll a condition every tick, with a timeout callback. |
| ROS actions | A goal, feedback while it runs, and an outcome of SUCCEEDED, ABORTED (gave up) or PREEMPTED. |
| mineflayer-pathfinder | The counter-example: an unreachable goal quietly stops at the nearest point and reports nothing. `goto()` fails with an error instead. |
| Voyager (Minecraft LLM agent) | A separate critic is given the task and the final state and decides whether it got done. |
| Malmo/MineRL, SimWorld | A synchronous (lockstep) mode keeps the game in step with the agent. |
| Factorio | Fully deterministic, so a click on tick N plays out the same every run. |

**OpenMW's patterns worth copying** (`scripts/data/integration_tests/`: `testing_util.lua`, `player.lua`,
`global.lua`):

1. **Steer every frame instead of sending a command.**
   - `rotate` sets `yawChange = (target − yaw) × 0.5` each frame.
   - The attack test re-reads the target, re-plans the path and re-aims every frame.
2. **Check what must stay true on every frame.** The attack loop checks `distance ≤ 100`, failing with "Target left
   melee reach", so the failure lands on the frame things changed.
3. **Wait until it's ready, with its own deadline.** Act only once the target is valid, both actors are on the
   ground, and the weapon is equipped and drawn, or fail "Attack setup timed out".
4. **Make the test the only thing that moves.**
   - The player's controls and looking are switched off (`setControlSwitch`).
   - The target's AI is off: a 3-line `idle.lua` calls `enableAI(false)`.
   - Every test starts at the same teleport spot.
   - Stats are boosted so hits can't miss, then restored.
   - Spawned objects are removed.
   - The frame rate is capped at 60.
5. **Two clocks.** Gameplay deadlines use simulation time, setup deadlines real time. The runner doesn't run
   while the world is paused.
6. **Wait for an outcome, not a duration.** "Until her health drops", else "Did not damage any targets in 10
   seconds".
7. **World setup and player steps are split.** The global script stages the scene and runs a player step, which
   reports back done or failed with a message.
8. **Failures explain themselves.**
   - A matcher returns its mismatch ("actual X, expected Y").
   - The runner prints `TEST_START`/`OK`/`FAILED <msg>`.
   - An error stops the run.

**What it would look like here.** C++ actions in the test harness, ticked from the frame hook, each one request in
and one outcome out:

```
act guest {
  ready:  on_ground, holding=bucket, no_menu
  do:     push_toward lydia
  until:  held_for 10s
  keep:   holding=bucket, lydia.moved<5, copy_gap<10
  watch:  min xy(bucket, lydia)
}
-> ok, or failed "frame 412 (6.9 s): not holding bucket (turned 9 degrees in a frame)" + watched values
```

- The host runs a matching watch on its side.
- **What OpenMW doesn't cover:** its tests are single-player. Checks across two games have to come from both games'
  traces, lined up by time.
- **Pausing both games between actions (lockstep)** is the expensive option. Both games and the network would have
  to pause together. A minimal scene (`freeze`, stage spots, an input lock) gets most of the effect.
- **Order:** checked in-game actions first (useful to scenarios at once), then the MCP layer on top of them. Next
  after copy presence (part A below), before exclusive ownership (part B), whose refused-grab scenarios need it.

## Sync references

**OpenMW is single-player.** Its multiplayer is TES3MP, GPLv3 like us, so its code can be copied. Morrowind's items
have no rigid-body physics, so neither project has any physics sync.

**TES3MP** (`apps/openmw/mwmp`, code read):

- **Copies move both ways** (`DedicatedPlayer::move`, `DedicatedActor::move`).
  - They're warped toward the sent position each frame: a lerp of `dt × 15`, snapping past 80 units for players
    and 40 for NPCs.
  - The direction is also written into the engine's movement settings, so the engine animates and steps them.
- **Cell authority.** The first player in a cell runs its NPCs (`Cell::setAuthority`), and the code turns copies
  into owned actors and back when that player changes. We keep the host owning every NPC (decided 2026-11-02).
  The hand-off is the same problem as objects changing owner.
- **Copies are stripped of their own will:** fight, alarm, flee and hello set to 0, AI packages cleared. We go
  further already.

**PLANCK** (`reference/plugins/activeragdoll/src/main.cpp`, code read):

- **A character's own step is what pushes bodies.** `objectInteractionCallback` and `processConstraintsCallback`
  on the character proxy; PLANCK scales the impulse.
- **`Actor_SetPosition_Hook`:** after a warp it ignores that actor's collision for a moment
  (`disableActorOnSetPositionTime`), because a warp puts its bodies inside things. Our copies warp every frame.
- **Its collision filter callback decides per pair of layers** (biped, character controller, HIGGS's held layer).
  HIGGS's held objects ignore NPC controllers unless the NPC has no ragdoll in the world.
- **It adds live actors' ragdolls to the world near the player** (`AddRagdollToWorld`): keyframed bones driven to
  the animation pose, so objects meet the actual body.
  - This is our fallback for copies, if `Actor::Move` on copies fails.
  - Not planned. It would also change vanilla's behaviour for owners.

**Glenn Fiedler, "Networked Physics in Virtual Reality":**

- **Authority and ownership are separate.**
  - Authority belongs to the last player who touched a body, and a thrown body takes authority over what it hits.
  - Ownership belongs to the holder, and nobody else can take it until they let go.
- **Two sequence numbers per body**, for authority and ownership. Ownership beats authority, and stale updates
  lose.
- **The host arbitrates and relays.** It accepts a guest's claim or rejects it with a correction.
- **Authority returns to default only once the host confirms the rest** (16 still frames forced it to rest).
- **A held body isn't sent as physics.** Its holder and relative pose go in the avatar state.
- **The non-authority side snaps state**, quantized the same on both sides. There's a priority accumulator for
  bandwidth, and a jitter buffer of 100 ms for avatars.

**Others:**

- **Roblox network ownership:**
  - assigned automatically to the nearest player's character;
  - the server can override it with `SetNetworkOwner`;
  - anchored parts always stay with the server;
  - the server can't check what an owner sends;
  - forum reports describe hiccups while ownership changes hands.
- **Coherence:** a player asks for authority and waits, so two players never claim the same object. That's the
  opposite trade-off to Fiedler's optimistic take.
- **Unreal Chaos physics replication:** Predictive Interpolation corrects a body by its velocity toward the server's
  state, never by teleport. Resimulation replays from a cached frame when the error is large.
- **Valheim:** the first player into a zone owns its objects (ZDO owner), and only the owner changes an object's
  state.
- **FiveM OneSync:** entities migrate owner as players come and go, and the server can hold unowned ones.
- **skymp:** drives remote actors with Papyrus `TranslateTo` plus `KeepOffsetFromActor` (already in
  `actor-movement.md`).

## What we take, and where it went

Plan: `docs/design/physics-sync.md`, with the decision in memory `sync-model-decisions` (2026-11-02).

**A. Copies step through the engine** (in progress):

```
CharacterService: CopyStep::Choose(lying, at, goal) -> Leave | Step | Place (past kSnap 64 units)
SkyrimBridge::ApplyRemoteMovement: Step stores the goal on the extension; Place warps as before
HookApplyMovement (copy): Actor::Move(dt, goal - position in its own frame)   vfunc 0xC8, 37507
  no controller or cell not loaded -> ForcePosition
```

- From TES3MP: the engine steps the copy, with a snap distance.
- From PLANCK: a warp isn't a presence.
- The engine fact, from IDA: `ApplyMovement` calls `Move(this, dt, &translation)`, where the translation is this
  frame's step in the actor's own frame (x right, y forward).

**B. Objects owned exclusively by their mover** (next), Fiedler's scheme:

- owner and authority with their own sequence numbers;
- a claim request to the host, which accepts or refuses;
- on every other game the body is keyframed on the owner's frames;
- authority returns to the host at the rest;
- it replaces the host steering guest holds.

## Sources

- **OpenMW:** [repository](https://gitlab.com/OpenMW/openmw), with `scripts/data/integration_tests` and
  `scripts/integration_tests.py` read locally.
- **TES3MP:** [repository](https://github.com/TES3MP/TES3MP), with `apps/openmw/mwmp` read locally.
- **Fiedler:** [Networked Physics in Virtual Reality](https://gafferongames.com/post/networked_physics_in_virtual_reality/),
  [State Synchronization](https://gafferongames.com/post/state_synchronization/).
- **Roblox:** [network ownership](https://create.roblox.com/docs/physics/network-ownership).
- **Coherence:** [authority transfer](https://docs.coherence.io/1.1/learning-coherence/first-steps-tutorial/2-physics-authority-transfer).
- **Unreal:** [physics](https://dev.epicgames.com/documentation/unreal-engine/physics-in-unreal-engine),
  [FWaitUntil](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Developer/CQTest/FWaitUntil).
- **Minecraft:** [GameTest (Forge)](https://docs.minecraftforge.net/en/1.21.x/misc/gametest/).
- **ROS:** [actionlib API review](https://whiteoak.umd.edu/roswiki/actionlib(2f)Reviews(2f)2009(2d)07(2d)20_API_Review.html).
- **mineflayer-pathfinder:** [issue #118](https://github.com/PrismarineJS/mineflayer-pathfinder/issues/118).
- **Voyager:** [repository](https://github.com/minedojo/voyager).
- **SimWorld:** [paper](https://arxiv.org/pdf/2512.01078).
- **Valheim:** [networking guide](https://supercraft.host/wiki/valheim/better-networking).
- **FiveM:** [OneSync](https://docs.fivem.net/docs/scripting-reference/onesync).
- **Factorio:** [FFF #60](https://factorio.com/blog/post/fff-60).
- **Sea of Thieves:** [GDC talk](https://gdcvault.com/play/1026366/Automated-Testing-of-Gameplay-Features).
