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
          -> TrapContactEvent -> TrapService -> TrapContactRequest
host    HostAuthority::OnTrapContact: from the touched player's own game, finite
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

- **Darts, bolts** (`Weapon.Fire`): `Projectile::Launch` (44108) with an object as shooter goes to every guest
  (`WorldProjectileRequest`), which launches it from its own reference. Each game's dart hits its own player, as an
  NPC's arrow does.
- **Casts** (`Spell.Cast`, `InterruptCast` by an object): relayed to every game as the script's call
  (`NativeReach::Call::kCast`, `PartyRules::RouteOf`), to show. What it does to a guest's player stays the host's
  (`MagicTarget`'s world cast -> `AddTargetRequest`), so a guest's own copy of an effect no actor casts applies nothing
  to its player (`TrapRules::AppliesWorldEffect`).
- **Hazards** (`PlaceAtMe`): a created reference the host sends like any other (`HostEngine::ReadCreate`).

## Tests

- ClientTests `[TrapRules]`, `[ObjectAnimations]`, `[ClipHolds]`; TPTests `[encoding.traps]`; HostAuthorityTests `[traps]`.
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
    the fire plate's hazard (both), falling rocks (both), the mace (the guest), the log lean-to the host pulls (the guest).

Open:
- **A guest pushing the host's swinging mace**: its body went to the guest's physics (`HostHands: ... moves 2464D`), as
  any body its player touches does; the trap's swing may then follow the guest's game. Not measured further.
- Floor rays look past triggers (layer 12: a pressure plate's volume, 15 above the floor characters stand on), in the
  harness's `floor_under` and the body readout (`EngineReads::GetBodyPose`), which read a player on a plate as sunk.

## A hazard's burn once (measured, 2026-11-23)

A placed hazard casts its spell on an actor only while that actor is not under it (`HazardTargets__CheckActor`, 43987:
`MagicItem::IsSpellTarget`). The host's copy of a guest never takes the effect, so the host's hazard cast again at every
tick and each reached the guest as a new burn (0.7 rising to 7 a tick). A guest now skips a cast with no caster of an
effect its player already has (`TrapRules::SkipsWorldEffect`, MagicService); its own copy of the hazard or explosion,
cast with no caster too, applies (`TrapRules::AppliesWorldEffect`), the host's then skipped.

## An explosion's poison once (measured, 2026-11-23)

The gas trap places an explosion every half second; an explosion's enchantment is cast by its own `NonActorMagicCaster`
(43827, 43849), so the effect's caster is the explosion (an object), and `AddTarget` sees no caster actor. The host's
cast on the guest's copy always reaches the guest first (the guest's copy of the explosion is made from the host's next
message), and the guest's copy then poisoned it again: 1.3 a tick against the host's 0.6, up to 7 as they stacked. Each
side now checks for the other's: the host's request is skipped while an effect of the spell by an object lasts
(`ActiveEffectInfo::CasterIsActor`, MagicService), and the guest's own cast with no caster while the host's (no caster)
lasts (HookAddTarget). GameTests "An explosion's poison on a guest's player is applied once", both orders.

## A tripped tripwire is broken on every game (read and measured, 2026-11-23)

A tripwire's script marks it destroyed when tripped (`ObjectReference.SetDestroyed` 56641 -> `TESForm__SetDestroyed`
14649: form flag 0x800000 and a form-flags change). World state's `kEnabled` row took that change but sent only
disabled and deleted, so the guest's tripwires stayed whole (the end comparison, on all three in WarehouseTraps). The row
now packs destroyed too (`Games/EnabledRow.h`, ClientTests `enabled_row_tests`), applied by 14649 on the guest; GameTests
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
only, its capsule rests where it does): the copy 0.2 off its owner, the guest poisoned in 4 rounds of 4. ClientTests
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
- The host's world-projectile send lives in `Services/Host/HostWorldProjectiles`.
