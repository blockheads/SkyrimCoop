# S3 reproduction: guest arrest, selection and travel

Scope: recreate the 2026-10-31 Fabien report before changing crime, dialogue or travel behavior. Worktree
`.worktrees/s3-guest-arrest`, branch `repro/s3-guest-arrest`, committed baseline `454fb752`.
The broader mouse/highlight timing and stuck-dialogue concerns remain leads, not assumed causes.

## Archived trigger

The normalized playtest timelines show:

- Host 14:57:40.447: `player 2 picked topic 267DB (info 267E0)`.
- Guest 14:57:40.622: `the menu's click on option 0`, then `267DB / 267E0`.
- Host 14:57:46.715: arrival at cell `42245`, worldspace `16BB4`.
- Guest 14:57:47.265: follows to that cell; phase 0 at 14:57:47.436.
- Guest 14:57:57.339: watchdog reports the main loop stalled for ten seconds, inside our DLL.

These are read from the archived timelines under main `build/coop-test/playtests/2026-10-31-fabien/`.
The native topic names and reference locations below come from this installation's `Skyrim.esm` records;
extracted rows are retained in the worktree's `build/s3-records.json`.

| Form | Native editor ID / meaning |
| --- | --- |
| `267DB` | `DGCrimePayFine` |
| `267E0` | The INFO the archived click selected |
| `267DD` | `DGCrimeGoToJail` |
| `42245` | `RiftenCitySouth`, an exterior release destination |
| `13A5C` | `IvarsteadVilemyrInn`; contains all four stolen references in the report |
| `2123C` | `GuardIvarsteadSonsCentralPatrol01Ref`, NPC base `6462E` |
| `2816B` | `CrimeFactionRift` |

The four objects are two gold references (`9E63D`, `9E63A`), bread (`9E5C2`, base `65C97`) and a hanging
pheasant (`9E572`, base `38B0C`). The archived action was a mouse click, so the topic alone does not establish
which line the player believed was highlighted.

## Expected behavior to distinguish

[Shared party bounty](../docs/design/shared-party-bounty.md#guard-arrest-and-jail) currently makes the host execute
native sentencing and fine payment, with party travel bringing the guest along. A fine's teleport goes to the
release marker without a jail sentence. A jail choice should create a positive native sentence and bring both
players into the actual jail. Host movement by itself does not distinguish a failure under that design.
The guest must remain responsive after either arrival.

## Native checks

First controls: existing `guest-guard-arrest-fine` and `guest-guard-arrest-jail`. They use a real witnessed guest
container theft and native guard dialogue, without injecting bounty or force-greeting the guard. An evidence
wrapper records both menus, the requested and highlighted topic/INFO, GFx selected-entry data and a screenshot
immediately before Accept, followed by both games' arrival state and logs. Existing assertions and whole-world
comparisons remain enabled.

Known reviewed DLL used for these controls:
`5ff6854cbae944c56a21cbfc3f0f96d7352ad92b991b41ab9ec4342300267d32` (SHA-256).
Deployment is checked explicitly because the harness's size/mtime shortcut can reuse another worktree's DLL.

## First native controls

Both canonical controls pass on Linux/Proton (the first boot failed in XAudio2_7 during startup and is retained
separately, before either scenario began):

- `guest-guard-arrest-fine`: PASS, 35.5 s. Actual menu: "You caught me. I'll pay off my bounty. (1 gold)";
  requested/highlighted topic `267DB`, INFO `267E0`. Both games completed fine travel and remained responsive.
- `guest-guard-arrest-jail`: PASS, 26.8 s. Actual menu: "I submit. Take me to jail."; topic `267DD`, INFO `AD7C7`.
  Both games reached Whiterun prison `4A376`; native faction `267EA`, sentence 1; native release also completed.

Evidence: `build/s3-native.log`, `build/s3-evidence/01-before-press-267DB.{json,png}` and
`03-before-press-267DD.{json,png}`. Both prefix process sets were verified empty and the duo released.

## Closer Ivarstead/mouse variants

Python-only flows added in this worktree:

- `guest-ivarstead-fine-mouse`
- `guest-ivarstead-jail-mouse`
- `guest-ivarstead-fine-mouse-sweep`

These use the actual reported inn and four pickup references, the same placed guard reference, native theft/arrest and mouse input.
They save and restore a checkpoint to keep the world's theft/bounty state isolated. Evidence records both games,
the requested and rendered choice, hover indices and arrival/sentence state. The mouse coordinate transform is
measured through actual cursor motion and the menu's row-local coordinates. The sweep variant moves across
several choices before clicking. Results below retain failed setup/check attempts as well as successful travel observations.


## Original-location attempts

- First attempt: a single pickup of gold `9E63D` sent the real theft proposal and removed the reference,
  but did not earn a native bounty within 10 s. It never reached arrest or choice; it is not an S3 reproduction.
  Evidence: `build/s3-rift-attempt1.log`, `build/coop-test/s3-repro/20261101-121040-fine-mouse/`.
- Four-pickup attempt: all four reported objects were picked up through player activation while the actual
  Ivarstead guard could detect the guest. The first three had no bounty in the captured snapshots; after the
  pheasant, both games had a one-gold Rift bounty. The guard naturally arrested the party.
- Mouse fine selection visibly highlighted “You caught me. I'll pay off my bounty. (1 gold)”. GFx and the
  host's callback agreed: topic `267DB`, INFO `267E0`. Host arrival at `42245 / 16BB4` occurred at 12:18:15.386;
  the guest followed and both were responsive. Both frame counters continued advancing during the subsequent
  ten-second assertion timeout. This recreates the original travel path, **without reproducing the freeze**.
- That run failed a separate “purse minus bounty” assertion: all four player/copy inventories agreed on 141
  gold afterward. Its accounting cause has not been established, so the whole scenario is not recorded as PASS.
  Fine payment confiscates stolen items as well; this S3 scenario now records before/after gold for investigation
  while its assertions focus on selected route, native sentence, shared bounty, arrival and responsiveness.
  No production behavior was changed to bypass that discrepancy.

Evidence for the four-pickup attempt: `build/s3-rift-attempt2.log`,
`build/s3-rift-attempt2-evidence/`, `build/coop-test/s3-repro/20261101-121736-fine-mouse/`;
report `2026-11-01-1218-b5c`. Own games were stopped and prefixes verified empty after each attempt.

## Current result and limits

S3's freeze is **not reproduced** on this Linux/Proton baseline. The original-location fine travel is
recreated with matching mouse text, topic, INFO and destination; the guest remains responsive. The canonical
actual-jail control also passes. No crime, dialogue or travel fix is justified by these observations yet.

The original-location mouse jail and hover-sweep variants are prepared but **not yet natively validated**.
After releasing at 12:18, this task queued at 12:19 behind the main S1 run. That reservation remained held
at 12:30 despite handoff requests, so the remaining native batch could not run. It must claim the shared duo
before executing:

```bash
python3 scripts/coop_test.py scenario guest-ivarstead-jail-mouse guest-ivarstead-fine-mouse-sweep --until-fail 1
```

These variants do not exhaustively test race conditions: even the sweep waits for its final highlighted choice
and captures evidence before clicking. A passing sweep would not establish that rapid click/highlight races
or all visual-dialogue problems are absent. The guest is positioned at each object and the placed guard is
moved into view as fixture setup; bounty and arrest are not injected.

Python compile, discovery dry-run and the 30 existing harness unit tests pass. No C++ source changed, no
GameTests regression or fix was attempted without a native failure, and nothing from this task was merged.

### Mouse-fixture correction (dialogue follow-up)

Reviewing the SWF source found that the first fixture treated `Entry0` as option zero. Skyrim's list reuses
clips and recenters them: an option's visible clip is identified by `itemIndex`, often `Entry4` for the first
option. The earlier screenshot's cursor was above the choices. Its selected text and actual fine callback
remain valid evidence, but it does **not** establish the claimed mouse hover path. `dialogue_mouse.py` now
finds the visible clip for each option before moving the pointer. Its Lucan shared-highlight and quick-click
controls now pass; the original Rift mouse variants remain pending.
