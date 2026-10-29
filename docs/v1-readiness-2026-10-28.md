# v1.0.0 playtest readiness — 2026-10-28

Historical audit of `e4085691` and the separate worktree. The session/item and party-script changes are now
integrated into dev, uncommitted; per-player dialogue is built. Current review and verification:
[v1 integration review](v1-integration-review-2026-10-28.md). Remaining agreed work:
[v1 checklist](v1-playtest-todo.md). The findings below retain the earlier audit's chronology.

Initially reviewed dev at `e4085691`. Amended after the 2026-10-28 sync from the `party-scripts-2`
agent: the original audit omitted its large uncommitted worktree, also based on `e4085691`.
The amendment reads that worktree's source/designs and incorporates its owner's validation report.
It distinguishes work already on dev, work built in the worktree, and work still in progress.
It does not claim a fresh Skyrim campaign playthrough or a merged-build validation.

## Verdict

The core is substantial enough to start a controlled vanilla two-player playtest. Guest story events,
shared scene participation and guest vampirism are **built in `.worktrees/party-scripts-2`**, with
native evidence reported there. They need integration and merged-build acceptance, rather than new
implementations. Per-player dialogue conditions/lists are actively being built by that worktree's owner.
The independent remaining wiring work is the F9/F6 shortcuts and ordinary session entry/exit cleanup.
Remove the F9 conflict before putting a normal foreground game in a tester's hands.

Accepted ragdoll imperfections, the deferred AI/quest lock report and mod compatibility are already
scoped in `bugs.md`. This assessment does not reopen those decisions.

## What is already built

The following table describes **dev at `e4085691`**. Worktree-only additions are identified separately
below. Native evidence is recorded in the linked designs; a passing fake-engine test alone does not
prove its engine integration.

| Area | Current implementation |
|---|---|
| Networking and entry | Steam friends-only lobbies, automatic hosting after a save/new game, invites, Steam P2P/relay tunnels carrying ENet, version/game/plugin-order refusals. Two-account native invite testing is recorded in [join-flow.md](design/join-flow.md#steam). |
| Campaign and characters | Host save transfer, importing a loaded character, returning from the main menu as a kept character, host co-save records, guest save/load refusal, party reload on host loads. First-time players must start/load their own character before joining. |
| Presence and combat | Movement/animations, equipment, casting, melee and spell damage, kill credit, personal combat XP, NPC death/resurrection, friendly fire, spectating, revival and checkpoint wipes. |
| Shared world | Quest stages/objectives/aliases, globals, time/weather, doors/locks/containers, cell-state catch-up, object changes and live physics. |
| Shared story | Party dialogue showing the host's list on every menu, with conditions judged for the host's player; a picked line's script runs for its picker. Messages/host-answered story boxes, position questions about the party, shared quest rewards, words/souls and story spells/factions. Per-player dialogue lists are in progress in `party-scripts-2`. |
| Everyday play | Item drops/pickups with extra data, shared gold, barter/training, crime/arrest/jail, shared rest, menus that leave the world running, mounts and party travel. |
| Followers and presentation | Vanilla follower slots with a member to follow and fallback rules; chat, party panel, join/error presentation and death UI. Follower acceptance has remaining native gaps below. |

The "Product pieces not started" section of `progress.md` is obsolete in several places. Steam,
plugin/load-order checks, main-menu return joins, save transfer, character records, guest save blocking,
host difficulty sharing and ended-state presentation all exist. Several older sync-table/loose-end
entries likewise describe gaps subsequently fixed. Do not use that section as the remaining-work list.

## Work I would prioritize

### 1. Remove the debug F9 hosting toggle

**Source finding; physical-key reproduction still needed.**
[`DebugService::OnUpdate`](../Code/client/Services/Debug/DebugService.cpp#L148) polls OS key state whenever
the game is foregrounded. F9 calls `StopHosting`/`StartHosting`, although F9 is also the native quickload
key used by the project's own scenarios. `DebugService` is constructed unconditionally in
[`World.cpp`](../Code/client/World.cpp#L101), and its source is not excluded by `test_harness=n`.

A real quickload can therefore also change the game's hosting role. The existing off-screen engine-input
scenario does not establish that the physical-key path is safe: it does not reproduce this foreground
OS-key polling. Remove or explicitly gate the legacy connection shortcuts, including F6's raw transport
toggle, and verify a physical quicksave/quickload in a foreground host and guest.

### 2. Guest story events — feature 12, rule 4: built in party-scripts-2

**Correction: built, uncommitted and not yet integrated into the reviewed dev tree.**

The worktree's `HostStory` and `StoryEvents` supply the production path: a guest's location changes
(`CLOC`) and crafts (`CRFT`) are raised on the host for its copy, and the story manager's visitors
judge that character. A party arriving together counts as one arrival. The initial finding described
dev alone and incorrectly presented this as project-wide work still to implement.

The owner reports Skyrim `guest-story-starts-no-quest`, `guest-location-change-starts-story` and
`guest-craft-raises-story` passing. Source:
[HostStory.cpp](../.worktrees/party-scripts-2/Code/client/Services/Host/HostStory.cpp);
design/status: [worktree progress, feature 12](../.worktrees/party-scripts-2/docs/progress.md).

Remaining readiness work: integrate the implementation and retain its affected acceptance evidence
on the merged build. Its owner is actively editing these files; do not duplicate the implementation.

### 3. Wire ordinary leaving, main-menu cleanup and terminal disconnects

**Source finding; native quit-to-menu reproduction still needed.**

The network leave/kick/reconnect mechanisms exist, but the player-facing lifecycle is incomplete.
[`SteamService::OnMenu`](../Code/client/Services/Generic/SteamService.cpp#L47) handles new-game loading;
it does not close a session when the main menu opens. I found no other production menu route doing
that cleanup. [`JoinService::OnDisconnected`](../Code/client/Services/Generic/JoinService.cpp#L251)
sets the kicked/ended state, and exhausted reconnects set another ended state, without returning the
guest to the main menu. The join panel's Back button only dismisses its status
([`UiService.cpp`](../Code/client/Ui/UiService.cpp#L420)).

That differs from the leave/kick contract in [join-flow.md](design/join-flow.md#leaving-and-kick).
It risks a lobby/listener outliving the host's loaded world and leaves a disconnected guest in the
borrowed world. Guest services are destroyed on disconnection, restoring local world decisions while
reconnection is pending. `SaveService::MaySave` does block saving during retries, but that block ends
when the join step becomes idle after a kick or exhausted reconnect, with no main-menu return.

Add a small ordinary Leave/End Session route and a common cleanup path for native quit-to-menu,
intentional leave, kick and exhausted reconnects. This need not grow into the deferred player-list or
settings UI. Verify that records are retained as intended, guests cannot keep progressing/saving the
borrowed world while recovery is pending, and the next loaded world gets a fresh session.

### 4. Shared scene participation — feature 9: built in party-scripts-2

**Correction: built, uncommitted and not yet integrated into the reviewed dev tree.**

The router's Body row shares story moves, idles and AI-driven actions to each game's own player.
`GuestScene` makes each guest's player play the host player's part in an engine scene. Mirrored moves
do not lead party travel, and personal moments stay personal. The owner reports the MQ105 Greybeards
and Elder Scroll vision scenarios passing (`greybeards-scene-casts-party`,
`elder-scroll-vision-reaches-party`). See
[worktree cutscenes.md, Step 2](../.worktrees/party-scripts-2/docs/design/cutscenes.md#step-2-each-players-own-character-built-2026-10-27).

That design still records scene-specific limits: `Debug.SendAnimationEvent` is not read by the router,
camera-attached effects/2D sounds have not been checked per scene, and several players may share a marker.
Those limits do not make step 2 unimplemented. Remaining work is integration and affected acceptance.

### 5. Guest vampirism — feature 8: built in party-scripts-2

**Correction: built on per-player system quests, uncommitted and not yet integrated.**

The disease and hunger run on the guest's own `PlayerVampireQuest` instance. Vanilla therefore picks
that character's race's vampire variant natively. The per-player quest state/globals are kept apart
from world sync and retained in the character record. The agreed story cure cures all vampires, and
the story's gift turns the party. See
[worktree system-quests.md](../.worktrees/party-scripts-2/docs/design/system-quests.md) and
[vampirism.md](../.worktrees/party-scripts-2/docs/design/vampirism.md).

The owner reports `vampirism-is-each-players` passing. Remaining work is integration and affected
acceptance. Falion/innkeeper dialogue conditions still reading the host are part of the ongoing
dialogue work, not evidence that personal vampire progression is absent. Broader disease-system
coverage should be assessed from its own evidence rather than inferred from this vampirism result.

### 6. Per-player dialogue conditions and lists: in progress, owned by party-scripts-2

**Correction to the built-features table:** dev shows the host's dialogue list on every menu and
judges it for the host's player ([shared-dialogue.md, The rule](design/shared-dialogue.md#the-rule)).
Routing a selected line's script to its picker does not make its eligibility conditions personal.

The worktree's owner is building judgment for the picker, per-player lists, skills on copies, and
intimidate/bribe judgment for those copies. This remains active work rather than a completed feature.
Leave `JudgedFor`/`JudgedRules`, the condition hook, dialogue bridges/services/messages, spawn skills,
and the related routing, persistence, tests and designs to that owner. After integration, accept
different players' skill, race, inventory and quest-dependent options through the full conversation.

## Additional checks with high value

- **Learned crafting knowledge across rejoin:** character records preserve skills, perks, spells,
  items and words, but contain no ingredient-effect knowledge or learned-enchantment ledger.
  The local CommonLib reference's `IngredientItem::LearnEffect` stores knowledge on the ingredient
  form (`gamedata.knownEffectFlags`, `kIngredientUse`), not in player skills/inventory. The production
  world-change stream handles references, quests and globals, rather than an ingredient-knowledge row.
  This makes guest-only crafting knowledge a likely persistence gap when the host's save is loaded
  again. Reproduce by having only the guest learn an ingredient effect and disenchant an item, then
  host-save, restart and rejoin. Establish whether knowledge is personal or shared and retain it
  accordingly. This is an uncovered source-level gap, not a native reproduction in this review.
- **Concurrent looting:** dropped-item pickup has host grants, but ordinary containers use a guest's
  absolute remaining count (`GuestWorldState::OnContainerChange` → `HostAuthority::OnContainerChange`
  → `HostEngine::SetItems`). There is no corresponding container-take grant/version check in that path.
  Two games taking the last item before either sees the other's change can both propose zero after
  receiving it locally. This is a source-level duplication risk, not a native reproduction from this
  review. Test a chest, corpse and merchant with delayed traffic; a world-equality checksum alone
  cannot prove item conservation across player inventories. A defect here can be prioritized as a bug
  within the user's playtest tolerance rather than requiring a new inventory architecture for v1.
- **Followers:** native recruitment, commands, inventory, fallback, save/load and a real load door have
  evidence. Quest teleports, jail, fast travel, 72-hour waiting, directed favors, combat during fallback
  and native three/four-player runs remain explicitly unaccepted in
  [followers.md](design/followers.md#acceptance-and-limits).
- **Real session size and network:** fast flows include three/four players, and real two-account Steam
  invites were tested. The duo primarily uses direct session traffic. Run one separate-machine Steam
  session from the packaged build, returning-character and first-time joins, an outage/rejoin and a
  party reload. Test three/four actual games before promising that capacity; fake-engine coverage is
  not native engine evidence.
- **Campaign evidence:** documented quest walks advance stages and do not play all intervening fights,
  dialogue and travel. Play a real dungeon/quest chain with the guest driving interactions, then save,
  leave, restart and continue. Complete a fresh full native regression/soak after the remaining wiring;
  the documented script-only soak is not a full-release soak. The automated new-character scenario
  uses Quick Start to skip the intro; it does not establish that co-op through vanilla Helgen works.
- **Different character builds:** the inspected combat evidence emphasizes melee, Firebolt/Flames,
  shouts and friendly fire. Explicitly accept guest archery/crossbows, stealth detection and sneak
  damage, conjuration/reanimation, healing another member, poisons/enchantment procs and soul trapping.
  These are coverage requests, not claims that the existing inherited implementations are absent.
- **Distribution:** add a reproducible player package and short setup instructions. The checked-in
  README describes deleted server/launcher/UI directories. The native harness sets `SKYRIMCOOP_UI_DIR`
  to the checkout; a normal installation instead needs the documents/fonts/images at
  `Data/SKSE/Plugins/SkyrimCoop/ui`. Verify a `test_harness=n` build, behavior data, dependencies and
  native Steam/SKSE launch on an installation without checkout paths. The local gate builds the
  currently configured development plugin; it does not validate that distribution.
- **Install matching:** the current gate compares build, runtime/SKSE and plugin names/order/light flags.
  It does not hash script/assets/plugin contents or check an SKSE-plugin manifest. A curated identical
  vanilla setup is sufficient for the first playtest; a full mod-sync/updater project can stay deferred.

## Historical validation from the initial dev audit

The results below apply to dev `e4085691`, not the uncommitted `party-scripts-2` implementation or
its future merged build. For the amendment, its owner reports `scripts/check.sh` passing and the
native results cited above; those checks were not rerun here. This amendment changes only this audit.

**Standing test policy:** run `scripts/net_check.sh` only very seldom, never routinely or as part of
an audit. The initial audit ran network checks too broadly, including an unnecessary repeat during
the runner switch. The retained results below are historical evidence, not a recommendation to
repeat them. No tests or network checks were run for this amendment.

Initial `scripts/check.sh`: **PASS** in 11 seconds.

| Suite | Cases | Assertions |
|---|---:|---:|
| ClientTests | 80 | 732 |
| HostAuthorityTests | 178 | 1902 |
| Encoding / TPTests | 22 | 281 |
| GameTests | 179 | 1937 |
| Total C++ | 459 | 4852 |

Python comparison checks, UI fixtures and the Windows plugin build also pass.
Logs: `build/check/`.

Initial GameTests results under both `scripts/net_check.sh` network profiles:

| Profile | Result |
|---|---|
| 50 ms delay, 30 ms jitter, 5% loss, seed 1 | **PASS**, all 179 cases / 1937 assertions, serial runner, 541 s. |
| 120 ms delay, 80 ms jitter, 10% loss, seed 2 | **FAIL**, all 179 cases executed, 178 passed / 1 failed; 8 isolated shards, 263 s. |

The higher-loss failure is `A party wipe loads the previous host save and saved guest character`
(`Code/tests/games/death_flows.cpp:287`): "both games load the previous checkpoint" did not happen
within the flow's 20-second network wait. The same case then **passed alone** under the same profile
(13 assertions, 7.882 s). Root cause is unresolved; this is evidence of an intermittent or order-dependent
integration failure, not proof of a reproduced native Skyrim failure. It should remain tracked, not
be counted as a green full network gate or dismissed because the isolated retry passed.

The initial wrapper's first profile completed successfully just before the runner switch. Its second
profile was stopped and replaced by the isolated shards above. The aborted sharded repeat of profile 1
is not additional validation. Current summaries and individual logs:
`build/check/readiness-20261028/summary.txt`, `serial-complete.log`,
`delay-120-jitter-80-loss-10-seed-2/*.log`, and `wipe-isolated.log`.

No native game was launched, deployed, hot-reloaded or stopped by this review. Existing native results
in the design documents are historical evidence, not fresh passes asserted by this audit.

## Suggested finishing order

Remove/gate the legacy F9/F6 shortcuts and close the normal session lifecycle. In parallel, the
`party-scripts-2` owner is finishing per-player dialogue judgment/lists and skills on copies. Integrate
that worktree's completed rule 4, scene participation and personal vampirism with its existing evidence;
they are not three missing implementations. Review the combined build and run the affected checks
appropriate to the actual integration changes, respecting the standing network-test policy above.

Reproduce ingredient-knowledge persistence and concurrent container takes, then validate a reproducible
player package through an ordinary Steam session and a real quest/save/rejoin loop. Followers and
three/four-game acceptance remain useful checks. The independent work areas are debug shortcuts,
`JoinService`/`SteamService`/`UiService` lifecycle, the item/knowledge findings, distribution, and the
obsolete "Product pieces not started" section; leave the worktree owner's listed files untouched.

Trade UI, map/compass players, the full settings/player-list screens, mod sync and third-party mod
adapters can stay deferred. The readiness picture is closer than the original dev-only verdict:
integration, completion of the ongoing dialogue work, session lifecycle and targeted acceptance are
the next steps.
