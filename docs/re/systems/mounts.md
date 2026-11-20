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

## A horse taken back mid-pair

Read in IDA 2026-11-18 unless marked measured; the branch journal `docs/re/journal/1.7.104/agent-horse-riding-copy.tsv`.

```
the mount package's step (AIProcess::MountProcedureStep 39669; the same in AIProcess::UpdateHighProcessAI 39637's mount case)
  Actor::TryStartMountInteraction (38704): a ridden mount refuses, else a new MountInteraction
    RefrInteraction::StartWithActivate (41530): the rider's sit state set, ActionActivate rider->mount
      refused by a graph (2): nothing attached; else AttachToRider (41540)
  refused: kGettingOnOffMount cleared on both, then InitiateMountPackage (37905) again; the next update retries
```

- Measured (duo, `horse-passes-between-riders`): a guest's ride ends and the host takes its horse back
  (`NotifyDismount`) while the horse copy still replays the guest's `pa_HorseExit` pair. Its stream's `PairEnd`
  comes later and is lost once the horse is the host's, so the host's horse stays in the pair (`bIsSynced` on). The
  host's next get-on is refused until the refusal's own `PairEnd` and the blend out of it: five tries in 0.3 s, each
  one a 37905, so our hook sends a `MountRequest` and the guest gets a `NotifyMount` each time.
- Measured: between those tries the host's player reads sit state 4. Its copy on the guest replays that, and a linked
  copy at 4 is taken off (`FinishDismount`, above). The try that finally starts calls no 37905 again, so no
  `NotifyMount` re-links the copy. It plays `HorseEnter` and sits (sit state 3) with no link: not riding, the horse
  copy not ridden, and the copy's `pa_HorseExit` is refused later.
- The first fix (2026-11-18) sent the horse `PairEnd` by hand on the host (`ActorEndMountPair`); replaced by the
  section below, which found why the stream's `PairEnd` never came. Not handled (inferred): a get-on the engine
  refuses for another reason still links and then takes off a copy the same way.

## A horse handed back between pairs (one-simulator.md piece 4a, 2026-11-19)

Measured on the duo (`horse-passes-between-riders`, `anim_trace` of the horse on both games, `bIsSynced`), with
`BGSSynchronizedAnimationInstance::Deactivate` (32797) read in IDA:

```
rider's game, one engine frame N
  the exit pair's instance ends: Deactivate (32797) sends the horse PairEnd    (input from 54F342)
  then RefrInteraction::Detach (41539): our DismountEvent                       (2 ms later, same frame)
  at our handler (frame N+1): the horse in no instance (32808), bIsSynced still on (blends out ~190 ms later)
the horse's copy on another game
  PairEnd only from its owner's graph stream: an event op, due with the owner's NEXT update (GraphReplayer::Feed)
  plays ~100-200 ms behind (CopyPlayback)
```

- Why the host's horse stayed synced (the bug above): the rider's game sends its graph ops once a round (~50 ms,
  `SendGraphStream`); its `NotifyDismount` came back in a few ms and giving the horse up dropped the unsent ops,
  `PairEnd` among them. The host's copy replayed the guest's ops up to 57 ms before the `Detach`, then nothing.
- Built: the rider's game hands the horse back once its next graph update after the `Detach` is recorded
  (`CharacterService::RunHandBacks`, `GraphStreams::Updates`), sending its ops first; the hand-back carries that
  update's tick (`Payloads::Dismount::Tick`, through `HostCharacters::GiveBack`). The host's game waits until its copy
  has run every op through that tick (`GraphReplayer::DoneThrough`, `ReplayState::TakesOverMount`,
  `CharacterService::RunTakeOvers`), then takes it. The host's `PairEnd` by hand is gone.
- Measured with it: taken 235 ms after the hand-back, the host's next get-on accepted at the first try (one
  `MountRequest`, against five before).
- **Not for a passenger who drives on** (measured, `party-shares-a-horse`): taken the same way, after its copy ran the
  old driver's `PairEnd` from the stream (`bIsSynced` off 200 ms later), the new driver's horse did not steer, nor after
  the harness's `take_reins` (slot, 40725, `EvaluatePackage`, `PairEnd`). Taken at once without a `PairEnd` it stayed in
  the pair and did not steer either. Taken at once with `PairEnd` by hand (`PlayerTakeReins`, `KeepDriverOn`), as
  before, it steers (3 of 3). That stays. No passenger trace read the old driver's instance at the handover; observed
  only: the new driver's horse graph ran the old driver's whole `pa_HorseExit` half, then did not steer. Why is not
  read (inferred: its graph left riderless by the exit, which the new driver's seat never sets again).
- The rider-side wait for the instance's end that piece 4's plan proposed (a `Deactivate` hook) is not needed: the
  instance is gone at every measured `Detach` (6 of 6 rides).
- `bIsSynced` is no signal for "the pair ended": it outlasts the clip on the owner too.

## A low-process mount gets its rider off

`AIProcess::UpdateLowMountRider` (39174): a mount with no middle-high data that carries a rider of process level 0 or 1
gets it off through `StopRidingQuick(rider, 2)`. Inferred, not measured: a copy horse dropped to low process under a
high-process rider copy would lose its rider this way, a copy standing while its owner rides.

## A ride the host refuses

Two players getting on one horse within a round trip: `HostCharacters::OnMount` keeps the first rider and sends the
second game `NotifyMountRefused`; that game's player gets off by `StopRidingQuick` (38705, mode 2, as PayFine).
Read: mode 2 forces 40352's finish, so the ride ends (Detach) inside the call, whatever the exit action did.
