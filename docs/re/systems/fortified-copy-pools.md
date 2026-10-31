# Fortified pools on copies

On 2026-10-30, `dialogue-question-answered-by-who-can` captured Onmund with magicka 205 on the host,
175 on the guest's copy, and permanent magicka 175 on both. The host had an active magicka effect
of magnitude 30; the copy's magic effects were suppressed. Its outfit matched. The dialogue menu
itself timed out, so that run is evidence for the pool difference, not a dialogue acceptance pass.
Native evidence: `build/coop-test/results/20261030-133538-9a38b3/at-failure.json` in `v1-copy-desync`.

`ActorValueOwner::GetPermanentValue` excludes TEMPORARY. A DAMAGE restore can heal only to the
permanent value plus the temporary modifier. Sending permanent and current alone loses the
owner's fortification, including when its current value is below the unfortified maximum.
The fake previously applied spawn current values directly, hiding this native limit. It now
uses the native permanent/TEMPORARY/DAMAGE operations. The `[fortified-pools]` GameTests flow
then failed with exactly 175 versus 205 (`build/magicka-red.log`).

Snapshots and current-value updates now carry temporary pool modifiers separately. Copies
restore permanent, then the exact temporary modifier delta, then current through DAMAGE.
Zero removes a former effect; local effects cannot stack onto the owner's modifier. Continuous
updates detect modifier changes even when the current value did not change. Other actor values
retain their existing base-value policy.

The typed query is CommonLibSSE-NG `Actor::GetActorValueModifier`, Address Library 38469
(RVA `0x6B29C0`), with actor in RCX, modifier in EDX and actor-value index in R8D. The native
readout was checked against that implementation before introducing the production query.

Unit tests `[actor-pools]` failed 10 assertions before the modifier restore and pass all 18 after.
They cover damaged fortified pools, effect removal, repeated snapshots and negative modifiers.
Service flows additionally cover live spending/recovery, removal, unchanged current with a changed modifier,
and local copy writes (31 assertions). The
wire fixtures intentionally include the additional temporary-modifier map. Full `scripts/check.sh`
passes (590 cases, 6,226 assertions), including the existing race-change skill restoration flow after
fixing the fake to reset base values alongside its current values. Combined Skyrim acceptance
remains pending.

Native acceptance readout (2026-10-30): both Onmund actors report base/permanent 175,
TEMPORARY 30, DAMAGE 0, current 205. The overall scenario failed on the separately corrected
ordinary-NPC identity overlay, retained in `build/coop-test/npc-identity-native.log`.

After narrowing the name overlay, `enchanted-npc-pools-match-owner` passes natively with the
same base identity and the full actor checksum. The next scenario reproduces Lucan's independent
armor failure. Evidence: `build/coop-test/armor-native-red.log` (3.1 s Onmund PASS).


The offline cache audit adds [fortified-record]: a guest gains the same +30 after joining, then another guest joins. Existing copies show205 but the later copy showed175 because HostCharacters kept current values without keeping TemporaryValues. That whole-game flow fails two assertions; the host unit fails one and verifies that another game cannot write the owner's modifier. OnValues now retains pool modifiers in the owner's player record beside the current values. The red/green evidence is in build/fortified-record-{red,unit-red,check}.log.

The completed 104-case native batch passes the dedicated Onmund 205 check and the original dialogue-question-answered-by-who-can. The pool mismatch is closed; unrelated combined failures remain reported. Latest gate and full combined results: [batch report](../../reports/2026-10-30-copy-desync.md).
