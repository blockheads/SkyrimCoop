# Physics bodies (Havok 2010)

What physics sync (`docs/design/physics-sync.md`) reads and drives, checked in IDA on 1.6.1170. Structs:
`Code/client/Games/Skyrim/Havok/bhkCollision.h`.

## From a reference to its bodies

```
TESObjectREFR -> its 3D (NiNode) -> each node's collisionObject (NiAVObject+0x40, bhkNiCollisionObject)
  -> body (+0x20, bhkWorldObject: a bhkRigidBody or bhkRigidBodyT) -> referencedObject (+0x10, hkpRigidBody)
hkpRigidBody -> world (+0x10, ahkpWorld) -> userData (+0x430, bhkWorld) -> worldLock (+0xC598, BSReadWriteLock)
```

A body's index (`BodyKey::Index`) is its place in a breadth-first walk of the 3D (`RigidBodiesOf`): the same .nif on
every game gives the same order.

## hkpRigidBody

| Offset | Field | Source |
|---|---|---|
| 0x130 | `simulationIsland` (hkpEntity) | CommonLib-NG; `hkpEntity::Activate` (60849) reads it |
| island+0x32 | bits 0-1 `isInActiveIslandsArray`, bits 2-3 `activeMark`: awake when `activeMark` is set | `hkpEntity::Activate`: `(byte[island+0x32] >> 2) & 3` |
| 0x150 | `motion` (hkpMaxSizeMotion) | CommonLib-NG |
| 0x160 | motion type (`hkpMotion::type`, +0x10): 1-3, 6 dynamic, 4 keyframed, 5 fixed, 7 character | `hkpEntity::Activate` skips 5 |
| 0x170 | `motionState.transform`: rotation columns, translation at 0x1A0 (Havok units) | CommonLib-NG |
| 0x1B0 | `sweptTransform`: centre of mass 0/1, rotation 0/1 (0x1D0, 0x1E0: x, y, z, w), local centre of mass (0x1F0) | `applyHardKeyFrame` reads 0x1C0 and 0x1F0 |
| 0x230, 0x240 | linear and angular velocity (motion +0xE0, +0xF0) | CommonLib-NG |

Havok units are the game's times 0.0142875 (`cHavokToGame`); angular velocity is rad/s in both.

## Calls

| Id | Name | Signature on 1.6.1170 | Notes |
|---|---|---|---|
| 62478 | `hkpKeyFrameUtility::applyHardKeyFrame` | `(const hkVector4* pos, const hkQuaternion* rot, float invDt, hkpRigidBody*)` | Velocities that reach the pose by the next step: linear from the target centre of mass (pos + rot·local COM) minus the current, angular from the rotation difference; activates the body. Takes no lock. Not used: see "A played body is placed" |
| 78077 | `bhkRigidBody::SetMotionType` | `bool(bhkRigidBody*, uint32 type)` | **Two arguments on AE** (Precision's SE typedef has four): calls `hkpRigidBody::setMotionType(body, type, activate 1, full check 0, -2)` (60908) under `HavokWorldEnterWrite`. Refuses keyframed/fixed to dynamic for a body without mass |
| 78082 | `bhkRigidBody::SetPositionAndRotation` (vfunc 0x37) | `(bhkRigidBody*, const hkVector4*, const hkQuaternion*)` | The Havok body's pose: `hkpRigidBody::setPositionAndRotation` (60898) under the world's write lock. `bhkRigidBodyT`'s override (78612) first applies its local offset (+0x40 rotation, +0x50 translation) to a node pose; we pass the Havok body's own pose, so the base call |
| 78089, 78090 | `bhkRigidBody::SetLinearVelocity`, `SetAngularVelocity` | `(bhkRigidBody*, const hkVector4*)` | CommonLib-NG |
| 77866 | `NiAVObject::SetMotionType` | `bool(NiAVObject*, type, recurse, force, allowActivate)` | every body under a node (`HoldBodies`) |
| 77928 | `bhkWorld::HavokWorldEnterWrite` | `(bhkWorld*)` | `LockForWrite(worldLock)` when `bUseWorldLock:HAVOK` is set |
| 61359, 61360 | `hkpWorld::add/removeIslandActivationListener` | | the array at `hkpWorld+0x1F8`; not used yet |
| 61366, 61367 | `hkpWorld::add/removeWorldPostSimulationListener` | | `+0x208`; not used yet |

## A keyframed body and its node

A body and its node sync one way, by motion type (`bhkNiCollisionObject::UpdateWorldData` 78378, run by
`NiAVObject::Update` when the collision object has `kSyncOnUpdate` 0x80; checked in IDA 2026-10-24):

```
collision flags at +0x18: 0x1 active, 0x4 notify, 0x8 set local, 0x20 use velocity (from the update's 0x2000),
                          0x40 reset, 0x80 sync on update; clutter (the ladles) has 0xAD
keyframed or fixed (78180: motion type 4 or 5)  -> node to body (78178): with use velocity, keyframed toward the node
                                                    (78100), else put there (SetPositionAndRotation, vfunc 0x37)
otherwise (dynamic)                             -> body to node: vfunc 0x2B (bhkCollisionObject 0x140E972C0) reads
                                                    the body's pose, then 78382 writes the node's world (and local,
                                                    with set local) and, with notify, calls the moved-reference
                                                    handler (0x1402EDDD0, which moves the reference)
```

So a keyframed body moved by `applyHardKeyFrame` never moves its node: the guest's ladle body followed the host's
while its 3D and reference stayed at the last rest until the next rest's move row (found 2026-10-24 by sampling
`world_state`'s `collision` against `position_3d` and `pose`). `GuestEngine::DriveBody` therefore calls vfunc 0x2B
itself after each drive (`FollowBody`), then `NiAVObject::Update` (70251, flags 0) for the node's children and
bounds; that update puts the keyframed body where it already is, which keeps the keyframe's velocities. HIGGS does the
reverse (`UpdateKeyframedNode`: the node set, then updated with 0x2000, the body keyframed toward it), and refreshes
the shadow scene node's list, since a keyframed node is not "dynamic"; not done here yet.

## A played body is placed, not keyframed over the frame

`applyHardKeyFrame` with `invDt` as one over the frame's time gives the velocity that reaches the pose in that time,
but Havok then moves the body by its own step (16 ms, sometimes two in a frame). Under Wine a frame can take 1 ms and
the next 24 ms: after the short one the velocity is many times too high and the body overshoots by 10-12 units, and
the next frame pulls it back (measured 2026-10-25 with the harness's `body_trace`: the guest's 3D one frame off by
11 units and back, two or three times per knocked ladle). Both games' followers (`GuestEngine::DriveBody`,
`HostEngine::DriveBody` for a guest's held body) therefore put the body at the pose each frame (`PlaceBody`:
SetPositionAndRotation 78082, then the pose's velocities, so it still pushes what it touches), and its node there.

## A flying body's node is sometimes left behind

On the host, a knocked body sometimes flies while its 3D and its reference stay where they were, through its rest:
the engine's body to node write (78382, from the collision object's `UpdateWorldData`, 78179 and 78378, flags
`0xAD`) does not run for it. First seen 2026-10-24 with the player about 3000 units away (300 away it followed), and
taken for a distance cut; measured 2026-10-25 it is none. A probe (the client's fireball knocks the host's ladles,
the host's own write off, the host's player placed at a distance) found the 3D left behind at 400 units in two runs
and followed at every distance from 780 to 2700; in a third run of the same knocks it followed at 400 and was left at
1550. No INI setting names a havok or physics distance. The cause is untraced; the island activation (25849, 25857)
moves the 3D under the cell's `DynamicNode` either directly or through the task queue, which is where to look next.

`PhysicsBodies` (host) therefore writes an awake reference body's node from its body (`FollowBody`, the engine's own
dynamic write), wherever the players are, but only when the node is off its body (more than 1 unit or 0.01 rad;
followed, the node's world sits on the body to 0.1 unit and its rotation matrix, read as is, is the body's
quaternion). Writing a followed body's node again re-places the body, which loses its contacts: a dropped bucket
went 21 units deeper into a head than in a game without a session (2026-11-01, `docs/design/physics-sync.md`,
"Matches a game without co-op"). That holds with the node's `NiAVObject::Update` and the collision object's
`kNotify` both left out: the write (vfunc 0x2B) itself does it.

## Motion type and kSetLocal

Waking a body runs the engine's island activation listener (25857), which takes its reference's extra data lock
(19556) under the world's write lock; `ExtraDataList::RemoveExtraData` takes them the other way round (77803). A free
at a rest therefore never wakes the body: `hkpRigidBody::setMotionType` (60908) without activation, under the world
lock and 78077's spin lock (411545).

`bhkCollisionObject::SetMotionType` (78182, the collision object's vfunc 0x2E; checked 2026-10-24) calls
`bhkRigidBody::SetMotionType` (78077) and then sets the collision object's `kSetLocal` (0x8) for a dynamic motion
and clears it otherwise. `NiAVObject::SetMotionType` (77866) reaches it for every collision object under a node
(visitor 77883, callback at `0x14202C618`). 78077 alone leaves the flags. Without `kSetLocal`, the body to node write
(78382) sets only the node's world transform, and the node's next update puts it back from its local one: a body
keyframed by 77866 (`HoldBodies`) and freed by 78077 alone showed its 3D frozen at its start while its body flew
(found 2026-10-24 by `guest-cast-moves-host`'s readout: "3D 0 off its start and 124 from its body"; flags 0xA5
against clutter's 0xAD). So a body is freed through 78182 (`FreeBody`), and `FollowBody` sets `kSetLocal` for its own
write. 78077 also takes a global recursive spin lock (`0x14328DEF8` owner, `0x14328DEFC` count) around
`hkpRigidBody::setMotionType` (60908, activation 1).

A body freed at a rest is put to sleep (`hkpEntity::deactivate` 60850: its island's marks cleared, the world told),
else a pose Havok had deactivated mid-rock (a ladle) rolls off again when it wakes.

## A player's push

The character proxy gives a dynamic body it walks into an impulse each step. `bhkCharProxyController`'s
`hkpCharacterProxyListener::objectInteractionCallback` (79133, vfunc 5; checked 2026-10-24) forwards to the
controller's own (`0x140EA1600`), which scales the result's impulse (`hkpCharacterObjectInteractionResult`
+0x00, its point +0x10) by the body's layer (x2, x3 for layer 10 when light, x0.1 for layers 6 and 14) and zeroes it
for a body without mass or heavier than `0x14202C8B8`; the body is the event's +0x30. Havok applies the result after
the callback, so a hook after the original reads what the body gets (`PlayerHands`). The listener is 0x10 before its
`bhkCharacterController` (the player's: `MiddleProcess+0x250`). A keyframed or fixed body gets no interaction (it is
a wall to the proxy), so a guest frees its bodies at rest for its player. The host applies a guest's push with
`hkpEntity::activate` (60849) and the motion's `applyPointImpulse` (hkpMotion vfunc 0x14; `hkpBoxMotion` 61737:
linear velocity += impulse x inverse mass, angular from the point's lever arm), Havok units, under the world's
write lock. `contactPointAddedCallback` (79131) fires for every new contact, the floor included: not a push.

A copy is moved by SetPosition and pushes nothing (`guest-bump-moves-host`, measured 2026-10-24: its copy came within
14 units of the host's ladle).

## A swing's hit (read in IDA 2026-11-13)

A melee swing's hit frame (`HitFrameHandler::Handle` 42828 -> 38603) calls
`Actor::CalculateCurrentHitTargetForWeaponSwing` (38628, `0x1406CD850`). It shape-casts the swing
(`hkpAllCdPointCollector`); an actor it meets is returned as the target. A dynamic body that is not an actor gets a
knock in place (read in IDA):

```text
38628, the swing meets a body (collector's first hit; its reference may be none)
  impulse = swing direction x min(cap 0x1420A6668, weapon weight + 3) x 0x1420A6650
            scaled down by mass / 0x1420A6680 for a lighter body (hkpMotion::getMass 60959)
            x 0x1420A6698 for layers 8, 32, 33; x 0.0142875 to Havok units
  world write lock -> hkpEntity::Activate -> motion's applyPointImpulse (vfunc 0x14, call at +0x6BE)
                      (impulse, the hit's contact point)
  TESHitEvent for the reference (ScriptEventSourceHolder+0x5D8)
```

Where the swing reaches (read in IDA 2026-11-13): 38628 gets its start and look from `GetEyeVector` (Actor vfunc
0xC2, no camera offset). `Actor::GetEyeVector` (37479) looks along the heading only (z 0); `PlayerCharacter`'s
(40505) starts at the first-person camera node and looks along heading and pitch. `LinearCastFrom` (25931) sweeps the
shared collector's sphere (`GetAllCdPointCollector` 25925 -> 25930: radius `fHitCasterSizeSmall`, 12) from there for
the reach (`fObjectHitWeaponReach` 150; anim types 5-6 `fObjectHitTwoHandReach` 112; hand to hand
`fObjectHitH2HReach` 64; no weapon `iActivatePickLength`), ignoring the swinger's group (25932). The hits are sorted
nearest first (60336) and the swing takes the first: for an object flat on a floor or table that is often the surface
under it. The harness's `swing_cast`, `aim_swing` and `last_swing` read this same cast (below).

It runs only in the swinger's own game: the host's copy of a guest's swing hits nothing
(`guest-swing-moves-host`, measured 2026-11-13: the guest's bucket woke, the host's never did). So before the
fix the guest's own physics moved it alone, `GuestHands` asked the host's pose a second later and the bucket
snapped back. `PlayerHands` now replaces the `call [rax+0A0h]` at +0x6BE with a call to its own (which calls
the motion's) and wraps 38628 to know whose swing it is: the player's hits go to the host as pushes
(`BodyPushRequest`), applied alike there.

A hit on a body the host's frames move (keyframed on the guest: knocked, rolling, or woken) moves nothing in either
game: a keyframed motion's inverse mass is 0, so `getMass` (60959) reads 0 and 38628 scales the impulse to 0
(read in IDA; measured on the duo with `last_swing`: the swing met the ladle with the line-up's look, no hit recorded,
the ladle stayed at 0.0 units/s). The host kept a ladle awake (0.0-0.09 units/s) with the guest's copy beside it, so
the guest's stayed keyframed and unhittable: the copy's own step woke it each frame (fixed 2026-11-13, below, "Woken
still"). A body really moving on the host's frames is no longer keyframed on the guest, so the swing knocks it
(2026-11-13, `docs/design/physics-sync.md`, "One rule").

## Every hand skips a keyframed body (read in IDA 2026-11-13)

Each way a player moves a body ignores one keyframed (motion type 4), so a body played keyframed is out of reach:

- walking into it: the character proxy gives it no interaction (79133, "A player's push");
- a swing: 38628 scales the impulse by `getMass` (60959), 0 for a keyframed motion ("A swing's hit");
- a shout: `ConeProjectile::ProcessHits` (43802, our name) pushes a hit body only when `hkpMotion::IsDynamicType`
  (78109, our name, read: motion types 1-3 and 6) or its layer is 20, through `bhkRigidBody::SetLinearImpulse`
  (78091) and a random angular impulse;
- an explosion: `Explosion::ApplyHavokImpulses` (43850, our name) runs once over the bodies its phantom
  (`Explosion+0xC0`) overlaps, with the same gate, then `Explosion::ApplyImpulseToBody` (43854): linear impulse from
  the blast's centre, scaled by mass, and a random angular one (`GetRandomNumber` three times), so two games' blasts
  never match;
- a grab: `CreateMouseSprings` (40555) refuses types 4 and 5 ("Grabbing").

Each knows its cause where it pushes: an explosion its `actorOwner` (`+0xF8`) and `actorCause` (`+0x108`,
`GetActorCause` 43846), a shout its projectile's shooter, a swing its swinger.

Contacts: the engine adds contact listeners only through `bhkWorldObject::AddContactListener` (77843, our name), which
calls `hkpWorld::addContactListener` (61383: the array at `hkpWorld+0x258`); Precision registers its own the same way
after `requireCollisionCallbackUtil` (61437) and hears every contact point with both bodies.

Sleep (read in IDA 2026-11-13 unless marked):

- `FOIslandActivationListener::islandDeactivatedCallback` (25850) is the engine's work when Havok deactivates an
  island. Per body it calls `PopTemporaryMass`, does the 3D's bookkeeping through the task queue, sets references
  flagged in their extra data to fixed (`NiAVObject::SetMotionType` 77866, 5), and keyframes large debris (layer 20;
  78077, 4).
- `hkpEntity::deactivate` (60850) clears the island's marks. It calls `moveIslandBetweenActiveLists` (61513, our
  name), which moves the island between the world's active (`+0x40`) and inactive (`+0x50`) arrays through 61511
  (island bookkeeping), then `markIslandInactive` (61523, our name, the mirror of `markIslandActive` 61525). That
  clears the activation bits and queues the island on the world's list at `+0x60`. Last, it resets each body's sleep
  reference (`+0x25C`, `+0x26C`; Havok's per-body deactivation reference, inferred from its motion layout).
- None of these calls the island listeners (world `+0x1F8`). They run where the world drains `+0x60`, not found yet
  (IDA's byte search returned nothing).

Havok sleeps an island only when all its bodies are quiet, which is why one animated body keeps its neighbours awake
("Woken still"). Since a reference's rest now comes from its own stillness, the two games differ here:

- On the host, 25850's work waits for Havok's island sleep, as in a game without co-op: beside a leaning Orgnar,
  never.
- On a guest, placing the host's rest calls 60850 (`BodyBridge::SettleFound`), as before this change. That queues the
  island inactive at once. Whether 25850 then runs for it is not read.
- Seen on the duo: the guest's object stays dynamic and its pose matches (`object-rests-beside-orgnar`). A reference
  that 25850 makes fixed on sleep could still end up fixed on the guest and dynamic on the host; that is not
  measured.

## Rays

`bhkWorld::PickObject` (vtable 0x33, 77860; checked 2026-10-24): a `bhkPickData` (CommonLib-NG, 0xD0) with `from` and
`to` in Havok units and the ray's layer in the input's filter info (+0x24, low 7 bits); with no collector, the closest
hit lands in the output (`hitFraction` +0x40, `rootCollidable` +0x80). It takes the world's read lock, and refuses
when the frame's pick budget is spent (`pickFailed` +0xC0). The world: `TESObjectCELL::GetbhkWorld` (18995). A hit's
reference: `TESHavokUtilities::FindCollidableRef` (26003); its layer: the collidable's filter info (+0x2C). Used by the
test harness only (`ray`, `surroundings`, the checked `place`).

## When a body is awake

The engine records a reference's havok move (change flag 0x4, `TESForm::AddChange`) every frame its body moves
(`PhysicsBodies` watches the change funnel for it). Each update the host reads those references' bodies; a body is
awake while its island's `activeMark` is set, and a reference rests when none of its dynamic bodies is awake (Havok
deactivates an island after `kNumInactiveFramesToDeactivate` quiet frames and zeroes its velocities).

Since 2026-11-13 a reference's body also rests once it keeps one pose for 300 ms (`BodyPrediction::Still`): its
island can stay active for good beside an animated body ("Woken still"), measured with a cheese wheel on Orgnar's bar
(`object-rests-beside-orgnar`: the host's body awake, no island sleep in 6 of 7 drops).

Havok's own listeners (island activation, post simulation) would say the same per island; they need an object with
Havok's vtable registered on each world as the player changes worlds. Not needed while the funnel's event and the
island's mark answer it.

## Order in a frame

The job list at `0x142011ba0` runs the cell animations and then `Job_Ragdoll_post_physics` (39068) after the physics
step; our update runs in the Papyrus VM's (`SkyrimVM::ProcessRegisteredUpdates` 53926, from `Main::Update` 36564).
The step itself (read 2026-11-09 on 1.7.104): `Job_Cell_animations` (39067), the job table's entry after
`Job_VM_update`, runs `TES::RunCellAnimationsAndPhysics` (13291, our name), which queues `UpdateExteriorWorldJob`
(18870) and that calls `bhkWorld::Step` (77851, our name: bhkWorld vfunc 50, Precision's pre-physics hook). The
physics substep count comes from `UpdateTimeManager` (`uMaxNumPhysicsStepsPerUpdate:HAVOK` 389138). Our playback of
bodies runs earlier still: `MainLoopTasks` drain in `HookMainLoop`, before `Main::Update` (36564), so a played body is
placed at its pose before the frame's step, and its node is drawn there (keyframed: node to body).

## Havok's step (S4, 2026-11-01)

`UpdateTimeManager` calls `sub_140E85250` (77850) each frame with the frame's seconds: the step is
`fMaxTime:HAVOK` (389102, default 1/60; `fMaxTimeComplex` 389144 while a complex scene runs) times the clock's
multiplier, and the time to simulate is `min(leftover + frame, uMaxNumPhysicsStepsPerUpdate (389138) × step)`, the
rest carried to the next frame. So vanilla steps by 1/60 whatever the frame rate. Above 60 fps the step is longer
than the frame and resting clutter wakes and creeps: the playtest's "objects float across the room", seen in single
player at 144 Hz and with Skyrim Together. At 60 fps with `fMaxTime` 0.04 (2.4 frames, as 1/60 is at 144 fps) 26
more objects in the Sleeping Giant Inn stayed awake, one moved 114 units in a minute.

The community fix (HIGGS `UpdatePhysicsTimesHook`, HavokFix, SSE Display Tweaks' Havok option): before that call, set
`fMaxTime` to the frame, split into equal steps no longer than 1/60 when the frame is long. Ours is
`Fixes/HavokStepFix.cpp` (`Physics::HavokStep`), keeping the INI's complex-to-simple ratio. It writes the same values
those mods would, so running with them is harmless. Scenario `clutter-rests-at-any-frame-rate`; `ClientTests`
`[havok-step]`; no GameTests flow (the fake has no physics).

## Grabbing

`PlayerCharacter::StartGrabObject` (40552, `0x14073ec70`; checked in IDA 2026-10-24): with no grab going (`+0xAFC`
zero), takes the crosshair's reference (`CrosshairPickData` +4), its 3D's collision object's rigid body, and if the
body's mass is at most the limit (`dword_142006168`) sets up the mouse springs (40555), then tests the springs' count.
`PlayerCharacter::DestroyMouseSprings` (40557, `0x1407414b0`): for each spring, removes it and activates its body
under the world's write lock, then looks up the held reference at `+0x8D0`, sends a script event for it and sets
the handle invalid. **The grab data sits 8 bytes later on 1.6.1170 than CommonLib-NG's comments** (its SE layout):
the springs (`BSTSmallArray<hkRefPtr<bhkMouseSpringAction>, 4>`) at `+0x8A0` (capacity), `+0x8A8` (data), `+0x8C8`
(count); `grabbedObject` at `+0x8D0`, not `0x8C8` (`PlayerCharacter::grabbedObject`; `world_state`'s player status
`grabbed`). Holding Activate past `fZKeyDelay:Controls` grabs; letting go drops it with its motion (read on 1.7.104,
2026-11-14: `DestroyMouseSprings` takes each spring off with its vfunc 0x150 and only `hkpEntity::Activate`s the body,
velocities untouched, so it flies on as the spring left it (the hand's speed: inferred); physics-sync.md, "The mover's until its rest"): vanilla has no
throw for a grabbed object (no `fZKey...Throw` setting; telekinesis has its own). The harness holds Activate as the
`activate` button. `StartGrabObject` needs the body in a world (`bhkSerializable` vfunc 0x27 `GetWorld1`) and light
enough (a keyframed body's mass reads 0); `PlayerCharacter::CreateMouseSprings` (40555, our name, `0x140752630`; read
on 1.7.104, 2026-11-13) sets the grab mode (`+0xB04`: 1 the player's, 2 telekinesis) and then refuses a body whose
motion type (`hkpRigidBody+0x160`) is 4 (keyframed) or 5 (fixed): `DestroyMouseSprings` at `0x140752ea3`. So a guest
grabs a resting body (dynamic, asleep), not one the host's frames still move (a hook freeing it first was removed,
`docs/progress.md`, loose ends).

`ActivateHandler::ProcessButton` (42420, `0x1407b2660`; read 2026-11-13): a press marks an activation pending
(`+0x18`); held past `fZKeyDelay:Controls` once, `StartGrabObject`; on release, a grab going (mode 1) lets go, else
the pending activation runs (`ActivatePickRef`): a refused grab turns a held E into a pickup, and an owned item's
pickup is a theft (`StealAlarm` 37422, `crime.md`). A grab holds the object where
the crosshair was (carried looking down at the floor, it hangs just off it); turning the player at once
(`setangle`) drops it, as does jamming it against something that does not give.

## Woken still: awake is not moving (2026-11-13)

Skyrim wakes resting clutter it never moves. The Sleeping Giant's bar bottles (33284, 33287, 33288, 3327A, 3328A) wake
15-18 times a minute and sleep where they were. Found with the harness probe `activate_watch` (MCP verb `who_wakes`:
hooks on `hkpEntity::Activate` 60849 and `markIslandActive` 61525, each hit with its call stack and the body it went
through), in a game with no session. The stack was measured; each step below was then read in IDA and named:

```
ProcessActorAnimSG (an actor's animation job, job thread)
  Character::RunPendingNodeUpdate (37371): its node update when pending
    Character::UpdateNode (37976): the 3D's NiAVObject::Update, flags 0x2001 -> NiNode::UpdateDownwardPass
      bhkBlendCollisionObject vfunc +0x160 (78910): its keyframed body to its bone
        bhkCollisionObject (78178) -> bhkRigidBody::DriveToPoseByVelocity (78100)
          bhkRigidBody::SetLinearVelocity (78089): hkpEntity::Activate (60849), under the world's write lock
            hkpWorldOperationUtil::markIslandActive (61525) on that body's island
```

- The body is Orgnar's (13486, motion type 4, keyframed by his animation) while he leans on the counter (sit state 3,
  `IdleCounterStart`, 17 of 18 ragdoll bodies awake). It shares the bottles' simulation island, so each update
  re-activates them (measured: every wake of the bottles went through 13486).
- A character standing against clutter does it too, through its proxy: `hkpCharacterProxy` 62648 calls `Activate` and
  `applyPointImpulse` on every non-keyframed body in its manifold each `CharacterStep`, whatever the impulse (measured:
  the player at the bar, 85 wakes in 45 s).
- Not ours: with our physics, actor, animation and reference hooks left out (`SKYRIMCOOP_DISABLE_HOOKS`: HavokStepFix,
  PlayerHands, PlayerCollision, CopyRagdollPhysics, Actor, KnockState, Animation, References, TESObjectREFR,
  MeleeStagger, Projectile, CopyAiState, PlayerCharacter) the rate is the same (23 in 90 s). Not every hook file is
  gated, and a game without our plugin cannot run the probe.
- A woken body can get a velocity burst from its contacts and go nowhere (a bottle tilted on the bar's edge).

Before the fix the host's `PhysicsBodies` took "island active" for "moving": every wake sent the guest frames, which
keyframed its bottle (`BodyService: follows the owner's ...`) until the rest, and the grab refuses a keyframed body
(above): a held E was a pickup, a theft in front of Orgnar. Now a reference sends frames only once a body has left
where it woke (`BodyPrediction::LeftRest`: 2 units, 0.05 rad); its rest still goes (`docs/design/physics-sync.md`,
"Woken still"). Measured on the duo: before, two held-E grabs of 33288 and 33287 timed out after 3 s and the
release picked them up; after, each took in 0.22 s (`guest-grabs-orgnar-bottle`).

A copy standing still did the same through its own body until 2026-11-13: our step gave it a new velocity each
frame, which woke its island (`hkpCharacterRigidBody::setLinearVelocity` 62456). A body that had moved and stopped
beside a guest's copy then never slept, so the host never sent its rest and the guest kept it keyframed (see
`actor-movement.md`, "Copies step": a still copy now steps by nothing).

## What stops a held body (2026-11-01)

Probed in the duo (harness `ragdoll`, which also reads the character controller's flags and filter, and `ray`):

- A standing NPC's ragdoll bodies are not in the world (18 bodies, `in_world` false, on the host and on the copy).
  What a ray meets is the character controller's capsule (layer 30), its top 128 units over the feet, in both games.
- A grabbed object (the mouse spring on a dynamic body) stops on that capsule in vanilla: the host's own grab of a
  bucket stays at 166 over Lydia's feet however far its player looks down, then drops.
- The controller keeps bodies out as it integrates (its proxy's object interactions, 79133). A copy's controller
  reads flags `40200000` where the host's NPC reads `40240500`: no `kSupport` (bit 8), `kCanJump` (10) or
  `kFarAway` (18), as copies skip `Actor::ApplyMovement` (37356, `HookApplyMovement`), whose movement integrates it.
- A keyframed body put at a pose each frame (`SetPositionAndRotation` 78082) meets nothing; a simulated body steered
  by its velocity (78089/78090) is stopped like the grab's.

## kFarAway

`bhkCharacterController` flag bit 18 (`kFarAway`, CommonLibSSE-NG) is set by `Actor::UpdateFadeSettings` (37992)
when the actor's `Get3D2` (TESObjectREFR vfunc 0x70) is none or has `kNotVisible` (NiAVObject flags bit 20): an NPC
the camera does not see. In a game without a session, turning the player away from Lydia sets it (40200500 to
40240500) and back clears it; a bucket dropped on her head rests the same either way.

## Knockdown

Checked in IDA 2026-10-25 for a downed player held in its ragdoll (`Games/Skyrim/KnockDown`, removed 2026-10-26 when
real death replaced it, `docs/design/death.md`); the engine's side holds for any living actor's knockdown.

- `AIProcess::KnockExplosion` / PushActorAway (39895, `0x140710c80`; process, actor, source point, force): refused
  for a dead or dying actor, a restrained one (life state 6), a race with `kImmobile` or `kNoKnockdowns`, or when
  `Character` vfunc 0xC5 (40292: the byte at `Actor+0x27A`, set by vfunc 0xC4 40295) is zero. With the graph's
  ragdoll (below) it adds the ragdoll to the world and sets the knock state (`ActorState` flags1 bits 25-27) to 2,
  explode lead-in; without one it only makes the 3D dynamic.
- `KnockExplosion` and `KnockParalyze` (39894) end by playing the default object ActionRagdoll (index 107) on the
  actor (`0x1407360D0`): the graph's "Ragdoll" input comes in the same call that sets the knock state. A copy on its
  owner's stream goes down when that action arrives (`KnockReplay::GoesDown`, `BodyBridge::KnockDown`); knocked only
  by its owner's first body frame it was 40-70 ms late, its graph in its ragdoll state with no ragdoll under it.
- The knock update (39896, `0x140711020`; process, actor, from `Actor::UpdateAnimation` and the animation jobs):
  - lead-in (2, 4): once the graph has its ragdoll, ragdoll-driven, state 1 (explode) or 3 (out), and the
    down timer (`middleProcess+0x2B0`) set from `dword_142006858`;
  - lying (1, 3): counts the timer down; gets up (5 queued, 6 getting up, 0) once the ragdoll is still (37689: its
    root's and torso's squared speeds under 0.025) or the timer runs out. It stays down while `IsDead(false)`
    (37483), which counts essential down (life state 7), and state 3 also stays while essential down.
- "The graph has its ragdoll": every graph of the actor's manager asked `BShkbAnimationGraph` vfunc 1 (63620):
  its `hkbRagdollDriver` (`+0xF0`) has a ragdoll instance. **A bleeding-out player has none**: knocked after it went
  down (the TESEnterBleedoutEvent), it stays in lead-in for good. Knocked first, it lies through its bleedout. So the
  knock comes in `Actor::SetLifeState` (37612, `0x140680740`; actor, state), before a living player's state
  becomes 7 or 8, with one knock update run there to take the ragdoll while it still exists.
- While the player bleeds out the knock update is not run for it at all (it lies on, the engine's
  essential-down hold); the stillness test (37689) never passes for it either. Its rest is read from its ragdoll's
  bodies instead: all asleep (their islands inactive), as for any body at rest.
- A copy is not bleeding out: its knock update runs (the removed hook skipped it for a held one while lying). Its
  replayed actions carry the owner's flags1 (`AnimationSystem::PlayReplayedAction`) but not the knock bits (since
  2026-10-26): set from an owner's action, a copy's knock state went to lying or getting up with its ragdoll never
  added to the world (its bodies read motion 0 at the origin).
- Knocking a copy down (checked 2026-10-26, `BodyBridge::KnockedDown`): `KnockExplosion` refuses a dead actor, an
  immobile or no-knockdown race, a restrained one, and one whose high process reads 9 at `+0x2D8`
  (`Character` 37485). It ragdolls only when `Character` vfunc 0xC5 (40292) reads true: the byte at `Actor+0x27A`
  (`knockRagdolls`, set by vfunc 0xC4 40295), else it pushes the character controller. Copies read 1. Off the main
  thread it goes through the task queue, so the knock state can still read 0 right after the call.
- A knocked down actor's ragdoll bodies stay dynamic while it gets up (knock states 5 and 6); its animation drives
  them again, toward its pose, not onto it (Getting up, below).
- **Getting up** (read in IDA 2026-11-11; knock states 5 and 6). What poses the actor then:
  - The graph plays the get-up clip its pose matcher picked (`hkbPoseMatchingGenerator` 59583).
  - `hkbGetUpModifier::modify` (59766), on its first modify, reads the ragdoll's world pose and writes the output's
    worldFromModel so the clip starts where the ragdoll lies (three bones: root, other, another); after that it only
    turns worldFromModel's up to the ground normal. It keeps nothing another node reads.
  - `hkbRagdollDriver::driveToPose` (58291) reads the output's powered and rigid body ragdoll controls and drives the
    ragdoll's bodies toward the output's pose. The bodies stay dynamic (motion types 2 and 3 through state 6):
    gravity and contacts still move them, and the skeleton shows the bodies.
  - So the pose shown while getting up is each game's own physics, as while lying. Measured (duo, `RagdollTrace`
    of `knockdown-lies-as-host`, 11 runs): the head either rises steadily from 2.5 s after the get-up starts, or
    stays 52-60 units above the floor at one place of the room (about -980, 2105 in QASmoke) and comes free 2 to 3 s
    later, or only when the get-up ends. Owners show both; in 4 of 11 runs owner and copy took different ones. The
    actor's heading, its knock state times and its clip were the same on both games in every run.
  - `PhysicsBodies` therefore reads a living actor's ragdoll through states 1 to 6 (`KnockReplay::BodiesSimulated`),
    and a copy steers to those frames until it stands (`BodyBridge`, `CopyRagdollPhysics` after 58291). Standing
    (state 0) the bodies cannot be read (they have left the world): the last frame read is the rest.
  - Its limbs shake while it gets up (forearm and hand reversing every physics step, 8-12 units, on the host's own
    Lydia about 0.8 s into state 6), so those frames carry no velocity: a copy blends between poses.
  - How it starts and ends (read in IDA 2026-11-18). `BeginGetUp` (40150; actor, send action) sets the graph's
    `iGetUpType` to 1 or 2, plays `kActionGetUp` (default object 108) and sets state 5; the knock update turns 5
    into 6. Only the graph ends it: its `GetUpEnd` event reaches `GetUpEndHandler` (42880), which for `iGetUpType`
    1 or 2 queues task 14 (`TaskQueueInterface::QueueEndKnockState`, 36918), and the task runs
    `AIProcess::EndKnockState` (39897: ragdoll keyframed or out of the world, knock state 0, the player's
    bleedout camera left). A graph that never sends `GetUpEnd` leaves the actor at 6 for good.
- **A kill move's victim copy keeps its ragdoll** (read in IDA and measured 2026-11-18). The victim state sends the
  graph event `AddRagdollToWorld` at the pair's start (`AddRagdollHandler` 42873: the 3D's collision, its ragdoll's
  bodies included, into the cell's world, biped layer 8; `RemoveRagdollHandler` 42877 takes it out with
  `TaskQueueInterface::QueueRemoveFromWorld` 36924). The owner's victim lies in its powered ragdoll (motions 2/3)
  for the whole clip. Its copy, on its own body for the pair, added its ragdoll about 200 ms before the owner's
  stream did. The copy mirrored its owner's body layer (`SkyrimBridge::SetCopyPhysics`, for seated and mounted
  copies): layer 0 calls the same 36924, so the copy's ragdoll was out of the world for the whole clip (traced
  `bodies_layer` 0 → 8 → 0, bodies unreadable). At death it went in again, keyframed for one frame and then
  dynamic, and was steered 20 units from the clip's pose to the owner's. In 1 of 2 runs two bones were off for that
  one frame: the "flickers" of `kill-move-kills-guest`. A copy in a kill move now keeps its bodies
  (`ReplayState::TakesOwnerBodies`), as one lying in its ragdoll already did.
- **Resurrect and the knock state** (read in IDA 2026-11-18). `Actor::Resurrect` (37321) zeroes `ActorState1`
  (`ActorState::ResetForResurrect`, 38989), so a revived actor's knock state is 0. It also plays
  `kActionResetAnimationGraph` (106) while the actor is still dead (an NPC copy's dead graph refused it:
  `docs/design/small-bugs.md`, NPC resurrection). `Actor::DoReset3D` (40255) rebuilds the worn models only, not the
  graph.
- **A copy at its owner's rest (2026-11-11).** Setting a body's velocity (78089/78090) wakes it, so a copy steered
  to its owner's rest on every step never sleeps. Its bodies cannot all be brought there one by one either: with the
  others still off, about three of a ragdoll's 18 to 22 are held a little away by their joints. All put at the owner's
  poses in one step they fit, and Havok lets them sleep about half a second later (`BodyPrediction::StepToRest`).
- Moving the actor (the party's travel, PositionPlayer) leaves its ragdoll's bodies where they lie; getting up then
  puts the actor back at its ragdoll. The bodies can be moved with it (78082, motion types kept; the removed
  `KnockDown::Carry` did).
  `BShkbAnimationGraph` vfunc 5 (63624) only resets the driver (`hkbRagdollDriver::reset` 58294), it does not move
  the bodies.
- The fall's push (checked 2026-10-26): `Actor::KillImpl` gives a dying actor's ragdoll the force a projectile
  recorded (`HighProcessData` `+0x27C` direction, `+0x288` force, 1.0 when none), through `Character` 38630 (an
  `AddExplosionImpulse` from 1024-2048 units back along it, scaled by `fDeathForceSpellImpactMult`). Only the
  projectile impact handler (`0x1407EC3F0`, from arrows, cones and missiles) records it, and only for a hit that
  kills: never for the essential player. Otherwise the body moves on at `fDeathForceForceMin` (Skyrim.esm: 4 units/s),
  the way its controller was going, or standing, forward along its heading (`NiAVObject` 26008, the 3D's bodies'
  velocity in game units). A real death gets this push from the engine itself.
- Real death instead (`docs/design/death.md`, "Real death"): the reload is `PlayerCharacter` 40748 counting
  `fPlayerDeathReloadTime` down while dying or dead. `Actor::Resurrect` (37321) dispels every effect (abilities too)
  and leaves the player's skeleton in the death ragdoll and its camera in the bleedout state (11); the revive there
  puts all of them back.

## The engine's own wake and rest events (read in IDA on 1.7.104, 2026-11-15)

`FOIslandActivationListener` is the engine's `hkpIslandActivationListener`; both callbacks get the island.

```text
islandActivatedCallback (25849), each body not keyframed, fixed or character
  hkpRigidBody::OnActivated (25857, our name; also from entityAdded and entitySetMotionType)
    the reference's havok move (AddChange 0x4; an actor's only in its ragdoll state)
    its 3D moved under the cell's DynamicNode (cell 3D's child 4; an actor's child 0), direct or by the task queue
islandDeactivatedCallback (25850), each body
    the telekinesis listener and a temporary mass taken off (26026, PopTemporaryMass 26024)
    its 3D taken back out of the DynamicNode, one node update queued
    a layer-20 body goes keyframed, its node written from the body once (78382)
```

- These are the wake and rest events `PhysicsBodies` now infers from the change funnel and each island's mark.
- Why a node can be left behind (inferred from the code, not measured): a dynamic body's node is written only inside
  its own `NiAVObject::Update` (`bhkNiCollisionObject::UpdateWorldData` 78378), so only while something updates it
  each frame, which is what the DynamicNode is for. 78378 also obeys a thread-local mode (TLS+2536): 3 no sync,
  2 no node to body, 1 no body to node. A body woken by a path that skips 25857, or whose move to the DynamicNode
  waits in the task queue, flies with its node standing.
- `hkpRigidBody::setPositionAndRotation` (60898) is a teleport: the motion is set, then
  `hkpEntity_updateMovedBodyInfo` (60918) updates the broadphase, redoes the body's collision agents and activates
  it. Nothing is swept and its contacts start over: a body placed every frame never keeps a contact.
- The character push has no player test: `bhkCharacterController::ScaleObjectImpulse` (our name, 78304, from
  79133) runs for every controller that steps, so a stepping copy pushes the bodies it walks into. It zeroes the
  impulse for a body with no mass (keyframed) or at `fCharacterPushMassLimit` (389158) and above, scales by
  layer (14 x0.1; 10 x3 when under half the limit, else x0.1; others x2) and caps impulse/mass at 1000.
- A temporary mass is telekinesis only: `TelekinesisEffect::LaunchGrabbed` (our name, 35052) is
  `PushTemporaryMass`'s one caller. The player's hand grab changes no mass.

## The collision filter (read in IDA on 1.7.104, 2026-11-15)

`bhkCollisionFilter::CompareFilterInfo` (78553, Precision's name; we hook it in `PlayerCollision.cpp`) decides
whether two bodies' filter infos collide; every Havok collision and query asks it.

```text
filter info (hkpRigidBody+0x4C, its collidable's +0x2C)
  bits 0-6 layer    bits 8-12 part    0x4000 no collision at all    0x8000 biped part    bits 16-31 group

either has 0x4000 (and a's layer is not 44)   no collision
either group is 0                             collide (the layer matrix is not asked)
same nonzero group                            only biped parts that are not neighbours, or layers 8/32/33 by the
                                              part matrix (+80); anything else: no collision
different groups                              the layer matrix (+464, 64 bits per layer); a layer-30 capsule with
                                              0x8000 meets only the layers in the mask at +976
```

- A body's group is written by `NiAVObject::SetCollisionLayerAndGroup` (77999, CommonLibSSE-NG): its walk
  (78011) sets layer and group in each rigid body's filter info when nonzero (it cannot clear a group), then
  calls `bhkWorldObject::UpdateCollisionFilter` (77794, our name; `bhkWorld` 77925 inside, PLANCK's
  `UpdateCollisionFilterOnWorldObject`), which makes the world re-evaluate that body's collisions.
- So a group of our own on the bodies another game moves, and a rule for it in the hook we already have, is how a
  played object can meet the world and not other objects (HIGGS puts a held object in the player's group the same
  way). Built 2026-11-15: `Physics/BodyGroups.h`, `SetBodyGroup` (the filter info written under the world's write
  lock, then 77794), `docs/design/physics-sync.md`, "One simulator per pile".

## A rest needs its contacts (read in IDA on 1.7.104, 2026-11-15)

`hkpRigidBody::setMotionType` (60908) runs `hkpWorldOperationUtil::setRigidBodyMotionType` (61518).
Only a change to or from fixed takes the body out of its island and adds it again with the activation asked.
Keyframed to dynamic takes the short branch: the motion object is swapped (61519) and the entity's collision filter is
updated (`hkpWorld::updateCollisionFilterOnEntity`, 61321), which makes the agents its new quality type needs: a keyframed body has none with fixed
geometry. Those agents hold no contact points until a collide step runs on the body.

So a body keyframed, put at a pose, set dynamic and deactivated at once (60850) sleeps on a floor Havok has no
contact with (read). Inferred, not read in the contact solver: a character's push has a downward part (the capsule's
round end on the object's top edge), which the floor's contact normally cancels; without it the body sinks for one
step and the next throws it up. Measured: the velocities below.

Seen in the duo (`push_mixed`, the guest walking into a cheese wheel the host's rest had placed): the wheel left at
(366, -44, 100) units/s and rose 30 units in 10 of 13 pushes, once after a first frame at -250 down. Vanilla's and
the host's own left at (508, -67, -7) and rose 1 to 3, in 40 of 40. Ruled out by switching each off: `HavokStepFix`,
the collision classes. The two players' controllers read the same; the wheel's pose matched in both games to 0.05
degrees.

With rests left awake the guest's wheel left at (509, -67, -2) in 6 of 6. Left awake to settle, though, the game
that placed the rest simulates it, reports it moving, and both games take a pile for their own: it shook in both (82
turn-backs on a kettle), and with no fallback at all a pile never settled.

So `BodyBridge::Rest` (`BodyPrediction::PlaceRest`) puts a simulated body at its rest awake and still for one
physics step, which is the collide step that finds its contacts, and puts it to sleep the next frame
(`SleepBody`, `hkpEntity::deactivate` 60850, which keeps the agents). The keyframe round trip is left for a body a
hold keyframed, and for one that left its rest within that step: that one does not stay there by itself and is held
asleep as before.

## A pile sleeps together (read in IDA on 1.7.104, 2026-11-16)

`hkpEntity::deactivate` (60850) acts on the body's island, not the body: a split check pending on the island splits
it first (61513), so only bodies touching it stay together; then every entity in the island gets its motion's
deactivation counters (+0x25C, +0x26C) set and the island is marked inactive (`markIslandInactive`, 61523: clears
its active mark, queues it on the world's dirty island list). There is no sleeping one body among others.

So a rest put into a pile (`SleepBody`) sleeps the pile only when every body in it is dynamic, nearly still and not
one this game moves, plays or holds; otherwise it waits for Havok (`BodyPrediction::RestSlept`, 2 s at most).
Sleeping the island regardless froze the guest's own carried object, still for a moment in its hands against the
pile (it jumped 75 units).


## When an island sleeps (read in IDA on 1.7.104, 2026-11-16)

Havok sleeps an island in two steps. Read in IDA (names ours, from Havok 2010's; 62263 and 62314 read for their
calls and the stillness writes, the rest whole): each function below; measured with
the harness's `body_trace` (`sleep`: each body's counts, fastest speeds and island size) in a game with no session
and on the duo.

```text
each physics step, hkpContinuousSimulation__integrateIslands (61809), every active island:
  62260 (0x140B838A0; with constraints 62314 -> 62263) integrates its bodies and counts their stillness:
    motion+0x11 ++; every 4th step reference 0 is checked, every 16th reference 1
    a check: w of the reference = max(w, |v|^2 + min(r,1)^2 |w|^2)   (entity+0x25C, +0x26C)
             moved off it (the class's maxDistSqrd / maxRotSqrd)  -> reference reset here, w 0, its count 0
             else                                                -> its count + 1 (entity+0x162, +0x164, low 7 bits)
  the island's least max(count0, count1) over 5 -> markIslandInactive (61523): only queued (world+0x60)
then hkpWorldOperationUtil__cleanupDirtyIslands (61522) drains the queue:
  hkpWorldOperationUtil__deactivateIslandIfStill (61507)
    hkRigidMotionUtilCanDeactivateFinal (62264, 0x140B869C0): for every body
      (|v|^2 + min(r,1)^2 |w|^2) / 4 - 0.01  <=  w of its reference with the higher count
    all pass -> asleep, every velocity zeroed, the deactivation listeners (61778)
    one fails -> active again
```

- The last test is what keeps a moving body awake: a falling or carried body goes faster than it went at its last
  check. Under about 14 units/s it passes whatever its w (the 0.01).
- `hkpEntity::Activate` (60849) zeroes the counts only when it wakes a sleeping island. The let go
  (`DestroyMouseSprings` 40557) only activates. Measured in a game with no session: a wheel held still in the hand
  reaches 6 counts and Havok sleeps it there (an island of one, about 0.5 s); the let go wakes it, its counts start
  at 0, and it falls (26 and 28 units in two rounds).
- `hkpEntity::deactivate` (60850) writes HK_REAL_MAX into both w of every body in the island and queues it. That
  passes the last test whatever the bodies do, and stays until each body moves off its reference. 60850 queues the
  island straight away: the counts of the first step are never asked. Measured on the duo
  before 2026-11-16: the guest's wheel put at the host's rest read w 3.4e38, a vanilla one 0.03.

What froze a let go wheel (the playtest of 2026-11-16, then `pile_carry_release` on the duo):

```text
guest: its player knocks pile wheels (its own); one rests, its rest goes to the host
host:  puts the rest a step awake (BodyPrediction::PlaceRest), it settles about a unit: a move row back
guest: GuestWorldState applied the row -> PlaceAtHostRest keyframes the wheel, FreePlaced frees it asleep (60850)
       -> its whole island asleep, HK_REAL_MAX passing the last test: the wheel the player carried (47 units/s)
          or had just let go (falling at 90 units/s) beside it stopped dead in the air
```

Seen in the playtest's logs (a row for 1C0C8 at 02.195, the let go 1C0CA and its neighbour asleep 32 ms later, 25
units over the pile) and in the duo's traces (each of three sleeps of the carried wheel 30 ms after a pile wheel's
row). Fixed both ways (`docs/design/physics-sync.md`, "A rest's move row comes back"): our island sleeps
(`DeactivateIsland` in `bhkCollision.cpp`, for `SleepBody` and a free asleep) refuse an island with a body that is not
dynamic and still (`BodyPrediction::StillToSleep`: the counts 60850 skips; a body sliding at a steady speed would pass
the last test alone) and put each body's w back after 60850, so Havok's own last test has its say too; and the guest
skips the row of its own rest.

## A lying ragdoll's layer and group (read in IDA on 1.7.104 and in Skyrim.esm, 2026-11-21)

For piece 10 of `docs/design/one-simulator.md`; journal `docs/re/journal/1.7.104/agent-piece-10.tsv`.

```text
the ragdoll enters the world       AddRagdollHandler (42873) -> task 18 (QueueAddToWorld 36922)
                                   -> bhkWorld vfunc +0x1A8 (world, 3D, recurse, 1, group, 0)
                                   group: the actor's character controller's (its filter info's high 16 bits)
the actor is knocked down          KnockExplosion (39895): NiAVObject::SetCollisionLayer (77998) of the 3D to 32
the actor dies                     Actor::KillImpl (37896): the same call, 32
up again / resurrected             EndKnockState (39897), GetUpEndHandler (42880), ClearDeathState (37613): back to 8
```

So a body lying in its ragdoll is on layer 32 (`L_DEADBIP`) in its actor's own group (read). What layer 32 meets is
data, not code: Skyrim.esm's `COLL` records (read with a script from the game's Skyrim.esm):

```text
L_DEADBIP (32) meets   static, anim static, terrain, ground, trees, water, triggers, traps
                       clutter (4), weapon (5), props (10), small and large debris (19, 20)
                       other L_DEADBIP (another actor's: a different group)
                       projectiles, spells, the item picker (40), line of sight (41)
             not       L_BIPED (8), L_CHARCONTROLLER (30), L_BIPED_NO_CC (33)
L_BIPED (8) meets      the world only: no clutter, no weapons, no other bipeds
```

- A character never pushes a corpse with its capsule (30 against 32: no), in any game. The harness's floor probe
  (`GetBodyPose`, a layer-30 ray) still hit a corpse under a player's feet and read the player 30 to 40 units sunk
  (measured, 2026-11-21): it looks past actors' bodies now.
- A lying ragdoll pushes loose objects and other lying ragdolls, and is stopped by them. A copy's ragdoll does
  that in its own game too, where its owner's never touched them.
- `Actor::IsInRagdollState` (37491): true when `Actor+0xE8` bit 31 is set, when dead (vfunc 0x99) without flags2
  `0x10`, at knock state 1 to 5 or 7, or with flags1 bits 21-24 at 3; knock state 6 (getting up) alone does not make it
  true (read). The transform delta sink (37996) keeps the actor on its ragdoll's root only while it is true.

Not read: Havok's constraint solver for a ragdoll's joints; what `hkbRagdollDriver::driveToPose` does for a dead
actor's bodies each update (the owner's ragdoll sleeps under it, so it does not wake them: measured, 2026-11-12).

## A translating reference (read in IDA on 1.7.104, 2026-11-21)

Papyrus `TranslateTo` / `SplineTranslateTo` (56237) give the reference an `ExtraRefrPath` (extra 161), mark change
`0x80000000` and add its handle to its cell's list (`TESObjectCELL::AddTranslateObject` 19417). `TESObjectCELL__UpdateTranslatingRefs` (18898)
walks that list for each attached cell every frame: `ExtraRefrPath__Advance` (12810) by this game's frame seconds,
then `SetPosition`, `SetAngle`. That is all that moves a critter (`critterMoth`, `Critter.psc`): the game whose VM
ran the script moves it, on its own clock; no other game is told. `Critter.psc` also places two landing markers for
itself (`PlaceAtMe(LandingMarkerForm)`: Skyrim.esm's `C2D47` for 13 critters, `22201` for one) and moves them with
`SetPosition`.

The harness tells a critter by its script: the VM's `FindBoundObject` (104792, vfunc 0x1C) visits the handle's bound
objects (104880) and, unless asked for an exact match, walks each one's class up its parents (104912), so a lookup of
`Critter` finds `critterMoth` (read).

Change flag `0x8000000` is `CHANGE_REFR_LEVELED_INVENTORY`: `InventoryChanges__AddLeveledEntries` (16128) sets it
when a leveled list's roll is added to a reference's inventory. Each game rolls its own.

## A revived player's ragdoll stays in the world (read and measured, 2026-11-22)

Measured (fresh duos; a plain `player.kill` does it, no kill move needed): a living player has no ragdoll instance
(0 bodies). After a death and our revive, its 18 bodies are in the world, dynamic (motions 2/3), the driver's
rigid-body controller on (output track 4 at 1.0, track 8 at 1.0 with no bone above 1), and every bone under
`NPC Root` on the bodies. After `coc Riverwood` the actor stood at (17226, -47203, -128) and `NPC COM` at
(-168, 1706, 2125) by the bodies: bugs.md's "pelvis 2,100 above its feet", 48,000 units off in x/y too.
For comparison: a living NPC (the host's Lydia) has the same tracks, but her ragdoll is out of the world and her
driver's controllers are off; the host's player after a knockdown and vanilla get-up has no ragdoll instance at all.

Read in IDA (1.7.104):

```
each frame (job table, actor-movement.md "Where a copy's step falls in the frame")
  Job_Actor_animation  graph update 63587: 63564 (a world, its ragdoll's first body in one)
                         -> GraphPhysicsStepListener::AddGraph 63761 (static listener 0x1420C5B40)
  Job_VM_update        our World::Update
  Job_Cell_animations  bhkWorld::Step
    listener prePhysics  -> UpdateRagdollPrePhysics 63562: every body not in the world added (hkpWorld_AddEntity),
                            then hkbRagdollDriver::driveToPose 58291: powered (+0xC7) = track 6 on,
                            rigid body (+0xC8) = track 4 on; with one on, a body not marked keyframed
                            (track 8 > 1 or track 9) is set dynamic
    listener postPhysics -> UpdateRagdollPostPhysics 63563 -> hkbRagdollDriver::postPhysics 58293:
                            a controller on -> the skeleton takes the ragdoll's pose (58311)
                            the list is cleared after each step
```

- So a graph runs the ragdoll passes in a frame only if its ragdoll was in the world at its update, and a pass
  then puts back any body that left the world since.
- Our revive (`PlayerCharacter::Revive`, from `World::Update`) took the ragdoll out with
  `RemoveRagdollFromWorld` 63622 between the listing and the step. Measured with the harness's `ragdoll_adds`
  (watches on the add visitor 32953 and on 63562): the only visitor adds were the death's (`KillImpl`, then its
  queued task), and exactly one 63562 pass found the bodies out of the world, at the revive, and added them back.
  From the next frame the graph is listed every frame, its bodies dynamic under the rigid-body controller.
- Vanilla ends a ragdoll from the task pool (`TaskQueueInterface::Push` 36993): the graph's `RemoveRagdoll`
  event (`RemoveRagdollHandler` 42877) queues `QueueRemoveFromWorld` 36924 on the 3D, and `GetUpEnd` queues task 14,
  `AIProcess::EndKnockState` 39897 (every graph's ragdoll out, the 3D keyframed on layer 8, knock state 0). 39897
  returns at once with knock state 0, which `Resurrect` already set, so a revive cannot use it.
- So the revive no longer takes it out itself: `DeathService` asks `IPlayerBridge::EndPlayerRagdoll`, which queues
  36924 and 36925 on the 3D (`QueueRagdollOutOfWorld`), as `RemoveRagdollHandler` does. Measured after the fix:
  `revived-guest-stands-after-travel` stands in Riverwood; `kill-moves-cross` then `host-rides-horse` passes.
  That the task pool drains outside the listing-to-step window is inferred from vanilla's own get-up and that run.
- Driver layout: CommonLibSSE-NG `hkbRagdollDriver` (from activeragdoll); the output tracks are
  `hkbGeneratorOutputTracks` in `Havok/BShkbAnimationGraph.h`.
