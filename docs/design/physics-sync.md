# Physics sync

Status: **steps 1 to 6 built 2026-10-24; owners (every game streams its own bodies) built 2026-10-26** (below, "As built" and "A guest's hand, as built"): objects and ragdolls
from the host, live; a guest's bumps, casts, grabs and drops reach the host. Design approved 2026-10-24 (the guest
holds what it grabs; only the host's loaded cells; frames sent on prediction error, not at a rate). v1.0.0 feature 5
(`docs/progress.md`, "v1.0.0 features"). Replaced the rest detection of `docs/design/world-state-sync.md`, "Physics
objects' rest" (`Bridges/PhysicsRests`, now `PhysicsBodies`), keeps its move rows.

## Owners (2026-10-26, built)

The game that simulates a body streams it; every other game plays it; the host relays. Today the split is by role
(the host sends, a guest plays, a guest's hands a path of their own), so the one body a guest simulates, its own
player's ragdoll, reaches nobody, and a copy takes ragdoll frames only once its own death put it in its ragdoll.

```text
who owns a body
  a reference (world object)     the host; while a guest's player holds it, that guest
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
    every awake reference body's 3D written from its body each update (FollowBody)
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

### Near each guest

A guest gets the frames of the awake bodies within `BodyRelevance::kGuestRadius` (4096 units, one exterior cell) of
its copy in the host's game (`HostGame::ReadPlayerPosition`), so a fight far from it does not spend its budget;
without a copy shown (joining, travelling) it gets them all. Rests go to every guest, and the host writes every awake
body's 3D wherever the players are (the engine's own write is sometimes skipped, at any distance:
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
  its player grabs one (Activate held: StartGrabObject 40552; a body the host's frames still move is keyframed, which
    the engine's grab refuses)
    -> each update its frames (BodySends' rule) as BodyFramesRequest: the host keyframes its body toward them, 100 ms
       behind (BodyPlayback), and forwards them to the other guests; the first holder keeps it
  it lets go (PlayerCharacter::grabbedObject empty) -> BodyReleaseRequest{its last frame} (reliable): the host puts
    its body there, simulated again with that velocity (bhkCollisionObject::SetMotionType 78182)
  a pushed, held or dropped body is the guest's own until the host's rest: its own physics shows it, the host's
    frames of it wait, the rest places it (then dynamic again, asleep: hkpEntity::deactivate 60850)
  its own physics moves one the host's frames never follow (its copy of a cast exploded elsewhere, a settle 2 units
    or 0.05 rad off) -> after 1 s BodyPoseRequest: the host answers that guest with its bodies' poses as rests
host game: a held body is keyframed, so not read as awake; a holder that leaves or goes silent for 2 s drops it
```

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

Also: a copy's ragdoll plays on the copies' clock (`Services/CopyClock.h`, kCopyDelayMs, as its movement); references
stay at 100 ms.

Readouts: `knockdown-smoke` (Lydia knocked down three times between the two players): `RagdollTrace.snaps` (a frame
its skeleton jumps and comes back) and `compare` (the copy against its owner, its delay taken out: facing, getting
up), failing on the copy's snaps its owner does not have. GameTests "A host NPC knocked down hard": the fake draws a
copy before its graph update (a lying copy moved after it counts a snap) and keeps a lying actor on its root body;
the copy stays on its ragdoll, never rises before it falls, never snaps.

After 3 and 5: four of five knockdown-smoke runs with no copy-only snap (before: one or two in every run).

Open (2026-10-26): the copy gets up on its own engine's timing and way (now and then 1.6 s early or 7 s late, or a
different get-up facing 40-110 degrees away for up to 2 s): its get-up follows its own knock update, not its owner's.
Skyrim pops a getting-up skeleton onto its reference for one frame at knock state 6 (owners too, up to 80 units);
a copy getting up apart from its owner shows it at another time, and knockdown-smoke counts it as the copy's own.
death-spectate-guest failed once after these changes (the host's copy of the dead guest 16 units from its body; it
passed before the physics work): not looked into.

A ragdoll driver's graph need not be an actor's (2026-10-28): a loaded Long Bow's comes through the drive-to-pose
hooks (`CopyRagdollPhysics.cpp`) too, and reading its holder as an actor crashed the client in
`dialogue-ends-on-travel`; the holder is now cast (`Cast<Actor>`) first. Only Skyrim shows it (the fake has no graphs).

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
