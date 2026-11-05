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

Players saw NPCs vibrate on the guest's game: 0.6 to 1.8 units back and forth, each frame, on copies going in or out of
furniture. Read on 1.7.104. `flags1 >> 14 & 0xF` is the sit/sleep state, set for chairs, beds and wall leans and on the
way in or out.

```
Actor::Move (37507)
  sit state 0    CharacterStep (78275): capsule collides, pushes; filter bit 0x8000 cleared
  sit state != 0 no step: position += delta (by GetHeading), capsule placed there, no collision
                 MoveHavok (37551) sets 0x8000: an owner in furniture has no capsule collision
then the write-back: bhkCharacterMoveFinishEvent -> 37998 -> 43691 -> TESObjectREFR::SetPosition
  its position: GetPosition(centre offset true) (79142): the proxy's place less the vector at +0xC0, rotated
HookSetPosition lets that write through on a copy while HasCopyGoal
```

So a stepped copy in furniture was written twice a frame, and the writes disagreed. Frodnar's copy, one frame:

```
Move        after 23127.230, -44667.000   = goal
write-back  to    23127.008, -44666.824   off along its facing, by 0.3 to 1.2, repeating every 3 frames
next frame  Move pulls it back to the new goal
```

Placed instead (`CopyStep::Choose`), `HasCopyGoal` is false and `HookSetPosition` refuses the write-back. Nothing is lost:
`Move` runs no step in furniture, so a step there never collided or pushed.

Measured with the `jitter` readout, frames turning back on themselves in the guest's game:

- Before, a 10-25 s window: Embry 181, 120 and 14, Frodnar 264, Hod 148 and 62.
- After, the same NPCs sent through furniture on two fresh boots: Embry 0 and 2, Frodnar 3 and 2. A 30 s window of all 22
  actors: none above 15 on any part (that 15 a walking pelvis; its place 3).
- Owners, in the two-game traces: at most 2.

Ruled out on the way, all by measurement or by reading the code:

- The calendar, sky and clock speed: about 10,000 frames with no hour going back, no weather or speed change. One 2.45 h
  jump, in the window after a hot reload's reconnect; none in the 1,805 frames after it. Not seen in play.
- Sync points (#12): 6 loads in 10 s; a graph load cannot shake at frame rate.
- The step's gain: velocity is delta / dt, 1 for a proxy controller.
- Root motion (37996): stored in the process for `ApplyMovement`, moved by it directly only in ragdoll.
- The sit branch's rotation: `GetHeading` is `rotation.z`, as `CopyStep::Local`; rotated twice only for the dead.
- A third writer: return addresses on `SetPosition` for a copy showed only 43691 and our own re-entry.

Not known: what writes +0xC0 (the offset follows the facing and repeats every 3 frames), and so why it appears now.
Copies loading an action's clips (#12, "Copies played clips before their files were in") would fit, since a clip that ends
at once moves nothing, but that was not tested. Why the user's lights looked wrong was not reproduced either.

Tests: GameTests "A copy in furniture stays on its owner's path", ClientTests `[copy-step]`, scenario
`copy-leaves-furniture-steady`. The fake's `InFurniture` copies land off their goal when stepped.

## Others

- Precision hooks the `ApplyMovement` call in `UpdateMovement` (+0xFB) to scale `dt` for hitstop; po3-Tweaks
  hooks the same call to clear a bumped body's collision filter (`ToggleCollision.cpp`).
- skymp drives remote actors with Papyrus `TranslateTo` toward the predicted point plus `KeepOffsetFromActor`
  for the gait (`skymp5-client/src/sync/movementApply.ts`): the engine moves them, it never teleports except
  across cells or past a distance.
