# Actor movement and the character controller

How the engine moves an actor each frame, which parts decide where it goes and which give it a physical body,
and what a copy needs. IDs are Address Library IDs for 1.6.1170; "our name" marks names with no community name.
Read 2026-11-02 in IDA (`ApplyMovement` 37356, `Move` 37507, `SetPosition` 37309, `UpdateMovement` 37350).
The jobs around it: `actor-ai-and-copies.md`. Held bodies against capsules: `physics.md`.

## The frame

```
Job_Actor_movement → Actor::UpdateMovement (37350)
  MovementControllerNPC::Update (41717)          decision: paths, packages' goals
  if dt > 0:
    37355
    Actor::ApplyMovement (37356)                 ← copies skip all of it today
      how far: one of
        graph bIsSynced     → the paired animation's offset (process 1406EF7F0/900)
        else                → movementController (+0x150) vfunc 7: direction, speed, turn
                              (bSpeedSynced: speed from the graph's Speed × scale)
                              translation = dir × speed × dt, rotation = turn × dt
        ragdoll/bleedout    → kFollowRagdoll (0x1000) on the controller, no translation
      ModifyMovementData (vfunc 0x11A 37990)      zero if the controller is pinned
      fly states (flags1 & 0x1C0000, not furniture as this said until 2026-11-05): ActorMover (41633)
      if the cell is attached:
        heading and pitch from the rotation
        Actor::Move (vfunc 0xC8, 37507)           ← the physical step, below
        ComputeMotionFeedbackSpeedAndDirection (0x11D)
        UpdateFeedbackGraphSpeedAndDirection (0x11E)  writes the graph's Speed/Direction
      else: TESObjectREFR::SetPosition, no controller
    vfuncs 0x11B, 0x11C(controller), 0xAF(controller), 0xB0, 1406EE240
```

`Actor::Move(dt, localDelta)` (CommonLib name, vfunc 0xC8):

```
if unseen/far (our name MoveOnNavmeshUnseen, 1406AB870):
    walk the navmesh, put the controller there, capsule collision OFF (layer 30, bit 0x8000)
    return                                        (this is what kFarAway, bit 18, gates)
input = {dt, angles, delta rotated by the heading, speed}
rigid-body controller: bhkCharRigidBodyController 78276
proxy controller:      bhkCharacterController 78275  → Havok's character step:
                       collide, slide, support, push bodies (objectInteractionCallback 79133)
state changes → fall / land actions (default objects +488 / +496) through ActorMediator
actor position = controller position × 69.99 (Havok units), land height clamp (> 30 below)
kChaseBip (0x800): follow the ragdoll's pelvis instead
```

`Actor::SetPosition(pos, true)` (37309, our `Actor::ForcePosition`): `TESObjectREFR::SetPosition`, then the
controller warped to `pos` (1406B01D0), its linear velocity zeroed (vfunc 0x38), +0x240 = 0, flag 0x80 cleared.

## Decision or physical presence

| Piece | Kind | A copy |
|---|---|---|
| `MovementControllerNPC` update (41717), its output (vfunc 7) | decision: where the AI goes | its owner's motion instead |
| `bIsSynced` paired offset | decision of the paired animation | its owner's motion |
| `ModifyMovementData` (0x11A) | decision (pinned controller) | not needed |
| `ActorMover` (41633) | decision: fly states (flags1 `0x1C0000`) | its owner's motion |
| Heading/pitch from the turn | presence | its owner's rotation |
| `Actor::Move` (37507): Havok's character step | **presence**: collision, support, pushes bodies | **needed** |
| Fall/land actions in `Move` | decision of the graph | refused on copies (`HookPerformAction`), the owner's replay plays them |
| `MoveOnNavmeshUnseen` (kFarAway) | presence, vanilla's cheap path | same as an NPC |
| Motion feedback (0x11D/0x11E): graph Speed/Direction | decision (graph floats) | would overwrite the replayed floats: skip |

## Why a copy's capsule does not push bodies

Before 2026-11-02 (below, "Copies step") copies skipped all of `ApplyMovement` and were placed each frame by `ForcePosition`. The capsule is at the copy's
position (the warp moves it), but:

- its velocity is zeroed each frame, so the character state never sees motion,
- Havok's character step never runs, so no support (`kSupport`, `kCanJump` off: flags `40200000` vs an NPC's
  `40240500`, `physics.md`), and
- the controller is a phantom: a dynamic body does not collide with it. Only the step's
  `objectInteractionCallback` (79133) pushes bodies away. Without the step nothing stops a held or falling body
  entering the capsule.

So a copy needs `Actor::Move` with its owner's motion, not `ApplyMovement`'s decisions.

## Copies step (2026-11-02)

```
CharacterService (each frame, the owner's interpolated place)
  CopyStep::Choose(lying, at, goal)   Leave (its ragdoll places it), Step, or Place past kSnap 64 units
SkyrimBridge::ApplyRemoteMovement
  Step:  the goal on the actor's extension (CopyGoal), HasCopyGoal
  Place: ForcePosition, as before
Job_Actor_movement -> HookApplyMovement (the copy's ApplyMovement)
  no controller or cell not loaded -> ForcePosition(goal)
  else Actor::Move(dt, goal - position in its own frame)
after the physics step: the engine writes the actor's place from its controller (TESObjectREFR::SetPosition 19790)
  HookSetPosition refuses that on a copy, except while HasCopyGoal
```

- **What `ApplyMovement` passes** (IDA, 0x14066A692): `Move(this, dt, &translation)`, where `dt` is its own
  argument (`xmm10`) and the translation is this frame's step in the actor's frame (x right, y forward, z up). The
  translation is direction × speed × dt, rotated by pitch only when swimming or flying (flags `0x3C000`).
- **The write-back is not in `Move`.** Its probe showed the actor's place unchanged across the call while the
  controller moved. The place is written later from the controller, through `SetPosition`. With our hook refusing
  that write, the controller ran 250 units off on its own (step velocities over 1000) while the copy stood still.
- **Measured** (`guest-holds-into-npc`):
  - the guest's bucket pressed against her copy stops 18.3 to 21 units from her axis, against 19.7 for an owner;
  - before the change it reached 1.8, and her copy's capsule was shoved 43 to 61 units off;
  - her copy's controller stays on her (0.0 off).
- **Tests:** GameTests "A guest's player presses a held plate against a host NPC's copy" (the fake's capsule pushes
  only for a stepped copy: `FakeEngine::Actor::Stepped`); ClientTests `[copy-step]`.
- **A stepped copy shoves a player it lands on** (2026-11-03, `follower-lands-on-guest`): the host's Lydia moved
  onto the guest's arrival spot; on the guest's game her copy kept its place and the guest was pushed 27 units out
  (ending 55 to 60 units apart). The arrival stack lets them pass through
  (`docs/design/host-authority-migration.md`, "Arriving together"); the fake models it (`FakeEngine::ShovePlayer`).
- **References:** TES3MP writes a copy's direction into the engine's movement settings, so the engine steps it, and
  snaps it past 40 to 80 units. PLANCK ignores an actor's collision for a moment after a warp. Notes:
  `docs/research/sync-and-testing-references.md`.

## Where a copy's step falls in the frame (2026-11-09)

Read on 1.7.104 (the frame job table at `0x1420B4D00`, `Job_VM_update` 39074, `Job_Actor_movement` 39053,
`Job_Cell_animations` 39067, `UpdateExteriorWorldJob` 18870, `bhkWorld::Step` 77851; names and comments in IDA):

```
frame N, the job table in order
  Job_Actor_movement   UpdateMovement -> our HookApplyMovement: Actor::Move toward
                       the goal set by frame N-1's update (velocity = delta / dt)
  Job_Actor_animation
  Job_VM_update        HookVMUpdate -> World::Update: copies' goals for N+1,
                       graph variables, Place (ForcePosition) at once
  Job_Cell_animations  13291 -> 18870 -> bhkWorld::Step: Havok integrates the
                       controller velocity over this frame's step
  Job_Ragdoll_post_physics, then render
```

So a stepped copy shows its goal one frame after our update sets it, a placed one the same frame: a constant lag,
but a frame held where a copy switches from Place to Step (a landing). Table order is read; that the entries run in
that order within one frame is inferred from the list's sync entries (to confirm with a frame-counter probe).
`copy-clock.md` ("In the frame") said our update runs before the movement job, from the direct call in
`Main::Update` (0x140658DB5), which runs only with `byte_143230521` set.

## A copy in the air (2026-11-03)

A stepped copy cannot jump. Havok's character step moves it across, but its height is up to its own support
and gravity: an upward step on the ground does nothing. Measured in `copy-jumps-smoothly-host`, before the fix
(heights above the ground, the owner's 74-unit standing jump in 0.9 s):

```
owner  0 → 10 → 27 → 43 → 55 → 64 → 70 → 73.8 → 70 → 61 → 49 → 33 → 13 → 0
copy  -1.8 (180 ms on the ground) → placed at 62.7 (past kSnap 64 off)
      → falls by its own gravity to 0.2 in 250 ms → placed at 71 → falls to 0
```

Two hops a jump, both ways (host's player on a guest, a guest's on the host): the stutter players saw.

```
owner: Movement::OnGround = its controller on the ground (state 0: SkyrimBridge::IsOnGround)
copy:  both snapshots around its playback time on the ground -> Step (Place past kSnap)
       otherwise (take-off to landing, swimming, climbing, flying, any state not met yet) -> Place
       late snapshots off the ground carry it on across, its height held (never into the floor)
```

The step is the exception, used where it was seen to follow faithfully (a controller on the ground, where it
gives a copy's capsule its pushes); a state we have not met falls to Place, right for position, without pushes.

skymp moves its copies with `TranslateTo` (kinematic: no gravity or support) the whole time; we keep the step on
the ground for its pushes. After the fix: copy peaks 73-74 against its owner's 74, turning once in the air.
Tests: GameTests "A jumping player's copy rises and falls with it, never stuck on the ground" (the fake's step
never rises), ClientTests `[copy-step]`, TPTests `ReferencesMoveRequest`; scenarios `copy-jumps-smoothly-host`,
`copy-jumps-smoothly-guest`.

## A copy in furniture (2026-11-05)

Players saw NPC copies vibrate on the guest's game, 0.6 to 1.8 units each frame, going in or out of a chair, bed or wall
lean. Read in IDA (1.7.104); names and comments in the database and `ida_notes_1.7.104.tsv`.

```
NPCs have a bhkCharRigidBodyController (bUseCharacterRB, Character 37171): physics simulates the capsule.
  (the player and flagged races have a proxy controller instead)
each frame   Actor::Move (37507)
               sit state 0:    CharacterStep (78275), velocity = delta / dt
               sit state != 0: StepOrStop (78276) with no step; capsule placed at position + delta
after physics  bhkCharRigidBodyController::postSimulationCallback (79185)
                 -> move-finish (38322) -> 37998 -> 43691: SetPosition(GetPosition(true))
into furniture BGSProcedureSit/Sleep -> AIProcess::SetSitSleepState (39912)
                 -> AIProcess::EnterFurnitureCollision (40063): capsule filter |= 0x8000, collision off
               leaving: SetSitSleepState clears 0x8000; Actor::Move clears it at sit state 0
```

A copy runs no AI, so it never runs `SetSitSleepState`: its sit state comes from its spawn's graph state (both actor state words) and its
owner's replayed actions, and its capsule kept colliding. In its chair or against its wall, physics pushed the capsule out each step. `HookSetPosition`
lets the write-back through while a copy is stepped (`HasCopyGoal`), so the copy moved there, and the next frame pulled
it back to its goal: the shake. The push depends on the furniture's shape, so it repeated every few frames.

The fix: a copy's capsule collides when its owner's does. Each movement snapshot carries `CapsuleCollides` (filter bit
`0x8000` clear on layer 30, `Actor::CapsuleCollides`), and the copy's capsule is set to match (`Actor::SetCapsuleCollision`,
37006). This is not a furniture rule: it follows whatever turned the owner's collision off (furniture 40063, mounting, a
mod), and a copy in a chair no longer shoves a player walking past.

A copy whose owner's capsule collides with nothing is placed, not stepped (`CopyStep::Choose`): out of furniture
`Actor::Move` clears `0x8000` before its step (`0x14068990A`, then 37006), so a step would turn the copy's collision back
on. The engine does not step such an owner either: in furniture `Move` takes its no-step branch, far off it moves on the
navmesh.

The capsule is not all 40063 does: it also puts the actor's own 3D's bodies, keyframed, on layer 33 in the world, and the
crosshair takes a seated actor only through them, and it detaches the actor's graphs from the physics world first. Since
2026-11-09 a copy's bodies and graphs mirror its owner's as its capsule does (`Movement::BodiesLayer`, `GraphsStepPhysics`;
`dialogue.md`, "The crosshair on a seated actor").

So an NPC moving far from the host's player (`MoveOnNavmeshUnseen`, past `fVisibleNavmeshMoveDist` 4096 or
`fNotVisibleNavmeshMoveDist` 2048), its collision off, has a copy the guest passes through, as the owner would.

Measured with the `jitter` readout, frames turning back on themselves while in furniture, two rounds of six Riverwood
NPCs sent through their furniture (about 20 s each):

- stepped, collision on (the old code): 824 and 134
- stepped, collision off as the engine does it: 1 and 0
- placed: 0 and 0
- both: 0 and 0; the scenario on two fresh boots: 0 in furniture, at most 3 overall
- owners: none

Collision off alone stops it, so the first fix (placing copies in furniture, keyed on their sit state) was dropped for the
generic one.

The first account here blamed the controller's centre offset `+0xC0`. Its writers are constructors (bhkCharProxy
ControllerCinfo, 0x1410970A0), so it does not change frame to frame. Also ruled out, by measurement or by reading:

- The calendar, sky and clock speed: about 10,000 frames with no hour going back and no weather or speed change. One
  2.45 h jump came in the window after a hot reload's reconnect; none in the 1,805 frames after it. Not seen in play.
- Sync points (#12): 6 loads in 10 s. A graph load cannot shake at frame rate.
- The step's gain: velocity is delta / dt.
- Root motion (37996): stored in the process for `ApplyMovement`; moves the actor directly only in ragdoll.
- `Move`'s sit branch heading: `GetHeading` is `rotation.z`, as `CopyStep::Local`.

Why it appeared now: copies have been stepped since 2026-11-02 (above, "Copies step"). Before that they were placed every
frame, and `HookSetPosition` refused every write-back. The animation merge (#12) is not the cause.

Tests: GameTests "A copy's capsule collides only when its owner's does" (the fake's colliding copies in furniture are
pushed off their goal, and a step out of furniture turns collision back on; fails without the sync or without placing),
ClientTests `[copy-step]`, TPTests `ReferencesMoveRequest` (the new field), scenario
`copy-leaves-furniture-steady`.

## A culled actor keeps its pose (2026-11-05)

Read in IDA on 1.7.104 (`owners-walk-slide`: 40335, 106433, 38054, 38009, 37361), checked on the duo. Why the host's
own NPCs seemed to "move without walking" after an arrival: the engine does not pose a skeleton the camera culls.

```
HighActorCuller::CullActor (40335, Precision's CullActors), each high actor, each frame:
  nibble = 2 if its 3D has a fade node | 4 if faded in (+0x130 >= 1e-5)
  if faded in and not yet culled: | 1, occluder test (BSCullingProcess::IsNotOccluded 106433:
                                        occlusion planes and boxes)
      failed -> 3D flags |= kNotVisible (1 << 20), nibble &= ~2
  Actor+0x27C low nibble = nibble     7 drawn, 5 faded in but occluded, 2 not tested
Actor::ModifyAnimationUpdateData (38054), the graph update's data:
  visible = !(3D flags & kNotVisible) or seen within a few frames
  force   = kForceOneAnimgraphUpdate / kForceAnimGraphUpdate (copies) / keyword
  neither -> the graph runs (its events fire, Speed follows the package) but no pose is
             written: the bones keep their last pose while the controller moves the actor
Actor::ProcessTracking (38009): kWasInFrustum (+0xE8 bit 21) = (nibble == 7)
  written only here, and ProcessTracking is gated (37361), so the bit goes stale
```

Measured 2026-11-05 (duo, animation trace with root, feet, cull and movement state): host owners in cull state 5 move
their feet 0.01-0.26 units per unit travelled, drawn ones (2 or 7) 0.74-1.17. Dorthe ran 1,234 units culled with her
foot bones still, while `kWasInFrustum` read true. Copies on the guest stay posed when culled (0.51-1.42): we set
`kForceAnimGraphUpdate` on them. The gait check (`GaitWatch`, `check_gait`) now counts drawn steps only, and the
observer's pose comparison reads `pose_visibility.drawn` (the 3D's `kNotVisible`) instead of `was_in_frustum`
(`culled-npc-pose-readings.md`). Vanilla control (`ref_steps vanilla=true`, the stand-in started in Riverwood): the
same NPCs walk with steady steps.

## Motion-driven and the Virtual Path Follower (2026-11-05)

Read in IDA on 1.7.104 (every ID below read; measured where it says so). `MovementControllerNPC` (Actor+0x150, `Code/client/Games/Skyrim/AI/Movement/`) has a movement
state at +0x1C0 and two agent sets, picked through its `IMovementMotionDrivenControl` (+0x128):

```
slot 1 SwitchToLoadedSet (41701)    +0x1C9 = 1: standard path agents (Bethesda's functor name)
slot 2 SwitchToUnloadedSet (41702)  +0x1C9 = 0: the set with the Virtual Path Follower
slot 3 StartAnimationDriven (41704) graph event AnimationDriven (42875): the graph's root motion moves it
slot 4 StartMotionDriven (41703)    graph event MotionDriven (42874): the controller moves it;
                                    unloaded set -> state 10/11, the Virtual Path Follower (Advance 92556):
                                    speed x dt along the path, a position write, no animation
slot 6 IsMotionDriven (41706)       states 1,2,3,7,8,10..14; slot 7 (41707) animation-driven 0,4,5,9
who switches:
  Actor::CreateMovementController (37793)  high -> loaded set, else unloaded
  Actor::MoveToHigh (37621)                -> loaded set + StartMotionDriven
  MoveToMiddleHigh (37624) / MoveToMiddleLow (37623) / MoveToLow (37622) -> unloaded set + StartMotionDriven
  MovementControllerNPC::LoadGame (41716)  saved state 10/11 -> unloaded set
Character::UpdateProcessLevel (37501) <- GetDesiredProcessLevel (37503):
  3D and cell attached -> high; loaded, not attached -> middle-high
```

A middle-high actor is in no animation job (only high actors are, 41466), so the Virtual Path Follower steps it every
few hundred ms while its graph stands. Seen 2026-11-05: Riverwood's 0x13491 went middle-high when the party entered the
Sleeping Giant (the exterior stays loaded, not attached) and stepped 36 times until the party came out. No player can see
it there. Once (unexplained, process level not yet recorded): Gerdur stepped 39 units every 0.46 s with an idle graph
while the party stood in Riverwood; not seen again in 55 s of co-op or 60 s of vanilla traces.

## Others

- Precision hooks the `ApplyMovement` call in `UpdateMovement` (+0xFB) to scale `dt` for hitstop; po3-Tweaks
  hooks the same call to clear a bumped body's collision filter (`ToggleCollision.cpp`).
- skymp drives remote actors with Papyrus `TranslateTo` toward the predicted point plus `KeepOffsetFromActor`
  for the gait (`skymp5-client/src/sync/movementApply.ts`): the engine moves them, it never teleports except
  across cells or past a distance.
