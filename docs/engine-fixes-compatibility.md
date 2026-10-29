# SSE Engine Fixes compatibility

Pre-fix `ca906283` results were **12/15**, with a host-load crash and two failed death checks.
The save-time VM re-entry race is now fixed; the full post-fix mod profile runs **23/27**, without
a crash, with four remaining world/pose failures.
See [the complete fix-mod matrix](mod-compatibility.md) and the current-build section below.
The original profile and runs below use `978a1bcd`.

Latest follow-up: corrected lock diagnostics pass **30/30** repeated Engine Fixes checks.
After fixing object world transforms on restoration, the final focused six-mod run is **14/15**,
without crashes; vampire retention/rejoin completes but three clutter movement-record flags
differ. The full main-menu join, potion rejoin, furniture and puzzle poses now pass. See
[the final matrix follow-up](mod-compatibility.md#final-object-pose-follow-up).

The v1 check targets Steam Skyrim 1.6.1170 with SKSE 2.2.6. Test the ordinary co-op flows before
recommending this dependency. This is separate from the later playtest modlist/package.

## Test profile

- Our committed baseline: `978a1bcd`, built in `.worktrees/v1-engine-fixes`. Other agents' uncommitted
  ability and menu changes in `dev` are not part of this initial comparison.
- [Engine Fixes 7.0.20](https://github.com/aers/EngineFixesSkyrim64/releases/tag/7.0.20), AE DLL and
  unmodified supplied configuration. All six memory replacements and `bDoublePerkApply` remain enabled.
  Archive SHA-256: `21330c95011f41859139635b43ce95ffb5fbadd3cd375d2f4358abcc3cb99407` (matches GitHub's digest).
- Author-published `d3dx9_42.dll` from the [6.0.2 Part 2 archive](https://github.com/aers/EngineFixesSkyrim64/releases/tag/6.0.2).
  Only the preloader is installed; 7.0.20 contains its allocator and does not need that archive's older TBB DLLs.
  Archive SHA-256: `84ece4027a93b993894ce9e3b03040e4262310afbf1c199d5c5391eb606520ee`.
  The later full matrix downloaded Nexus preloader 7 and confirmed its DLL is byte-for-byte
  identical; see [the archive hashes](mod-compatibility.md#acquisition-and-configuration).
- Proton 10.0, isolated game directory and prefixes; `WINEDLLOVERRIDES=d3dx9_42=n,b` selects the native preloader.
  Existing Address Library and Crash Logger are retained. The normal Steam install is not modified.

For a manual reproduction, extract the installer archive's `Required/SKSE/Plugins` and
`AE/SKSE/Plugins` files into the isolated game's `Data/SKSE/Plugins`, and put the preloader beside
`SkyrimSE.exe`. From this worktree, use the existing runner with the isolated game directory:

```bash
export SKYRIM_GAME_DIR="$(realpath build/compat-game)"
export COOP_DISPLAY=:99
export COOP_CLIENT_USER=
export WINEDLLOVERRIDES=d3dx9_42=n,b
python3 scripts/coop_test.py duo --keep-running
python3 scripts/coop_test.py scenario late-join-client host-loads-party host-beast-form-stays-host death-wipe-checkpoint fus-ro-dah-host-hits-guest fus-ro-dah-guest-hits-host
python3 scripts/coop_test.py stop
```

Reserve the shared duo before launching, and release it immediately after stopping. The baseline
comparison removes the preloader and all `EngineFixes*` files before a fresh launch. Keep the same
DLL and saved-game prerequisites for both profiles; the absence of a launch error is not activation proof.

## Source findings

Engine Fixes preloads before SKSE loads our plugin. It replaces the game's allocation, reallocation and
free functions (68115/68116/68117). Our allocation hook must still expand actors and construct their
extensions, then forward into that replacement; our normal frees resolve the game's replaced free.
The old launcher-only rehook is not evidence that this SKSE loading path needs another rehook.

Two other shared functions have patches away from our entry detours: the water-flow patch changes
36564 at +0x26B, and the optional sleep/wait speed patch changes 52486 at +0x1D0. The latter is
inactive at its supplied `fSleepWaitTimeModifier=1.0`. Keep the water-flow patch enabled for this check.

The double-perk fix replaces actors' virtual base-perk application with queued work. Our copy perks
are written to their unique temporary NPC bases on the main loop. Verify actor recreation, save/load
and character retention rather than assuming these paths compose correctly from source alone.

## Native results (2026-10-28)

The isolated baseline passes `scripts/check.sh`: 538 cases / 5,761 assertions, UI fixtures and the
Windows plugin build. This does not exercise Engine Fixes inside Skyrim.

**Not cleared yet.** Both games' logs confirmed Engine Fixes 7.0.20 preloading, TBB allocation,
global/Havok memory replacement, CRT import replacement and the double-perk patch. No settings
were disabled to obtain these results. No production compatibility change has been established.

| Run | Result |
| --- | --- |
| Without Engine Fixes, six baseline scenarios | Five pass; host-to-guest Fus Ro Dah times out on the victim walking after getting up. |
| Engine Fixes, full run on the usual `:99` / `:100` displays | 15/16 pass. Host walking after revival times out; the remaining scenarios pass. |
| Focused baseline without Engine Fixes | Wipe/checkpoint reload, host death/revival and host-to-guest Fus Ro Dah all pass. |
| Focused Engine Fixes follow-up | Host exits with an access violation during the wipe/checkpoint sequence; guest remains running. The scenario times out waiting for restoration. |
| Engine Fixes with a debugger attached | Three successive wipe/checkpoint scenarios pass; no exception is caught. |
| Engine Fixes with temporary raw exception capture | Twelve successive wipe/checkpoint scenarios pass; no exception is caught. |

The 15 passing full-run scenarios cover leave/rejoin, host load and quickload, guest-character save
retention, werewolf transformation/reversion, both Fus Ro Dah directions, guest death/revival,
party-wipe recovery, shared waiting/sleeping, melee and projectiles against NPCs and friendly fire.

An earlier Engine Fixes run also exposed a test limitation: the revival scenario compares every
active effect with its pre-death list. A Firebolt effect from the preceding combat scenario disappeared
during revival and was incorrectly reported as a missing ability. The subsequent full run puts death
before projectile combat; that false failure must not be described as a lost permanent ability.

The unexpected host exit was logged at 23:36:10: `c0000005`, address `0x6ffff9502e04`. Crash Logger
produced no new report. The host had exited before attachment; the retained debugger stack is the
guest's, not the faulting host's. The fault's module, originating operation and relationship to the
historical save corruption are unproven. Subsequent passes do not establish a fix.

The walking timeouts have not been attributed to Engine Fixes; a walking timeout also occurred in
the baseline without it. Keep them visible rather than assuming either a control bug or blocked geometry.

Evidence is in this worktree's ignored `build/` directory: `engine-fixes-profile.json`,
`baseline-scenarios.log`, `engine-fixes-default-scenarios.log`, `baseline-revival.log`,
`engine-revival.log`, `engine-original-access-violation.log`, `fault-capture-scenarios.log`,
`raw-capture-scenarios.log` and `engine-fixes-{host,client}.log`. The capture-only change to
`CrashHandler.cpp` was removed and the normal DLL rebuilt. The duo was stopped and released.

Windows and the eventual `test_harness=n` package still need their own checks. The later individual
and combined mod profiles are recorded in [the full matrix](mod-compatibility.md).
A passing compatibility run does not establish the cause or resolution of the historical intermittent
save corruption or werewolf graph crash.

The initial runs above predate the full mod matrix. Keep their intermittent failures visible when
assessing the current-build results; a later passing run does not establish their cause.

## Current-build continuation (2026-10-29)

The full matrix uses `ca906283` with the same Engine Fixes 7.0.20 configuration and the verified
identical current preloader DLL. The initial current-build run completes ability retention and
leave/rejoin, then fails host-death combat tracking and cannot reset. The unrun cases continue on
fresh games; earlier failures are retained. Guest death also hits the known body/ragdoll pose
tolerance (three bones, maximum 17 units). See the [full matrix](mod-compatibility.md) for the
completed totals, isolated dependency results and combined profiles.

The combined technical profile is 18/20, while the profile also including USSEP 4.3.8a is 15/18.
The latter crashes the guest during host load through the same reference-cleanup call path seen
with Scrambled Bugs alone. Engine Fixes is active, including its allocator and double-perk patches;
it does not eliminate this failure. No production compatibility workaround was applied.

The completed current-build result is **12/15**. The host-load continuation captures a host crash
in our DLL, RVA `0xB1C84`, through `Net::Server::GetTick` / `TransportService::GetTick` /
`World::Update` from `HookVMUpdate`. It reads through invalid pointer `0x22797469746E65D4`.
The originating write remains unknown; this does not identify the module of the older
23:36:10 fault. The last fresh-launch wipe/checkpoint check passes. Current evidence is in
`engine-new-matrix-scenarios{,-initial,-continued-1,-continued-2}.log`.

## After the VM re-entry fix

The [save investigation](re/crashes/2026-10-29-save-vm-reentry.md) identifies a real race in our
VM hook: saving drains a frozen VM, and we ran a full world update inside that drain while
another update was active. The native regression fails after one save before the fix and
passes after guarding the correctly typed frozen field at `0x680`.

With Engine Fixes enabled and its supplied settings unchanged, six focused checks pass, including
load, quickload, transformation/reversion, checkpoint recovery and guest-character retention.
The combined profile with all six fix mods completes 23/27 without crashes; its remaining four
failures concern world/pose checks. Three additional cycles complete both players' transformations
without a crash, but the third host load fails a native AI/save lock-order audit after restoring
play. This exact lock variant remains open and was not allowlisted. A final comparison without
extra fix mods passes saving, both transformations, loading and quickloading (5/5).

The fix and counters remain in `.worktrees/v1-engine-fixes`, not merged into `dev`. No Engine
Fixes-specific workaround or allocator hook is retained. Historical intermittent corruption is
not proven to share this cause; failed body/world checks and the eventual Windows/player-package
acceptance remain open. Evidence paths and limits are in the linked investigation.
