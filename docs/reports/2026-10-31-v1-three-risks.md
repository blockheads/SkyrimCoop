# Three v1 risks, 2026-10-31

Completed in `.worktrees/v1-copy-desync`, preserving its prior uncommitted fixes. No commits or merges.
This supersedes the earlier deferral for only lock-order reports, enemy-copy death delay, and Belethor's
lingering scene. Other deferred findings remain deferred. Combined acceptance is **20 PASS / 2 FAIL**;
a fresh barter/death follow-up is **5/5 PASS**. The two unrelated failures remain recorded below.

## Classifications

| Risk | Evidence and disposition |
| --- | --- |
| Mounted departure / placed NPC lock family | Native engine orders, incorrectly attributed through our travel wrapper. No contention observed in the focused probe; no universal claim that engine deadlock is impossible. Narrow frame attribution corrected; raw reports retained. |
| Border-travel lock family | Our worker callback synchronously loads cells against native main-thread load work. Conflicting modes and disassembly establish unsafe scheduling, not an observed deadlock. Cell-loading moves now run between frames. |
| Checkpoint lock family | Our worker callback writes quest aliases against native main-thread alias/package evaluation. Unsafe scheduling corrected by queuing quest application and retries between frames. |
| Enemy-copy death delay | Stale scenario handle after travel, not delayed death of the current copy. Current copy receives death state in52ms and replays death in136ms in the original failing run. Reacquire its current session/copy; keep the original five-second assertion. |
| Belethor scene and barter reopening | Real stale owner-scene cache after a native scene-end event, plus a guest conversation left open invisibly after barter. Refresh active owner scenes after native cleanup and finish the guest's native conversation on close. Original and spawned merchant controls now complete two transactions. |

## Lock evidence and correction

All three families were investigated separately. The first fresh four-case run records mounted-guest-leaves
FAIL on its write/write pair; leveled-placed-npc-is-hosts, guest-rides-across-border and death-wipe-checkpoint
PASS. `build/risks-lock-native/scenarios.log` retains both acquisition stacks and thread IDs. Archived
final105 reports are from the current mode-aware checker, not the earlier mixed-mode reporting defect.

**Border:** VM worker440 holds reference extra-data lock `8CAB4108` for write (170402), then requests the
process-lifetime all-forms map lock `1420FC018` for write (1DDC3F). Main384 previously holds the latter for
read (61C130), then requests the reference for read (61C249). The worker is
TravelService::FollowTo -> CellAt -> SkyrimBridge::LookupCellByCoords -> TESWorldSpace::LoadCell -> stored
changes -> leveled NPC construction. The reverse is native LoadGame work. Modes conflict; a worker load
can overlap the main thread's engine work.

**Checkpoint:** VM worker444 holds quest alias lock `8843DC90` for write (3CDB45), then requests reference
alias-array lock `289BE028` for write (182241). Main384 previously holds the latter for read (1823D0), then
requests the quest lock for read (3D1507) during native cell-load alias initialization/package evaluation.
Our enclosing callback is GuestWorldState::OnQuests -> ApplyQuest -> ClearFills -> ClearRefAlias.
Both stacks and conflicting modes match the disassembly. Historical addresses alone cannot prove a live
wait or that an alias-array allocation survived unchanged across loads; neither report establishes a deadlock.
The fix removes our concurrent mutation rather than assuming the history is false.

Party follow, actor-copy cell moves, guest quest updates and alias retries now use the existing
MainLoopTasks phase. Pending service work is cancelled on destruction; delayed moves check actor existence
and connection. No new native call or hook is added. The fake distinguishes known-cell native movement
from potentially loading bridge lookup and records worker cell loads/quest writes. The two scheduling
flows fail with the old callbacks even on the corrected fake (`build/risks-scheduling-confirmed-red.log`),
then pass with the queue. Disconnect and task-cancellation tests cover delayed service lifetime.

**Mounted/placed family:** main384's travel detaches a reference's Havok object while holding its
ExtraDataList for write (17A479), then asks for the exterior bhkWorld write lock (E82682). Worker432's
UpdateExteriorWorldJob holds that same world for write (E85437), then trap/trigger processing reads or
writes that reference's extra data (16B61D,17928D,165105). The probe identified the same surviving reference
and world; freeing/reusing a lock address does not explain this family. The travel's own world update uses
JobList::Begin + Finish synchronously. Across mounted-guest-leaves twice, party-travel-mounted-guest,
party-fast-travels and party-changes-cells, the focused probe saw220 removal lock asks per game and no
other thread holding the world at those asks. This finite sample does not prove that every possible
per-frame world job is serialized with travel.

Our HookProcessQueuedTravel defers a frame or calls native40438 from its original caller/thread; the queued
party callback runs separately. A2026-10-23 report contains the same engine pair before this wrapper was
on its stack. The correction registers only this wrapper's code range, alongside the existing main-loop
pass-through ranges. Its final PDB code size is856 bytes (`0x358`); the matching symbol dump is archived.
The order checker, modes, both stacks and raw reports remain enabled. No lock-site allowlist was added.
The extracted attribution regression proves a genuine service callback inside or below the wrapper still
marks a report ours, including the exact exclusive range boundary. The final native mounted case emits
the same strict write/write report as an engine-only warning and passes.

The mounted investigator's proposed cross-thread overlap patch is **not integrated**: its concurrent reads
of mutable held-lock entries lacked coherent publication and its initial reverse-order test omitted modes.
Only its focused native probe evidence is used; no new claim of strict live-cycle detection is made here.
The original checker still fails reports attributed to real callbacks and self-deadlocks.

References: local CommonLibSSE-NG engine declarations and
[task interface](https://ng.commonlib.dev/class_s_k_s_e_1_1_task_interface-members.html);
IDA lock calls in `build/risks-lock-native/decompiled*.c`; symbolized original stacks in
`build/coop-test/final-weapon-105.log`. Mounted probe logs are copied to
`build/risks-combined-native-evidence/mounted-probe/` from the separate investigator's worktree.

## Enemy-copy death

The exact original helper is preserved in `build/risks_original_combat.py`. A fresh instrumented run uses
`COOP_ORIGINAL_DEATH_PROBE=1`, fails its original five-second assertion, then observes20 additional seconds
without changing that FAIL. Original run: **1 PASS / 6 FAIL**,159.3s, including fixture/reopening failures
retained in `build/risks-runtime-original-evidence/scenarios.log` with its matching DLL/PDB.

The old wolf copy `FF000F36`/session9 unloads during travel. The owner reference `FF000E87` survives but
returns as session12, with current copy `FF000F9B`. At the first sample of the original wait, the current
copy is already dead, life-state2, with no deferred or queued kill, a ready graph, replay queue0 and Ragdoll.
Owner and copy both report health1 (the scenario forced that value); their native death/life state, not a
health-zero assumption, establishes death. Focused samples: `build/risks-original-death-observation.json`.
Owner death-action tick579147002 is followed by death notification579147054 (+52ms) and replay579147138
(+136ms). The original predicate watches the unloaded copy. The extended observation separately shows
spectator recovery; it does not turn the failure into a pass. The earlier103-unit baseline cleanup failure
is retained historically and does not recur in these corrected acceptance runs.

The canonical scenario reacquires the enemy's current session/copy after return, before the survivor's
blow. Its death wait remains **five seconds**. The fake already recycles temporary form IDs; the regression
occupies the unloaded ID with another reference, then kills the returned NPC. Both successful and refused
native death-action sections verify its new copy dies. Existing death/event flows cover delayed/reordered
notifications and replay. The real-scenario Python unit fails against the old stale handle and explicitly
checks the five-second timeout (`build/risks-death-oracle-unit-red.log`). GameTests RED evidence is
`build/risks-death-travel-red2.log`. Corrected native travel passes, including combat clearance, spectator
recovery, walking, equipment/magicka and baseline restoration, last in both final batches.

## Belethor scene and reopening

The original placed reference is `1A672`; the spawned merchant shares its base `13BA1` but is a separate
reference. Fresh failure snapshot `build/coop-test/results/20261031-011626-7aa560/at-failure.json` shows the
original owner's native/effective scene0; the guest's native scene is also0, but its cached owner scene and
effective gameplay getter remain `C791A` (817434). This is a real cached gameplay-state defect. The premise
that the compared scene bypasses the override was checked against the code: the diagnostic readout calls
hooked37247. The new harness readout retains all three values separately, without changing production
interfaces or hiding the raw local scene.

Disassembly explains the missed transition: BGSScene::Stop24140 (`1403A0FA0`) sends TESSceneEvent at3A108B
before its cleanup; cleanup3A2300 calls actor cleanup3A2570, which clears the actor's scene via virtual+600
at3A267B. AiStateEvents queues the change while the old state can still be visible. AiStateService now
refreshes only owned actors whose last sent scene is active. It publishes the actual final full state,
preserves replacement scenes, deduplicates, and stops reading after the scene becomes inactive. Missing
owners/copies are skipped. No scene ID is forcibly cleared. Copies showed no independent local scene
activity in the observed mismatch.

The fake learns native event-before-clear ordering and keeps local scene activity distinct from effective
owner state. `[scene-clear]` GameTests fail before the refresh; `[scene-refresh]` units fail before its
implementation and cover final clearing, nonzero replacement, missing owners and stopping reads. Evidence:
`build/risks-scene-native.json`, `build/risks-scene-check.log`, archived disassembly in
`build/risks-combined-native-evidence/skyrim-scene-*.c`.

After that fix, native host controls pass, but both guest controls fail on their second activation:
**3 PASS / 2 FAIL**,148.7s (`build/risks-scene-accept-evidence/scenarios.log`). The guest native speaker
remains set with its menu closed; the host's is0. DialogueService disabled native menu-close termination
for barter and did not restore it afterward; GuestDialogue::OnClosed did not finish that hidden native
conversation. The fake now models Start on an existing hidden speaker as leaving its menu closed.
`[barter-reopen]` GameTests fail on the stale speaker and second opening (`build/risks-barter-smoke-red2.log`),
then pass after role-aware close policy restoration and guest native conversation cleanup. The policy unit
fails three assertions first and passes six afterward. The flow gives the returned menu a frame before
closing, reproducing normal native timing rather than relying on the opening timeout's fallback cleanup.

All four native controls now close/reopen dialogue and barter and complete a second purchase, verifying
Speech stays personal. Each transaction compares effective gameplay scene to owner native state for both
the traded merchant and original reference, and logs native/cached/effective readings. In the fresh run all
scene values are0 after each transaction, and all four controls pass. The original controls also pass in the
combined batch. The temporary bounded death/AI event watcher was removed before both final batches;
typed actor/scene diagnostic fields remain in the harness.

## Acceptance and retained failures

`scripts/check.sh`: **629 C++ cases / 6,462 assertions** (Client160/1553, Host190/2049, encoding29/322,
GameTests250/2538),23 harness lifecycle units,17 comparison units, remaining Python checks, UI fixtures,
Windows DLL and launcher all pass. `build/risks-final-check.log`. `git diff --check` passes.

The combined affected batch completes **20 PASS / 2 FAIL in401.5s**, dead-guest travel last.
`build/risks-combined-native-evidence/` contains its exact log, matching DLL/PDB, check log and wrapper symbols.

| Scenarios | Result |
| --- | --- |
| mounted-guest-leaves; leveled-spawned-npc-is-hosts; leveled-placed-npc-is-hosts; enchanted-npc-pools-match-owner | 4 PASS |
| dialogue-options-are-the-partys; follower-switches-anchor; follower-anchor-saved; follower-trade-to-picker | 4 PASS |
| host-loads-party; npc-torch-repairs-in-default-slot; both Beast Forms; host/client-kill-move; quest-gold-turn-in-guest | 7 PASS |
| guest-rides-across-border; death-wipe-checkpoint | 2 PASS |
| dialogue-host-placed-barter-reopens; dialogue-guest-placed-barter-reopens | 2 PASS, two transactions each |
| party-travel-dead-guest (last) | PASS, original five-second death bound |
| dialogue-host-barter-earns-speech | FAIL before reopening: guest Sigurd `CDD73` marked own/session-1, missing LocalComponent/unregistered. Seen in the earlier checkpoint-followed-by-shop run too. |
| dialogue-guest-barter-earns-speech | FAIL before reopening: original Belethor owner turnStop/action612862/idle612865 versus copy CyclicFreeze/action77826/idle0. |

No exemption or tolerance change hides those two observations. They remain outside the current three-risk
fixes; this does not clear broader registration or animation-marker findings. Historical final105 remains
**87 PASS / 18 FAIL**; its body/ragdoll, facing, gait, paired-refusal and story-save findings stay deferred.

A fresh launch of the same binary then completes **5/5 PASS in139.2s**: spawned host/guest barter, original
host/guest barter (two transactions each), and dead-guest travel last. Evidence:
`build/risks-fresh-barter-native-evidence/scenarios.log`. This rerun is additional acceptance; it does not
replace the two combined-batch failures. Detailed runtime samples remain `build/risks-runtime.jsonl`.

Every native turn reads shared CHAT/DUO, saves its claim before deployment, uses only this worktree's
prefixes on :99/:100, then stops them, verifies exit and releases before offline work. DUO is now free;
no waiting entry remains for this worktree. No net_check, commits or merges were performed. Combined-dev
integration/acceptance remains separate.
