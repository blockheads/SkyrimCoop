# Party enemy targeting, 2026-11-02

Feature: `feature/party-aggro`, originally based on `dev` at `f91f5983`; reviewed with committed dev `1fa0233b` integrated for the user-authorized merge.

## Reproduction before the change

Steam 1.7.104, two real games, isolated prefixes. Three scenarios drove actual Firebolt attacks against a host-owned wolf with enough health to survive.

- Host first: one host hit, four guest hits, all health decreases verified. The enemy kept targeting the host even with the guest closer.
- Guest first with both visible: the enemy already preferred the host before the first guest hit, then never changed after greater guest damage.
- Guest first with the host initially invisible: the enemy selected the guest. Revealing the host and landing four host hits still did not change its target.

The read-only native candidate diagnostic showed the guest in the combat group's known targets. The fault was initial-target stickiness, with the visible host usually picked first, rather than the guest being unable to enter combat.

Evidence retained privately in `build/aggro-evidence/`: `native-baseline.log`, `native-candidates.log`, the corresponding result JSON files, and `baseline-captures/*.json` with native controller/actor/Papyrus targets, candidate detection and health.

## Fast regression and implementation

Before implementation, both GameTests attack-order flows failed at retargeting. Four initial pure cases failed as well. With the rule, both real-service combat flows pass and the guest's replicated gameplay AI state sees the chosen target. Added lifecycle coverage for death, combat ending, guest departure and stopping hosting. Existing Steam hosting-to-guest transitions caught a dynamic UpdateEvent listener lifetime error in the first draft; the host service now updates its aggro role directly, keeping the dispatcher listener stable.

Final fast gate: `scripts/check.sh` passes, 200 ClientTests / 198 HostAuthorityTests / 32 TPTests / 286 GameTests, plus harness/runtime/comparison checks and UI fixtures. The Steam 1.7.104 DLL and launcher build.

Hooks `38606` and `47191` were checked against actual vtables on both executable versions. Primary disassembly and TrueHUD's health-damage virtual signature establish applied damage and the selector's caller-provided output buffer. The 1.7.104 engine reference records the bindings; private IDA evidence is retained with the reproduction logs. See [design and bindings](../docs/design/party-aggro.md).

## Native acceptance

Final Steam 1.7.104 acceptance: **6/6 pass**, `build/aggro-evidence/native-collision104.log` and `collision104-results.json`.

- `party-aggro-host-first`: the guest draws aggro with greater applied damage, including from behind the enemy.
- `party-aggro-guest-first`: the host draws aggro with greater damage.
- `party-aggro-guest-visible-control`: the initially hidden host is revealed and can draw aggro from the guest.
- All three verify the guest's target and 300 frames of target stability after switching.
- `copy-in-owners-combat`: strengthened to check the persistent copied combat target.
- `guest-player-in-hosts-combat`: guest combat status follows the host's chosen target.
- `cast-host-player`: casting and stopping held Flames still synchronize.

Two further native facts were reproduced, taught to the fake, and fixed after failing smoke tests: copies lose the target during native search-state updates; a damaging attacker behind an enemy can remain LOST with cached LOS false. The final code restores the owner's weak handle on copies and uses a physical visibility ray for recent attackers rejected by native awareness. Unit cases cover hidden players, walls, decay, hold timing, switching margins and native reevaluation.

The pair was stopped, its private prefixes verified empty, and the duo released immediately after acceptance. The user cancelled the queued 1.6 compatibility run: routine builds/tests now target 1.7 only. The implementation run did not merge or push; the later user-authorized review and merge is recorded below.

## Review and merge acceptance

Review fixed two issues before merging: a valid live target could roll back if selection ran again before its host snapshot updated, and a copied actor could expose its previous package after the owner cleared it. Both leave failing whole-game and pure regressions that pass after the fixes. The short native diagnostic did not repeat the target flip, but did reproduce the stale package in the cleanup oracle. Temporary native tracing was removed.

Cleanup removes duplicate threat-score and native target reads, inactive snapshot allocation/lookup and duplicate physics-world reads. Weights and timing remain unchanged.

The combined `scripts/check.sh` passes: **202 client / 199 host / 33 encoding / 290 game cases**, harness/runtime checks, UI fixtures, primary DLL and launcher. Final native **6/6 PASS** with current committed dev and the review fixes: `build/aggro-evidence/native-review-final104.log`, `review-final104-results.json`. The scenario runner deduplicates repeated names, so this is six distinct cases. Own games stopped and both private prefixes verified empty before offline work. Only Steam 1.7.104 was built and exercised for review.

The merge preserves the main checkout's unrelated repro/profiling work without including it in the aggro commit.
