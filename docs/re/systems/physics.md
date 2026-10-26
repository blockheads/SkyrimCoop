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

`PhysicsBodies` (host) therefore writes each awake reference body's node from its body every update (`FollowBody`,
the engine's own dynamic write), wherever the players are; a guest does the same for the bodies it holds.

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

Havok's own listeners (island activation, post simulation) would say the same per island; they need an object with
Havok's vtable registered on each world as the player changes worlds. Not needed while the funnel's event and the
island's mark answer it.

## Order in a frame

The job list at `0x142011ba0` runs the cell animations and then `Job_Ragdoll_post_physics` (39068) after the physics
step; our update runs in the Papyrus VM's (`SkyrimVM::ProcessRegisteredUpdates` 53926, from `Main::Update` 36564).
Not checked: where the step itself runs against our update. It does not matter for a hard keyframe, whose velocities
hold until the next step whenever that runs; the physics substep count comes from `UpdateTimeManager`
(`uMaxNumPhysicsStepsPerUpdate:HAVOK` 389138).

## Grabbing

`PlayerCharacter::StartGrabObject` (40552, `0x14073ec70`; checked in IDA 2026-10-24): with no grab going (`+0xAFC`
zero), takes the crosshair's reference (`CrosshairPickData` +4), its 3D's collision object's rigid body, and if the
body's mass is at most the limit (`dword_142006168`) sets up the mouse springs (40555), then tests the springs' count.
`PlayerCharacter::DestroyMouseSprings` (40557, `0x1407414b0`): for each spring, removes it and activates its body
under the world's write lock, then looks up the held reference at `+0x8D0`, sends a script event for it and sets
the handle invalid. **The grab data sits 8 bytes later on 1.6.1170 than CommonLib-NG's comments** (its SE layout):
the springs (`BSTSmallArray<hkRefPtr<bhkMouseSpringAction>, 4>`) at `+0x8A0` (capacity), `+0x8A8` (data), `+0x8C8`
(count); `grabbedObject` at `+0x8D0`, not `0x8C8` (`PlayerCharacter::grabbedObject`; `world_state`'s player status
`grabbed`). Holding Activate past `fZKeyDelay:Controls` grabs; letting go drops it with its motion: vanilla has no
throw for a grabbed object (no `fZKey...Throw` setting; telekinesis has its own). The harness holds Activate as the
`activate` button. `StartGrabObject` takes the body only if its vfunc 0x27 says so (a keyframed body: no): a guest
grabs a resting body (dynamic, asleep), not one the host's frames still move (a hook freeing it first was removed,
`docs/progress.md`, loose ends). A grab holds the object where
the crosshair was (carried looking down at the floor, it hangs just off it); turning the player at once
(`setangle`) drops it, as does jamming it against something that does not give.
