# Deletion survey

Step 1 of the cleanup ([cleanup-plan.md](cleanup-plan.md)): what nothing uses, how that was shown, and what went.
Surveyed 2026-11-26 on `agent/cleanup` (94ea6cfad). Verdicts: **deleted**, **keep** (why), **owner** (needs a
decision; all went to the owner and are settled in the second pass below).

## How "unused" was shown

```
C++, plugin side:   every object rebuilt with /Gy (one COMDAT per function), relinked with
                    /OPT:REF /OPT:NOICF /VERBOSE, test harness and Tracy on (scripts/xmake-msvc.sh f)
                      -> "Discarded <symbol>": nothing in the DLL reaches it
                      -> kept only symbols defined in exactly one of our objects (not inline copies)
                      -> each name grepped over Code/ (Linux tests, 1.6.1170 branches, comments)
                    static libraries: members the link never loads ("Loaded <lib>(<obj>)")
C++, headers:       includes resolved by path tail; a header only its own .cpp includes
C++, structs:       names used outside their file (mostly engine layout: nested types kept)
messages:           per opcode, a constructing sender and a handler (On<>, Relay<>, sink<>,
                    const T&) in Code/sync, engine, plugin, harness
scripts:            ruff F401/F811/F841, vulture --min-confidence 60, each name grepped over the
                    repo (Code, scripts, docs, .claude, CLAUDE.md, AGENTS.md); registries checked
                    (registrations.py, pkgutil loaders, @tool, Session verbs by name, mock.patch targets)
```

Hooks were counted before and after (non-comment lines): `HOOK_ID` 284, `HOOK_ID_IMMEDIATE` 4, `HOOK` 2,
`MH_CreateHook` 2, unchanged. `HOOK_IAT` 2 -> 1 and `HookVTable` 19 -> 18 are `D3D11Hook::Install` and the
vtable hook only it installed; nothing calls `Install` (the linker discarded it). `Hooks::Initializer` 101 -> 99:
the two whose only hook line was commented out (`TESNPC.cpp`, `LoadingScreen.cpp`). Papyrus natives: 4
registration lines before and after. `docs/re/ENGINE_REFERENCE.md` regenerated: 291 -> 288 hooks, the three
never-installed ones (14375 `HookSetLeveledNpc`, 37698 `HookGetPerkRank`, 51925 `RequestLoadingText`).

## Deleted

| Kind | Commit | Lines |
| --- | --- | --- |
| Repo leftovers | Cleanup: repo leftovers nothing uses | -362 (+2), plus 6 binary files |
| Scripts | Cleanup: scripts nothing uses | -393 |
| C++ dead code | Cleanup: C++ nothing references | -1420 (+15) in code; the regenerated reference -280 +225 |
| Encoding structs | Cleanup: encoding structs nothing uses | -168 |

### Repo leftovers

| Path | Lines | Evidence |
| --- | --- | --- |
| root `xmake.lua` `before_build` writing `build/BranchInfo.h` | 26 | `grep -rn BranchInfo\|IS_MASTER Code scripts`: no includer |
| `modules/version.lua` | 39 | only that `before_build` imported it |
| `GameFiles/Skyrim/SkyrimTogether.esp` | binary | not in `scripts/package.sh`'s file list, not deployed by `coop_harness/runtime.py` (only `SkyrimCoopStart.esp`), named nowhere |
| `GameFiles/Skyrim/scripts/*.pex`, `scripts/source/*.psc` (SkyrimTogether*, PetFramework*) | 258 + 4 binaries | same: never packaged or deployed; the Verify/PlayerAlias scripts are attached by that `.esp` only |
| `GameFiles/Skyrim/meshes/Interface/logo/*.nif` | 2 binaries | named nowhere, never packaged |
| `.githooks/commit-msg` | 10 | `core.hooksPath` is `scripts/hooks` |
| `NOTICE` LGPL paragraph for `code/launcher` | 6 | no such directory; no CitizenFX-derived file found (`grep -rni citizen\|fivem Code`) |
| root `.clang-format` | 21 | no C++ file outside `Code/`, which has its own `Code/.clang-format` |

`docs/re/systems/main-loop-and-papyrus-vm.md` now says the `DidLaunchSkyrimTogether` native's script is no
longer bundled.

### Scripts

| What | Lines | Evidence |
| --- | --- | --- |
| `scripts/spikes/player_scan.py` | 254 | its name appears only in itself; its research note is gone |
| 119 unused imports, 2 duplicate imports (ruff F401, F811) | 121 | none is a `mock.patch` / `patch.object` target (`time` stays in the modules tests patch) |
| `flows/flow.py` `quest_completed` + `QUEST_RUNNING, QUEST_COMPLETED` | 14 | no caller, flows included |
| `common.py` `JOIN_BASE_FLAGS`, `ONMUND_NOT_A_NORD_LINE`, `KEYFRAMED_MOTION` | 3 | only users were unused imports |

All 11 `scripts/test_*.py`, `check_scenarios.py`, `check_re.py` pass; every `coop_harness` module imports
(except `windows.py`, which needs `_winapi`, as before).

### C++

Linker-discarded, then grepped:

- `engine` (56 in place): `Actor` (`GetSpeed`, `SetSpeed`, `DispelAllSpells`, `StartCombatEx`, `SetCombatTargetEx`,
  `StartCombat`, `GetEquippedAmmo`, `SetPlayerTeammate`, `PlayIdle`, `PickUpObject`), `TESObjectREFR` (`Enable`,
  `Disable`, `GetCellId`, `IsItemInInventory`, `PlayAnimation`, `PlayAnimationAndWait`), `TESQuest` (`getState`,
  `GetObjective`, `CompleteAllObjectives`, `Kill`, `EnsureQuestStarted`), `PlayerControls::SetEnabled`,
  `SetCamSwitch`, `BSInputEnableManager::EnableOtherEvent`, `CombatController::UpdateTarget` and its sort,
  `ExtraContainerChanges` `Save`/`Load`/`IsQuestObject`, `ActorValueInfo::Resolve`,
  `TESObjectCELL::GetCOCPlacementInfo`, `IAnimationGraphManagerHolder::SetVariableFloat`, `IMenu::SetFlag`,
  `UI::DebugLogAllMenus`, `EffectItem::IsHealingEffect`/`IsInivisibilityEffect`, `MagicCaster::CastSpell`,
  `MagicTarget::DispelAllSpells`, `MenuTopicManager::Append`, `ModManager::GetCellFromCoordinates`,
  `PlayerCharacter::SaidRecently`/`RememberSaid`, `Rtti::IsForm`, `SubtitleManager::HideSubtitle`,
  `BSRandom::UnsignedInt`, `PlayerCamera::WorldPtToScreenPt3`, `DInputHook` `Acquire`/`Unacquire`/`Update`/
  `SetEnabled`, `D3D11Hook::Install` and its two hook functions, the hook bodies `HookCharacterDestructor`,
  `HookGetPerkRank`, `UIMessageQueue__AddMessage` (their `HOOK` lines were commented out).
- Whole files: `AnimationExperiments.cpp` (429; 11 "re-" copies of engine paths, "do not use"; its live
  `hkEventContext` constructor moved to `Havok/hkEventContext.cpp`), `LoadingScreen.cpp` (55),
  `Renderer.h/.cpp` (73), `DefaultStringManager.h/.cpp` (24), `Components/TESLeveledList.h` (34, a second
  `TESLeveledList` no file includes), `tests/encoding.h` (3, declares a function defined nowhere).
- `TESNPC.cpp`'s never-installed `HookSetLeveledNpc` and its initializer.
- harness: `SendGraphEvent(BShkbAnimationGraph*, ...)`; plugin: `ImguiService::WndProcHandler`, `RawInputHandler`.
- `sync`: `AnimationSystem::Clean`, `InterpolationSystem::Clean`, `CharacterService::DeleteTempActor`,
  `HostSaveTransfer::IsSending`, `SaveLibrary::Clear`, `BodyService::Shows` (no caller in the plugin or any test).
- `BehaviorVar.cpp`'s `#ifdef HAS_IMMERSIVE_LAUNCHER` branches (defined nowhere).

A second /Gy pass after the deletions found nothing new beyond the "keep" and "owner" items below.

### Encoding

| What | Lines | Evidence |
| --- | --- | --- |
| `Structs/LockData.h/.cpp` | 47 | the link never loads `LockData.cpp.obj` from `SkyrimEncoding.lib`; `IFormBridge.h` included it and used nothing |
| `Structs/Vector2_NetQuantize.h/.cpp` + its TPTests section | 99 + 19 | only its own round trip used it; also never loaded by the link |

**Messages and opcodes: none removed.** Every entry in `Opcodes.h` has a constructing sender and a handler; the
12 notifications with no explicit sender (`NotifyActorValueChanges`, `NotifyGraphSync`, ...) are `Relayed<>` and
built by `HostRouter`'s `Relay`. Nothing had one side only. Because nothing was removed from the opcode list,
the numbering question did not arise. Checked anyway: `scripts/coop_harness` names no opcode (only
`source_tags.py` has the word, as a path), and no JSON fixture under `scenarios/`, `Code/tests` or the UI fixtures
has an `op`/`opcode` key.

## Keep

| What | Lines | Why |
| --- | --- | --- |
| 65 `Structs/Skyrim/AnimationGraphDescriptor_*` | 4174 | all 32 descriptors are registered in `AnimationGraphDescriptorManager.cpp` and looked up by graph hash (`TESObjectREFR.cpp` `DescriptorFor`), read by `Save/LoadAnimationVariables`, which `CharacterServiceSpawn` and the bridges call. Which creature still needs one is a runtime question |
| `Code/ui_models`, `Code/ui_workbench`, `Code/vanilla_harness` | | linked into the plugin / built and run by `check.sh` / `coop_test.py repro --vanilla` |
| 18 debug views | | all drawn from `DebugService::OnDraw` |
| `BSCRC32::GenerateCRC`, `BSScript::IObjectHandlePolicy::Get` | | called from header templates (`BSTScatterTable`, `GetObjectForHandle`) no one instantiates today; deleting breaks the first use |
| Engine nested types and event structs (`EventDispatcher.h`, `BSGraphicsRenderer.h`, ...) | | engine layout: members that keep offsets |
| `scripts/build_start_room.py`, `re/behavior_dump.py`, `net_check.sh`, `analyze_green_flash.py` | | generate a shipped `.esp` / documented RE tool / documented manual check / `green_pixels` imported by `scenarios/physics_green.py` |

## Second pass: the owner's items (owner: delete them all)

Done 2026-11-26 on `agent/cleanup-delete-2`, one commit per item. Lines are the commit's deletions.

| What | Lines | Verdict |
| --- | --- | --- |
| Skyrim 1.6.1170 support | -611 +207 (the +207 is mostly the regenerated reference's line numbers) | **deleted**: the `skyrim_version` option and `COOP_SKYRIM_1_6_1170` with every branch (1.7.104 side kept), the 1.6 config/build dirs in `xmake-msvc.sh`, `package.sh`, `check.sh`, `coop_harness/common.py`, `COOP_SKYRIM_VERSION`, `scripts/lock_order_known.txt`, the Address Library v2 (1.6 format) readers in `AddressLibrary.h` and `address_library.py`, the 1.6 branches of `build_re_data.py`, `check_re.py`, `symbolize_crash.py`, and the docs (`CLAUDE.md`, `docs/re/README.md`, `runtime-versions.md`, `testing.md`). Kept: `docs/re/ida_notes.tsv` whole (`build_re_data.py` loads it for 1.7.104, marked "From 1.6.1170"; `check_re.py` no longer ID-checks it, its library is not installed); `fetch_references.sh`'s powerof3 CommonLibSSE (cited by RE docs); comments that cite 1.6.1170 as where an address was read |
| `GameFiles/Skyrim/SkyrimTogetherRebornBehaviors/` | -5029 (199 files) | **deleted** with `BehaviorVar`, `BehaviorOrig*`, `BehaviorVarsMap` and `README-ANIMATION-MODS.md`: nothing installs the folder and no doc of ours tells players to add one (the README is upstream STR's, never packaged). Two paths that worked without the folder stay, inline: the player's first-person graph falls back to the humanoid descriptor (`TESObjectREFR.cpp` `DescriptorFor`), and `Actor::IsDragon` compares with `BHR_Master`'s key |
| `DidLaunchSkyrimTogether` native | -236 | **deleted** with `HookBindEverythingToScript` (55739, only bound it), `FakeNativeVm` and its two tests (they tested only its lifetime) |
| `D3D11Hook` signals | -263 | **deleted** with `Hooks/Signal.h` (its only user), `RenderSystemD3D11`'s connections and `OnReset`, and the empty `ImguiService::Reset`. `RenderSystemD3D11` stays: `BSGraphicsRenderer.cpp`'s hooks create and render it |
| `DInputHook::IsEnabled` | -29 | **deleted** with its branches and `Get()` |
| `World` getters | -43 | **deleted** (all seven, after the id-index merge) |
| `BodyPlayback::RestComing` | -21 | **deleted** with its test |
| `CopyRecords::WaitsFor`, `ControlHolds::HeldBy` | | **keep**: readouts in tests that check other behaviour (applied parts; cutscene and party holds, 6 cases) |
| `scripts/spikes/`, `re/graph_compare.py`, `re/graph_snapshot.py`, `release.sh` | -1691 | **deleted**; no doc tells anyone to run `release.sh`. The harness `player_probe` stays |
| Harness verbs no code calls | -16 | rechecked: 31 (not 33) have no caller in `scripts/`, `Code/`, `docs/`, `.claude/`. **Deleted** 2, duplicates: `knows_word` (`word_state`'s first half), `observe_handles` (the MCP `observe` tool). **Keep** 29, readouts and repro moves an agent calls by name, none broken (every `ctx.` attribute resolves, every harness command exists): `alpha_trace`, `body_watch`, `bought_horse_ride`, `collision_apart`, `collision_baselines`, `collision_check_alone`, `collision_restless`, `collision_suite`, `combat_targets_of`, `console_watch`, `crime_watch`, `graph_machines_watch`, `hands_compared`, `heap_quarantine`, `host_death_watch`, `knock_to_rest`, `loose_sword_rows`, `make_angry`, `mount_timeline`, `open_inventory`, `player_effects`, `quit_to_main_menu` (used in a 2026-11-22 `bugs.md` investigation), `sight_watch`, `speak_calls`, `swing_at_moving`, `talk_again_as_menu_closes`, `talk_twice`, `trap_watch`, `world_projectiles` |
| ruff F841 / F541, unused inline methods | | **open**: not in the owner's list for this pass |

Hooks, base `agent/cleanup` -> this pass (non-comment lines in `Code/engine`, `plugin`, `harness`): `HOOK_ID` 288 -> 286
(the 1.6-only `52488` line, never compiled for 1.7.104, and `55739`); `HOOK_ID_IMMEDIATE` 5, `HOOK` 3, `HOOK_IAT` 2,
`HookVTable` 1, `MH_CreateHook` 8, `Hooks::Initializer` 99, unchanged; `BindNativeMethod` 2 -> 1 (the call; the
declaration stays). The regenerated `ENGINE_REFERENCE.md`: 288 -> 287 hooks.

## Test doubles (report only)

Same-named helpers in more than one test folder: `Plugins` (15 files, `games/` and `host_authority/`), `Join`
(3), `ControlsCall`, `Fortify`, `Has`, `Outdoors`, `ValueOf` (2 each). Small; a shared helper header would
merge them, not a deletion.

## References to removed design notes

264 files under `Code/` and `scripts/` name a note removed on 2026-11-26 (`docs/design/*` other than
glossary, repro-mcp and testing; `docs/research`, `docs/reports`): 359 references. Left as they are
(cleanup plan, step 8).
