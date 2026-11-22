# Physics sync

Status: **held to a vanilla baseline since 2026-11-15 ("One simulator per pile"); steps 1 to 6 built 2026-10-24; owners (every game streams its own bodies) built 2026-10-26** (below, "As built" and "A guest's hand, as built"): objects and ragdolls
from the host, live; a guest's bumps, casts, grabs and drops reach the host. Design approved 2026-10-24 (the guest
holds what it grabs; only the host's loaded cells; frames sent on prediction error, not at a rate). v1.0.0 feature 5
(`docs/progress.md`, "v1.0.0 features"). Replaced the rest detection of `docs/design/world-state-sync.md`, "Physics
objects' rest" (`Bridges/PhysicsRests`, now `PhysicsBodies`), keeps its move rows.

## One simulator per pile, held to vanilla (2026-11-15, built)

The reverted #56 ("One rule") and #58 ("The mover's until its rest") are back, measured this time against a game
with no session. What they left was four more places where two games simulated one object.

```text
the vanilla baseline (scripts/coop_harness/actions/collisions.py, baselines/collisions.json)
  one game, no session: 40 rounds of each trial on one lane in QASmoke, one trial's objects there at a time
    20 with our physics hooks off (HavokStepFix too: plain vanilla)
    20 with the plugin as it ships (our frame-step fix on): "session idle"
  trials
    push_line        a ladle walked into, which runs into a second
    push_mixed       a cheese wheel into a cabbage and a bucket
    tavern           16 items in a grid (bottles standing, food, pots), walked through
    carry_into_pile  a bucket grabbed, carried into 12 items, let go
  per object (a pile as one)
    moved, path, seconds
    peak    its fastest five frames, by its 3D (one step's impulse is caught or not by where the frame falls)
    jump    a frame's move beyond its speed and beyond twice the steps around it (a teleport)
    turns   frames that turn back on the one before (a shake)
  the duo, three rounds a scenario
    every round, both games: no jump or turns beyond the trial's highest in those 40
    the middle round, both games: moved, path, seconds and peak inside the 40 rounds' range
    every round: both games rest each object within 2 units
  why three rounds: one vanilla round is outside another 20 rounds' ranges 1 time in 5 (Havok is not repeatable)
  scenarios: host-/guest- push-like-vanilla, push-mixed-, tavern-, carries-into-pile-like-vanilla

who simulates what (one game per object, always)
  a player's character walks into it, hits it, holds or drops it    that player's game (the mover)
  what the mover's objects knock (within 100 units, GuestHands::OwnKnocked)   the mover's too
  a copy's capsule                                                   pushes nothing (79133's impulse zeroed)
  everything else                                                    the host
  the host's game places it itself (a script, the console)          the host's again, at once (HostHands::OnHostPlaced)
```

- **No relayed push.** `BodyPushRequest` is gone: the host gave its body the guest's impulse and then followed the
  guest's frames too (a sweet roll flew 133 units off at 535 units/s and was put back in one frame).
- **Copies push nothing.** The character push has no player test (`ScaleObjectImpulse`, `docs/re/systems/physics.md`), so
  a guest's copy shoved the host's pile while its player shoved the guest's. `PlayerHands::SetCopies`, each update.
- **The pile is the mover's.** Only what the player touched was the guest's; what its held bucket hit was simulated by
  both games, the host's result steered the guest's pile against the bucket, and the jam dropped the hold after 1.4 s.
- **The host's game placing wins.** A funnel move (`CHANGE_REFR_MOVE`) of a reference a guest moves: nothing steers it
  from that moment (`Bridges/PlacedByGame`), the host reads where it was put, stops following, and the mover gets that
  place with `NotifyBodyRests::Placed` and gives the object back (`GuestHands::GiveBack`).
- **A hold on another game is carried by velocity** (`SteerKeyframed`), not put at the frame each step: placing
  (60898) starts its contacts over every frame.
- **What a body meets depends on who simulates it** (`Physics/BodyGroups.h`, in the filter hook 78553): each
  reference body another game moves is in our "played" collision group, each one this game moves in "moving"
  (`IBodyBridge::SetMoving`, set by `BodyBridge::Drive`, the host's `TakeBodies`, a guest's `Own`; given back at its
  rest). A played body meets the world, characters and moving bodies; not other played bodies (their owner's game
  settled those), and not a loose object nobody moves: its owner's game never hit that one. Before, the other game
  moved such a neighbour by itself and the first game followed it (a resting bucket jumped 26 units).
- **A body a guest asks about is followed from then** (`HostGame::FollowBodies`, `PhysicsBodies::Follow`): one
  leaning on the host's player stands there awake and unmoved (no frames), and falls in the guest's game, where a
  copy pushes nothing; put back at the host's pose it fell again, and the guest asked once a second for good.
- **A slow frame is steered softly** (`BodyPrediction::SteerSeconds`): a played body's gap to its frame is closed
  over half a second at 10 units/s and under, over 150 ms when fast. Steered stiffly a slow one shook against what
  it lay on; left alone in a dead zone it went its own way and was pulled back at the zone's edge (a rolling apple,
  46 turn-backs); stiff when fast, a body knocked from rest ran 10 to 15% faster than its owner's while it caught up.
- **A rest gets its contacts** (`BodyPrediction::PlaceRest`, `docs/re/systems/physics.md`, "A rest needs its
  contacts"): put at its rest awake for one physics step, then asleep (only when alone in its island: the engine's
  deactivate sleeps the whole island, and froze the mover's own bucket in the air). Set dynamic and asleep at once,
  Havok had no floor contact for it and the guest's first push threw a cheese wheel 30 units up (10 of 13 pushes).
- **A rest is glided to** (`BodyPrediction::GlidesToRest`): a body up to 64 units off its owner's rest is steered
  there for half a second at most, then put. The move row of a body whose rest is on its way is left to that rest
  (`BodyService::RestComing`): the row came first and keyframed it there in one frame.
- **A rest being put is not this game's move** (`BodyBridge::SetMoving`, `Rested::Done`): the glide onto a guest's
  rest moved the host's body, so the host's own reads (`PhysicsBodies`) took it as moved until it rested, and each
  read began the rest again (`SetMoving` erased it). It never slept: the host's ladle hopped at its rest for 15 s and
  the host streamed it to the guest (350 frames), whose copy crept after it (192 turns, 2 rounds in 6).
- **A rest in a pile is done when the pile sleeps** (`BodyPrediction::RestSlept`, `StillToSleep`, `SleepBody`): the
  engine's deactivate sleeps a whole island (`docs/re/systems/physics.md`, "A pile sleeps together"), so a rest among
  others sleeps it only when every body in it is still and not one this game moves, plays or holds, else waits for
  Havok, and stays the owner's rest until then (2 s at most; one pushed off meanwhile is held there at once, half a unit off, not 12
  later). Done at once, it lay awake among them, the host took their nudges for its own moves and streamed them to
  the guest, whose pile shook, asked and snapped (jumps of 15 units, rests 3 to 10 units apart). The tavern passed
  from both walkers after it.
- **The host's own placement drops a guest's rest** (`HostHands::OnHostPlaced`): a rest the host's game was still
  putting (waiting for its pile to sleep) put the object back after the console placed it.
- **A rest is asked again only after a physics step** (`BodyService::m_restsTried`): put awake in one frame, it was
  asked again in the same main-loop pass and slept with no collide step between, so without its contacts.
- **A guest's rest whose frames never came is taken** (`HostHands::Defeated`): a push's claim is its unreliable frames;
  lost or late, the reliable rest was refused and the guest's object snapped back. Refused only for a mover another
  claim defeated (remembered `kDefeatedMs`), or a body the host's game took back.
- **A let go does not skip ahead** (`BodyBridge::Free`): the holder's frames show 100 ms behind and its let go came at
  once, so the held object jumped up to 16 units to where it was let go.
- **Every moved body is asked about together** (`GuestHands::AskHost`): asked one at a time, each of the host's rests
  landed among neighbours still off theirs and was moved again, every second for good.
- **A rest's move row comes back** (2026-11-16, `GuestHands::RestedHere`, `BodyService::RestedHere`): the host puts a
  guest's rest a step awake, the body settles up to a unit or two, and the host's game records that as a move: its
  row reached the guest, which keyframed its own resting wheel there and freed it asleep (60850). That slept the
  wheel's whole island with HK_REAL_MAX as every body's fastest speed, so Havok's last sleep test passed for the wheel
  the guest's player carried or had just let go beside it: it hung in the air (the playtest's cheese wheel,
  `docs/re/systems/physics.md`, "When an island sleeps"). The guest skips a row within the send limits (2 units) of
  the rest it sent, and our island sleeps (`SleepBody`, a free asleep: `DeactivateIsland`) refuse an island with a
  body not still (60850 skips Havok's stillness counts) and put back each body's fastest speeds, so a moving body
  keeps its island awake, as in vanilla (ClientTests "An island is put to sleep only when every body in it is dynamic
  and still": a steady 90 units/s slide). Trials `stack_release`
  (a wheel lifted off a stack, held, let go) and `pile_carry_release` (carried through a pile of wheels first): the
  let go wheel's `fell` is held to vanilla's (20 to 37 units in 40 rounds), and in no game is it put to sleep while it
  moves over 20 units/s (`collision_metrics.slept_moving`: before, 9 of 12 guest rounds; after, 0 of 9, vanilla 0 of
  20). Scenario `guest-pile-release-falls`.

Results (duo, 2026-11-16, each trial 3 rounds against 40 vanilla rounds): full runs on the final build (the pile
sleeps as one) passed 6, 7, 3 and 5 of the 8 trials; in the 7 every trial that ran matched vanilla, and the eighth
timed out in staging. Pushes and carries pass in nearly every run; the misses are pile rests apart, a snap, staging.
The suite is noisy at 3 rounds. Vanilla judged by its other sample's ranges (3 rounds drawn from one sample of 20,
`compare` against the other's `envelope`, 2026-11-16) passes carry 0.56 to 0.83, push_line 0.90 to 0.99, push_mixed
0.96 to 0.98, tavern 0.75 to 1.00 of the time: all 8 in one run about 0.3, about 7 of 8 on average, without the
rests-apart check co-op adds. The last: every push, carry and the host's tavern as vanilla; one tavern
object in one round rested 3.9 units apart. The misses seen: a pile rest 2 to 8 units apart, a snap of 6 to 13
units, a push round's rest 2.1 apart (the limit is 2), a staging timeout. Both push trials and the tavern pass in most runs from either walker; the misses are
intermittent: a pile rest sliding off and snapped back on the guest during the host's carry, pile rests left a few
units apart, carry seconds a tenth over vanilla's highest, and staging timeouts. A ladle hopping at its rest in the
host's game after a guest's push is fixed (below).

Open (`docs/bugs.md`, "Physics against vanilla"):

- The mover's own objects peak slower than vanilla in some runs; with the plugin on and no session they do not.
- The harness's `world_state` crashed the host once reading an object's 3D (EngineCalls.cpp, ReadPose).
- A guest pile rest sliding off and snapped back; pile rests left a few units apart after two asks.

Tests: GameTests `[mover]` "The host's game places the bucket a guest's push still moves", "A guest's copy walks into
the bucket in the host's game", "A guest's player bumps the bucket into the plate", "...the host's is never faster
than the guest's push made it"; HostAuthorityTests "the host's game placing a body a guest moves takes it back";
ClientTests "A played body's gap is closed slowly while its frame moves slowly, stiffly when fast", "A rest is put awake for one step so its contacts form...", "A body off its rest glides onto it..."; GameTests "The host puts the guest's rest of the bucket in place...", "The host's rest of the bucket has its contacts", "...asked about all of them at once", "...asked twice, then left where it lies"; `scripts/test_collision_metrics.py`.

## The mover's until its rest (2026-11-14, built)

What a player moves lands where that player's game puts it. Until now a guest's let go, push, hit or drop was
simulated twice: the guest's own physics showed it, the host re-ran it (a let go from the release frame, a push from
the impulse), and the host's rest went to every guest, the mover included, which placed it. The two games' falls part (measured on the
duo: a thrown ladle rested 10 units apart, a pushed one 13.6, the dropped sword of `docs/bugs.md` 15 to 80; why, inferred
and not read: contacts, the copies' capsules a moment behind, Havok's steps), and the mover's body jumped to the
host's rest. The let go itself adds nothing: `PlayerCharacter::DestroyMouseSprings` (40557, read in IDA 2026-11-14)
takes the spring off and only activates the body, leaving its velocities as they were (that they are the hand's is
inferred from the spring holding it), and the host frees its body at the release frame with those velocities.

```text
guest game (GuestHands): its player pushes, hits, holds, lets go of or drops a body -> its own (m_own)
  held:      frames as BodyFramesRequest{Held}            (as before: keyframed everywhere else)
  let go:    BodyReleaseRequest                           host frees it there, every other game's hold ends
  moving:    its awake frames, the send rule's, free fall marked (BodyMotions), as BodyFramesRequest{!Held}
  rests here (PhysicsBodies: Havok's sleep or 300 ms still, as on the host): BodyRestsRequest, its own no more
  taken and rested: the host's frames and rests older than that tick are dropped (BodyPlayback's cutoff)
  not awake for kOwnMs: its last pose as its rest; never moved here: the host's again, nothing sent
  meanwhile the host's frames and rests of it are not taken (TakesFrame, KeepsRest), nor its own rest come back,
  nor the host's move rows of it (GuestWorldState: BodyService::MovedHere, a drop adopted this frame included)

host (HostHands): the first mover keeps it (m_holds, Held or not)
  free frames -> every other game, the host's too: played steered, mass kept ("One rule")
  its rest    -> every other game: placed, asleep
  pushes on it skipped; HostPhysics sends none of the host's own frames or rests of it,
  nor the host's rest at the mover's rest after (HostRests); one moved on from there is the host's again
  mover silent kSilentMs or gone: the host's again where it is (a hold: dropped where it was, as before)
  a rest refused with no claim left (a second mover, the first already rested): the sender gets the host's pose
```

- Tests: GameTests `[mover]`, each failing before: a bump, a sword hit and a carried let go, the host's table holding
  harder than the guest's (`FakeFlight::Friction`, each game's own); the guest's body is never placed by another
  game and the host's lies where the guest's stopped. HostAuthorityTests "a body a guest's player moved is that game's
  until it rests there" and the let go case (nobody else takes it while it falls).
- Scenario `guest-throw-lands-where-thrown` (MCP verb `moves_and_watches`: the mover's body traced each frame;
  `expect` `steady`: no frame's step three times its neighbours' and over 4 units; `at_rest_of`: the host's at the
  guest's rest). The guest's own scenarios (`guest-bump-moves-host`, `guest-throw-lands-on-host`) now wait for the
  guest's rest and check the host's there.
- The host's row of a guest's drop (`create` then `move` to where it was dropped) put the guest's falling sword back
  in the air, asleep: it never fell there, the guest handed it back, and the host's fall won
  (`guest-drops-item-stays`, 235 units). GameTests "A guest's player moves the bucket: the host's move row of it does
  not put the guest's back".
- Review (2026-11-14): a host frame from before the takeover can land after the guest's rest (unreliable channel):
  GameTests "A host frame from before the guest's push comes after its rest"; a second mover's rest after the first's
  rested: HostAuthorityTests "a second mover's rest after the first's rested gets the host's pose back".
- Open: two guests moving one body at once: the first keeps it; the second shows its own until the first's rest
  or the host's answer to its refused rest places it.

## One rule (2026-11-13, built)

Everything another game owns is a copy here, the same way for an NPC, a ragdoll and an object (memory
`sync-model-decisions`): physically present, nudged toward its owner's place each frame, placed only when far off, and
deciding nothing itself. NPC copies already worked so ("Copies step", `actor-movement.md`); objects were keyframed
puppets, and every way a player moves a body skips a keyframed one: walk-in, swing, shout, explosion, grab
(`docs/re/systems/physics.md`, "Every hand skips a keyframed body"). Each bug of the old playback sat at a switch into
or out of that mode.

```text
a reference's body another game moves (BodyService -> IBodyBridge::Drive)
  not held there: kept simulated, its mass kept
    velocities set to reach the frame over 1.5 frames, at least 50 ms (BodyPrediction::Steer, SteerSeconds): once a
    frame, a fixed 50 ms overshot on 100 ms frames and diverged past them
    64+ units off (BodyBridge::kPlaceFar): put there (SetPositionAndRotation 78082, motion kept)
    keyframed for a hold that ended: its own motion back first (FreeBody 78182)
  held there (a hold's frames, NotifyBodyFrames::Held): keyframed as before, so this game's grab refuses it
the owner's rest: placed and put to sleep (60850), an awake played body included
the host's own player's hold: sent as a hold too (HostBodies::Held), every guest keyframes it
```

- A guest's swing, shout, blast or walk-in on a played body now moves it in its own game at once; the host's result
  follows (its copies replay those actions; a swing goes as a push, "A swing's hit").
- Measured on the duo against the keyframed playback (`physics-push-reaches-guest`, `guest-cast-moves-host`): both
  follow the host within 0.1-0.5 units typically and flicker 0 times; a 22 s, 118-unit roll was 5.4 units off at
  worst. A guest's swing at a ladle the host's frames moved: keyframed, nothing moved (2 of 2); nudged, knocked
  (MCP verb `swing_at_moving`, the ladle slid by the harness's `body_velocity`). No scenario keeps it: the slid ladle
  sometimes stops before the swing lands (4 of 5 passed); GameTests keeps the case.
- Tests: GameTests "A guest's player hits the bucket the host's knock still moves: it keeps its mass, so both games
  are knocked" (the fake's `Drive` keyframes only a hold), HostAuthorityTests "the host's player holds a body: each
  guest gets it as a hold".
- **Rest from a body's own motion** (step 3): a reference's body rests once Havok sleeps it or it keeps one pose
  (0.5 units, 0.01 rad) for 300 ms (`BodyPrediction::Still`, in `PhysicsBodies::Take`, on both games); its rest goes
  out when all its bodies rest. Havok sleeps an island only when all of it is quiet, so a body beside an animated
  one (Orgnar leaning on his bar, 78910) or a character never got a rest. A rest within 0.05 units and 0.002 rad
  of the last one sent (a body Orgnar keeps waking where it lies) goes again only after 2 s
  (`PhysicsBodies::kRestAgainMs`): the guest's own Orgnar wobbles its copy too, and the rest puts it back. With the
  still limits as the "same" test, a slow settle's last tilt never went out and the inn's clutter ended turned
  differently on the guest (ClientTests "A body still for a while rests though its island stays awake"). Ragdolls keep the island rule (their own lying and
  get-up rules). Scenario `object-rests-beside-orgnar` (a cheese wheel dropped on the bar; the old rule: 6 of 7
  without a rest, keyframed on the guest; now 5 of 5), GameTests "A knocked bucket stops where an animated body keeps
  its island awake" (the fake's `KeptAwake`), ClientTests "A body is at rest once it keeps one pose".
- A body this game's player holds counts as moving, so a body held still in the air does not rest the moment it is
  let go (GameTests "The host's player holds a bucket still in the air and lets go").
- **Kept: the guest's "moved by itself, ask the host"** (step 4 of the plan, dropped). The nudge corrects only a body
  the host sends frames of; one the host never moves (a guest's own blast's push the host's copy missed) gets no
  frames, so the guest asks for the host's pose (GameTests "A guest's own blast throws a bucket the host's does not").
  It fires less now: the host replays the guest's casts, shouts and walk-ins through its copy.

The keyframed playback below ("How a guest applies it", "As built") is replaced for objects not held.

## Owners (2026-10-26, built)

The game that simulates a body streams it; every other game plays it; the host relays. Today the split is by role
(the host sends, a guest plays, a guest's hands a path of their own), so the one body a guest simulates, its own
player's ragdoll, reaches nobody, and a copy takes ragdoll frames only once its own death put it in its ragdoll.

```text
who owns a body
  a reference (world object)     the host; while a player holds it, that player's game alone (Exclusive holds)
  an actor's ragdoll             the actor's owner (HostCharacters): the host's NPCs and player the host,
                                 a guest's player its game

every game (BodyService, Services/Generic)
  frames and rests of bodies it does not own -> BodyPlayback, 100 ms behind -> IBodyBridge drives/rests them
  (references; copies' ragdolls, a copy not in its ragdoll yet put there)

a guest (GuestHands)
  its own bodies: its player's ragdoll while it simulates (dead, knocked down), what its player holds
    -> BodyFramesRequest (its send rule), their rests -> BodyRestsRequest, a held one let go -> BodyReleaseRequest
  pushes on the host's bodies, and what its own physics moved alone (as today)

the host (HostPhysics, HostHands)
  its own bodies to each guest (as today: near it, its send rule and budget)
  a game's own bodies (the sender owns them, checked) to every other game, the host's own game included
```

Build order:

1. **One body bridge for every game** (built 2026-10-26): `IBodyBridge` (`Bridges/BodyBridge`; the fake engine's in
   GameTests) finds, drives, rests, frees and pushes a body by its key, references and copies' ragdolls alike, out of
   `GuestEngine` and `HostEngine` (each sets how its references are found). `BodyService` (every game) took
   GuestPhysics' playback; `GuestHands` is its keeper on a guest. HostHands forwards a holder's frames to every
   other game, the host's included, which plays them in its `BodyService` (its own playback went; a holder silent
   for 2 s drops the body). Fast tiers pass; physics scenarios pass (`guest-cast-moves-host` once 0.4 units over its
   jump limit, then 2/2).
2. **A guest's player's ragdoll** to the others (built 2026-10-26): `PhysicsBodies` on a guest also watches its
   player (`GuestEngine::TakeOwnBodies`); `GuestHands` sends the ragdoll's frames with its held ones (one send rule,
   `BodyFramesRequest`) and its rest (`BodyRestsRequest`, new; the guest's question for the host's pose is now
   `BodyPoseRequest`). `HostHands` passes them on to every other game when the sender owns the character
   (`HostCharacters::OwnerOf`). `death-spectate-guest` and `death-spectate-host` check that the copy lies as its
   owner's body.
3. **Living knockdowns** (built 2026-10-26): an actor's frames carry `Knocked` (its owner lives, knocked down). A
   living copy whose owner's frame says so is knocked down with no force (`BodyBridge::KnockedDown`,
   `AIProcess::KnockExplosion`), and its ragdoll plays the frames once its own ragdoll has it. The owner's game counts
   a living actor's ragdoll only while it is knocked down (knock state 1-4); up again, where it lay is its rest. The
   copy gets up by its own knock update once its ragdoll is still (no hold hook needed). Replayed actions keep the
   copy's knock state (`AnimationSystem::PlayReplayedAction`): the owner's would put the copy in a knock state
   without a ragdoll. `knockdown-lies-as-host`, `guest-knockdown-lies-on-host` (3/3 each).

Found on the way:

- A dying owner's frames reached its copy before its death did, and knocking that living copy down left it with no
  ragdoll once it died (0 bodies): hence `Knocked`, false for a dead owner's frames (the copy waits for its death).
- Step 1's engines asked `World::Get()` for the body bridge in their destructors, which runs while World is being
  destroyed (hot reload unloading): a crash. `HostEngine` and `GuestEngine` now take the bridge by reference.
- A knocked down owner gets up as soon as its ragdoll is still, often before every body sleeps: the copy's checks
  compare it with where the owner's lay a moment before (within 4 units, 0.2 rad), not at an exact rest.

Still open: a dead body's pose when a guest loads its cell or joins, a small bug parked in `docs/ideas.md`.

## Scene updates between frames (2026-11-02)

The update hook runs in the Papyrus VM job. `BodyService` applies playback and rests through a scoped
`MainLoopTasks` task between frames, with current state and synchronous bridge results. Received object rows
(`GuestWorldState`) use the same phase. Updating their scene graphs inside the VM job made a watched held
tankard flash green (S2).

`PhysicsBodies` still reads physics snapshots during the update. Its left-behind node repairs are queued and
rechecked before writing, preserving contacts if the engine already followed them. A rested reference's move
row is recorded after those writes, so its saved place matches its repaired node. Forgetting the reference
invalidates queued repairs and records; destroying the physics reader cancels its tasks. GameTests observe
manual scene writes outside the main-loop drain, and ClientTests cover repair/rest ordering and cancellation.

## As built (2026-10-24, split 2026-10-25)

Where each part lives:

```
host   HostPhysics (Services/Host/)   the host's bodies out to each guest: send rule, relevance, rests
       HostHands                      a guest's player's hands on the host's bodies: push, hold, let go
       HostEngine (Bridges/)          the engine side: HostEngineBodies.cpp, PhysicsBodies (what physics moves)
every  BodyService (Services/Generic/) plays the bodies other games own (BodyPlayback, 100 ms behind), IBodyBridge
       GuestHands                     this player's pushes, holds and let gos, and bodies its own physics moved
       GuestEngine (Bridges/)         the engine side: GuestEngineBodies.cpp
both   Physics/                       the pure halves (ClientTests): BodyPrediction, BodySends, BodyPlayback,
                                      BodyRelevance
       Bridges/ReferenceBodies        a reference's bodies by BodyKey::Index; Havok/bhkCollision the engine calls
```

The flow:

```
host game
  change funnel's havok move (0x4) on a REFR or ACHR -> PhysicsBodies (Bridges/): awake references
  each update, HostEngine::TakeBodies: each awake reference's dynamic bodies, read under the world's lock
    a reference: its 3D's rigid bodies, breadth first (RigidBodiesOf)
    an actor: its ragdoll's bodies in order (RagdollBodiesOf), by its session id; a living actor's are keyframed
      by its animation, so only a dead or knocked-down actor's count
    awake: its island's active mark; none awake -> rested (every dynamic body's pose) + the move row (references)
    an awake reference body's 3D written from its body when the engine left it behind (FollowBody; since
      2026-11-01 only then: writing a followed one re-placed it, "Matches a game without co-op" below)
  HostPhysics (Services/Host/): BodyMotions marks free fall; each guest's BodySends picks the frames its guess got
    wrong (2 units, 0.05 rad), or 500 ms old, the most wrong first within ~8 KB/s, of the bodies within 4096
    units of that guest's copy (BodyRelevance)
    -> NotifyBodyFrames (unreliable, ~1 KB packets) to that guest; NotifyBodyRests (reliable) to everyone
guest game
  BodyService (Services/Generic/): BodyPlayback, 100 ms behind the host's clock: the last frame's guess blended
    into the next frame (or the rest) when that is already here, a late frame's correction blended out over
    100 ms, the first frame blended from where this game's own physics put it, a rest placed once
    -> IBodyBridge::Drive: keyframed (78077), put at the frame (SetPositionAndRotation 78082) moving at its
       velocities, then its node and reference to the body (FollowBody: a keyframed body never moves its node)
    -> IBodyBridge::Rest: bhkRigidBody::SetPositionAndRotation (78082), velocities 0, the node likewise; a
       reference's then simulated again, asleep (steps 5-6: its player's to push and grab)
    a copy's ragdoll body is taken only once its own ragdoll simulates it (the copy dead by its owner's action)
```

- Pure halves, tier 1 (`ClientTests`, `physics_tests.cpp`): `Physics/BodyPrediction` (the guess), `BodySends`
  (the send rule and budget), `BodyPlayback` (the guest's playback). A thrown, bouncing, sliding body sends 16
  frames over its flight (`FakeFlight`), the guest never more than a correction off.
- Tier 2 (`GameTests`, `physics_flows.cpp`, `[physics]`): the fake engine's bodies fly (`FakeFlight`) in the host's
  network time; a knocked bucket's guest twin is held and rests exactly; a body the guest lacks is skipped; a host
  NPC's ragdoll waits for the copy's own death, then lies as the host's bone by bone.
- Tier 3: `physics-push-reaches-guest` (the host's fireball scatters ladles: the client's 3D follows keyframed and
  rests at the host's spots), `ragdoll-lies-as-host` (a wolf killed by fireballs: the copy's bones as the host's).
- Readouts: `world_state`'s `collision` gives each body's motion type, awake, rotation and velocity; `ragdoll`
  (harness command) an actor's ragdoll bodies. Log lines: `HostPhysics: <guest> gets the frames of ...`,
  `HostPhysics: sent N rests`, `HostPhysics: <guest> got N frames of ...` (at its rest), `BodyService: follows the
  owner's ...`, `BodyService: placed the owner's rest ... after N frames`, `BodyService: lost ...` (no
  frame for 2 s and no rest), `BodyService: ... is not in this game`. `world_state`'s `collision` also
  gives each collision object's flags and whether its body is offset from its node (`bhkRigidBodyT`).
- Engine notes: `docs/re/systems/physics.md`.

### Following the host smoothly (2026-10-25)

The fireball scenarios trace both games' 3D each frame (harness `body_trace`, `coop_test.py` `FlightTrace`) and
compare the guest's with the host's 100 ms earlier. Keyframing toward each pose over the frame's time made the guest's
ladles jump 10-12 units for a frame and back (frames of 1 ms then 24 ms under Wine, Havok's step 16 ms); placed at
the pose each frame instead, blended into frames it has early and from its own blast's push, the guest's 3D is
typically 0.1 units off, at most 1-4 during the first 100 ms of a knock (its own blast's push blended out), its
biggest frame-to-frame jump beyond the host's 3 units, and it never flickers. The scenarios fail on a flicker or a
jump past 6 units (`FlightTrace.check`). Skyrim's own flight is less even than the guest's: in a game that is not
hosting (none of ours running), a knocked ladle's speed per frame swings 80% from frame to frame (Havok steps at 60
Hz, the frames come 1-57 ms apart), as on the host; the guest plays by the clock, 10-30%.

**On the frame clock (2026-11-09).** Frames were stamped with the host's session time as read and played at the
guest's session time as read less 100 ms: both uneven (S10, `copy-clock.md`), so a flying body's shown time jumped 27
then 6 ms between steady frames, and in the fake its steps went -1.8 to 11.8 units a frame against a steady 5. The host
stamps frames with the frame clock (`HostAuthority::Update`'s frame tick), and every game plays them at the frame clock
less 100 ms, or the line's lateness if more (`BodyService::PlaybackTick`, the copy clock). GameTests "A flying body moves
evenly on the guest's game though the session's time is read unevenly" (the fake's Havok now steps by the engine's
frame when a test sets `FrameMs`, as `HavokStepFix` steps Skyrim). On the duo `physics-push-reaches-guest` passes, the
guest 140 to 180 ms behind, its extra step beyond the host's per frame typically 0.04 to 0.06 units (p95 0.25 to 0.45;
`FlightTrace` now fits the delay).

### Woken still (2026-11-13)

Skyrim wakes islands it never moves: an actor's animated body sharing their island, a character's proxy touching
them (`docs/re/systems/physics.md`, "Woken still: awake is not moving").

```
host PhysicsBodies: a reference wakes (havok move, island active)
  each body's frame at the first read: where it woke (Moving::Woke)
  until one leaves it (BodyPrediction::LeftRest: 2 units, 0.05 rad) or the host's player holds it:
    no frames (a guest would play it keyframed, and the engine's grab refuses that)
  then frames as before; asleep: its rest as always (settles of under 2 units included)
guest: never driven, so dynamic: its player grabs it; the host's rest places it where it already is
```

Bug: the guest's held E on a bottle on Orgnar's bar picked it up (a theft) instead of grabbing it: each of Orgnar's
wakes keyframed the guest's bottle. Tests: `ClientTests` "A woken body that never left where it woke is not moving",
`GameTests` "The bottles on Orgnar's bar wake without moving" (the fake's `Wake`, woken three times), scenario
`guest-grabs-orgnar-bottle`.

### Near each guest

A guest gets the frames of the awake bodies within `BodyRelevance::kGuestRadius` (4096 units, one exterior cell) of
its copy in the host's game (`HostGame::ReadPlayerPosition`), so a fight far from it does not spend its budget;
without a copy shown (joining, travelling) it gets them all. Rests go to every guest, and the host writes an awake
body's 3D wherever the players are when the engine skipped its own write (sometimes, at any distance:
`docs/re/systems/physics.md`). A body that comes near mid-flight starts getting frames (its guess is old); one that
leaves stops at its last guess until its rest. Tests: `ClientTests` "A guest gets the frames of the bodies near its
character", `HostAuthorityTests` "a guest gets the frames of the bodies near its character, every guest the rests".

## A guest's hand, as built (steps 5 and 6, 2026-10-24)

```
guest game: a reference's body at rest is dynamic, asleep (the host's frames hold only a moving one)
  its player's character pushes one (bhkCharProxyController::objectInteractionCallback 79133, hooked: the impulse
    Havok applies, after the engine's scaling by layer and mass; PlayerHands)
    -> BodyPushRequest{key, impulse, point} (reliable): the host pushes its own alike (hkpEntity::activate 60849,
       the motion's applyPointImpulse)
  its player's weapon hits one (Actor::CalculateCurrentHitTargetForWeaponSwing 38628's applyPointImpulse, patched:
    PlayerHands; built 2026-11-13) -> a push like the above: the host's copy of the swing hits nothing
    (docs/re/systems/physics.md, "A swing's hit"); before, the guest's knock snapped back to the host's pose. Since
    2026-11-13 a body the host's frames move keeps its mass here ("One rule"), so the swing knocks it too
  its player grabs one (Activate held: StartGrabObject 40552; a body the host's frames still move is keyframed, which
    the engine's grab refuses)
    -> each update its frames (BodySends' rule) as BodyFramesRequest: the host steers its body after them, 100 ms
       behind (BodyPlayback; since 2026-11-01, "Steered holds" below); the first holder keeps it
  it lets go (PlayerCharacter::grabbedObject empty) -> BodyReleaseRequest{its last frame} (reliable): the host's
    body goes on from where the host has it, at that velocity
  a pushed, held or dropped body is the guest's own until the host's rest: its own physics shows it, the host's
    frames of it wait, the rest places it (then dynamic again, asleep: hkpEntity::deactivate 60850)
  its own physics moves one the host's frames never follow (its copy of a cast exploded elsewhere, a settle 2 units
    or 0.05 rad off) -> after 1 s BodyPoseRequest: the host answers that guest with its bodies' poses as rests
host game: a held body is simulated and steered, so awake: HostPhysics sends it to the other guests; a holder that
  leaves or goes silent for 2 s drops it
```

### Exclusive holds (2026-11-02, replaces "Steered holds" below)

An object is its mover's alone (memory `sync-model-decisions`), after Fiedler's ownership
(`docs/research/sync-and-testing-references.md`). Under Steered holds the host's game simulated a guest's held body,
so the host's player could grab it out of the guest's hands (`host-cannot-take-guest-hold`: at frame 14).

```
guest holds a body -> BodyFramesRequest (as before)
  HostHands: the first holder keeps it; its frames (Held) to every other game, the host's too (were: the host's only)
  each other game's BodyService: Drive, keyframed on the holder's frames (were: the host Steer-ed it, simulated)
    -> the engine's grab refuses a keyframed body, so nobody else grabs it; pushes skip a held body (as before)
    -> the holder's own game decides where it goes: its copies step and stop it (actor-movement.md, "Copies step")
  let go: BodyReleaseRequest -> the host simulates it from the last frame (BodyBridge::Free), as before
```

- **What decides now:** the holder's own physics, as the decision intends. Part A made that safe: a guest's copy of
  Lydia stops the bucket in the guest's game (`guest-holds-into-npc`), where before only the host's steer did.
- **Removed:** `IBodyBridge::Steer` and its bridge and fake implementations.
- **Tests:**
  - scenarios `host-cannot-take-guest-hold` and `guest-cannot-take-host-hold` (checked actions: the taker holds
    Activate 3 s and must hold nothing, the holder must keep it);
  - GameTests "A guest's player holds the plate: the host's player cannot grab it, nor can another guest's" (the
    fake's `TryGrab` refuses a keyframed body), and the carry and head flows rewritten for keyframed holds;
  - HostAuthorityTests "a guest's player holds a body, its alone".
- **Not built:**
  - A refused second holder. Two players grabbing within one round trip both hold it locally. The host keeps the
    first, and the second keeps its own until it lets go, while the first's frames reach it.
  - Fiedler's authority chain: bodies a held or thrown body knocks stay the host's.
  - Sequence numbers: the host's tick ordering and `m_released` already drop stale frames and releases.

### Steered holds (2026-11-01, replaced 2026-11-02)

S1 of the 2026-10-31 playtest: a guest's held object went through people's heads on the host. The host
keyframed its body onto the holder's frames (`SetPositionAndRotation` each frame), and a keyframed body moved so
meets nothing: whatever the holder's game let through, the host's did too (scenario `guest-bucket-on-npc-head`,
first run: the guest's bucket 50 units over Lydia's feet in its own game, 69 on the host, keyframed).

```
guest holds a body -> BodyFramesRequest (as before)
  HostHands: to the host's game only, Held set (NotifyBodyFrames::Held)
  the host's BodyService: a Held frame is steered (IBodyBridge::Steer): simulated, its velocity set onto the
    frame's path (BodyPrediction::Steer, as copies' ragdolls), put there only past 64 units (BodyBridge::kSteerFar)
    -> the host's physics has the last word: Lydia's capsule stops it on her head
  HostPhysics: the steered body is awake, so the other guests get the host's result, not the holder's frames
  let go: BodyBridge::Free gives it the frame's velocity where the host has it
```

The holder still shows its own hold until the host's rest. A copy's capsule pushes held bodies only while its
character controller integrates. Until 2026-11-02 copies skipped `Actor::ApplyMovement` (37356), which does that; they
now step through `Actor::Move` (`actor-movement.md`, "Copies step")
(`docs/re/systems/physics.md`, "What stops a held body"); the duo's later runs had the guest's own bucket stop on
the copy's head anyway (168 to 170 units, 131 to 138 in two), so when the holder's own game lets one through is
still open. Tests: GameTests "A guest's player lowers the plate onto a host NPC's head" (the fake's capsules:
`FakeEngine::OutsideCapsules`), HostAuthorityTests "a guest's player holds a body, the host's game steers its own",
scenarios `guest-bucket-on-npc-head`, `host-bucket-on-npc-head`, `guest-throw-lands-on-host`, and the bucket trick
(`guest-bucket-trick`, `host-bucket-trick`: the wide bucket 00012FDF upside down, centred, stays on her head in both
games; the bucket 000747FB and the cast iron pot roll off, as in a game without co-op).

### Held still is not at rest (2026-11-02)

A bucket held still (pressed against Lydia) is put to sleep by Havok while the hand holds it.

- Before: `PhysicsBodies` sent it as a rest. On the guest a rest is free and asleep, so in mid-air it fell; the
  guest's "moved by itself" asked for the host's pose, got the same rest, and the bucket fell again, about once a
  second.
- Now: the body this game's player holds (`IBodyReads::PlayerHolds`, `PlayerCharacter::grabbedObject`) stays
  awake for the send rule until it is let go.
- Scenario: `host-holds-into-npc` checks the other game's bucket (26 falls in 30 s before, 0 after).
- Flow: GameTests "The host's player holds a bucket still".

### Matches a game without co-op (2026-11-01)

The same drop (the wide bucket upside down, 60 units over a restrained Lydia's head, at the stage spot in QASmoke)
rested 152.4 units over her feet in a game with no session, and 131.3 on a host, hosting alone or with a guest,
every time. Traced frame by frame (`body_trace`): without a session the bucket meets her at 152 and settles; on the
host it never met that contact and sank to her capsule's top. The cause was the host's own `FollowBody`: writing an
awake body's node every update, even when the engine had already written it, re-places the body and its contacts
are lost (the node's update and the moved-reference notify both off still did it: the write itself). The host now
writes a node only when the engine left it behind (more than 1 unit or 0.01 rad off its body; followed, they agree
to 0.1 unit), and the same drop rests at 153.5 in the duo, the guest's exactly where the host's is. Writing on any
offset was not enough the other way: a 32-unit threshold left small knocks' nodes behind (`guest-cast-moves-host`'s
put-back failed). `kFarAway` on an NPC's controller (bit 18, set by `Actor::UpdateFadeSettings` 37992 while her 3D
is not visible) is vanilla and did not change the drop. `host-bucket-trick` is the regression scenario.

`PhysicsBodies` now reads through `IBodyReads` (the engine's `BodyReads`: the change funnel's watch and the engine
calls), so GameTests run the real one on the fake engine, whose bodies have a node the engine writes each step unless
it leaves it behind, and count a node write on a followed body as re-placing it (`FakeEngine::Body::Replaced`).
Flows: "The host's engine moves a knocked bucket's 3D with it" (no write, nothing re-placed), "...leaves a knocked
bucket's 3D behind: the host writes it, after a small knock too" (the 32-unit threshold fails it), "...leaves a
spinning bucket's 3D turned as it was" (a position-only check fails it).

- Pushes are the engine's own numbers: the guest's game computes the push its player's character gives (the
  character proxy's interaction), the host applies the same impulse. A copy moves by SetPosition and pushes nothing
  itself (measured below), so no contact fallback on the host was needed.
- A guest shows its own push, hold and drop at once and the host's result when its body rests: no round trip
  before the player sees it move; a big difference between the two simulations shows as a jump at the rest.
- At rest a reference's body is simulated again on the guest, asleep, so its player's character can push it and
  its player can grab it (a keyframed body is a wall to both). Held keyframed from every 3D's load (the design's
  first idea) was tried: its own blasts then moved nothing, but a carried or pushed body jammed against the held
  clutter around it and the grab dropped (`guest-throw-lands-on-host`, 2026-10-24).
- Tier 1 and 2: `ClientTests` as before; `HostAuthorityTests` `[Physics]` (a push, a hold the others see and a second
  holder refused, a release and its late frames, a holder leaving, a guest asking for poses); `GameTests`
  `[physics]` (a guest's push, its carry and drop, its own blast the host never followed).
- Tier 3: `guest-bump-moves-host`, `guest-throw-lands-on-host`, `guest-cast-moves-host` (with steps 1-4's), 3 rounds
  each on one duo. The physics scenarios put what they moved back (`put_back`), aim at the most clutter
  (`knock_clutter_with_fireball`), and carry off facing the object where the harness's `surroundings` show the floor
  clear (`clear_way`; a turn drops a held object). The harness gained the `back`, `strafe_left` and `strafe_right`
  buttons.
- Log lines: `GuestHands: this game's player pushes / holds / let go of the host's ...`, `GuestHands: this game
  moved the host's ... by itself`, `GuestHands: the host did not move its ... after this game's player did`,
  `HostHands: <guest> pushes ... (<impulse> at ...)`, `HostHands: <guest> holds ...`, `HostHands: <guest> let go
  of ... at ..., moving ...`, `HostPhysics: <guest> moved ... by itself, gets the host's ...`.
- Engine notes: `docs/re/systems/physics.md`, "A player's push", "Grabbing", "Motion type and kSetLocal".

Full-suite fixes (2026-10-24): a rest for a body not held and already there is left alone (placing it recorded a
move the host's game had not: `late-join-gets-calendar`); a free at a rest never wakes the body (60908 without
activation: waking ran the engine's activation listener under the world lock, a lock-order report in
`placed-leveled-items-are-hosts`); a copy's ragdoll body is taken only once the copy is dead (a chicken's powered
ragdoll is dynamic while it lives: `spec:npc-death-reaches-copy`, as the fake engine already had it); a guest's moves
watcher reads references only. A hook on `StartGrabObject` to free a moving body for a grab was built and removed:
adding any hook (one on a function the join never calls too) leaves the host's player 40 units off its baseline after
`guest-brings-character-into-hosts-world` (`docs/progress.md`, loose ends).

Not built: a refused grab (the second holder keeps its own until its release, which the host ignores; the design's
refusal would drop it), a guest's frames sent unreliably (a guest's requests are all reliable),
a guest far off in an exterior the host's grid has not loaded (its bodies are the host's there too), a ragdoll a
guest pushes (a corpse stays the host's frames').

Steps 5 and 6, measured 2026-10-24 before they were built (the scenarios were written first):

- `guest-bump-moves-host`: the client's copy walks within 14 units of the host's ladle and does not push it (copies
  move by SetPosition). Step 5 needs the host to push on the copy's contact.
- `guest-throw-lands-on-host`: the client's game refuses the grab (`StartGrabObject` takes no keyframed body): a
  body the guest holds for the host must be handed back to its own physics when its player grabs it.
- `guest-cast-moves-host`: the cast reaches the host (its copy's replay throws the host's clutter, the client's
  follows and lies as the host's), but some host references' nodes stay where they were while their bodies fly: the
  engine sometimes skips that write (not by distance), fixed in `PhysicsBodies` (`docs/re/systems/physics.md`, "A
  flying body's node is sometimes left behind").

Differences from the design below: awake comes from the change funnel's havok move (already hooked) and the island's
active mark, not Havok's listener objects; the guess has gravity (a frame says whether the body falls freely) but no
damping yet; a guest's relevance is distance from its copy only ("Near each guest"), not its view; the guest holds a body only
while the host's frames move it, not every body in the host's world from a cell load (step 5 decided: its player
must push and grab them; what its own physics moves alone gets the host's pose back, above).

## The idea

```
 HOST GAME (simulates)                          GUEST GAME (shows)
 ─────────────────────                          ──────────────────
 Havok steps every body                         every dynamic body of the host's world
   │ island activated  -> body awake              held KEYFRAMED (infinite mass, no sim)
   │ island deactivated -> body asleep                    ▲
   ▼                                                       │ hard keyframe toward the
 a guess goes wrong: that body's frame ─unrel.─►  buffered frames, guessed on at (now - 100 ms)
 body asleep: its rest ──────────reliable───────►  exact pose, velocity 0, record's move row

 the guest's player acts on the world:
   bumps, shouts, spells, arrows  -> its copy on the host does it there, the host's bodies react
   grabs / telekinesis            -> the guest holds it, proposes its pose, the host takes it
   lets go / throws               -> release velocity to the host, which simulates the flight
```

One rule, as for the rest of the world: **the host's game simulates every body; a guest shows the host's result,
except what its own player holds.**

## What the host sends

### Which bodies

```
a body is sent while all of:
  in the host player's bhkWorld (the party's: the tether keeps everyone in one cell or worldspace)
  dynamic motion (not fixed, not keyframed: doors, platforms, animated objects stay theirs)
  awake (its simulation island active)
  owned by a reference the guests can name (a plugin's REFR; created refs after feature 6)
      or by an actor's ragdoll while the ragdoll drives the body (dead, knocked down)
```

Awake comes from the engine's events (as built: the change funnel's havok move, the body's island mark; the
design's first version used Havok's listeners, `hkpIslandActivationListener` as HIGGS does):

```
funnel: havok move (0x4) on a REFR or ACHR -> awake.add(reference)              // engine threads, a leaf lock
host update: for reference in awake: its dynamic bodies; none with an active island -> asleep

host update (main thread, under the world's read lock), awake bodies only:
  for body in awake:
     actual = Read(body)                                                          // position, rotation, velocities
     guess  = Predict(lastSent[body], now)                                        // what the guest shows now
     if Error(guess, actual) > (2 units, 0.05 rad) or now - lastSent[body].tick > 500 ms:
        due[body].priority += Error x Relevance(body, guest)                      // near that guest, in its view
  per guest: the highest priorities first, up to its budget (~8 KB/s, ~1 KB packets)
     -> NotifyBodyFrames{tick, frames}, unreliable; a sent body's priority back to 0, lastSent = actual
  for body in asleep: rest.add(Read(body)); awake.remove(body)
  send NotifyBodyRests{tick, rests}, reliable                                      // the last frame
  + the reference's move row (kMove, as today): its record keeps the rest for cell loads
```

**Sent when the guess goes wrong, not at a rate** (dead reckoning, with a budget as in Fiedler's state sync):
both games run the same `Predict` (`Code/client/Physics/BodyPrediction`, pure: Havok's gravity while the frame says
the body falls freely, which the host reads from its velocity's change; damping later, read on each game from the
same .nif). A flight is an arc, so a throw sends a frame
at the release, at each bounce, while it rolls, and its rest: the contacts are the events. The 500 ms refresh covers
a lost packet; the budget caps a shouted shelf, the bodies nearest each guest first, the others guessed on. Ragdoll
bones are pulled by their constraints, so they guess worse and send more, only while they fall.

How other games do it, and why this one: lockstep (inputs only) needs a deterministic simulation, which Havok across
two machines is not; snapshots at a fixed rate (Source, shooters) send every moving body many times a second;
state sync with a budget (Fiedler, Unreal, GTA Online) lets clients simulate and corrects them, which fights a
guest's own simulation; local-only physics is the drift this replaces. Keyframed guests with dead reckoning keep the
host the only simulator and send the least.

### Body keys

```
BodyKey = { Owner, Index }
  Owner: a reference's GameId (mod + base id, as object changes)
         | an actor's SessionId (its ragdoll)
         | later a created reference's host id (feature 6: dropped items)
  Index: the body's place in a fixed walk
         reference: its 3D, breadth first, each node's collision object (the same .nif on both games)
         ragdoll:   hkaRagdollInstance::m_rigidBodies order (the same skeleton on both games)
```

As built the walk goes the other way: from each awake reference (or actor) to its bodies, so a body never has to be
traced back to its reference; the index is the walk's, each update.

### A frame on the wire

```
NotifyBodyFrames { Tick, Frames[] }                     (Payloads::BodyFrames, Structs/BodyFrame)
  Frame { Key: Object (GameId), Actor (session id), Index u8
          Position   3 x int24, 1/32 unit (±262144: every worldspace, no origin needed)
          Rotation   smallest three, 32 bits
          LinearVel  3 x int16, 1/4 unit/s (±8192)
          AngularVel 3 x int16, 1/512 rad/s (±64)
          Gravity    1 bit: falls freely }                         <= 32 bytes (BodyFrame::kWireBytes)
NotifyBodyRests  { Tick, Rests[] }  full floats, reliable        (Payloads::BodyRests)
```

Bandwidth (estimates, to measure with the scenarios; ~26 bytes a frame):

```
                                   every 50 ms (first draft)    on a wrong guess
  a bucket thrown, 2 s, 2 bounces   40 frames, 1 KB              ~8 frames, 0.2 KB
  a shouted shelf, 40 bodies, 3 s   21 KB/s                      capped at the budget, ~8 KB/s
  one ragdoll falling (~15 bones)   7.8 KB/s                     less, mostly while it slides
  asleep                            0                            0
  each guest gets its own packets (its budget, its nearest bodies first)
```

The thresholds (2 units, 0.05 rad), the refresh (500 ms) and the budget are constants to tune with a scenario.

## How a guest applies it

```
guest: a reference's or copy's 3D loads in the host's world (TESObjectLoadedEvent / cell attach)
  for body in its bodies with dynamic motion:
     SetMotionType(keyframed)         (77866, as HoldBodies does today)
     held.add(key)
  (its own player's bodies, and what its player holds, never)

guest: NotifyBodyFrames
  buffer[key].push(tick, frame)       // ticks on the synchronized clock, as movement's

guest update, every frame, under the world's write lock:
  t = now - 100 ms
  for key in buffer:
     a = the last frame at or before t
     target = Predict(a, t)      // the host's own guess: the host sends again where it would be wrong
     b = the next frame (or the rest), when already here: target blended into Predict(b, t) (back from b),
         arriving on b when it is due
     (a frame that came too late for that: its correction blended out over 100 ms instead of a jump)
     put the body at target, moving at its velocities; its node there (as built: no applyHardKeyFrame, which
     overshoots when the frame's time and Havok's step differ: docs/re/systems/physics.md)
guest: NotifyBodyRests
  at the rest's tick: set exactly there, velocities 0, buffer[key] cleared
```

Why keyframed, not dynamic and corrected: two games simulating the same body diverge in a frame (the "double
sword"), and a correction pulls against the guest's own simulation. Keyframed, the guest's Havok does what the host
sends and still collides properly: a moving keyframed body pushes the guest's player and the guest's own character
stands on it. Hard keyframes, not teleports, so contacts on the guest see a velocity (HIGGS and PLANCK move bodies
the same way).

Outside the host's world (a guest in an exterior cell the host's grid has not loaded): the host simulates nothing
there, so the guest leaves those bodies dynamic, its own, as today; the host's record wins when that cell next
loads on both (`world-state-sync.md`, "State at a cell load"). The host tells guests which cells it has attached
(cell attach and detach events), so a guest knows which bodies to hold.

## A guest's own actions

```
the guest's player...            on the host                                on the guest
───────────────────────          ───────────                                ────────────
walks into a cabbage             its copy's character proxy pushes it        body keyframed: the player
                                 (to measure: copies move by SetPosition,    stops at it; the push shows
                                 the proxy may have no velocity: then the    ~150 ms later from the host
                                 host pushes on the copy's contact, the
                                 engine's character-object interaction)
shouts Unrelenting Force,        its copy's cast or shot happens on the      its own impulse moves no
casts, shoots an arrow           host (copies replay casts and shots): the   keyframed body; the host's
                                 host's explosion or impact moves bodies     result arrives
kills an NPC                     the host's NPC dies there (as today) and    the copy's death action,
                                 its ragdoll falls with the host's force     then the host's ragdoll
grabs (E held) or telekinesis    GrabRequest -> taken unless someone else    the guest's game holds it:
                                 holds it; then the body follows the         dynamic, its own spring,
                                 guest's pose (hard keyframed on the host),  its pose streamed to the
                                 pushing the host's bodies; the others get   host (NotifyBodyFrames, the
                                 it in the host's frames                     other way)
lets go, throws                  ReleaseRequest{pose, velocity}: dynamic     keeps simulating it until
                                 again with that velocity, the host          the host's frames after the
                                 simulates the flight                        release arrive, then blends
                                                                             to them (200 ms), keyframed
refused grab                     -                                           the guest drops it
```

The held object is the one exception to "the host simulates": while its player holds it, the guest's game moves
it the way it moves its own character, and proposes the result, as a guest's lock or container change is a proposal
the host's game takes. The host still decides: it refuses a second holder, and everything the held object hits is
the host's simulation.

## Ragdolls

```
host: an actor's ragdoll takes over its bodies (death after the death animation, a knockdown)
      (the ragdoll driver's state: hkbRagdollDriver, Actor::IsInRagdollState)
  -> RagdollStart{session id} (reliable), then its bodies are awake bodies like any other

guest: RagdollStart for a copy
  copy not ragdolled yet (a knockdown no action replayed): put it in its ragdoll (engine call to find:
    the knock state PushActorAway sets, 39895, with no force)
  its ragdoll bodies held keyframed; the frames drive them; the copy's ragdoll post-physics job
  (39068, runs for copies) maps the bodies onto its skeleton, so the body lies as the host's

host: the ragdoll's bodies fall asleep -> NotifyBodyRests (its last pose)
host: the actor gets up -> the bodies leave the ragdoll; the copy gets up by its owner's action, as today
```

A copy dies by its owner's death action (`copy-contract.md`): the death animation plays on both games from the same
action, and the stream takes over only once the host's ragdoll does, so the two never fight.

As built (2026-10-24), dead actors only: no `RagdollStart` message. The host's actor streams once its ragdoll
bodies are dynamic and awake (a living actor's are keyframed by its animation, so they never stream). The guest
takes a copy's ragdoll body once its own ragdoll simulates it (dynamic), which follows the copy's death action, and
keeps it, so a corpse kicked again follows the host's too. Living knockdowns since 2026-10-26 ("Owners", step 3):
the copy is knocked down by its owner's frames, not by a replayed action. Still open: a dead actor's pose at a cell
load or join (below).

A dead actor already lying when a guest loads its cell (or joins): its copy spawns dead, ragdolled by the guest's
engine where it stands. The host adds the ragdoll's rest pose to the copy's spawn (`NotifyCharacterSpawn`, the dead
actor's bodies), applied as a rest once the copy's 3D is in.

### A copy's ragdoll, steered (2026-10-26)

Seen as bodies going back and forth, or heading one way and snapping back. `RagdollTrace` (`coop_test.py`; the
harness's `body_trace` with each body's bone, motion type, life and ragdoll state, and the actor's reference) now
runs in `ragdoll-lies-as-host`, `knockdown-lies-as-host` and `kill-move-kills-guest`, and fails on a copy's jitter
(an owner's own is Skyrim's). Two causes, one fixed:

```
1. placed vs simulated (fixed)
   playback: PlaceBody keyframes each body at the frame, every update
   the copy's ragdoll driver: makes them simulated again (motion 2/3 in every sample, not 4)
   -> Havok moves them on between placements; the skeleton follows one side, then the other
   now: BodyBridge::Drive steers a copy's ragdoll body (BodyPrediction::Steer: the frame's velocities plus its gap
        closed over 50 ms; SteerBody sets them, 78089/78090, which wake it); placed only when 64+ units off
   wolf killed by fireballs: 61 flickers -> 0; Lydia knocked down: 24 -> 4-7

2. the engine kept from moving the copy (fixed 2026-10-26)
   owner: the engine keeps an actor lying in its ragdoll on its root body, turned with it, in its graph update:
     Actor's BSTransformDeltaEvent sink (37996): its controller, SetRotationZ (19789), SetPosition (19790)
   copy: HookSetPosition and HookRotateZ dropped every engine move of a copy, that one too; position sync moved it
     instead, 300 ms behind at 10 a second: its skeleton strayed from its ragdoll (60-120 units on a knockdown), and
     any move of ours came after the graph update, so the skeleton showed its old pose at the new place for a frame
     (the spins and snaps seen at a get-up's start and while lying)
   now: HookTransformDelta lets 37996's moves through for a copy that LiesInRagdoll (dead or knocked down, 37491),
     and position sync leaves its place alone then (SkyrimBridge::ApplyRemoteMovement)

3. its owner's lying place taken (fixed 2026-10-26)
   owner: lying in its ragdoll, its place is its pelvis (~70 units above its feet)
   copy: position sync swept to that place, or (stepped) jumped there a frame before its own knock landed
     (KnockExplosion runs on the task queue): it stood 70 units up, and its ragdoll started from there (a pop)
   now: each snapshot says Movement::Lying (LiesInRagdoll); a copy is never placed from a lying point, and steps
     between lying and standing points (InterpolationSystem); its own ragdoll places it meanwhile

5. a jump onto its owner's place at its get-up (fixed 2026-10-26)
   copy: up from its own ragdoll (knock state 5 -> 6) it stood ~16 units from its owner; placed there at once, Skyrim
     drew its skeleton there for a frame
   now: CopyEase: from where its engine stood it onto its owner's place over 500 ms (CharacterService)

4. a living copy's rest held (fixed 2026-10-26)
   its owner's rest is its getting up; placed there (keyframed), its bodies stayed at the rest while it stood, and its
   next knock flung them from it (3400 units/s up, a 70-170 unit pop): BodyBridge::Rest leaves a living copy's ragdoll
   to get up from where it was steered
```

Also: a copy's ragdoll plays at its copy's time (`Services/CopyClock.h`, as its movement, [copy-clock.md](copy-clock.md));
references stay at 100 ms.

Readouts: `knockdown-smoke` (Lydia knocked down three times between the two players): `RagdollTrace.snaps` (a frame
its skeleton jumps and comes back) and `compare` (the copy against its owner, its delay taken out: facing, getting
up), failing on the copy's snaps its owner does not have. GameTests "A host NPC knocked down hard": the fake draws a
copy before its graph update (a lying copy moved after it counts a snap) and keeps a lying actor on its root body;
the copy stays on its ragdoll, never rises before it falls, never snaps.

After 3 and 5: four of five knockdown-smoke runs with no copy-only snap (before: one or two in every run).

Since 2026-11-11 the copy's get-up is its owner's: its clip by the graph stream's pick, and its ragdoll's bodies by
its owner's frames until it stands (they stay simulated while getting up: re/systems/physics.md, Getting up). GameTests
"A host NPC gets up from a knockdown" (the fake's `Rising`, a body its room holds: `Caught`).

Open before that (2026-10-26): the copy gets up on its own engine's timing and way (now and then 1.6 s early or 7 s late, or a
different get-up facing 40-110 degrees away for up to 2 s): its get-up follows its own knock update, not its owner's.
Skyrim pops a getting-up skeleton onto its reference for one frame at knock state 6 (owners too, up to 80 units);
a copy getting up apart from its owner shows it at another time, and knockdown-smoke counts it as the copy's own.
death-spectate-guest failed once after these changes (the host's copy of the dead guest 16 units from its body; it
passed before the physics work): not looked into.

A ragdoll driver's graph need not be an actor's (2026-10-28): a loaded Long Bow's comes through the drive-to-pose
hooks (`CopyRagdollPhysics.cpp`) too, and reading its holder as an actor crashed the client in
`dialogue-ends-on-travel`; the holder is now cast (`Cast<Actor>`) first. Only Skyrim shows it (the fake has no graphs).

### A dead body is the mover's until it rests (2026-11-21, piece 10e)

A guest's player dragged its copy of a host corpse; the host's never moved (duo: 151 units apart). Now a ragdoll of an
actor the host owns is an object's bodies on the mover path ("The mover's until its rest"): the guest's grab (40555
takes a dead actor) makes it the guest's, its frames move the host's corpse (its own actor driven like a copy's), its
rest is put there and comes back as the host's `kRagdoll` part. A hit on a corpse is the hit path's and moves no corpse
in either game (measured): left as it is. Plan: `piece-10-plan.md`, 10e.

### A copy's ragdoll rests as a pile (2026-11-21, piece 10)

```text
owner's ragdoll rests       its game declares its kRagdoll record part (BodyService::DeclareRagdolls): every body's
                            rest and its time; the host keeps it, every other game gets it, every later spawn carries it
copy, each graph update     CopyRagdollPhysics, after 58291, all its bodies together (RagdollRest)
  every body near and still     put to sleep (SleepBody: its joints make it one island), done
  off, under half a second      steered toward the rest (BodyPrediction::Steer over kRestGlideStep)
  half a second gone            all put there in one step, awake; the next update, put to sleep
                            (one SleepBody: inferred that its joints keep it one island, the solver not read;
                            measured: every copy's bodies slept from that one call, duo 2026-11-21)
  done                          untouched: a steer wakes a body (78089/78090)
  woken off it after            taken there again, twice a rest at most
lying, its bodies               in the played collision group (BodyBridge::Find), given back as it stands
```

- Before, the rest was steered 30 steps, put there awake and left: Havok settled it again 3 to 14 degrees off
  (`death-spectate-host`, 11 of 13 traces, 2026-11-12). The island sleep refuses an island with a body this game moves
  (its group: what sleeping one froze, "A pile sleeps together").
- Measured (duo, 2026-11-21, the step log `CopyRagdollPhysics: ... at its owner's rest`): the glide never brought the
  bodies within a unit in half a second, so every rest was placed; right after, the bodies moved against their joints, and
  waiting for them to stand still they slept 1.3 to 1.8 s later more than a unit off, or never (1 of 3 runs). So the
  ragdoll's own bodies' motion does not keep its island awake (`SleepBody`'s acOwnMotion), and keeps 60850's HK_REAL_MAX
  so Havok's last test (62264) lets them sleep: every copy then slept in the update after its place, near its rest and
  still, in `kill-move-kills-guest` 3 of 3 and `death-spectate-host` 3 of 3 (one run failed on a wolf's stamina).
- Played (`Physics/BodyGroups.h`): a lying ragdoll is on layer 32, which meets clutter, props and other corpses
  (`docs/re/systems/physics.md`, "A lying ragdoll's layer and group"); in its owner's game it settled among them, so
  here it passes what nobody moves and what another game moves, and meets the world and what this game moves.
- The rest was a reliable `NotifyBodyRests` sent once: a copy made later never got it ("A copy that stays off its
  owner"). `NotifyBodyRests` carries reference bodies only now.

Tests: ClientTests `[ragdoll-rest]`, `[played-ragdoll]`; GameTests "A dead copy at its owner's rest falls asleep there
and stays asleep" (the fake learned that a steer wakes a copy's ragdoll body and an unsteered awake one settles off),
"A guest joining beside a host corpse sees it lie as the host's"; TPTests the part.

## What a cell load sends

```
a guest's cell loads (CellStateRequest, as today)
  bodies at rest     their references' move rows from the host's records (kMove), as today: nothing new
  bodies awake       nothing: they are in the next frames, which the guest applies once their 3D is in
  dead actors        their copies' spawns carry the ragdolls' rest poses (above)
```

## Dropped items (feature 6, built 2026-10-26)

A created reference goes by its host id (`kCreatedModId`), mapped on each guest to its twin, so a dropped item is a
body like any other, awake while it falls (`docs/design/dropped-items.md`). A guest's own drop is made by the host's
game and the dropper's reference becomes its twin; its body stays the dropper's own until the host's rest
(`GuestHands`, as a push or a hold), everyone else plays the host's frames.

## Engine pieces

All through typed structs in `Code/client/Games/Skyrim/Havok/` with `static_assert`ed offsets, checked against
CommonLibSSE-NG and IDA. Ids checked in IDA (1.6.1170) unless marked:

| Id | Name | Use | Checked |
|---|---|---|---|
| 62478 | `hkpKeyFrameUtility::applyHardKeyFrame(pos, quat, invDt, body)` | not used (it was: guest: drive a held body; it overshoots, physics.md) | IDA: activates, sets linear then angular velocity from (target - centre of mass) x invDt; Precision, HIGGS |
| 61359 / 61360 | `hkpWorld::add/removeIslandActivationListener` | host: awake and asleep | IDA: the array at `hkpWorld+0x1F8` (CommonLib-NG `islandActivationListeners`) |
| 61366 / 61367 | `hkpWorld::add/removeWorldPostSimulationListener` | if a frame must be read right after the step | IDA: `+0x208`; Precision |
| 61383 | `hkpWorld::addContactListener` | host: a copy's contact (only if the measurement needs it) | IDA: `+0x258`; Precision |
| 77866 | `NiAVObject::SetMotionType` | guest: hold, release | in use (`HoldBodies`) |
| 78077 | `bhkRigidBody::SetMotionType` | one body | Precision |
| 78088 | `bhkRigidBody::SetActivated` | wake a body to keyframe it | Precision |
| 78089 / 78090 | `bhkRigidBody::SetLinear/AngularVelocity` | a release's velocity; a copy's ragdoll steered (`SteerBody`) | IDA: the world's write lock, `hkpEntity::Activate`, then the motion's setter |
| 60849 | `hkpEntity::activate` | | meh321, Precision |
| 58286 | `hkbRagdollDriver::getRagdoll` | a ragdoll's bodies | Precision, PLANCK |
| 39068 | `Job_Ragdoll_post_physics` | runs for copies (bodies to skeleton) | `actor-ai-and-copies.md` |
| 39895 | `AIProcess::PushActorAway` | a copy's ragdoll start, to check | Precision |
| `bhkWorld+0xC598` | `bhkWorld::worldLock` (`BSReadWriteLock`) | every read and write of a body | CommonLib-NG |
| 79133 | `bhkCharProxyController::objectInteractionCallback` | guest: its player's character's push (hooked) | IDA: scales the result's impulse by layer and mass |
| 61737 | `hkpBoxMotion::applyPointImpulse` (hkpMotion vfunc 0x14) | host: a guest's push | IDA |
| 78182 | `bhkCollisionObject::SetMotionType` (vfunc 0x2E) | both: a body simulated again (sets kSetLocal) | IDA |
| 60850 | `hkpEntity::deactivate` | guest: a rest freed asleep | IDA |
| `PlayerCharacter+0x8A0` | grab data: `grabSpring` (count +0x8C8), `grabbedObject` (+0x8D0) | guest: what its player holds | IDA (40552, 40557): 8 bytes past CommonLib-NG's SE comments |

Grab start and end: `PlayerCharacter::StartGrabObject` (40552) and `DestroyMouseSprings` (40557), read in IDA
(`docs/re/systems/physics.md`, "Grabbing"). Still to find: the ragdoll state test (`Actor::IsInRagdollState`, PLANCK), the reference of a
collidable (`TESHavokUtilities::FindCollidableRef`).

Engine notes go in a new `docs/re/systems/physics.md`: the job order around the step (`Job_Ragdoll_post_physics`
and the cell animations after it in the job list at `0x142011ba0`), the world lock, the listeners' threads.

## Tests

```
tier 1  ClientTests     frame quantization and the body walk's order; Predict (a flight's arc, damping); the host's
                        send rule (a straight flight sends nothing, a bounce sends, the 500 ms refresh, the budget
                        and its priorities); the guest's buffer (guessing on, blending into a frame it has
                        early, a late correction blended out, starting from its own pose, a rest); the awake set from activation events
        TPTests         NotifyBodyFrames, NotifyBodyRests, RagdollStart, GrabRequest, ReleaseRequest round trips
tier 2  GameTests       the fake engine gets bodies (owner, index, motion, awake, pose) and island events:
                          a host body wakes -> frames reach the guest, which holds it keyframed and follows
                          the body sleeps -> the guest has its rest and the record's move row
                          a guest grabs -> the host takes it; a second grab is refused; a throw's velocity
                          a ragdoll starts -> the copy ragdolls and follows; a late joiner gets the dead pose
                          a body outside the host's attached cells stays the guest's
tier 3  scenarios       (driving the game as a player would, waiting on body state)
          physics-push-reaches-guest      host knocks a plate off a table; guest's plate flies and lies where the host's does
          guest-bump-moves-host           guest walks into a ladle the host put on the floor before it; the host's
                                          moves, then the guest's (written 2026-10-24)
          guest-cast-moves-host           guest's fireball at the clutter (its copy's replayed cast); bodies fly on
                                          the host, then the guest (written 2026-10-24; a shout is the same path)
          guest-throw-lands-on-host       guest grabs a ladle (Activate held), carries it, lets go; the host's
                                          follows the hold and it lands at the same spot on both (written 2026-10-24;
                                          vanilla has no throw for a grabbed object: a carry and a drop is one)
          ragdoll-lies-as-host            the host kills an NPC; the copy's body lies as the host's, bone by bone
        harness readouts (TestHarness/EngineCalls): a reference's bodies (motion type, awake, pose, velocity) and an
        actor's ragdoll bodies; world-diff compares bodies, not only references' positions
```

## Build order

```
1  readouts and the fake engine's bodies                       see before changing
2  Predict and the send rule (tier 1), then host: listeners, awake set, frames on a wrong guess;
   guest: hold + follow                                           physics-push-reaches-guest
3  rests by event (PhysicsRests' polling goes), the move row kept
4  ragdolls: start, follow, rest; the dead pose at spawn           ragdoll-lies-as-host
5  guest's bumps and casts: measure, then the contact fallback if needed  guest-bump-moves-host (built: the
                                                                          guest's own impulse, no fallback)
6  grab and throw                                                  guest-throw-lands-on-host (built)
```

## Decisions (2026-10-24)

1. **A guest's grab: the guest holds it** while its player does. The other way (a spring on the host for a copy)
   would trail the guest's hands by the round trip, and the engine makes that spring only for its own player.
2. **Frames on a wrong guess, a budget per guest, 100 ms behind;** the constants start as above and are tuned by
   the scenarios.
3. **Only the host's loaded cells are simulated:** a guest far off in an exterior keeps its own physics there.

## Related

- `docs/design/world-state-sync.md`: rest positions, the move row, cell loads.
- `docs/design/copy-contract.md`: a copy dies by its owner's death action.
- `docs/re/systems/actor-ai-and-copies.md`: the ragdoll jobs run for copies.
- References: Precision (`reference/plugins/Precision/src/Offsets.h`, AE ids), HIGGS and PLANCK (VR, Havok names),
  DragAndDrop (the vanilla grab on 1.6.1170).
