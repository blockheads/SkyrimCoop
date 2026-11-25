# Traps (2026-11-23)

Playtest 2026-11-22, items 13 and 14. The engine's side: `docs/re/systems/traps.md`.

**The host runs the trap; each game judges what touched its own player; the player's game takes the hit.** The same
split as NPC melee (a copy's replayed swing on this game's player is applied here) and physics (a copy's capsule pushes
nothing; each player's game pushes).

```
setting it off (plate, lever), its script, its states and damage   host
its animation                                                      host's, relayed; each once its graph takes it
what touched a guest's player                                      that guest's game -> the host's script
what touched the host's player or an NPC                           host
the hit                                                            the touched actor's owner (DoTrap1 37509)
```

## A touch

```
guest   TESTrapHitEvent on its own player  (hook on the VM's sink 53999, TrapRules::OfContact)
          -> TrapContactEvent -> GuestTraps -> TrapContactRequest
host    HostCombat::OnTrapContact: from the touched player's own game, finite
          -> HostGame::TouchTrap -> Traps::RaiseContact: TESTrapHitEvent on the guest's copy
          -> the trap's script (state CanHit, damage) -> ProcessTrapHit(copy)
          -> hook on DoTrap1 (TrapRules::OfHit: AskOwner) -> TrapHitRequest (the host's game only)
guest   NotifyTrapHit -> IActorBridge::TakeTrapHit -> DoTrap1 on its own player: its armor, difficulty, stagger, sounds
```

The host's own contacts with a guest's copy are dropped: they would hit it twice. The cost is a round trip, and a touch
at the very end of a `CanHit` window may come too late.

## Animations

A guest's graph refuses the host's next event while its own clip still runs (measured on the spike wall: from then on
it was out of phase, two to four of eight swings never shown). The host's script waited for that clip, so the guest
does: `ObjectAnimations` (GuestWorldState) keeps an object's animations in order, each sent once its graph takes it, 3 s
at most. The host's own graph of a trap out of its camera's sight is not updated (19384), so its script waited forever
for the clip's end: `FarAnimations` keeps updating an object its script animated in the last 15 s. On the host an animation repeated on an object with no 3D is sent once (`TrapRules::SendsAnimation`): a scythe
left looping sent about 30 a second for minutes.

## What a trap fires, casts and places

One rule for everything the world does to a player: **the host's game decides, every game shows.**

- **What an object shoots** (a dart port's `Weapon.Fire`, a flame jet's or a statue's `Spell.Cast`, Storm Call's
  activator, a catapult), of any kind: `Projectile::Launch` (44108) with no actor shooter goes from the game that
  launched it to every other with shooter 0 (`CombatService`, the same path as Storm Call's bolts,
  `docs/re/systems/projectiles.md`), and is relaunched there with no shooter, so it shows and applies nothing there.
  Its hit on a guest's copy is the host's world hit, sent to the guest's game (`HitRules::Of`, `aWorldHit`).
- **Casts** (`Spell.Cast`, `InterruptCast`): run where their script does (`NativeReach::Call::kCast`, never the
  screen's); what they launch reaches the others as above.
- **Hazards and explosions** (`PlaceAtMe`): created references the host sends like any other (`HostEngine::ReadCreate`);
  an explosion's placed hazard each game's explosion places.
- **What they do to a guest's character:** the host's verdict on its copy (`MagicTarget`'s world cast, any cast no actor
  casts -> `AddTargetRequest`; a world hit, no actor aggressor -> a health change); a guest's own copy of a world cast
  or world hit applies nothing to its player (`TrapRules::AppliesWorldEffect`, `HitRules::Of`). The host's hazard ticks
  can reach the guest twice before its copy answers: a guest skips a world cast of an effect it holds from no actor
  (`TrapRules::SkipsWorldEffect`). A trap's script that asks the copy about its effects gets its player's answer
  (copy-contract.md), so the host's gas and hazards stop at a player already under them.

## Tests

- UnitTests `[TrapRules]`, `[ObjectAnimations]`, `[ClipHolds]`; TPTests `[encoding.traps]`; HostAuthorityTests `[traps]`.
- GameTests `trap_flows.cpp`: a guest's touch hurts it once on its own game (fails with the contact or the hit routing
  off, or with the host judging its copy too), CannotHit hurts nobody, the host and an NPC take theirs on the host, the
  animations wait for the guest's clip, a dart flies on every guest once.
- Scenarios, each with the guest and the host as the victim, each hurt on its own game:
  - `spike_wall_hurts_both` (Bleak Falls Barrow, the wall's plate): every round; before, the guest never.
  - `scythes_hurt_both` (Bleak Falls Barrow, the blades while the other player holds their trigger box): every round.
  - `darts_hurt_puller` (Bleak Falls Barrow, a wrong pull of the pillar lever): the puller, in at least one of three
    pulls (the ports fire at random; about 9 health a dart).
  - `flames_hurt_both` (Morvunskar, in front of the jets while the other player is on the plate): every round, the
    guest's burn the size of the host's (not applied twice).

  - `warehouse_traps_hurt_both` (the test cell WarehouseTraps, a fresh duo: rocks, mace and logs fire once a load):
    the fire plate's hazard (both), falling rocks (both), the mace (the guest), the log lean-to the host pulls (the
    guest), the poison gas each player walks onto (both).

Open:
- **A guest pushing the host's swinging mace**: its body went to the guest's physics (`HostHands: ... moves 2464D`), as
  any body its player touches does; the trap's swing may then follow the guest's game. Not measured further.
- Floor rays look past triggers (layer 12: a pressure plate's volume, 15 above the floor characters stand on), in the
  harness's `floor_under` and the body readout (`EngineReads::GetBodyPose`), which read a player on a plate as sunk.

## A hazard's or explosion's effect is the host's verdict (measured, 2026-11-23 and 24)

A causeless hazard or explosion (a trap's) casts with no caster at all: a hazard through a `NonActorMagicCaster` on the
stack (`HazardTargets__CheckActor` 43987), an explosion through its own (43827, 43849). On the host it hits the guest's
copy and its effect is relayed; the guest's own copy applies nothing. Tried and reverted: each game's own copy judging
its own player. On WarehouseTraps' gas the guest's copies of the first explosions failed their line of sight for about
3 s, while the host's script, seeing an unpoisoned copy, kept firing; their poisons then landed together and stacked.

The host's scripts and hazards ask the copy about its effects: the gas script (`TrapPoisonGas`'s `onTrigger`:
`HasMagicEffectWithKeyword(gasKeyword)`, its source in Enderal's mirror of the Skyrim scripts) and a hazard's
`IsSpellTarget` (43987). A copy held no effects, so both fired at it again and again (the fire plate's burn 0.7 rising to
7 a tick). Copies now answer with their owner's active effects (`MagicTarget::HasMagicEffect` 34517,
`HasMagicEffectWithKeyword` 34518, `MagicItem::IsSpellTarget` 34513 hooked; the owner's list sent with
`NotifyActiveEffects`, copy-contract.md). GameTests "A hazard's or explosion's effect on a guest's player is the host's
verdict, once", "A script asking a guest's copy for an effect gets its player's answer".

## A tripped tripwire is broken on every game (read and measured, 2026-11-23)

A tripwire's script marks it destroyed when tripped (`ObjectReference.SetDestroyed` 56641 -> `TESForm__SetDestroyed`
14649: form flag 0x800000 and a form-flags change). World state's `kEnabled` row took that change but sent only
disabled and deleted, so the guest's tripwires stayed whole (the end comparison, on all three in WarehouseTraps). The row
now packs destroyed too (`Games/EnabledRow.h`, UnitTests `enabled_row_tests`), applied by 14649 on the guest; GameTests
"A reference the host's script marks destroyed is destroyed on the guest".

The scenarios' hits: a burn drains a little each frame, under the 0.5 a readout step counted, so `trap_rounds` adds the
drain up until it is a hit (the flames' first round read as missed while the host's burn was applied four times).

## A copy stands at its owner's height (measured, 2026-11-23)

The gas plate never poisoned a guest who walked onto it (the host: every round). An explosion takes its normal from the
nearest non-actor surface its phantom touches (`Explosion::sub_43851` 43851: contact point and normal, an actor's bodies
skipped) and starts its line of sight along it (43852). The host's copy of the guest stood half a unit under the guest
(272.07 against 272.6): a copy is an NPC, its `bhkCharRigidBodyController` resting lower than a player's proxy (body
readouts: a player's feet 0.4 above a floor ray, NPCs' 0.7 to 1.5 below), and within `CopyStep::kStill` it steps by
nothing. The explosion placed at its feet was under the plate's surface, faced down and its ray started under the floor;
the guest's copy of the explosion, placed where the host's went off, the same. Now a stepped copy on its owner's place
(standing or walking: its step lands 0.04 short; not one pushed off it or a ledge apart) takes its owner's height at the
write-back after physics (`HookSetPosition`, `CopyStep::RestHeight`; the reference
only, its capsule rests where it does): the copy 0.2 off its owner, the guest poisoned in 4 rounds of 4. UnitTests
"A copy on its owner's place stands at its owner's height", GameTests "A guest's player standing still: its copy on the
host stands at its height" (the fake's copy controller rests `kRigidRest` under its owner); the gas steps in
`warehouse_traps_hurt_both`.

## Review fixes (2026-11-23)

- The host's dedupe of an animation repeated on an object with no 3D (a script looping there) now drops a repeat only
  within `TrapRules::cAnimationRepeat` (1 s): a trap fired again later still reaches the guests.
- Not taken: dropping a refusal at once when no clip of ours runs. Measured: a tripwire's graph refused the host's
  first `TRIGGER` and took it on a retry; dropped, the guest's mace never fell. Every refusal waits up to `kWait`.
- An explosion's placed object (a hazard: `Explosion::Func162` 43830 through `CreateReferenceAtLocation` 13723) is
  not sent as created: each game's explosion places its own (`ModManager.cpp`). A script's `PlaceAtMe` hazard still is.
- The host-only trap code lives in `Services/Host/HostTraps` (its hit on a guest's copy, what its objects fire), the
  guest-only in `Services/Guest/GuestTraps` (its contact, the host's hit, the host's projectiles).
