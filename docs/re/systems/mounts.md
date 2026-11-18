# Mounts

How a ride starts and ends in the engine (1.7.104, read in IDA 2026-11-17 unless marked measured or inferred; the
branch journal `docs/re/journal/1.7.104/agent-horse-riding.tsv` records each read), and where the co-op code hooks it. Design: `docs/design/party-travel.md` ("A ridden horse"), `docs/design/copy-contract.md` ("Player mounts").

## Getting on

```
Actor::InitiateMountPackage (37905)            hooked: MountEvent (an own rider) -> MountRequest, once started
  returns false for a refusal and for the at-once attach; true for the package
  mount null -> the rider's ExtraHorse (0x7B, 12033), else FindNearbyFreeHorse (38045)
  Character::CanRide (38706) gates
  low-process rider: MountInteraction (40345) attached at once (41532), no package   <- Actor::PutOnMount does this
  else: mount package (type 41), flags2 kGettingOnOffMount (8) on both, deferred kill on both
the clip: PairedStopHandler (42868, sit state 2 -> 3); MountDismountEndHandler (42881) clears kGettingOnOffMount
```

`Actor::MountHorseAtOnce` (37904) is the same at-once link for the rider's own horse.

## Getting off: every path ends at RefrInteraction::Detach (41539)

```
Actor::Dismount (37906)                         the player's activate, Papyrus, the console, get-up package
  refuses: not on a mount, kGettingOnOffMount set, midair / bInJumpState, the player with no clear spot
  low process: EndMountInteraction (41535) at once
  else: dismount package (type 42), mount's ClearMountPosition (37907), kGettingOnOffMount + deferred kill on both
  -> returns true: the ride has NOT ended yet
the HorseExit clip -> StopMountCameraHandler -> Actor::FinishDismount (38696)
  rider placed beside the mount, flags cleared, EndMountInteraction (41535) -> Detach (41539)

StopRidingQuick (38705, a mode)                 no Dismount, no package, no kGettingOnOffMount
  mode 2: PayFine (40659), GoToPrison (40655), QueueJailSentenceTravel (40656), FinishLoadGame (40542),
          Main 36581, 40445, AIProcess::UpdateLowMountRider (39174), our refused ride
  mode 0: AIProcess::DismountProcedureStep (39670), Dismount's package
  -> MountInteraction::StopRiding (41531): sit state wants-to-stand, then PlayExitOrFinish (40352):
     ActionActivate rider->mount (HorseExit + the mount's pa_HorseExit)
     mode 0: the clip ends it (FinishDismount as above); refused: sit state 0, FinishDismount at once
     modes 1, 2 force the finish: sit state 0, FinishDismount (38696) -> Detach at once, the clip only shown
Predestroy, GarbageCollector::Add -> EndMountInteraction (41535) -> Detach
```

`SetSitSleepState` (39912) to 0 or 3 for a rider only clears `kGettingOnOffMount` on both; it does not end the
interaction. `ChairFurnitureExitHandler` (42835) sets sit state 0 (a copy's replayed getting off).

**Hooked:** `RefrInteraction::Detach` (41539, `HookDetachInteraction`): an own rider's ride ended, by any path above
(`DismountEvent` with its mount) -> `DismountRequest`, the host takes the horse. Until 2026-11-17 the hand-back hooked
`Actor::Dismount`, which only starts a dismount: the horse went back mid-clip, and a fine, jail or load (no Dismount)
left it the guest's ("The guest's ride ends with the horse still the guest's", bugs.md).

## Activating a horse

```
ActivateHandler (42420) -> PlayerCharacter::ActivatePickRef (40548)
  the crosshair's ref; only with none, a rider's own mount
  -> TESObjectREFR::ActivateRef -> Character::ActivateMount (37864, activator, target a mount)
       riding it          -> Dismount (37906)
       free               -> InitiateMountPackage (37905)
       ridden by another  -> nothing
```

- A rider aiming at an NPC or another player activates that, and stays on: a scenario's rider looks clear first
  (`get_off`).
- A copy runs no mount or dismount package, so `InitiateMountPackage` or `Dismount` on a copy leaves
  `kGettingOnOffMount` set on it (37462 `SetGettingOnOffMount`: every other caller clears it). Until 2026-11-17 a
  player's activation of a host horse went to the host as an NPC activation, whose replay did exactly that on the
  host's copy of that player (`horse-passes-between-riders`). A mount's activation is not sent now: the ride goes as
  `MountRequest`.

## A dismount too soon after getting on

```
the dismount package's step (AIProcess::DismountProcedureStep, 39670)
  -> StopRidingQuick(rider, 0) -> StopRiding (41531) -> MountInteraction::PlayExitOrFinish (40352)
       ActionActivate rider->mount: HorseExit + the mount's pa_HorseExit
       refused (or forced): sit state 0, FinishDismount (38696) at once
```

Measured 2026-11-17, with no session too: a dismount within ~0.1 s of the rider's sit state 3, while the mount's own
`pa_HorseEnter` half still runs (its `bIsSynced` on), is refused this way. The rider is off with no exit clip, its
graph left in its riding pose: its body reads crouched (pelvis at its feet). A scenario's rider gets off only once the
horse's half is over too (`seated_on_mount`).

## A copy rider's ride ends with its owner's

- Its owner played the exit clip: the copy replays `HorseExit` (sit state 4) and its clip's own exit event unlinks it,
  a moment after its owner's `Detach` (the guest's horse copy still read ridden after the host was off, 2026-11-17).
  Until then that game's player's Activate on the horse does nothing (37864).
- Its owner got off with no exit clip (above): no `HorseExit` reaches the copy, which would stay seated (sit state 3)
  for good. On `NotifyDismount` a copy rider still seated is taken off as 40352 does (sit state 0, `FinishDismount`);
  one getting off already is left to its clip (`IActorBridge::ActorEndRide`).

## A low-process mount gets its rider off

`AIProcess::UpdateLowMountRider` (39174): a mount with no middle-high data that carries a rider of process level 0 or 1
gets it off through `StopRidingQuick(rider, 2)`. Inferred, not measured: a copy horse dropped to low process under a
high-process rider copy would lose its rider this way, a copy standing while its owner rides.

## A ride the host refuses

Two players getting on one horse within a round trip: `HostCharacters::OnMount` keeps the first rider and sends the
second game `NotifyMountRefused`; that game's player gets off by `StopRidingQuick` (38705, mode 2, as PayFine).
Read: mode 2 forces 40352's finish, so the ride ends (Detach) inside the call, whatever the exit action did.
