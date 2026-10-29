# V1 integration and code review — 2026-10-28

The outstanding session/item work and party-script work are combined in the main **dev working tree**, based on
`e4085691`. No commit was created and the index was left unchanged. The source worktrees remain intact.
The agreed upstream improvements in [v1-playtest-todo.md](v1-playtest-todo.md) remain pending; this integration
does not claim to implement that new queue.

## Scope and merge

Reviewed the uncommitted changes on dev, `.worktrees/v1-session-items` and `.worktrees/party-scripts-2`.
The session worktree matches dev's implementation; its changes were already present there. Integrated the
party work against their common base, preserving both sets of behavior across 180 changed/new source, test and
documentation paths. Also incorporated the party worktree's later dialogue-test progress notes and third-person
body-check documentation. With this review document, the final dev diff covers 182 changed/new files.

Four textual conflicts were resolved:

- `Bridges/HostEngine.h`: retain container transfers and story-event callbacks.
- `TestHarness/CommandsSession.cpp`: retain session cleanup and both knowledge/system-quest descriptions.
- `encoding/Opcodes.h`: retain the new container, story and system-quest requests, each with a distinct opcode.
- `tests/games/xmake.lua`: build both container-transfer logic and the system-quest/event routing support.

The other shared edits merged without textual conflicts: knowledge restoration and per-player quest state in
JoinBridge, player skills and corpse death upkeep in Actor, host callbacks, encoding tests, fake engines and the
scenario runner. Reviewed those combinations explicitly rather than relying on a clean textual merge.

Before integration, copied each worktree's changed/untracked files, binary diff and SHA-256 manifest into
`.git/review-backups/20261028-190346/`. The initial dev files were hash-checked before replacement.
The source worktrees' Skyrim tests were not stopped or their files changed.

## Findings fixed during review

### P1: another guest could interrupt the host's scene

`GuestScene::OnAiState` originally accepted every other player's copy. With a host and two guests, an ordinary
AI-state notification from the second guest, carrying no scene, ended the first guest's active host scene and
returned control prematurely. A guest's unrelated nonempty scene state could also start the wrong package.

The service now identifies the actor through `RemoteComponent` / `PlayerComponent` and checks the session roster's
host flag. Only the host's player's updates can start/end the shared scene. This removed the overly broad
`GuestEngine::IsPlayerCopy` interface. The service's scene/package IDs are explicitly initialized as empty too;
`GameId`'s default-initialized fields were otherwise indeterminate.

The new three-player GameTests case first reproduced cancellation at the assertion that the first guest was still
AI-driven after the second guest's update (`build/dev-review-scene-red.log`). It now proves another guest can neither
start nor cancel that scene, while the host starts and ends it normally. Added the production GuestScene service to
GameTests and its guest lifecycle; previously only its adjacent script/body routing was exercised there.

### Documentation: obsolete product backlog

Replaced progress.md's stale "Product pieces not started" section, which listed already-built Steam joining,
plugin-order checks, returning-character main-menu joins, character records and host difficulty as missing.
The earlier readiness audit is now marked historical and points here. The v1 checklist remains uncompleted.

## Unresolved findings and verification limits

- **P1 — host reload freeze remains unresolved.** Native evidence from the session/item work shows a corrupt
  actor-value map sentinel while HostFollowers builds spawn data after a load. Restored positions/records alone
  do not establish that reload works. See [session/item report](v1-session-items-2026-10-28.md#reload-freeze-still-unresolved).
- **P1 — the host's werewolf reverting can crash the guest.** The party worktree records graph/heap failures,
  including with dev's earlier DLL; this review does not establish a fix. See [party-script bugs](bugs.md#party-scripts).
  The queued upstream animation safety/ownership work is relevant, but has not been proved to resolve that crash.
- **Known gaps —** the same bug list records story `Debug.SendAnimationEvent` reaching only the host's player,
  crafting-station exit state occasionally sticking on a copy, and the guest-local marker created during vampire
  transformation. Keep these distinctions when reading the native evidence; the marker mismatch alone is not a
  demonstrated visible gameplay failure.
- The source worktree reports native dialogue/story/scene/vampirism evidence in its designs/progress notes.
  This review did **not** rerun those native scenarios against the merged DLL or take over the running Skyrim duo.
  The newly fixed scene filter is covered by a real three-game service flow on the fake engine, not a native
  three-player Skyrim run.
- Packaging/clean installation, the new upstream work queue and the real Fus Ro Dah scenario remain unfinished.
  Accepted ragdoll imperfections and the previously deferred quest/AI lock inversion remain scoped as recorded.

## Validation of the combined tree

Final `scripts/check.sh` passes (`build/dev-review-final-check.log`):

| Check | Result |
| --- | --- |
| ClientTests | 93 cases, 819 assertions |
| HostAuthorityTests | 182 cases, 1,944 assertions |
| Encoding / TPTests | 26 cases, 302 assertions |
| GameTests | 201 cases, 2,071 assertions, eight shards |
| Harness and ragdoll comparison tests | pass |
| UI workbench fixtures | pass |
| Windows SkyrimTogetherClientDLL build | pass |

The scene regression failed before the filter fix and passes after it. Python compilation of `coop_test.py` and
`coop_v1.py` passes. Diff whitespace checks pass. `scripts/net_check.sh` was not run.

The current tree is available for the user's diff review and commit; these build/test results are not a claim
that the remaining v1 work or native playtest acceptance is complete.
