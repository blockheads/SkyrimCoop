# Traps

Read in IDA on 1.7.104 and in the vanilla script sources (the game's `Scripts.zip`), 2026-11-23. Journal
`docs/re/journal/1.7.104/agent-traps-synced.tsv`. No SKSE mod or source handling trap hits turned up (one web search;
`gh search` does not exist here, so GitHub code was not searched; in `reference/plugins` only skymp's event tables
name it).

## A trap that touches (blades, spike walls, bear traps, physics traps, spikes)

```
a character steps (the player's proxy bhkCharProxyController__StepProxy 79149, an NPC's rigid body 79186)
  bhkCharacterPointCollector::ReportTrapContacts 80510   bodies on layer 14 (trap) or 16 only
    -> the cell world's TESTrapListener (bhkWorld +0xC5C0, made by 19195)
       ContactAdded 26067 (actors only) / ContactRemoved 26068
  bhkTrapListener::Step 26053
       new contact    -> TESTrapHitEvent 0  (26033)
       still touching -> TESTrapHitEvent 2  (26032), at most every fTrapHitEventDelayMS (500)
       gone           -> TESTrapHitEvent 1  (26068)
  SkyrimVM's sink 53999 -> OnTrapHitStart / OnTrapHit / OnTrapHitStop on the TRAP's scripts
script: TrapHitBase & co, in state CanHit only
  akTarget.ProcessTrapHit(trap, damage, velocity, position, ...)   56212
    -> target vfunc 0x2C0, Actor::DoTrap1 37509 -> HitData (44000: no aggressor, sourceRef the trap)
    -> ProcessHit 38586 -> DoDamage 37335 (hitter null), stagger, sounds
```

- The engine itself deals no trap damage; `Actor::DoTrap2` (37508, each step in contact) does none (read).
- Contacts come from a character's own step: no special case for the player in 80510 or 26067 (read). That a copy's step
  collects them too is inferred (a copy's plates do fire: triggers are the other half of the same listener).
- `CanHit` is script state: set by the trap's `fireTrap` around its animation (`TrapSwingingWall`, `TrapBear`,
  `BatteringTrap`), or always with a one-hit latch (`BladeTrapHit`, `PhysicsTrapHit`). It lives where the script runs.

## The others

| Kind | What the script does | What hurts |
|---|---|---|
| darts, crossbow, ballista | `Weapon.Fire(linked ref)` | an arrow whose shooter is an object: `Projectile` hit 44123 -> ProcessHit |
| magic trap, flamethrower | `Spell.Cast(self)`, `InterruptCast` | the spell's effects, caster not an actor: `AddTarget` 34526 |
| gas, poison bloom, fire plate, generic | `PlaceAtMe(hazard)` | `Hazard` 43959: its spell on actors in range |
| flame pillar, oil, infected bones | `PlaceAtMe(explosion)` or destruction | `Explosion` 43849 -> ProcessHit |
| logs, rocks, mace | `SetMotionType`, impulse | TrapHit contact, if fast enough (`PhysicsTrapHit`) |

Plates, tripwires and levers (`TrapTriggerBase`) activate the trap; `OnTriggerEnter` is what fires them.

## Today, with two games (read in our code)

- Guest: `WorldEvents` drops `OnTrapHit*` (the handle is the trap, not the player). Since this branch: `docs/design/traps.md`.
- Host, a guest's copy touched: the script runs, `DoTrap1` on the copy, `HitRules` (OtherPlayer, None) = Mirror: nothing.
- `Projectile::Launch` with an object as shooter: `CombatService` sends only an actor's. Hazards and explosions the host
  makes are not references `HostEngine::ReadCreate` sends.

## Seen in the 2026-11-22 playtest's host log

- The spike wall `54B15` (`180For`/`180Back`) and the scythes `236E3`, `236EA` (`Single`/`Reset`, every 2.4 s) were
  sent to the guest: the animation is not what is missing.
- After the party travelled out (20:26:25 and from 20:36:17) `236E3`'s script looped with no 3D: `PlayAnimationAndWait`
  returned at once and about 30 animations a second went to the guest, for minutes. Why the loop was running there is
  not read (its `OnCellDetach` should end it).
- Item 13's host side, measured on the duo 2026-11-23: the host stepping on the plate takes the wall's hit (60 of 75 at
  its difficulty) every round, with or without a guest near; not reproduced.

## A far trap's script waits on a clip that never ends (read and measured, 2026-11-23)

```
TrapSwingingWall.fireTrap: PlayAnimationAndWait("180For", "Done")   56206: registers for the graph's "Done"
UpdateAnimationJob 19204: an object's graph updates (20120 -> 20119) only while BSFadeNode__IsFadedIn 19384
BSFadeNode__IsFadedIn: this game's camera fades the node in (+0x130 > 0)
```

So with the host's camera away from the wall, its graph is not stepped, "Done" never comes and the script stays in
`fireTrap` (`isFiring` true, read): the guest stood on the plate eight times and the wall swung once. Measured with the
fix (`FarAnimations`: 19384 also answers yes for an object the host's script animated in the last 15 s): 8 of 8.
In vanilla the player is always near what its own trap does, so this cannot happen there (inferred).

## A guest's graph refuses an event in a clip (measured, 2026-11-23)

`ObjectReference.PlayAnimation*` on a guest returned false for the host's "180Back" while its own "180For" still ran
(it arrived 30 ms after the host's ended): two to four of every eight swings were never shown and the guest's wall ran out
of phase with the host's. Fixed by `ObjectAnimations` (docs/design/traps.md).

## A trap's cast was classified as the screen's (measured and read, 2026-11-23)

`NativeReach`'s `ClassifyCall` had no bucket for `Call::kCast`: `ObjectReference.InterruptCast` (56187 -> 34408) and
`Spell.Cast` (55747, its `CastFunctor` vtable 218111 addressed) fell into the screen's. `InterruptCast` reached every game
by that accident; `Spell.Cast` from a stack a guest's step caused (its copy on the plate) was that guest's "moment" and
stayed on the host, so the guest saw no flames. The addressed vtable's sink also lost its `Argument` (the caster is
Spell.Cast's first argument), so the caster read as the spell. Both fixed (ClientTests `code_reach_tests`); on the duo the
guest's game then casts the flames from its own jets, its copy of the host burning for nothing (copies mirror).

## A trap's bodies stay the host's (measured, 2026-11-24)

WarehouseTraps' mace 2464D: 14 bodies, its base, pin and 11 links on layer 10 (props), its head `Mace01` on 14; the
falling rocks one body on 14. By the physics authority's touch rule the guest's player took the swinging mace, and the
host's only followed it: contacts with the copy at no speed, no hit (`warehouse_traps_hurt_both` failed four runs). A
reference with any body on a trap's layer is now the host's, all of it: the guest never takes one (`GuestBodies`), the
host refuses a claim (`HostBodies::MayClaim`). Then the host judged the hit in four of four tries and the scenario's
trap steps passed. Its rests on the guest still show small pose drift (a rock's node turned, rope links up to 20 units).

## A physics trap's hit is the contact's speed (read and measured, 2026-11-24)

`PhysicsTrapHit` hits on a new contact (`OnTrapHitStart`) whose event velocity squared is at least 6
(`damageVelocityThreshold`), and is not hit-once: a return swing hits again. A character's trap contacts come only from
its own proxy step (`bhkCharProxyController::StepProxy` -> 80510; a still copy steps by nothing and reports them too).
The event's velocity (`TrapTargetEntry::GetVelocityAndPosition` 26034) is the trap body's, less its projection of the
character controller's velocity: a target moving with the trap is hit softer, one walking into it harder.
WarehouseTraps' mace, someone standing on its tripwire: the swing is spent there, first contacts squared 3.4 to 14 on the
guest's copy and on the host's own player alike, each missed some runs. Walked into across the wire
(`warehouse_traps_hurt_both`), the mace meets the guest near the bottom of its arc and hit in both runs. A copy hit
twice where the host's own player is hit once: the hit's push moves the host's player at once, the copy only when the
guest's movement arrives, so the return swing meets it still standing (not fixed).

## Darts carry their damage (read in IDA and measured, 2026-11-23)

- Read in IDA: a relaunched dart is built by `Projectile::Constructor` (44100) from the launch data's weapon and ammo
  (`weaponDamage` +0x1A0 = `ActorValueOwner` 26410 over them, temper 1 for an object shooter), which
  `HitData::InitializeArrowData` (44002) reads.
- Measured on the duo: both games logged weapon 142E3 with the same damage, about 9 health a hit on either game.
- Corrected 2026-11-24: the strike itself is 0.8 (the host's own player too); the 10-13 a round is the dart's poison,
  enchantment 101932 / effect 101931 (−3 health a second for 8 s). `HandleRefImpact` (44205) casts it by the hit
  actor's own instant caster on itself. Sent to a guest with no caster, the guest's engine refused it (its add check
  34525 judges the effect's conditions with the caster as reference); sent with its caster, the hit copy, which is the
  guest's own player there, it lands as on the host (`Projectile::WorldImpact`, GameTests "A dart an object fires").

## Explosions: their hit, poison and line of sight (read and measured, 2026-11-23)

- `Explosion::ApplyToTargets` (43849) handles each actor in its list once: a `TESHitEvent` first, before any check
  (so a hit event does not mean it reached), then `Explosion__HasLineOfSight` (43852), then a hostile explosion's
  `HitData` and `ProcessHit` (38586) unless the actor is its cause, then the explosion's enchantment. The enchantment is
  cast by the explosion's `NonActorMagicCaster` (made by `Explosion__Ctor` 43827 when its cause is no actor) or by its
  cause actor's caster, and never on that cause.
- 43852 casts a ray (layer 43, the target's controller group skipped) from the explosion's position, moved along its
  normal (+0x130..+0x138), to the target's 3D bound centre. Measured on WarehouseTraps' gas plate: an explosion placed
  at an actor a little inside the plate faces down (0, 0, −1) and its ray starts 24 units under the floor: blocked.
- A placed hazard casts by a `NonActorMagicCaster` on the stack with no reference (`HazardTargets__CheckActor` 43987),
  so its effects have no caster at all. An explosion's effects have the explosion as caster.
- `MagicCaster__CastSpellImmediateBase` (34404) goes to `FindTargets` (34410); an area effect visits the actors in range
  (`MagicCaster__AreaTargetVisitor` 34461: skipped if the caster's actor, without 3D, a ghost, or out of the area).
