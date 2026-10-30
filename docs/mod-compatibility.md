# V1 fix-mod compatibility

Latest assessment (`2f38f777` plus the `v1-mod-profiles` fixes, merged into dev 2026-10-30):
**131 pass / 24 fail / 0 not run** across all **155 required cases**. The confirmed
synchronous-save deadlock and late-save harness fixture are fixed; final `scripts/check.sh`
passes **567 C++ cases / 6,102 assertions**, nine late-baseline tests, comparisons/UI and
Windows DLL/launcher builds. **The six-mod profile is not accepted for v1.** Extended follower,
equipment/world-state, lock and setup failures remain. See the
[final assessment and complete failure table](reports/2026-10-29-v1-mod-profiles.md#final-acceptance-and-recommendation).

All shortlisted archives are acquired. The earlier pre-fix matrix below reproduced load crashes.
A [save-time VM re-entry race](re/crashes/2026-10-29-save-vm-reentry.md) was fixed before this batch;
that post-fix combined run completes 27 cases without a crash (23 pass, four world/pose checks fail).
No dependency-specific workaround was applied in that earlier run.

Earlier focused follow-up: corrected lock diagnostics pass six repeated Engine Fixes cycles
(30/30), then the object-pose fix runs **14/15** with all six mods, no crashes. Main-menu join,
potion rejoin, furniture and puzzle poses pass. The remaining check differs only on three clutter
movement records after vampire rejoin. See [the final follow-up below](#final-object-pose-follow-up).

Record individual dependency results before testing the combined profile. These are native Skyrim checks
on Linux/Proton, not Windows acceptance or a claim about every possible mod combination. Packaging
the playtest modlist remains a later publishing task. These are two local games on one Steam
account over the direct session transport; separate-account Steam and Windows acceptance remain open.

The 2026-10-29 matrix uses committed `dev` **ca906283**, Skyrim **1.6.1170**, SKSE **2.2.6**, Address
Library and Crash Logger **1.25.0**. Games use the usual **:99 / :100** displays, isolated game files
and prefixes, and the shared `build/coop-test/DUO.md` reservation. Uncommitted diagnostics/network
changes in the main checkout are excluded. Two older Skyrim instances from another task were also
running during this matrix; frame-rate samples include that load and are not a performance benchmark.

## Pre-fix matrix (`ca906283`)

| Profile | Pinned release | Result |
| --- | --- | --- |
| Ours without additional fix mods | ca906283 | Initial 4/4; focused comparison 5/5 (dialogue lists, training, barter speech, host-to-guest shout, host load). |
| [Engine Fixes](https://www.nexusmods.com/skyrimspecialedition/mods/17230) | 7.0.20, preloader 7 | 12/15 on ca906283; death combat tracking, guest-death ragdoll pose and host-load crash fail. [Earlier and current evidence](engine-fixes-compatibility.md). |
| [powerofthree's Tweaks](https://www.nexusmods.com/skyrimspecialedition/mods/51073) | 1.17.1, AE1170 DLL | 10/10 pass. Supplied fixes/settings, without enabling extra optional tweaks. |
| [Scrambled Bugs](https://www.nexusmods.com/skyrimspecialedition/mods/43532) | 21, AE 1.6.629+ file | 9/10 on the completed pass; guest crashes during host load. Stock configuration. |
| [Bug Fixes SSE](https://www.nexusmods.com/skyrimspecialedition/mods/33261) | 10, AE 1.6.629+ file | 6/9; training setup, a lock-order report and death combat tracking fail. Attribution pending. |
| [SSE Display Tweaks](https://www.nexusmods.com/skyrimspecialedition/mods/34705) | 0.5.25 | 60 FPS: 7/7. Cap 120: 6/7; host-to-guest shout walking check times out. Observed ~71 updates/s, not sustained 120 FPS. |
| [USSEP](https://www.nexusmods.com/skyrimspecialedition/mods/266) | 4.3.8a | 9/13; equipment desync after dialogue, known vampirism marker, death combat tracking and host-load crash. All required masters active in both games. |
| All technical fixes together | Versions above, without USSEP | 18/20; host-death ragdoll pose and extra weapon after host-load cleanup fail. Wipe/checkpoint recovery passes. |
| All fixes plus USSEP | Versions above | 15/18; known vampirism marker, host-death copy mismatch and guest crash during host load. Wipe/checkpoint passes after a fresh launch. |

The new baseline passes `scripts/check.sh`: **542 cases / 5,772 assertions**, harness/ragdoll
comparisons, UI fixtures and the Windows DLL build. This gate does not replace the native checks.

## Acquisition and configuration

Archives came from the authors' GitHub releases or the authenticated Nexus API. No API credentials
or signed download URLs are stored in this report or the profile manifests. The original dependency
archives and extraction trees are cached in this worktree's ignored `build/third_party` directory.

| Dependency | Author file | Archive SHA-256 |
| --- | --- | --- |
| Engine Fixes | GitHub 7.0.20 | `21330c95011f41859139635b43ce95ffb5fbadd3cd375d2f4358abcc3cb99407` |
| Engine Fixes preloader | Nexus 17230 / 725261 | `35633dc2904521f35861dbb582e084a10e77bec5b8f08fb08963d7578b07452f` |
| powerofthree's Tweaks | GitHub v1.17.1 | `5aeadd195dce06e3d72d7684373bab9116f8eadf3833af5cdf45c0d2ad4f9268` |
| Scrambled Bugs | Nexus 43532 / 368378 | `8412974b83426e10925865527e7d4764ea79db0e88a3666e79c108fad9d88ebd` |
| Bug Fixes SSE | Nexus 33261 / 368384 | `d5dcfec3d1318b258e1c0ea6adb47b5dc746cea98b9da4e44e1c49dd6d0b2e8c` |
| SSE Display Tweaks | Nexus 34705 / 797175 | `4f7a4980955eec472f3e73219df487fb990c48fd5ad0020c101cd8082b6b2c8c` |
| USSEP | Nexus 266 / 733846 | `5036e8b4abef7234a1f35d43e33780f529be8f85f449e41211ec4876d03e95d9` |

The preloader DLL from current Nexus file 725261 is byte-for-byte identical to the older author
archive's DLL used in the initial Engine Fixes check: `cf366987da6237559eb6e113ea717ec21762c9eec3a87d9dc4fa9ddfe7789c26`.
Both the newer archive and the DLL were downloaded and compared; an archive-name difference is
not a different tested executable here.

USSEP 4.3.8a requires Saints & Seducers as well as the other listed masters. The development install
has its BSA but lacks `ccbgssse025-advdsgs.esm`. The isolated profile uses the existing owned copy from the Windows
ElderTeej stock-game directory; saved headers confirm every required master is active in both games.
Enable USSEP in both games' Plugins.txt and verify both saved-game headers list it as active.
SKSE logs verify DLL plugins; they do not list ordinary ESP plugins. Each profile starts
a fresh game; do not load a USSEP test save after deactivating it.

## Evidence

Each completed profile writes `build/<profile>-profile.json`, `<profile>-matrix-duo.log`,
`<profile>-matrix-scenarios.log` and per-game SKSE logs. The manifests pin actual installed file
hashes and scenario names. Profile cleanup stops its own games, restores the original plugin list
and dependency files, and releases the shared reservation. If a failed case prevents resetting the
world, its result remains a failure and the not-run cases continue after a fresh duo launch.
Continuation logs and manifests preserve that boundary; later passes never replace earlier failures.

powerofthree's Tweaks passed `ability-stays-on-copy`, `late-join-client`, `host-loads-party`,
`death-spectate-host`, `death-spectate-guest`, `host-beast-form-stays-host`, `party-uses-door`,
`dialogue-peer-starts`, `firebolt-client-hits-host-npc` and `host-save-keeps-guest-character`.
Evidence: `build/po3-matrix-scenarios.log`, `po3-{host,client}.log` and `po3-tested.ini`.

## Failures requiring follow-up

These are observations from the pinned build, not established conflicts with the mod present in
a profile. Do not mark the combined package ready while its standard save/load flows are failing.

- **Reload stability:** the first Scrambled Bugs run exits on the host during load; the completed
  pass crashes the guest at `SkyrimSE.exe+0x2DD96C`, in the reference cleanup path called by
  `GarbageCollector::Add` (36459). Crash Logger identifies reference `FF000E8D`. The USSEP pass
  crashes the host at `SkyrimSE.exe+0xBC292E`, reading through invalid pointer
  `0x3F800000000F0001` while restoring animation state. Neither stack establishes the originating
  write or dependency responsible. Preserve these alongside the already open v1 reload issue.
- **Death combat tracking:** the wolf kills the host, but no longer targets the standing guest
  when the scenario checks combat. This occurs in the Bug Fixes, current Engine Fixes and USSEP
  profiles. Record it as a failed standard-flow check, without assuming a movement-fix conflict.
- **Bug Fixes training/barter:** training setup fails before opening the training menu: adding
  2,000 gold leaves all players/copies at 140. Bartering itself completes, then the engine
  AI/quest lock-order check reports an inversion with our condition hook on its stack. The same
  engine lock-site pair appeared in the earlier no-fix-mod baseline; compare focused cases.
- **USSEP equipment:** the dialogue-options flow completes, then its postcheck finds the host's
  copy missing the guest's clothes and weapon. The vampire turn/cure flow completes its behavior
  checks but leaves the already known guest-only `VampireChange` marker.
- **Walking after a shout:** the cap-120 profile passes knockdown, displacement and getting up,
  then times out on forward movement. A similar walking timeout occurred without extra fix mods
  in the older baseline. Failure diagnostics must distinguish blocked geometry from lost control.

Full crash reports are retained in `scrambled-matrix-scenarios.log` and
`ussep-matrix-scenarios.log`, as well as the isolated prefix's Crash Logger directory.

The technical combined profile completes all 20 planned cases across three fresh launches.
`death-spectate-host` fails its visible-body/ragdoll pose tolerance (three bones, maximum 17 units);
`death-spectate-guest` passes. `host-loads-party` completes its restoration and liveness checks,
then fails resetting equipment to the baseline: the guest still wears an additional iron sword
(`00012EB7`). `death-wipe-checkpoint` passes on the final fresh launch. See
`combined-matrix-scenarios{,-continued-1,-continued-2}.log`. This is 18/20, not a clean acceptance.

The all-fixes-plus-USSEP profile also crashes its guest during host load (01:05:45, invalid
execution address `0xFFFFFF03`). Engine Fixes is active here, so it does not eliminate the reload
failure. The host-death check also finds the owner's actor dead while its copy lives. These are
failed checks, not a basis for blaming USSEP without a controlled reproduction.

The Engine Fixes-only continuation finishes **12/15** across the initial interrupted batch and
three continuation batches. Host load crashes at 01:10:15 in our DLL at RVA `0xB1C84`, reached
through `Net::Server::GetTick` → `TransportService::GetTick` → `World::GetTick` → `World::Update`
from `HookVMUpdate`. The invalid pointer is `0x22797469746E65D4`. This establishes the faulting
call path for this crash, not the source of the corruption or the cause of the older unlocated
fault. Wipe/checkpoint recovery passes on a subsequent fresh launch. Evidence:
`engine-new-matrix-scenarios-initial.log`, `engine-new-matrix-scenarios.log` and
`engine-new-matrix-scenarios-continued-{1,2}.log`.

## Focused comparison without the extra fix mods

After the matrix, remove all six dependencies and cold-start the same co-op DLL. The five
focused cases pass: `dialogue-options-are-each-players`, `dialogue-picker-trains`,
`dialogue-guest-barter-earns-speech`, `fus-ro-dah-host-hits-guest` and `host-loads-party`.
Evidence: `baseline-focused-profile.json` and `baseline-focused-matrix-scenarios.log`.

Training and barter also pass in the technical combined profile; dialogue lists pass in both
combined profiles. Their isolated-profile failures have not been reproduced in a controlled
comparison and are not evidence to remove those mods from the proposed list. Conversely, five
baseline passes do not establish that the historical intermittent reload corruption is fixed.

All individual profiles and both combined profiles have now been attempted, including cases
initially skipped when a failed reset interrupted a batch. The version-pinned archives are ready
for the later modlist task. Compatibility acceptance stays open until the ordinary reload and
death failures are resolved or explicitly accepted; Windows, separate-account Steam and the
player package still need their own acceptance. No dependency-specific compatibility workaround
was justified by this pre-fix pass, and no merge into `dev` was performed. The subsequent
investigation fixes our VM re-entry bug, described below.

## Post-fix combined profile (2026-10-29)

`ca906283` plus the frozen-VM guard, all six pinned mods, supplied settings and the Display Tweaks
60 FPS cap: **23/27 pass, no crashes**. The cases include the union of the earlier technical and
USSEP suites, the new save regression, and the guest's Beast Form/reversion.

Host load, quickload, wipe/checkpoint recovery, character retention, both transformations, shouts,
menus, crafting, combat, quest stages/completion and the player-stage scene pass. The four failed
checks are: a furniture animation pose after the story scenario, the known guest-only vampirism
marker, the host-death copy's ragdoll pose, and the guest's body pose after revival/reset. Keep
these failures visible; this result is not a blanket clean compatibility claim.

The death-pose failures require fresh launches before continuing the cases initially skipped by
the reset. The final live games report zero frozen, saving/loading or overlapping plugin updates.
Evidence: `postfix-combined-ussep-matrix-scenarios{,-continued-1,-continued-2}.log`,
`postfix-combined-ussep-{host,client}-trace.json`, and the version/hash manifest
`postfix-combined-ussep-profile.json`. The tested DLL/PDB is retained in
`build/reload-fix-acceptance-binary/`. No memory guards or debugger were enabled.

After removing temporary allocator experiments from the source, the full gate passes again.
Three additional Engine Fixes cycles complete both players' transformations without a crash;
the third host load restores play but fails a native AI/save lock-order audit, stopping the
fourth planned cycle. The final comparison without extra fix mods passes 5/5 (save regression,
both transformations, host load and quickload). Main-menu joining restores the character but
still fails the known animated-object world comparison. Frozen-world and overlapping-update
counters remain zero. These failures stay visible; no extra lock allowlist entry was added.
See [the full investigation](re/crashes/2026-10-29-save-vm-reentry.md#change-and-validation) for
the stacks, evidence paths and limits. The shared duo is released; nothing is merged into `dev`.

## Final object-pose follow-up

The [next investigation](re/crashes/2026-10-29-lock-history-and-button-pose.md) corrects mixed
reader/writer provenance in the lock checker, then passes six Engine Fixes cycles (30/30).
It also fixes the long-standing join pose: graph restoration updated local bones but left stale
world transforms. A full-save snapshot experiment failed and was reverted; the final change
publishes the restored transforms and does not skip equal graph rows.

The final DLL, all six pinned mods, stock settings and Display Tweaks capped at 60 FPS complete
**14/15, no crashes**. Save/load/quickload, host and guest transformations, the full main-menu
join, puzzle rings, both players' furniture animations, ability retention, effects on a later
copy, guest location story events, potion retention across rejoin and checkpoint recovery pass.
Vampirism retention/rejoin completes, but its postcheck finds movement-record flags on three
clutter references (`32AFF`, `32AFE`, `32B00`, base `319E5`) only on the guest. That failed check is
deferred past v1 ([bugs.md](bugs.md#plugin-compatibility), 2026-10-29). A separate earlier standalone join passed the corrected pose assertion but failed
because arrival displaced the host about 50 units; its planned repetitions were skipped.

Both final games report zero frozen/saving/loading updates and zero update overlaps. Evidence:
`graph-pose-combined-ussep-{profile.json,matrix-scenarios.log,host-trace.json,client-trace.json}`;
matching DLL/PDB: `build/graph-pose-final-binary/`. This focused run does not replace the earlier
27-case result or clear every retained death/marker failure. Windows, separate-account Steam,
3–4-player play and the player package remain unverified. Games are stopped, profiles restored
and the duo released. No merge into `dev`.

## Merged-build investigation on `2f38f777`

The isolated `v1-mod-profiles` follow-up verifies all seven documented archive hashes and
passes the initial full build/fast gate (563 cases, 6,087 assertions). Its initial matrix
uses `2f38f777` production source with the verified follower/door-autosave harness handoff.
[Run details and evidence](reports/2026-10-29-v1-mod-profiles.md).

The first Scrambled Bugs save/load gate is **0 pass / 1 fail / 2 not run**. All five loads
and subsequent frame/liveness checks complete without a crash or freeze, but the final
Riverwood NPC animation/scene comparison fails. Both games’ frozen/saving/loading/update
overlap counters remain zero. Original reports, settings, hashes, active masters and saves
are retained under `build/profiles/20261029-153928-scrambled/` in the new worktree.
This is not a passing gate or an established Scrambled Bugs conflict. The no-extra-mod
control also fails AI-state checks; a separate no-mod door-only control reproduces Cow/Frodnar
animation mismatches. The specific stale scene field remains unresolved.

The separately recorded USSEP, historical all-six and final-playtest gates each finish
**0 pass / 1 fail / 2 not run**. USSEP fails on Lydia's ordinary/combat-override package
after quickload. Both all-six profiles complete all five loads, then fail Riverwood NPC
animation comparisons. No new crash, freeze or overlapping plugin update is observed.
The other agent's proposed follower package-notification fix remains unverified and was
reverted: its test failed at combat setup. Its later standard-bootstrap diagnostic did not
reproduce the package write; no verified follower fix was imported. The user requested this
Lydia issue fixed for v1. All failed gates remain retained. The
original-DLL combined acceptance reaches **40 pass / 5 fail / 110 not run**, including the
retained failed final-playtest gate. The six-mod, 60-FPS configuration remains a candidate.

B6 follow-up: Bug Fixes SSE 10 completes **10/10** primary training repetitions, **2/2**
detailed gold probes and **2/2** purse/barter checks. The same-DLL no-extra-mod primary
control is **6 pass / 1 inventory-lock diagnostic fail / 3 not run**; two detailed training
probes and purse/barter checks pass there too. Both profiles’ detailed probes show shared
gold 140 → 2,140 → 1,890 (one 250-gold payment). The historical gold setup failure is
unreproduced, with no compatibility workaround added. The new precise baseline inventory
lock pair remains a failed diagnostic, retained and symbolized without an allowlist change.

B9 follow-up: cap 60 totals **9 pass / 2 fail / 9 not run** (one body/floor postcheck,
one charge-frame timeout during stalls). Cap 120 totals **13 pass / 1 fail / 6 not run**;
the walking timeout lands under a native `FarmTable01` reference, with controls enabled
and native Forward input dispatched. Movement remains blocked there and resumes after
placement on clear ground. A fresh no-Display control also fails a body-height postcheck;
no dependency-specific conflict is established. The nominal 120 cap measures only
**55–76 updates/s**, so sustained 120 FPS remains untested. Full original-state, collision,
body/input and measured-rate evidence is linked in the [run report](reports/2026-10-29-v1-mod-profiles.md).

The fresh combined-profile vampire retention/rejoin passes. Immediately afterward, every
host reference change has a row and the live worlds match. The requested save-file comparison
cannot complete because the guest correctly refuses saving; that diagnostic limitation is
recorded separately. This standalone pass does not erase the historical B8 failure.

The next combined bootstrap exposes an actual early console-save hang. The
[save-phase investigation](re/crashes/2026-10-29-synchronous-save-phase.md) establishes a
native VM frozen-lock inversion: a synchronous save runs from the console queue after the
VM worker starts. The fix defers device-2 saves to the existing frame boundary and preserves
synchronous internal join saves. Its fast regression passes 4/4 (15 assertions) after failing
under direct dispatch; full `scripts/check.sh` passes **567 cases / 6,102 assertions**, all
comparisons/UI checks, the Windows DLL and launcher.

Replacement DLL SHA-256: `28ed9f11e7a62287b2aa37620d2744a4c846986878e37838afea28c432bc0bff`.
Both early activation saves and **7/7** explicit native save/load/menu/retention/rejoin checks
pass. Four fresh affected profile gates subsequently complete startup saves, but each remains
**0 pass / 1 fail / 2 not run** on separate fade, animation/body or Lydia-package checks. The
historical all-six and final-playtest configurations are recorded as separate launches even
though their dependency lists match. The [run report](reports/2026-10-29-v1-mod-profiles.md)
links their original evidence. Final acceptance on this exact DLL completes **131 pass /
24 fail / 0 not run**: ordinary cases **131/22/0**, both separately launched late scenes
**0/2/0**. Earlier results and original failures remain separate history. No final profile
is accepted. Four ordinary regressions pass again after the late-baseline harness correction.

The candidate uses Engine Fixes **7.0.20** with preloader **7**, powerofthree's Tweaks
**1.17.1 AE1170**, Scrambled Bugs **21**, Bug Fixes SSE **10**, SSE Display Tweaks **0.5.25**
and USSEP **4.3.8a**, with supplied configurations except `FramerateLimit=60`.
Display Tweaks retains `EnableVSync=true`, `VSyncPresentInterval=1`, `EnableTearing=false`.
The merged first-time-join assets are enabled (`SkyrimCoopStart.esp` in both actual saved-game
headers); the bundled start asset hash, complete active masters and all installed settings/file
hashes are recorded in every profile manifest and snapshot. Skyrim is **1.6.1170**, SKSE
**2.2.6**, Address Library is the **1.6.1170** data file, Crash Logger **1.25.0**.
These versions/settings describe the tested candidate; its remaining failures have no new
acceptance from this batch.

The explicit vampire retention/rejoin, guest leave, container extra-data, corpse loot, Take All,
tempered items, potion persistence, guest ingredient knowledge, both enchantment-knowledge
cases and `guest-story-starts-no-quest` pass. Host ingredient behavior completes but its
cover-plate pose check fails. Guest quit and host quit also retain world/cleanup failures.
The Elder Scroll’s mirrored movement completes, then fails copied-equipment checks. The
Greybeards case loads `LateAfterDragonRising`, stages the host in High Hrothgar and times out
waiting for its host scene to start; guest scene behavior remains unverified. Existing late-save
fixture failures are corrected with nine fast regressions while all native state assertions remain.

A fresh comparison without extra fix mods passes **5/5** (native reach audit, persuade
achievement routing, critter spawn, shared highlighting and party dialogue options). With all
six mods still installed, changing only Engine Fixes `[Patches] bEnableAchievementsWithMods`
to **false** passes the audit and persuade case **2/2**. Its patch removes the vanilla player
read used by the co-op native classifier; this compatibility interaction is confirmed by the
controlled setting comparison. Turning off that achievement-enabling feature is a narrow
workaround, not a completed classifier fix or verified Steam-award support. The full matrix
with that setting change is **not run**. Critter, topic-list and equipment profile differences
remain unresolved; no individual dependency is blamed without an isolating comparison.

Final evidence (worktree removed 2026-10-30, not kept): `build/acceptance-{plan,summary}.json`,
`acceptance-ledger.ndjson`, `comparison-ledger.ndjson`, `profile-index.json` (58 manifests),
`final-check.log` and the linked report. Bootstrap errors, original/outer reports, adjacent logs,
settings, active masters and DLL/PDB hashes remain retained. All owned games are stopped,
profiles/settings/orders/control directories restored, the waiting entry removed and duo released.
All changes remain uncommitted, with no merge into `dev`; `scripts/net_check.sh` was not run.
Windows runtime, separate-account Steam, larger parties, packaging and sustained 120 FPS remain
outside this batch’s acceptance claims.
