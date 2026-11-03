# Steam 1.7.104 port acceptance

Implemented in `.worktrees/steam-runtime-audit`, branch `audit/latest-steam-runtime`, from `dev` base `eb913f95`.
The branch makes **1.7.104 the default**, with **1.6.1170 retained as a separate build**. No merge into `dev`, push,
Steam installation change or replacement of the old IDA database was performed.

[Build and RE usage](../docs/re/systems/runtime-versions.md) describes runtime selection, artifact staging and bindings.
[The historical assessment](../docs/reports/2026-11-02-steam-runtime-port.md) records upstream sources and initial findings.

## Changes

- Decode the exact runtime's Address Library (v5 for 1.7, v2 for 1.6), reject mismatches/truncations, and skip absent IDs.
- Select player +8 / VM +16 layouts at build time, including the position event source and perk-point store classifier.
- Replace the absent sleep/wait callback ID 52488 with verified 1.7 ID 524025. Move the BookMenu call patch from
  `51049+0x6F` to `+0x71`, checking its opcode and expected target before patching.
- Advertise and enforce exact runtime compatibility in the plugin, resident loader, vanilla harness and launcher.
- Isolate both build configurations, production packages and retained debug symbols. Stage the executable, SKSE,
  scripts, library and plugin by contents when switching versions; keep Steam's files read-only.
- Generate versioned engine references/annotations and select crash symbols by runtime. Reject IDA imports unless
  executable version and SHA-256 match. Preserve legacy note provenance.

## Fast and offline validation

`scripts/check.sh` passed: ClientTests **185 cases / 2,869 assertions**, HostAuthorityTests **198 / 2,173**,
TPTests **32 / 335**, GameTests **275 / 2,705**, plus harness, UI and Windows DLL/launcher checks.
Both runtime configurations also compiled the resident loader and vanilla harness, and built production packages
without the test harness. The later Python staging/lock-pattern changes passed **8 runtime tests** and **31 harness
tests**, with the runtime tests run under both selections; no C++ changed after the full gate.

The C++ decoder and independent Python reader agree on all **435,162** new-library mappings and **428,461** legacy
mappings. Offline disassembly checked the used interior patches; the BookMenu call was the sole shifted instruction
site among them. The three RTTI IDs missing on both versions remain unused, documented gaps.

Official downloads used, obtained through the user's existing Rust app Nexus credentials without recording the key:

| Artifact | Nexus mod / file | Archive SHA-256 |
|---|---|---|
| SKSE Steam 2.3.1 | 30379 / 795992 | `7bad616ed360823a027f8828801d91e3a4aa2eacd952023af7f3e31adb2ae250` |
| Address Library v13 | 32444 / 795954 | `be0c7c07fb63fd1c1403690d4797c7a8fdf8799118acceb605f285d7b0ee055f` |

## Native acceptance

Both versions booted through their matching SKSE, joined, showed each other's player and the host's spawned NPC.
The same bounded scenario batch then produced:

| Scenario | 1.7.104 | 1.6.1170 control |
|---|---|---|
| shared-wait-host / shared-wait-guest | PASS / PASS | PASS / PASS |
| shared-sleep-host / shared-sleep-guest | PASS / PASS | PASS / PASS |
| host-book-keeps-world-running / guest-book-keeps-world-running | PASS / PASS | PASS / PASS |
| player-equipment-host / player-equipment-guest | PASS / PASS | PASS / PASS |
| party-quick-travel | PASS | PASS |
| physics-push-reaches-guest | PASS | FAIL: playback max 11 units vs 10-unit tolerance |

The legacy physics failure had 0 flickers over 731 frames, typical error 0.5 units and final-rest error 0.4 units.
It remains a baseline physics test failure; this port does not change its threshold or claim to fix it.

The initial 1.7 sleep run completed the player flow but its comparison failed because the reviewed engine lock-pattern
list still contained 1.6 RVAs. All **45** unique return sites were mapped through stable IDs and checked against an
matching instruction sequence (at least 33 instructions) and the same lock-function callee in both executables. The
runtime now selects its own list. No new lock family was accepted, and a regression test confirms live overlaps still
fail. The subsequent 1.7 batch passed **10/10** with those existing reviewed signatures relocated.

Switching back to legacy also reproduced a staging failure: same-size newer 1.7 DLL bytes were retained by the old
mtime heuristic. The exact runtime guard refused the DLL before engine hooks. A Python deployment test failed on that
behavior, then passed after plugin and loader copies were changed to compare contents. The subsequent legacy pair
booted and completed the batch. This filesystem deployment fact belongs to the Python harness tests, not the fake
Skyrim engine in GameTests.

All owned games were stopped and their private prefixes verified empty before each release or offline fix/build.
Shared `CHAT.md` / `DUO.md` record each claim and release. Main-checkout games were never used or stopped.

## IDA and retained evidence

The new database is `build/re/SkyrimSE_1.7.104.i64` in the worktree, with matching unpacked input beside it.
Its successful import processed **53,352 annotations**: 40,617 names applied, 11,215 existing names kept, one function
created. The old database and its input remain intact. The new database's input path points into this worktree;
retain the worktree or update that input path if relocating it.

Private evidence is under `build/runtime-audit/`: `check.log`, `native-1.7-final.log`, `native-1.6.log`,
`native-1.7.log` (initial diagnostic), `native-1.6-staging-failure.log`, `lock-sites.json`, `patches.json`,
`plugin-runtime-check.json`, and the IDA analysis/import logs. Native screenshots, status and traces are in the private
`build/coop-test` results directory. Executables, vendor downloads, IDA databases and credentials are not committed.

This is targeted native acceptance, not a claim that every quest or third-party mod was tested on 1.7.
