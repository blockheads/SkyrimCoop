# S9 guest dialogue exit verification

Request: determine whether recent dialogue changes fixed a guest being unable to leave until the host joins
and leaves. Target committed dev `454fb752`; its dialogue code matches the prior native reproduction baseline.
No newer committed dialogue changes are present in this checkout. Tests run in `.worktrees/s3-guest-arrest`.

**Result: S9 was not reproduced in the covered paths.** Five rounds of each scenario pass (10 native runs,
30 exit checks). Every exit clears both engines without the host joining. This verifies working exits on the
current Linux/Proton build; it does not establish a before/after fix of the original unknown trigger.

## Expected behavior

[Shared dialogue](../docs/design/shared-dialogue.md#what-players-see) says Tab/Escape leave independently;
the last participant leaving ends the host's background conversation. It explicitly says walking away does
not end a conversation (no engine distance rule). Movement input and a failed Tab exit are separate observations.
The verification does not change that contract or force-enable movement while dialogue is open.

## Coverage

- Existing `dialogue-guest-exits-alone`: guest starts Lydia dialogue with the host out, Tab from its list,
  Escape from its list, and Tab while the host answers. Each exit clears both speakers, restores gameplay
  input, and permits the next guest start without host intervention.
- New `dialogue-guest-s9-exits`: uses actual placed Wylandriah `19DF0` (ESM `WylandriahRef`, base `19DEF`), the
  NPC in the S9 candidate at 15:09:04. She is staged in QASmoke and restrained as fixture setup. The guest
  starts and exits by Tab, uses Tab around its greeting, then sends real Back input with dialogue open.
  Captures include player position, menu list, native controls and SWF state. Tab then must close it;
  real Back input after closing must move the guest at least 25 units. The host never joins.

Five bounded rounds were run. Each game is driven through native input dispatch; no dialogue-pick RPC,
synthetic engine close or control-enabling console command substitutes for the tested exits.
Python compile, discovery and the 30 existing harness unit tests pass. Native results follow.

## Interpretation

A passing current-version scenario means **S9 was not reproduced in those covered paths**. There is no captured
original failing scenario to demonstrate a before/after fix, so it cannot establish that all S9 variants are fixed
or identify which commit fixed the reported incident. The separately confirmed refused-choice blocked list
remains open ([report](2026-11-01-dialogue-reproduction.md)); its GameTests regression is intentionally failing,
and is distinct from a proven inability to leave with Tab.

## Native results (17:18–17:21)

| Scenario | Rounds | Result |
| --- | --- | --- |
| `dialogue-guest-exits-alone` | 5 | PASS; 14.7–15.2 s per round |
| `dialogue-guest-s9-exits` | 5 | PASS; 17.6–18.3 s per round |

The full batch took 165.8 s, apart from fresh duo startup/stop. Independent capture review confirms 30 exit
snapshots with both speakers cleared, and no host dialogue menu opened in any captured phase. The repeated
starts succeed, so a previous guest exit does not leave the next conversation dependent on host intervention.

With its dialogue menu open, real Back input moves the guest 161.1–164.2 game units. The dialogue list stays open
(state 1); walking that distance does not close it. After Tab, contexts return to gameplay (`[0]`), the native
controls mask returns to `4294967295`, and a second Back press moves the guest another 32.1–42.3 units. This is
working movement and a working Tab exit, consistent with the contract's absence of a walk-away exit rule.
It is not evidence of the reported unresponsive/stuck state.

The early Tab probe starts in SWF state 3 (the list's opening transition) in all five captured runs, then completes
or skips the opening and exits. This does not cover every possible frame of `SHOW_GREETING` or every quest menu.
Wylandriah was staged in QASmoke; the original Riften scene and Windows guest were not recreated exactly.

DLL SHA-256: `5ff6854cbae944c56a21cbfc3f0f96d7352ad92b991b41ab9ec4342300267d32`.
The deployment hash and audio-off routing were verified. Both own prefixes were verified empty after stopping,
and the duo was released at 17:21 before offline review. Other agents' games were not used or stopped.

Evidence: `build/s9-verification.log`, `build/s9-verification-evidence/`, and the five timestamped pairs under
`build/coop-test/dialogue-repro/20261101-171838-guest-exits/` through
`20261101-172105-s9-wylandriah-exits/`. Each movement capture records initial/final position, menus, controls and
both dialogue states. To rerun, first claim the shared duo, then:

```bash
python3 scripts/coop_test.py scenario dialogue-guest-exits-alone dialogue-guest-s9-exits --until-fail 5
```

## Other dialogue bugs remain

The separately confirmed refusal bug is still unfixed on this same committed production code: selecting another
player's named option gives the refusal notice but leaves the chooser's list hidden. Its existing GameTests
flow was rerun offline: 44 assertions, 40 pass and 4 fail (host/guest `ListShown` and `PickedText.empty()`).
Evidence: `build/s9-other-dialogue-status.log`. Those intentional regression failures are unrelated to S9's
30 passing exits, and the full project gate is not claimed green. No production fix or merge was made.
