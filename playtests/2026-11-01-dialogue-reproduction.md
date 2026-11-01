# Dialogue reproduction follow-up

User request: investigate concrete dialogue bugs, including mouse/highlight/click races, before broad cleanup.
Baseline: committed dev `454fb752`, with the S3 reproduction additions at `a6ca4796`; isolated worktree
`.worktrees/s3-guest-arrest`. No production change or merge is authorized by this reproduction work.

## Leads and native checks

| Case | Input and assertion | Status |
| --- | --- | --- |
| `dialogue-guest-picks-at-greeting-end` | Existing early-press reproduction without the guest voice-end wait; keep actual SWF readiness check | Prepared; native run pending |
| `dialogue-refused-pick-restores-list` | Nord host selects Onmund's guest-only answer; local list returns without the guest answering | Prepared; native run pending |
| `dialogue-guest-exits-alone` | Guest opens Lydia with the host 800 units away; Tab, Escape, then Tab during an answer; both engines clear and guest can talk again | Prepared; native run pending |
| `dialogue-mouse-highlight-stays-shared` | Guest hovers several visible Lucan rows; both menus agree; host moves highlight while guest mouse is stationary; stays stable for 60 frames | Prepared; native run pending |
| `dialogue-fast-mouse-pick` | Guest sweeps multiple rows without waiting for highlight convergence, clicks; actual host topic must match; INFO and logs captured | Prepared; native run pending |
| `dialogue-picked-text-visual` | Original two-claw-line flow; both screens and SWF selected-copy state captured immediately after each pick | Prepared; native run pending |

The last case is evidence capture for manual visual review, not an automated assertion that overlap is absent.
The hover sweep uses normal command-socket input dispatch; it does not guarantee two events in the same frame
or exhaust all timing interleavings. No injected pick/highlight RPC or production SWF mutation is used.

## Concrete refusal candidate (not yet confirmed in Skyrim)

The menu's `onSelectionClick` first changes to `TOPIC_CLICKED`, plays its pick animation and displays the chosen
text, then calls `TopicClicked`. This ordering is in the local Skyrim UI source
`reference/plugins/skyrimui/src/dialoguemenu/DialogueMenu.as` and reflected in `FakeDialogue::PlayerPicks`.
`DialogueService::OnPickAsked` refuses another player's named option locally and returns true after its chat
notice; it does not restore the list in that branch. The existing Onmund scenario checks the refusal and then
has the guest answer, which can hide a menu stuck between those steps. The new case asserts recovery first.
This is a specific stuck-list hypothesis; it is not yet evidence of S9's inability to exit, S3's freeze or a race.

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
mouse variants use this corrected helper too. Native validation is pending.

## Validation and coordination

Python compile, scenario discovery and 30 existing harness unit tests pass. No new native results yet.
The shared duo remained reserved by main S1 from 12:19; this task queued at 12:32, marked READY at 12:35 and
requested a handoff in shared `CHAT.md`. No other agent's games have been touched. The native runner will
verify a saved claim and the deployed DLL hash, keep audio off the physical ALSA device, and stop/release its
own pair before any offline analysis or GameTests work.

Focused native commands after a saved duo claim:

```bash
python3 scripts/coop_test.py scenario dialogue-refused-pick-restores-list --until-fail 1
python3 scripts/coop_test.py scenario dialogue-guest-exits-alone dialogue-mouse-highlight-stays-shared dialogue-fast-mouse-pick dialogue-picked-text-visual --fail-fast
python3 scripts/coop_test.py scenario dialogue-guest-picks-at-greeting-end --until-fail 20
```

Stop and release own games before inspecting a failure offline or adding its GameTests flow. The repetition
above is the existing documented early-press probe; scenarios themselves do not retry failed input.
