# Enemy targeting between party players

The host chooses enemy targets. The guest's NPC copies receive the resulting AI state; they do not run a second threat calculation.

## Selection

After a party player damages an NPC, keep a damage history for that fight. Applied health damage counts after resistance, including host attacks, forwarded guest hits and spells. Damage threat halves every eight seconds. Candidates must already be members of the NPC's native combat group and pass native target validation. Dead, unloaded, invisible and undetected sneaking players cannot be selected. Native detection or known current-target memory normally establishes awareness. A recent damaging attacker can also qualify through a clear visibility path, even when Skyrim marks that attacker lost.

Each eligible party player scores `20 / (1 + distance / 512) + 4 * recent damage`. Distance is in Skyrim units. Keep the current target on ties; a challenger needs more than 20% extra score. Keep a valid target for at least 1.5 seconds after a change. An invalid current target does not impose that hold. These rules have no host/guest preference.

The rule runs only after damage in a party fight, when Skyrim's standard selector already selects a party player. It leaves other NPC targets, scripted scenes, custom selectors and attacks already in progress to Skyrim. The native controller applies the returned target and the existing AI-state path sends it to guests. Compare against the controller's actual current target, even when the periodic native selector proposes a different one. If AI jobs select again before the main-thread snapshot catches up, keep a valid live target rather than rolling back to the native proposal. We do not stop/restart combat or write the actor's target directly.

Damage history is removed when the victim dies, disappears or leaves combat, or the attacker leaves the party. A three-second grace period allows the first damage callback to precede native combat startup. Ending or reloading the host session clears the published policy.

## Crime fights

An NPC fighting a player because of a party crime is a party fight too, with no damage needed (`party-model.md`, Crime):
the same score, the first pick made at once, and any player it sees eligible (`crime.md`). "Because of a party crime":
its faction is the party's enemy, a crime against it, kAngryWithPlayer, or it is on a party crime's witness list (a
guard answering the alarm); `CrimeRules::PartyIsCriminalTo`, `crime.md`, "Who an NPC fights for a party crime".

## Evidence and engine bindings

The original 1.7.104 game kept targeting the host after four guest Firebolts, despite verified health damage and the guest entering its native candidate list. With the host invisible before spawning the enemy, Skyrim selected the guest, then kept that target after the host was revealed and attacked four times. This isolates initial-target stickiness rather than guest ineligibility.

Community sources checked before RE:

- [SmartTargetingNPC](https://github.com/RedyellowUnit/SmartTargetingNPC), MIT, demonstrates damage/proximity threat over the native combat group. Our party-only rule uses the native selector instead of writing the actor's target.
- [Aggro Management in Skyrim](https://www.nexusmods.com/skyrimspecialedition/mods/178565?tab=description), the mod's description of threat factors.
- [TrueHUD](https://github.com/ersh1/TrueHUD), `Hooks.h`/`Hooks.cpp`: Actor virtual slot `0x104` takes `(Actor*, Actor* attacker, float healthDelta)`; damage is a negative delta.
- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG), combat group, target selector and actor layouts, checked against native code.

The Steam 1.7.104 bindings are:

| Address Library ID | Binding | 1.7.104 RVA |
| --- | --- | --- |
| 38606 | Actor::HandleHealthDamage, virtual `0x104` shared by Actor/Character/PlayerCharacter | `0x6CC3D0` |
| 47191 | CombatTargetSelectorStandard::SelectTarget, virtual 6 | `0x862040` |

`47191` has an explicit caller-provided output handle in RDX: `uint32_t*(selector*, uint32_t*)`. Treating its result as an ordinary 32-bit return value corrupts the call. Its cached handle is `CombatTargetSelector::hTarget`, offset `0x18`. `47196` checks native target validity, `20203` reads the attacker's encounter zone. The existing controller target setter `33235` remains responsible for target changes and AI-state events.

Copy the combat-group targets under its read lock, then release it before resolving handles or calling native validity checks. Publish immutable state to combat jobs. Only the main-thread host service updates the roster and damage history.

## Recovering a visible attacker behind the enemy

The host-first scenario also showed a guest behind the wolf landing four Firebolts while remaining `LOST`, with detection zero and stealth points 50 despite no crouch or invisibility. Native `HasLineOfSight` (53829) reads cached detection state and was false too. It is insufficient to establish a clear physical path behind an actor.

When native awareness rejects a recent attacker or the current target, check the existing Havok ray helper between native look points (`GetLookingAtLocation`, virtual 0x5B). Use the NPC's cell's physics world, require both actors to share it, and pass through the NPC's own collider. A wall or missing physics result rejects recovery. Ordinary native selection avoids this extra ray. The helper's default player-cell behavior remains available for join placement.

## Keeping targets on copies

The first feature run switched the host's enemy correctly in all three cases, and host logs showed target session IDs being sent. Guests still read target zero. `Actor::UpdateCombatSearchState` (37273, RVA `0x675600`) clears `Actor::combatHandle` every update when native `IsInCombat` is false. That is expected for copies without a combat controller, but it undid the handle written by `SetCopyAiFields`.

Cache the owner's weak handle atomically in `ActorExtension` when applying AI state. After the native search-state update, restore only that handle on copies. Preserve the original return register, searching flags and combat-state allowlist; no combat controller or AI is started. The weak handle expires normally if its target disappears. Releasing a copy clears its cached handle. Existing condition, Papyrus and diagnostic readers then see the same target. No new Papyrus/condition hooks are needed.

The fake now performs the same native clear on every copy update. Both attack-order flows failed their sustained guest-target assertion with that engine fact, then passed with the cached-handle restoration.

## Regression coverage

`party-aggro-host-first`, `party-aggro-guest-first` and `party-aggro-guest-visible-control` drive real Firebolt hits and check the host's native target plus the guest's replicated gameplay AI state. `combat_targets` is a test-build-only readout of the controller, actor and native candidate list. A guest copy has no combat controller; its restored actor/Papyrus target and `ai_state.combat_target` must match the host's decision.

GameTests runs real combat services and AI replication with a fake native selector that preserves the first target unless the shared selection policy changes it. Both attack orders failed before the rule, then passed. Lifecycle flows cover death, combat ending, leaving and stopping hosting. Pure tests cover decay, proximity, hysteresis, hold timing, invalid candidates and preservation of non-party targets.

## Validation target

Normal development and validation target Steam 1.7.104 only. The existing optional 1.6 configuration remains, but compatibility builds/tests and legacy RE work require an explicit request.

## Review fixes

Review adds a two-pass fake selector to cover AI selection before the next host snapshot. Its failing attack-order flows exposed a rollback path in `Resolve`; a valid live target now stays until the published snapshot catches up. Invalid targets and non-party native proposals still pass through.

The native cleanup oracle also exposed an old package after the host player's package became empty. `GetCurrentPackage` (37399, RVA `0x67F680`) can read `ExtraPackage` under a do-nothing placeholder. A copy now answers none when its owner's package is explicitly zero; named packages still come from the actual native slot. The cached owner package ID is atomic for engine readers. A native reproduction, failing whole-game flow and failing pure test precede the fix.

Cleanup caches candidate scores, reads each tracked NPC's combat target once per host update, avoids inactive native snapshot allocation/lookup, and avoids duplicate physics-world reads. No selection weights or timing constants changed.
