# Dialogue reproduction follow-up

User request: investigate concrete dialogue bugs, including mouse/highlight/click races, before broad cleanup.
Baseline: committed dev `454fb752`, with the S3 reproduction additions at `a6ca4796`; isolated worktree
`.worktrees/s3-guest-arrest`. This task changes reproduction coverage and documentation only; no production fix or merge.

## Result: one confirmed blocked-list bug

| Case | Input and assertion | Status |
| --- | --- | --- |
| `dialogue-guest-picks-at-greeting-end` | Existing early-press reproduction without the guest voice-end wait; keep actual SWF readiness check | PASS 20/20; accepted picks verified in logs |
| `dialogue-refused-pick-restores-list` | Nord host selects Onmund's guest-only answer; local list returns without the guest answering | Native FAIL; GameTests FAIL for both host and guest recovery |
| `dialogue-guest-exits-alone` | Guest opens Lydia with the host 800 units away; Tab, Escape, then Tab during an answer; both engines clear and guest can talk again | PASS, 14.4 s |
| `dialogue-mouse-highlight-stays-shared` | Guest hovers several visible Lucan rows; both menus agree; host moves highlight while guest mouse is stationary; stays stable for 60 frames | PASS, 17.9 s |
| `dialogue-fast-mouse-pick` | Guest sweeps multiple rows without waiting for highlight convergence, clicks; actual host topic must match; INFO and logs captured | PASS, corrected fixture, 10.5 s |
| `dialogue-picked-text-visual` | Original two-claw-line flow; both screens and SWF selected-copy state captured immediately after each pick | Both picks captured; no overlap observed; whole flow FAIL later on inventory assertion |

The last case is evidence capture for manual visual review, not an automated assertion that overlap is absent.
The hover sweep uses normal command-socket input dispatch; it does not guarantee two events in the same frame
or exhaust all timing interleavings. No injected pick/highlight RPC or production SWF mutation is used.

## Refusal failure confirmed in Skyrim

The menu's `onSelectionClick` first changes to `TOPIC_CLICKED`, plays its pick animation and displays the chosen
text, then calls `TopicClicked`. This ordering is in the local Skyrim UI source
`reference/plugins/skyrimui/src/dialoguemenu/DialogueMenu.as` and reflected in `FakeDialogue::PlayerPicks`.
`DialogueService::OnPickAsked` refuses another player's named option locally and returns true after its chat
notice; it does not restore the list in that branch. The existing Onmund scenario checks the refusal and then
has the guest answer, which can hide a menu stuck between those steps. The new case asserts recovery first.
The native result below confirms the stuck list. It does not establish S9's inability to exit, S3's freeze or a race.

## Existing recorded leads

- [S9](2026-10-31-fabien.md#s9-a-guest-gets-stuck-in-dialogue-until-the-host-joins-and-leaves-it): guest could not
  leave until the host joined and left. No contemporaneous bug bundle establishes its exact state.
- [Dialogue bugs](../docs/bugs.md#dialogue): a lost press at the copy's greeting end was previously reproduced;
  current canonical selection waits past that window. Overlapping picked text in the claw flow was reported
  but not captured. These historical observations are not new successful reproductions in this task.

## Mouse fixture correction

The earlier S3 fixture used `Entry0` for option zero. Skyrim recenters and reuses row clips; `Entry0` may be
invisible while option zero occupies `Entry4`. That screenshot showed the pointer above the options: its selected
fine text and actual fine callback are valid, but its intended hover path was not validated. The new helper
selects a visible clip by `itemIndex`, and records each slot, item index, text, frame and mouse bounds. S3's
mouse variants use this corrected helper too. The helper passed the Lucan mouse tests below. The original Rift arrest variants remain pending.

## Validation and coordination

Python compile, scenario discovery and 30 existing harness unit tests pass. Native results follow below.
The native runner verified a saved claim, queue order and the deployed DLL hash, kept audio off the physical
ALSA device, and stopped/released its own pair before each offline investigation or GameTests run. No other
agent's games were used, stopped or reloaded.

Focused native commands after a saved duo claim:

```bash
python3 scripts/coop_test.py scenario dialogue-refused-pick-restores-list --until-fail 1
python3 scripts/coop_test.py scenario dialogue-guest-exits-alone dialogue-mouse-highlight-stays-shared dialogue-fast-mouse-pick dialogue-picked-text-visual --fail-fast
python3 scripts/coop_test.py scenario dialogue-guest-picks-at-greeting-end --until-fail 20
```

Stop and release own games before inspecting a failure offline or adding its GameTests flow. The repetition
above is the existing documented early-press probe; scenarios themselves do not retry failed input.

## Native result (13:04–13:05)

The duo was observed free at 13:04, claimed in queue order, then the committed baseline DLL was atomically deployed
and hash-verified. Audio-off verification passed. `dialogue-refused-pick-restores-list` failed as intended at
“a refused choice restores the choosing player's list without the guest answering”, after a five-second timeout.
The first fail-fast batch stopped there; later batches exercised the other cases below.

Trigger: a Nord host and Breton guest hear Onmund's question. Both menus show the guest-only answer `66963`
(INFO `6696E`, rendered `... [Prisoner]`). The host selects it. Its local log reads
`option 0 (topic 66963) is another player's: not sent`; its chat explains the refusal. No answer is said.

| State after refusal | Host (chooser) | Guest |
| --- | --- | --- |
| Dialogue menu open | yes | yes |
| SWF state | 2, `TOPIC_CLICKED` | 1, `TOPIC_LIST_SHOWN` |
| `bAllowProgress` | 0 | 1 |
| Picked-text copy visible | yes | no |
| List restores without another answer | no, timed out | remains usable |

This is a **confirmed blocked option list**, not a frozen game or proven inability to exit: frame counters
advance and normal Tab cleanup closes both engines' conversations. The original S3 and S9 links remain unproved.
No production code changed. A regression was added to the existing GameTests party-options flow for both host and guest refusal paths;
the fake already models the native click hiding the list before the callback.

Evidence: `build/dialogue-native.log`, `build/dialogue-evidence/`,
`build/coop-test/dialogue-repro/20261101-130510-refused-pick/after-refused-pick.json` and the matching host/guest PNGs.
The later `failure.json` is after the wrapped canonical scenario's cleanup; use the immediate refusal capture
for the blocked UI state. Future runs capture `still-blocked` before cleanup too.
Own games stopped and prefix processes verified empty; duo released at 13:05 before offline work.

## GameTests result

After the native failure, games were stopped and the existing party-options flow was extended to require the
chooser's list to remain usable and its picked-text copy to clear after refusal. No fake-engine or production
changes were required. This is intentionally **RED**, ready for the fix's next tiers:

```bash
scripts/xmake-linux.sh build -j8 GameTests
build/linux/x86_64/debug/GameTests '[dialogue]'
```

Result: 15 cases, 14 passed and 1 failed; 136 assertions, 132 passed and 4 failed. Both host and guest refusal
paths fail `ListShown` and `PickedText.empty()`; refusal notices, unchanged world dialogue and the other menu
remaining usable pass. Evidence: `build/dialogue-gametests-red.log`. Full `scripts/check.sh` is not claimed green;
this reproduction branch contains the deliberately failing regression and has not been merged.

## Exit and mouse controls (13:09–13:11)

`dialogue-guest-exits-alone` passes all three exits without host intervention. Corrected visible-clip mouse
hovers pass, with matching rendered text on both menus and a stable shared highlight. Quick mouse selection
also reaches the requested host topic. Evidence: `build/dialogue-attempt2.log`,
`build/dialogue-attempt2-evidence/`, and `build/coop-test/dialogue-repro/20261101-130956-guest-exits/`,
`20261101-131010-mouse-highlight/`, `20261101-131028-mouse-pick/`.

The first quick-pick fixture ended on “Do you sell spells?”, opening `BarterMenu`, which its dialogue cleanup
left open. The following claw flow then waited behind that menu and could not exit through the harness's
greeting guard. **This was fixture contamination, not a second confirmed dialogue bug or S9 reproduction.**
The corrected fixture finishes on the dialogue-only first option and requires both players to take gameplay
input after cleanup. Its fresh native run passes (10.5 s) in `build/dialogue-attempt3.log`.

## Picked-text captures (fresh run, 13:14–13:15)

The fresh claw flow reaches both picks, including the guest-only claw line `5D46F / 5D471`. Both screenshots
show a single picked line at the captured moment; duplicated overlap was not reproduced. The host's picked-text
local Y is 6.25 versus the guest's -5.75 (12 local units of alignment difference), retained as visual evidence,
not promoted to a separate automated regression here.

The complete canonical flow subsequently fails its inventory assertion: the claw remains in the guest's bag
for 20 seconds after the host says the chosen answer. That separate inventory/quest outcome is not investigated
or counted as a passing dialogue flow. Evidence: `build/dialogue-attempt3.log`,
`build/dialogue-attempt3-evidence/`, `build/coop-test/dialogue-repro/20261101-131421-claw-picked-text/`.
Own games stopped, prefix processes verified empty, duo released at 13:15.

## Greeting timing probe (13:16–13:19)

Twenty complete rounds pass with the guest voice-end wait removed and actual SWF readiness retained. Independent
log verification finds **20 guest option-zero callbacks and 20 host accepted picks** of `B0EE6 / B0EE9` in
13:16:58–13:19:44. The earlier greeting-end lost-press race did not recur on this baseline under this probe.
This bounded negative result does not exclude narrower same-frame races or other dialogue paths.

Evidence: `build/dialogue-greeting.log`, `build/dialogue-greeting-evidence/`, and the twenty timestamped
`build/coop-test/dialogue-repro/*-greeting-end/after-early-press.json` captures. Own games stopped, prefixes
verified empty, duo released at 13:19; main S1 claimed it afterward. No other agent's games were touched.

## Later S9 verification

[Focused S9 verification](2026-11-01-s9-verification.md) adds the actual Wylandriah `19DF0` candidate and real
movement input, then repeats both NPC exit scenarios five times (10 runs, 30 exits): all pass without the host
joining. Walking with the menu open moves the guest 161–164 units but keeps dialogue open as specified; Tab
closes it and locomotion remains normal. The refused-choice regression is still red. S9 is marked not reproduced
in these paths, not conclusively fixed across the original scene and platforms.
