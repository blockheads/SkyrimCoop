# V1 fix-mod compatibility

All shortlisted archives are acquired. The pre-fix matrix below reproduced load crashes. A
[save-time VM re-entry race](re/crashes/2026-10-29-save-vm-reentry.md) is now fixed in the worktree;
the post-fix combined run completes 27 cases without a crash (23 pass, four world/pose checks fail).
No dependency-specific workaround was applied.

Latest focused follow-up: corrected lock diagnostics pass six repeated Engine Fixes cycles
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
clutter references (`32AFF`, `32AFE`, `32B00`, base `319E5`) only on the guest. This remains a
failed check. A separate earlier standalone join passed the corrected pose assertion but failed
because arrival displaced the host about 50 units; its planned repetitions were skipped.

Both final games report zero frozen/saving/loading updates and zero update overlaps. Evidence:
`graph-pose-combined-ussep-{profile.json,matrix-scenarios.log,host-trace.json,client-trace.json}`;
matching DLL/PDB: `build/graph-pose-final-binary/`. This focused run does not replace the earlier
27-case result or clear every retained death/marker failure. Windows, separate-account Steam,
3–4-player play and the player package remain unverified. Games are stopped, profiles restored
and the duo released. No merge into `dev`.
