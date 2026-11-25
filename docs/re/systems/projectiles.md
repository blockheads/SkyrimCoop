# Projectiles: who shoots, and what lands

How a spell's projectile is launched, by an actor or by an object, and what applies its spell when it hits. Read in IDA
(1.7.104) for bug 18 (2026-11-24): Storm Call's lightning showed only on the game that cast it.

## Storm Call (records, Skyrim.esm)

- `StormCallShout` 7097D: words 6029A/B/C, spells `VoiceStormCall1/2/3` (18609, 1860A, 1860D), self-delivered.
- Each spell: `VoiceStormCallEffectN` (on the actors around, by its conditions), `VoiceStormCallEffectNSelf` (on the
  caster: `magicSetWeatherScript` and the same storm script) and an empty AI effect; 60/120/180 s, areas 100/100/150.
- Both run `magicProjectileStormScript` (vanilla source, `Data/Source/Scripts`): `OnEffectStart` places an activator
  (`PlaceAtMe`) above the target, then every 1-3 s moves it and calls `SpellRef.RemoteCast(activator, caster, target)`.
  `SpellRef`: the self effects and `VoiceStormCallEffect1` cast `StormCallLightningBolt01` (E4CB7),
  `VoiceStormCallEffect2/3` cast `...02/03` (E98A2, E98A3; 60 and 80 damage against 40). All three are
  `ShockDamageBoltStormAimed` (E4CB6) with projectile `ShockBoltAimStorm` (E4CB5): a beam whose base has no explosion
  (flag 2 off). The effect's own explosion field (`StormCallImpactSoundExplosion` EAFE3) is a sound with no damage or
  enchantment record. On the caster itself it misses on purpose (`bDontHitCaster`): bolts around the shouter.
- So the script runs where the effect is applied: the NPCs' on the host (their owner), the guest's own on the guest.

## Spell.RemoteCast to the launch

| ID | Engine function | What it does |
|---|---|---|
| 55748 | `papyrus::SPELL::RemoteCast` | The same `CastFunctor` as `Spell.Cast` (55747) with the blame actor's handle at +0x20, queued (53954). |
| 55744 | `CastFunctor::operator()` | The source's `GetMagicCaster(kInstant)` (vfunc 0x2E0); `InterruptCast`; `CastSpellImmediate(spell, false, target, 1.0, false, 0, blame)`. |
| 34697 | `NonActorMagicCaster::CastSpellImmediate` (vfunc 1) | Keeps the blame actor's handle (MagicCaster +0x50), then 34404 → `FindTargets` (34410). |
| 34698 | `NonActorMagicCaster::GetCasterAsActor` (vfunc 0x0C) | The blame actor. CommonLibSSE-NG says it returns 0: it does not. |
| 34699 | `NonActorMagicCaster::GetCasterObjectReference` (vfunc 0x0D) | Returns 34710, the reference itself (MagicCaster +0x48); the out actor stays unset. |
| 34410 | `MagicCaster::FindTargets` | For an aimed spell, the shooter is vfunc 0x0D's reference (at `0x1405CBB51`): an actor for `ActorMagicCaster`, the activator for a `NonActorMagicCaster`. |
| 34452 | `MagicCaster::FireProjectile` | Fills `Projectile::LaunchData` (shooter = that reference) and calls `Projectile::Launch` (44108, our `HookLaunch`). |

So a bolt cast by a script from an activator is a projectile whose shooter is the activator, never an actor, and the
blame actor is not in its `LaunchData`.

## What applies a projectile's spell

| ID | Engine function | What it does |
|---|---|---|
| 44169 | `Projectile::GetMagicCaster` body (vfunc 0x5C) | With a spell (+0x160): the shooter's (+0x128 handle) `GetMagicCaster(castingSource +0x168)`. No shooter: null. |
| 44205 | `Projectile::HandleRefImpact` | A hit on a reference: with a spell, 44206; with only an enchanted weapon (+0x1B8), the shooter's instant caster casts the enchantment (no shooter: nothing). |
| 44206 | `Projectile::ApplySpellOnHit` | Takes 44169's caster; with none it does nothing: no death force, no `MagicCaster::ApplyAtImpact`. |
| 34412 | `MagicCaster::ApplyAtImpact` | The spell's effects on what was hit and, for an area, on the actors `ProcessLists` finds around, each through `AddTarget`. |
| 43849 | `Explosion` apply to targets | An explosion applies only its own base's damage (HitData) and enchantment, not the spell; 43850 and 43854 push bodies. |
| 44108 | `Projectile::Launch` (at `0x1407F9A4D`; 44120 too) | The projectile's explosion (+0x158) is its base's `explosionType` (`BGSProjectile` +0xB0). |

A projectile's spell lands only through its shooter's caster. An activator's caster blames the blame actor (34698), so
`GetCasterAsActor` answers the shouting player (or its copy); whether `AddTargetData::caster` is it was not read. The explosion is apart from the caster: what a
shooterless explosion does (its damage, enchantment and push) was not read when this was written; one whose base
explodes is now relayed like any other (below, "How we sync it"), its hit on a guest the host's. Not read either: where an effect's own explosion field (EAFE3 here) is played.

## How we sync it

`HookLaunch` (44108) raised `ProjectileLaunchedEvent` for every launch, but `CombatService` sent only a launch whose
shooter is one of this game's actors (`LocalComponent`). A script's activator is not one, so every Storm Call bolt
stayed on the game that ran the script: the NPCs' bolts on the host, the guest's own on the guest. The same goes for
any spell an object casts (a statue, `Spell.Cast` from an activator, the random lightning triggers).

Now `ProjectileLaunchedEvent::FromObject` marks a shooter that is no actor. A projectile so marked, of any kind (a
spell's, a dart trap's, one that explodes on impact), goes out with shooter 0 (`ProjectileLaunchRequest`), from whichever game launched it (the host relays it to the others:
`HostAuthority::OnProjectileLaunch`). The others relaunch it from the same origin and angles with no shooter
(`SkyrimBridge::LaunchProjectile`, which calls the engine's launch past our hook): it flies and strikes, and 44169
gives it no caster, so it applies nothing there. The effects arrive as for any other spell: the launching game's
`AddTarget` on a copy goes to the target's owner.

A weapon projectile (a dart) applies its hit in `Projectile::Func190` (44204) only while its weapon damage (+0x1A0) is
above zero, which its constructor (44100) works out from the shooter: relaunched with no shooter it strikes for nothing
(measured: the guest struck, no health lost). Shot by an object on the host, its `HitData` has no aggressor
(`InitializeArrowData` 44002), and `ProcessHit` (38586) on a guest's copy reached `DoDamage` (37335) with no hitter and
was dropped. A hit with no actor aggressor is now the host's world hit (`Projectile::WorldHit`, scoped on each run of
38586, which queues itself off the main thread: `TaskQueueInterface::ShouldUseTaskQueue`): on another player's copy it
goes to its owner as a health change (`HitRules::Of`, `aWorldHit`). On a guest, its own copy of a world hit on its
player (the relaunch of a projectile that explodes on impact, the Fishing catapult: force 2300, radius 2000, whose
explosion 43849 hits through 38586) is dropped: the host's verdict counts, once. The host's relayed trap hit, applied
through `DoTrap1` (37509 -> 38586) with no aggressor too, is exempt (`Projectile::RelayedHit`). Not measured on the
catapult.

A concentration spell an object casts (Morvunskar's flame jets, Flames 12FCD) is a stream of short projectiles
(12FCF), each its own launch through 44108: 117 in a 7 s round, measured 2026-11-24. `HookLaunch` left every
concentration launch to the cast sync, which only an actor's copy runs, so the guest saw none. Now only an actor's is left
(`TrapRules::SendsLaunch`); an object's go out like any shot, and the guest relaunched all 117.

Its weapon's enchantment is cast in `HandleRefImpact` (44205) by the hit actor's own instant caster on itself. With
no actor shooter (`Projectile::WorldImpact`, scoped on 44204) that cast on a copy is the world's, sent to the owner
with the copy as its caster, as the engine had it: a dart's poison is conditioned on its caster (traps.md).

Outdoors the message carries the worldspace, and the cell goes as none when the engine made it (no plugin record:
`ToSessionCell`); the others find the cell from the worldspace and the origin (`TravelService::CellAt`), as for actors.

**A guest's storm on a host NPC (inferred from our code, not measured).** The host runs that NPC's effect script, which
blames the guest's copy. If `AddTargetData::caster` is that blame actor (not read), the host's `HookAddTarget` drops an effect on the host's own actor whose caster is a copy
(the copy's owner sent its own request), so the bolt's damage lands nowhere. Filed in progress.md, loose ends.

Measured (2026-11-24, scenario `storm-call-bolts-reach-guest`, counters `projectile E4CB5` and
`projectile E4CB5 relaunched`): before, the host's shout struck 3 bolts and the guest showed 0; the guest's struck 3
and the host showed 0. After: the host struck 4 and the guest relaunched 4; the guest struck 3 and the host relaunched
3. With every storm stopped before the count (`MAGProjectileStormVar` 0), the match is exact: host 3 own, guest 3
relaunched; then host 4 own / guest 4 relaunched and guest 3 own / host 3 relaunched.

The guest's own storm (its Self effect) places its activator through the host (`PlaceAtMe` is the host's,
[summons.md](summons.md)) and casts from its copy of it, so the bolt is the guest's launch.
