# Dialogue fixes and expanded regression coverage

Implemented in `.worktrees/s3-guest-arrest`, branch `repro/s3-guest-arrest`, based on dev `454fb752`.
This batch is not merged into dev. Native verification below ran on Linux/Wine; Windows is unverified.

## Changes

| Approved improvement | Coverage |
| --- | --- |
| Reliable pick completion | The scenario checks the accepted topic and INFO, then usable matching lists or both menus and speakers fully closed. Nine predicate cases reject hidden, transitioning, blocked, wrong-INFO and partially closed states. |
| Stricter dialogue fake | Hidden or blocked lists and personal menus reject picks; hidden highlights are unreadable; unchanged lists retain selection; reopening preserved dialogue can show a list with input disabled. |
| Competing picks and double-clicks | Real simultaneous mouse input and double-click scenarios; GameTests verifies one accepted line, matching picked text, recovery with an unchanged next list, and the next legitimate pick. |
| Close and immediate reopen | Guest switches Lydia → Amren → Lydia after local Tab close. GameTests injects old list, pick, highlight and close notifications after a new conversation opens. |
| Mouse selection by INFO/owner | Real Lucan rows sharing a topic but carrying different INFOs; another player's line is refused, input recovers, and the guest's actual INFO is accepted and said. |
| Return from personal menus | Host/guest make two real purchases without restarting dialogue. Training reopens from returned dialogue without a second purchase or charge, then exits normally. |

No hook signatures changed. The production bridge's list refresh has an explicit resume mode used after barter;
it is not a harness interface. No new wire messages or authority rules were needed.

## Three native failures fixed

1. **Locally refused choice leaves the list hidden.** The prior Onmund reproduction put the chooser in
   `TOPIC_CLICKED` with progress disabled. The service now requests a list refresh and preserves/publishes its
   highlight; the bridge refills an unchanged list when it is hidden. Both host and guest recover and can say
   their next legitimate option in GameTests. The native Onmund flow passes without another player answering
   to rescue the chooser.
2. **A losing simultaneous choice keeps displaying the wrong text.** With 150 ms UDP loopback delay, the host
   selects `93808 / 93809` ("Are you always drunk?") while the guest selects `6001F / 60022` (the coin).
   The host accepts one choice, but the guest's `TextCopy_mc` retains the rejected coin text. An already-clicked
   menu now replaces its text with the accepted choice without replaying the animation. Three delayed rounds
   pass with independently recorded distinct local callbacks, matching accepted text, one accepted action,
   the correct cost and both usable lists restored. These rounds all accepted the host's non-coin line; the
   separate double-click case verifies exactly one coin is spent.
3. **Guest barter return can leave a visible list permanently blocked.** Guest-only fresh barter passed, but
   host barter followed by guest barter reproduced the failure: state 1, matching options and speaker, progress
   0 beyond 15 seconds. The fake now models a preserved menu reopening without input readiness. Barter resume
   invalidates the shown step and explicitly refills/re-arms native voice readiness after the menu opens. Two
   host→guest rounds pass: four scenarios and eight purchases, with second choices and normal exits.

Each fix followed native failure → GameTests failure → unit failure → fix → passing GameTests and full check.
The refusal failure was established in the [earlier reproduction](2026-11-01-dialogue-reproduction.md).

| Failure | Native evidence | Fast RED evidence |
| --- | --- | --- |
| Refusal | `build/dialogue-native.log`; `dialogue-repro/20261101-130510-refused-pick/after-refused-pick.json` | `build/dialogue-gametests-red.log`, `build/dialogue-policy-red.log` |
| Competing text | `build/dialogue-competing-delay-red.log`; `dialogue-repro/20261101-183521-competing-picks/failure.json` | `build/dialogue-winning-games-red.log`, `build/dialogue-winning-unit-red.log` |
| Barter return | `build/dialogue-barter-sequence-red.log`; `dialogue-repro/20261101-185638-barter-return/blocked-return.json` | `build/dialogue-resume-games-red.log`, `build/dialogue-resume-unit-red.log` |

The `dialogue-repro/` paths above are under `build/coop-test/`. Captures include both SWF states and screenshots
before cleanup. Earlier low-latency competing-input attempts did not reach distinct local choices and do not
establish that race; the delayed run did. Temporary delay was removed and verified before releasing the duo.

## Validation

`scripts/check.sh` passes (`build/dialogue-check-final.log`):

- ClientTests: 176 cases, 1,643 assertions.
- HostAuthorityTests: 198 cases, 2,171 assertions.
- TPTests: 32 cases, 334 assertions.
- GameTests: 267 cases, 2,638 assertions, including 18 dialogue cases.
- Harness checks, UI fixtures, Windows plugin and launcher builds pass.
- Final harness review added the open-host-menu negative case, observed it fail, fixed it, and reran all 31
  harness tests successfully (`build/dialogue-completion-review-red.log`, `build/dialogue-harness-final.log`).

Native results:

| Scenario | Result |
| --- | --- |
| `dialogue-competing-picks-recover` | PASS 3/3 with 150 ms UDP delay and distinct host/guest callbacks |
| `dialogue-refused-pick-restores-list` | PASS before and after the resume fix |
| `dialogue-double-click-says-once` | PASS |
| `dialogue-close-immediate-reopen` | PASS |
| `dialogue-same-topic-mouse-picks-info` | PASS, including teardown and checkpoint restoration |
| `dialogue-host-barter-return-accepts-choice` | PASS 2/2 in the final alternating sequence |
| `dialogue-guest-barter-return-accepts-choice` | PASS 2/2 in the final alternating sequence |
| `dialogue-picker-trains` | PASS before and after the resume fix |
| `dialogue-guest-s9-exits` | PASS before and after the resume fix |
| `dialogue-guest-exits-alone` | PASS |
| `dialogue-fast-mouse-pick` | PASS before and after the resume fix |
| `dialogue-pick-reaches-host` | FAIL before any pick: queued guest greeting, described below |

Logs: `build/dialogue-fix-native-green2.log` (three delayed rounds and refusal),
`build/dialogue-fix-native-green3.log` (expanded coverage), and
`build/dialogue-barter-sequence-green.log` (final barter sequence and four controls, all pass).
The first fixed race run passed its action checks but failed checkpoint cleanup; the fixture now waits for an
actual saved-time transition on both games before deleting the save. This is retained in
`build/dialogue-fix-native-green.log`, not counted as a passing complete run.

Final DLL SHA256: `0186e04875edae42f95fe5d59e34eade031b02ca545f8dfbb30233728951f9e0`.
The earlier native fixed-text coverage used `c6a498a003ac04bc10c48d5d2ad8799dfda8cb5eaba6355310e61f62c907b6b5`,
before the scoped barter-resume change. Each deployment was atomic and hash-verified. Own games were stopped,
prefix processes verified empty, and the shared duo released before offline work; final release was 19:06.

## Remaining voice failure and limits

`dialogue-pick-reaches-host` times out waiting for Lydia's guest greeting to finish, both after Onmund and on a
fresh duo. The capture shows subtitle "Hey, slug-breath!", callback 3, sound 162, duration 0 and seconds -1;
the host has already finished. The scenario has not pressed a choice. Evidence:
`build/coop-test/results/20261101-184559-4535af/at-failure.json` and
`build/coop-test/results/20261101-184816-e5c473/at-failure.json`.
This voice-start failure is still open. Its cause and whether it predates these changes are not established;
it is not counted as a native pass for the repaired completion predicate.

The Lucan mouse case checks dialogue selection and recovery, not the separate claw inventory/quest handoff
failure in the earlier report. Screenshots are evidence at captured moments, not an automated guarantee
against every visual overlap. S9 remains not reproduced in the covered exit paths; these results do not prove
the original unknown trigger fixed or connect it to S3.

To reproduce the competing overlap after claiming the duo, use the scenario with 150 ms delay on test-namespace
loopback UDP only (TCP harness traffic must remain undelayed). The test run used a `prio` root on `coop2`'s `lo`,
a `netem delay 150ms` child at `1:3`, and an IPv4 protocol-17 filter to `1:3`. Remove that temporary root qdisc
in cleanup before stopping/releasing own games. No `scripts/net_check.sh` run was used.
