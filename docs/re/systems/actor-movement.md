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
      furniture: ActorMover (41633) for sit/sleep states (flags2 & 0x1C0000)
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
| Furniture `ActorMover` (41633) | decision: sit/sleep approach | its owner's motion |
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

## Others

- Precision hooks the `ApplyMovement` call in `UpdateMovement` (+0xFB) to scale `dt` for hitstop; po3-Tweaks
  hooks the same call to clear a bumped body's collision filter (`ToggleCollision.cpp`).
- skymp drives remote actors with Papyrus `TranslateTo` toward the predicted point plus `KeepOffsetFromActor`
  for the gait (`skymp5-client/src/sync/movementApply.ts`): the engine moves them, it never teleports except
  across cells or past a distance.
