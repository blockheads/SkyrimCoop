# Projectiles: who shoots, and what lands

How a spell's projectile is launched, by an actor or by an object, and what applies its spell when it hits. Read in IDA
(1.7.104) for bug 18 (2026-11-24): Storm Call's lightning showed only on the game that cast it.

## Storm Call (records, Skyrim.esm)

- `StormCallShout` 7097D: words 6029A/B/C, spells `VoiceStormCall1/2/3` (18609, 1860A, 1860D), self-delivered.
- Each spell: `VoiceStormCallEffectN` (area 100: the actors around, by its conditions), `VoiceStormCallEffectNSelf` (on
  the caster: `magicSetWeatherScript` and the same storm script) and an empty AI effect, 60 s.
- Both run `magicProjectileStormScript` (vanilla source, `Data/Source/Scripts`): `OnEffectStart` places an activator
  (`PlaceAtMe`) above the target, then every 1-3 s moves it and calls `SpellRef.RemoteCast(activator, caster, target)`,
  `StormCallLightningBolt01` (E4CB7: `ShockDamageBoltStormAimed` E4CB6, projectile `ShockBoltAimStorm` E4CB5, a beam;
  its explosion `StormCallImpactSoundExplosion` EAFE3 is a sound, no damage or enchantment). On the caster itself it
  misses on purpose (`bDontHitCaster`): bolts around the shouter.
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
| 43849 | `Explosion` apply to targets | An explosion applies only its own base's damage (HitData) and enchantment, not the spell. |

A projectile's spell lands only through its shooter's caster. An activator's caster blames the blame actor (34698), so
on the host `AddTarget` sees the shouting player as the caster.

## How we sync it

`HookLaunch` (44108) raised `ProjectileLaunchedEvent` for every launch, but `CombatService` sent only a launch whose
shooter is one of this game's actors (`LocalComponent`). A script's activator is not one, so every Storm Call bolt
stayed on the game that ran the script: the NPCs' bolts on the host, the guest's own on the guest. The same goes for
any spell an object casts (a statue, `Spell.Cast` from an activator, the random lightning triggers).

Now `ProjectileLaunchedEvent::FromObject` marks a shooter that is no actor. A spell projectile so marked goes out with
shooter 0 (`ProjectileLaunchRequest`), from whichever game launched it (the host relays it to the others:
`HostAuthority::OnProjectileLaunch`). The others relaunch it from the same origin and angles with no shooter
(`SkyrimBridge::LaunchProjectile`, which calls the engine's launch past our hook): it flies and strikes, and 44169
gives it no caster, so it applies nothing there. The effects arrive as for any other spell: the launching game's
`AddTarget` on a copy goes to the target's owner. Projectiles without a spell (an arrow trap's) are not sent: what a
weapon projectile with no shooter does on a hit was not read.

Measured (2026-11-24, scenario `storm-call-bolts-reach-guest`, counters `projectile E4CB5` and
`projectile E4CB5 relaunched`): before, the host's shout struck 3 bolts and the guest showed 0; the guest's struck 3
and the host showed 0. After: the host struck 4 and the guest relaunched 4; the guest struck 3 and the host relaunched
3 (the host meanwhile struck 9 more for the NPCs both storms hit; the guest relaunched 10).

The guest's own storm (its Self effect) places its activator through the host (`PlaceAtMe` is the host's,
[summons.md](summons.md)) and casts from its copy of it, so the bolt is the guest's launch.
