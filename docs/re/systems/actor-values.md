# Actor values: passive regeneration

How health, magicka and stamina regenerate, and why copies must not (1.6.1170, verified in IDA 2026-10-11).

| ID | Name (ours) | What |
|---|---|---|
| 38451 / 38453 / 38452 | `Actor::UpdateHealthRegen` / `UpdateMagickaRegen` / `UpdateStaminaRegen` | Per frame, `(actor, deltaSecs)`. Each checks the process and `38461(actor, av)`, then calls 38459 with its actor value (24 / 25 / 26). |
| 38459 | `Actor::RegenerateActorValue` | `(actor, av, deltaSecs)`. Gets the rate from 38460; if the value is damaged, restores `rate * deltaSecs` into the damage modifier (`ActorValueOwner` vfunc +0x30). **We hook it** (`HookRegenerateActorValue`, `Code/client/Games/Skyrim/Actor.cpp`). |
| 38460 | `Actor::GetActorValueRegenRate` | `(actor, av)` → per-second amount: `HealRate`/`MagickaRate`/`StaminaRate` percent of the maximum, times the combat multipliers (`CombatHealthRegenMultiply`, `fCombatMagickaRegenRateMult`, ...), perk entries, and the `...RateMult` value. Also called when an attack's stamina cost empties stamina (38603). |

## When a pool may regenerate (1.7.104, read in IDA 2026-11-23)

`Character::UpdateMagicAndRegen` (38785) calls the three updates only while `IsDead(true)` (vfunc +0x4C8) is false.
Stamina (38452) then regenerates only if all of these hold:

| Gate | Where | Set / cleared by |
|---|---|---|
| no regen delay left | `HighProcessData::staminaRegenDelay` (+0x20; health +0x1C, magicka +0x24), counted down only in 38461 | `ActorProcess::SetActorValueRegenDelay` (39536), which keeps a new delay only if longer and caps it at `fStaminaRegenDelayMax` (`fHealth`/`fMagicka...`): from `SendActorValueUpdate` (38470) on any loss (`fDamagedStaminaRegenDelay`), sprinting that empties stamina (38022, a fixed setting), an attack that empties it (38603: `(cost - before) / GetActorValueRegenRate(26)`) |
| "damaged" flag | `AIProcess::cachedValues->flags` 1<<23 (health 1<<21, magicka 1<<22) | edge-triggered by the modifier sinks (38488 through 38470): set when a loss leaves it under max, cleared when a gain reaches max; recomputed from the values by a load (38480, `FinishLoadGame`), a race switch, and the cache reset (`ResetActorValueCaches`, from 38479 and Actor vfunc 37922) |
| not sprinting | `ActorState1` bit 8, copied each update from the graph's `IsSprinting` (38022) | the behavior graph |
| no bow zoom (player) | `PlayerCamera::bowZoomedIn` (+0x161) | the bow zoom |

Rate (38460): `max × StaminaRate% × fCombatStaminaRegenRateMult (in combat) × StaminaRateMult%`. At a rate of 0 the
38603 quotient is +inf, but 39536 caps it (`fStaminaRegenDelayMax`): a zero rate stops regeneration
only while it lasts. `Actor::Resurrect` (37321) restores only health.

The snapshot's `values.stamina_regen` reads all five (delay, damaged, rate, sprinting, bow_zoomed:
`Diagnostics::EngineReads::GetStaminaRegen`), in bug reports and the harness.

## A relayed effect's conditions (1.7.104, read in IDA 2026-11-23)

A magic effect's own conditions (`EffectSetting` +0x190) are checked once, when an effect is added:
`MagicTarget::AddTarget` (34526) → `CheckAddEffect` (34525) → `EffectSetting::ConditionsPass` (11085,
`TESCondition::IsTrue(target, caster)`), with the resistance roll. Survival Mode's override of the trap disease Rattles
relies on it: `TrapDisDamageStaminaRegen` when `Survival_ModeEnabled == 0`, `Survival_TrapDisDamageStaminaRegen` when
`== 1`, each `StaminaRate -2.5` (read from the plugins).

`magichit` (34547) gives 34525 the resistance from the target's own check (vfunc +0x50: spell, effect, source), and
34525 also refuses a hostile effect on an invulnerable target and asks the archetype's `ShouldApply`. Our relay used to
force 34525 true on the owner's game (upstream's `s_autoSucceedEffectCheck`), so a trap's Rattles left a guest with
both effects: rate 0. The force and its hook are gone: the owner's engine decides whether a relayed effect lands, as
for its own casts. Conditions about the caster or the player are judged for the casting player's copy
([conditions.md](conditions.md#a-spell-from-another-players-character-1.7.104-read-in-ida-2026-11-23)).

## Copies

The owner sends every actor value (`ActorValueService::BroadcastActorValues`, health also through `RunHealthUpdates`); copies force health,
magicka and stamina onto the damage modifier. A copy that also regenerated would drift upward between
updates, so `HookRegenerateActorValue` returns at once for remote actors, for all three values.

## History: the 37448 hook

Tilted Online added this hook in 2021 (`c957ecb8`, "Sync passive health regen") at SE `0x140620900`.
The AE port (`8eaca858`, 1.6.317) moved it to `0x140607080`, which is ID **37448**: `Actor::GetSubmergedLevel(zPos, cell)`
(how deep under the water an actor is, 0..1; CommonLibSSE-NG names it). The address-library move in 2022 kept
that ID, and upstream still hooks it. Effects while it was hooked with the wrong signature
`(actor, int, float)`:

- every water-depth query went through our hook with its arguments misread; for a remote actor whose
  `edx` happened to hold 24 the hook returned 0 ("not submerged");
- passive regeneration was never blocked on copies (the health event it raised for local actors is ignored anyway: owners send health through `RunHealthUpdates`).

Found by checking our hooks against CommonLibSSE-NG's names (the `PL`/`CL` columns of
[ENGINE_REFERENCE.md](../ENGINE_REFERENCE.md)): a hook whose community name doesn't match its purpose
is worth a look. The SE → AE history comes from meh321's `skyrimae.relib` (offsets for every AE version).
