# Actor values: passive regeneration

How health, magicka and stamina regenerate, and why copies must not (1.6.1170, verified in IDA 2026-10-11).

| ID | Name (ours) | What |
|---|---|---|
| 38451 / 38453 / 38452 | `Actor::UpdateHealthRegen` / `UpdateMagickaRegen` / `UpdateStaminaRegen` | Per frame, `(actor, deltaSecs)`. Each checks the process and `38461(actor, av)`, then calls 38459 with its actor value (24 / 25 / 26). |
| 38459 | `Actor::RegenerateActorValue` | `(actor, av, deltaSecs)`. Gets the rate from 38460; if the value is damaged, restores `rate * deltaSecs` into the damage modifier (`ActorValueOwner` vfunc +0x30). **We hook it** (`HookRegenerateActorValue`, `Code/client/Games/Skyrim/Actor.cpp`). |
| 38460 | `Actor::GetActorValueRegenRate` | `(actor, av)` → per-second amount: `HealRate`/`MagickaRate`/`StaminaRate` percent of the maximum, times the combat multipliers (`CombatHealthRegenMultiply`, `fCombatMagickaRegenRateMult`, ...), perk entries, and the `...RateMult` value. Also called when an attack's stamina cost empties stamina (38603). |

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
