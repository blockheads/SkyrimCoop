# The player's horse

What makes a horse "the player's" in the engine and in vanilla scripts. Read in IDA (1.7.104, 2026-11-17) unless
marked otherwise.

## The engine: one slot, `lastRiddenMount`

| Step | ID | What |
|---|---|---|
| The slot | `PlayerCharacter+0x924` | `lastRiddenMount`, an actor handle. Set by vfunc 0x113 `SetLastRiddenMount` (41131), read by 0x114 `QLastRiddenMount` (41076). Written in the player's save (`PlayerCharacter::SaveGame` 40538 / `LoadGame` 40540, vtable slots 0xE/0xF). Only `PlayerCharacter` has it: a copy (a `Character`) answers none |
| Getting on | 40359 `MountInteraction__OnRiderMounted` | Mounted weapon anim variables; a horse the rider does not own (`IsAnOwner` 20210, unless the race has flag 0x80) raises `StealAlarm` (horse theft). The player then calls 40725 |
| The player got on | 40725 `PlayerCharacter::OnMountStarted` | An owned horse (`IsAnOwner`): the previous `lastRiddenMount` demoted from dynamic persistence, this one promoted, `SetLastRiddenMount`. A stolen horse never becomes the last ridden |
| Fast travel | 40445 `Actor::FastTravel` (our `HookFastTravel`) | Keeps `lastRiddenMount` only while the player is an owner and it is alive (else clears it), then places it beside the destination: the horse comes along |
| Conditions, Papyrus | `IsPlayersLastRiddenMount`, `Actor.IsPlayersLastRiddenHorse`, `Game.GetPlayersLastRiddenHorse` | All read the slot through vfunc 0x114 |

`IsAnOwner` on an actor reads its own `ExtraOwnership` (crime.md); `PlayerFaction` as the owner means every game's
own player, so a party horse is "owned" on every game, and each game's engine keeps its own player's last ridden.

## Vanilla scripts (Papyrus sources, read 2026-11-17)

- Each stable (`Stables` quest 0x68D73) has one horse for sale (`Alias_<City>Horse`) and one `PlayersHorse` alias.
  The purchase (`TIF__00068CF4` and its four siblings) takes the gold, puts the horse in `PlayerHorseFaction` rank 1,
  `SetFactionOwner(PlayerFaction)`, and `PlayersHorse.ForceRefTo` it: a second purchase anywhere replaces the alias.
- `PlayerHorseScript` (on `PlayersHorse`): when that horse dies and unloads, it is deleted and a fresh
  `LvlHorseSaddled` placed at the stable, owned by the stable, for sale again. Only the aliased horse is replaced.
- Hearthfire (`BYOHHouseHorseScript`): one horse per house, enabled on cell attach after `bBoughtHorse`.
- Inferred, not read (the topic conditions are in the ESM): a stable stops offering its horse once sold.

## Two riders on one horse (read in IDA and probed on the duo, 2026-11-17)

- A horse holds one interaction. `RefrInteraction::AttachTo` (41532 calls it) replaces only the rider's own
  `ExtraInteraction`; `MountInteraction` vfunc 0x30 (40355) then calls `ExtraDataList::SetInteractionData` (12074) on
  the mount, which adds one only when it has none. So a second `Actor::PutOnMount` leaves the first rider riding and
  the horse still naming the first. A later `Detach` (41539) of either rider removes the horse's slot, whichever rider
  it names.
- Both ways onto a ridden horse refuse: `Character::ActivateMount` (37864, the activation) and `Character::CanRide`
  (38706, `InitiateMountPackage` 37905's gate) each ask `Actor::IsMountBeingRidden` (38303), which is only whether the
  mount's `ExtraInteraction` exists.
- Measured (Riverwood, the host riding its horse, harness `put_on_mount` for the guest's player on the guest's game
  and its copy on the host's): the interaction alone makes `riding` true and turns the guest's camera to the horse,
  but places nothing. The guest's player stayed on the ground (sit state 0) and its copy on the host sat on the ground.
  The host's rider stayed seated. `sae HorseEnter` / `HorseEnterInstant` on the guest's player afterwards: "not
  processed by the graph". Activating the ridden horse copy with real input: refused.
- The seat comes from the mount path, not the interaction: the get-on animation's end sets sit state 3
  (`HorseEnterInstant` does it at once: `Actor::SetSitSleepStateFromGraph` 39000, from 39004). Each update
  `MountInteraction::Update` (40356, vfunc 0x8) ends the ride of a rider not seated (sit state not 3 or 7) and seats
  one that is: `MountInteraction::SeatRider` puts the interaction on the mount (if it has none), sets the rider's
  collision layer 33, attaches the rider to the mount's node and adds graph channels feeding the mount's variables
  into the rider's graph.
- Measured, second probe (harness `board_ridden`: the horse copy's `ExtraInteraction` removed on the guest's game,
  then the engine's own `InitiateMountPackage` 37905 for the guest's player, not our hook): the guest's player played
  `HorseEnter` and sat on the horse copy (sit state 3, standing on the horse, pelvis -40 / head 12 like the host's
  rider) and moved with it; the host stayed seated as driver. On the host, the guest's copy replayed `HorseEnter`
  with no interaction (the probe sent no mount message) and hung beside the horse at saddle height; a
  `put_on_mount` afterwards left it at sit state 2, never seated. Meanwhile the host's horse drifted about 1,500
  units with no input: inferred, not checked, the unseated copy's capsule pushing it.
- Why the copy never seated: a copy's get-on ends at `PairedStopHandler` (42868, sit state 2 to 3, then
  `Actor::OnInteractionSeated` 41533) when its paired clip plays out. A copy's `HorseEnter` replays without the
  mount's half (`AnimationSystem`: the mount's owner plays it), and a passenger's horse is driven by another rider,
  so nobody plays that half and the clip never ends. `HorseEnter` itself only sets sit state 2 (39004); 3 also comes
  from `HorseEnterInstant`.
- Measured, third probe (2026-11-17): the guest boarded as before; the host's horse began drifting at once (about
  470 units in seconds, no input) while the guest's copy stood at sit state 2 beside it. Harness `seat_on_mount` on
  the host (`PutOnMount`, sit state 3, `OnInteractionSeated`): the copy stood on the horse (pelvis -41 / head 11, as
  every rider), the drift stopped, and when the host drove 2.5 s forward both riders went with the horse on both
  games, 0 units apart once it stopped. So the drift was the unseated copy's capsule against the horse.
- Measured, fourth probe (2026-11-17, Riverwood, after dev's `agent/horse-riding` merge; harness `board_ridden`,
  `seat_on_mount`, `take_reins`):
  - a. The passenger's W (the guest, on the host's horse): neither game's horse moved, the guest stayed seated.
    Nothing to block.
  - d. The passenger gets off (real Activate): the host kept riding and steering. On the host its horse still read
    ridden; on the guest the horse copy read not ridden while the host's copy sat on it (the guest's own Detach cleared
    it: `RefrInteraction::Detach` 41539 calls `MountInteraction` vfunc 0x30 with null, which removes the horse's slot
    whichever rider it names). `take_reins` for the host's copy there put it back. Why the host's slot survived its
    copy's Detach is not read (`MountInteraction::Update` 40356 re-seats only from its own caller, not traced).
  - b. The driver gets off with a passenger on: the passenger stays seated on both games; the horse goes back to the
    host as now.
  - c. A passenger left alone on a horse its own game owns (the guest drove, the host boarded, the guest got off, the
    horse the host's again): the host's W does nothing, its horse's slot empty. Setting the slot and running
    `PlayerCharacter::OnMountStarted` (40725: camera, controls, the mount's `MovementControllerNPC::EnablePlayerControl`
    41710) still does nothing. Adding an `evp` on the horse (`Actor::EvaluatePackage`) steers it: 1,000 units on flat
    ground, the guest's copy following. A real ride shows the same: the horse's running package changes when the
    player gets on (FF0009A3 -> FF0010A0); the handed horse kept its old one.
  - The replayed `HorseEnter` of a copy seated before it arrived put it back at sit state 2 (the host's copy on the
    guest, seated at once by `seat_on_mount`), and the horse drifted again: a seated copy's `HorseEnter` must not replay.
- `MovementControllerNPC::EnablePlayerControl` (41710, vfunc 0x68) sets +0x1C5: the player rider's input moves the
  mount. `DisablePlayerControl` (41709, vfunc 0x60) clears it, from `MountInteraction__OnRiderMounted` (40359) for a
  rider that is not the player: a copy's get-on hands its horse to AI.

## Passengers (built 2026-11-17)

```
our player's Activate on a ridden horse (37864 does nothing)
  HookActivateMount -> BoardEvent -> CharacterService: a party player drives it
    horse's slot emptied, engine get-on (37905, not our hook) -> BoardRequest
    refused (CanRide 38706: midair, getting on or off, reach) -> the driver's copy holds the slot again
host: OnBoard -> Passengers.push, NotifyBoard to the others (refused: NotifyMountRefused)
other games: NotifyBoard -> PutOnMount (linked); its get-on's hold ends -> SeatOnMount (sit state 3, 41533)
  (its owner seated already, a late joiner's: seated at once, HorseEnter replay skipped)
driver's Detach -> DismountRequest -> HandOver: first passenger drives, NotifyMount to all
  its game: TakeOwnership + PlayerTakeReins (slot, 40725, EvaluatePackage 37401)
  others: its copy on already -> slot only (ActorHoldMount)
  then NotifyUnboard (old driver) to the others
passenger's Detach -> UnboardRequest -> NotifyUnboard to the others
NotifyUnboard: the rider's copy still on that horse -> ActorEndRide (its owner's ride may end with no exit clip,
  38705/40352: mounts.md); a pending board dropped
any Detach -> the driver still on holds the slot again (KeepDriverOn)
```

Not built: passengers carried by a party travel (only the driver's horse is, `CarryMountTo`).

## The new driver's horse left in the old driver's exit pair (duo, 2026-11-17)

- `MountBehavior` (`HorseBehavior.hkx`, `scripts/re/behavior_dump.py machines`): wildcard `pa_HorseExit` ->
  `DismountSynced`, which leaves only on `PairEnd` (-> `RootState`); `pa_HorseEnter` -> `MountSynced` the same.
  `bIsSynced` is the graph's own variable (no code writes it). A pair's `PairEnd`, then `PairedStop` if not taken,
  comes from its instance alone (`BGSSynchronizedAnimationInstance::Deactivate` 32797, which also restores the
  participants' character controller flags).
- Measured: after the handover the guest's horse stayed in `DismountSynced` and steered nothing. Its exit half had
  started while it was a copy (the old driver's exit), and the pair's instance was on the old driver's game, so
  nothing here sent `PairEnd`. `TESObjectREFR::SendAnimationEvent("PairEnd")` (harness `graph_event`) took it out at
  once and the guest drove off. The console's `sae` / `sendanimevent` never reached the graph (`anim_trace` saw no
  input), so those earlier tries proved nothing.
- Built: the new driver's game sends its horse `PairEnd` at the handover and again when the old driver's copy ends
  its exit there (`Actor::EndMountPair`, from `PlayerTakeReins` and `CharacterService::KeepDriverOn`).

## A copy on a mount (duo, 2026-11-17)

Copies never run the engine's own movement (`HookApplyMovement`: only our step toward the owner's place, or a forced
place). The engine keeps a rider on its horse inside that movement, so a copy on a mount needs its own rules:

- Stepped toward its owner's snapshots while seated, a passenger's copy dragged the host's horse off across Skyrim
  and a driver's copy shook on the saddle (`body_trace`: its 3D swinging up to 70 units). Updated as a copy with no
  step (`CopyMove::Leave`) but its capsule and physics set from its owner's (`SetCapsuleCollision`, `SetCopyPhysics`)
  every frame, the copy's capsule pushed the horse again: seating puts the rider on collision layer 33
  (`MountInteraction::SeatRider` 40358), which that undid. The host's own horse ran 14,000 units with nobody pressing.
- Built: seated (sit state 3, linked), a copy's goal is cleared and the rest of its update skipped: the seat carries
  it, its collision the engine's. Measured: the host drove 1,100 units with a passenger on, the horse stopped, 0 apart.
- Getting off (sit state 4, still linked until its clip's end): stepped, the copy's 3D, attached to the horse, flew up to
  1,100 units off and back (the launch); left alone, it stood on the horse until the clip's end, then jumped to its
  owner's place. Built: at sit state 4 the copy is taken off the horse at once with `Actor::FinishDismount` (38696, what
  the clip's end calls, its sit state left so the clip ends on its own body: `ReplayState::CopyOwnBody`), then placed as
  any copy. Measured, passenger and driver: reference and 3D together from then on, no frame's move over 30 units.
- A mount's own `ExtraInteraction` holds its rider's link (targetHandle the mount itself), so `GetActorMount` counts a
  link only for its rider (`actorHandle`): reading the target alone froze the horse copy as a "rider".
- A rider's reference rides its horse by a `REFRSyncController` (20318, `TESObjectREFRSync`) that `SeatRider` puts on
  its 3D (`REFRSyncController_Attach` 20319 via `TESObjectREFR::AttachSyncControllers` 20312, which does it for both its 3Ds): each update it calls
  `TESObjectREFR::SetPosition` (19790) and `SetRotationZ` (19789) on the rider from the mount node's transform. Our
  `HookSetPosition` refused engine writes on any copy without a copy goal, so a seated copy's reference stayed where it
  got on: measured 1,105 units behind its horse on both games after a drive. Built: a copy linked to a mount at sit
  state 3 takes those writes (`ReplayState::CopyTakesEnginePlace`), and our `HookRotateZ` lets its rotation through
  the same way (it refused every copy's). `party-shares-a-horse` checks every riding copy's
  reference is within 80 units of its horse after each drive (`riders_with_horse`): fails without the fix, passes with.

## A passenger's copy getting on (duo, 2026-11-18)

- Seated at once (`NotifyBoard`: `SeatOnMount`), the copy jumped 174 units from the ground to the saddle, then played
  its whole get-on up there: the playtest's "T-pose for a second". Built instead: `NotifyBoard` links the copy as a
  driver's copy is (`PutOnMount`), its owner's get-on plays from the ground, and it is seated as its stream hold ends
  (`ReplayState::SeatsAtHoldEnd`: on this game's own mount nothing sends the pair's end that seats a rider, 42868).
- Climbing, the copy shoved the host's own horse 60-75 units, and its owner's clip, aimed at where the horse stood,
  ended beside it: the snap the playtest saw as "the guest's physics push the horse". Measured (`anim_trace`
  `bodies_layer`, controller flags): the copy's bodies went to layer 33 (`kBipedNoCC`, its owner's, by
  `SetCopyPhysics`) in the copy's own collision group; the horse's controller took `kTryStep` (0x4), lost support and
  stepped away. Read in IDA: `MountInteraction::SeatRider` (40358) puts the rider's 3D on layer 33 **with the mount's
  group** (`NiAVObject::SetCollisionLayerAndGroup`, the mount's filter high word), and one group never collides. Built:
  a copy linked to a mount takes its mount's group for its bodies (`Actor::SetBodiesLayer`'s `apGroupOf`). Measured:
  the horse moved 0.0 units through the guest's get-on.
- Also kept: a linked copy not seated yet has its capsule collide with nothing (`ReplayState::GettingOnMount`), as
  its owner's on its own game (filter `0x8000`). Tried and dropped: the copy's or the horse's controller out of the
  simulation (`kNoSim`, the owner's flags in its get-on), and the rider layer in the copy's own group: each left the
  horse moving.
- `party-shares-a-horse` checks both get-on and get-off for jumps on both games (`watch_riders` / `riders_smooth`:
  pelvis moves per frame; one 76-106 unit snap allowed per body, a normal get-on's to its mark). 2 of 2 runs pass
  every step; one failed only the end comparison on the plains' critters.

## The Ride prompt (duo, 2026-11-17)

`TESNPC::GetActivateText` (24716) gives a horse `sRide` + its name only when `Character::CanRide(player, horse)`
(38706) passes, which refuses a ridden one; a horse that fails gets no text: no E prompt over a horse a party player
drives. Built: `HookGetActivateText` offers Ride for a horse whose slot's rider is another player's copy (the same
`OffersBoarding` as the activation hook). Harness `activate_text`, verb `prompt_offers`; `party-shares-a-horse` checks it.

IDA: recorded in the shared database (2026-11-17), with this branch's earlier names and comments, which an unsaved
close had lost (replayed from the journal).
