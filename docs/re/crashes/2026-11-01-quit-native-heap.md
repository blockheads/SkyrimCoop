# C3: a registered Papyrus native freed through the wrong heap

The 2026-10-31 Fabien playtest's quit crash reproduces on Linux/Proton with dev `c0aa4403`.
A fresh solo game at the main menu, selecting Quit and confirming it through native keyboard input,
crashed at 2026-11-01 10:48:52. No session or partner was needed. The game was stopped and the shared duo
released before offline regression work.

Evidence in `.worktrees/c3-quit/build/c3-red-native/`: `crash-2026-11-01-10-48-52.log`, `tp_client.log`,
`symbolized.txt`, the SKSE logs and final status. The exact DLL/PDB used are retained beside those logs.

## Cause

The symbolized stack is:

```
rpfree
allocator/AllocatorSymbolOverride.cpp: Free
operator delete(void*, size_t)
BSScript::DidLaunchSkyrimTogetherFunc: scalar deleting destructor
ObjectTypeInfo::ReleaseData (104323)
VMClassInfo::Destroy (104299)
VirtualMachine / SkyrimVM teardown
```

`HookBindEverythingToScript` correctly constructs the native with `Memory::New`, because the VM requires a
game-heap allocation. On teardown the VM releases the last `IFunction` reference and invokes its virtual deleting
destructor. Without a class-specific deallocation function, C++ selects the DLL's global `operator delete`, which
uses rpmalloc. That heap cannot free a Skyrim heap block.

This is independent of World/service lifetime and network shutdown. The `HookVMDestructor` breadcrumb preceding
it is the player's destruction, not the object being incorrectly freed.

The existing native member destructor address is correct: SKSE `PapyrusNativeFunctions.h` uses AE RVA
`0x143E330` for both `NativeFunction` and `NativeFunctionBase::Impl_dtor`. Address Library ID 104655 maps there
in 1.6.1170. The unpacked executable at `build/re/SkyrimSE_1.6.1170_unpacked.exe` shows it releasing the doc string,
parameter table, state, object and function strings and returning, without freeing `this`. Disassembly is in
`build/c3-native-dtor.asm`. C++ destruction must still free the object once, with `Memory::Free`.

[CommonLibSSE-NG's NativeFunctionBase](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/include/RE/N/NativeFunctionBase.h)
also models a virtual destructor and those owned members. No engine hook or vtable slot changes are needed.

## Regression boundary

The native function factory, class, getters, callback and C++ virtual deletion compile into GameTests. The fake
supplies only engine construction/destruction, the string pool and allocation entry points. Each fake VM registers
the real launch native and releases it through `IFunction*`, as Skyrim did. Its game-heap ledger records whether
release returned to the allocating heap; the backing storage uses the process heap so a failed assertion can name
the mismatch instead of corrupting the test process.

The supporting source moves make the native type header usable by the Linux tests. The Windows VM hash-map
layout checks remain enabled for the MSVC ABI; its multiple-empty-base layout differs on ELF.

## Validation

- Native RED: the fresh main-menu quit described above, before any fix.
- GameTests RED: both `[quit-desktop]` branches fail with `GameHeapFrees == 0`, expected 1;
  `build/c3-smoke-red.log`.
- Unit RED: two independently registered natives both return to the wrong heap;
  `build/c3-units-red.log`.
- After the class-specific deallocator: `[native-lifetime]` and `[quit-desktop]` each pass all 11 assertions.
- `scripts/check.sh`: 665 C++ cases / 6,704 assertions, Python harness/comparisons, UI fixtures, Windows plugin
  and launcher pass. Final gate log: `build/c3-check-final.log` (36 s). The harness now has 24 lifecycle cases,
  including a quit recipe that must fail on a crash even if the process already exited.

The native recipes are `coop_test.py repro quit-main-menu` and `repro quit-in-game` (fresh solo launches).
Final native acceptance PASS: both fresh solo launches quit without a new crash report, with no manual input.
The main-menu run took 13.5 s including launch, the in-game run 15.9 s. Evidence: `build/c3-native-green.log`
and `build/coop-test/runs/20261101-1113*-quit-main-menu/`, `20261101-1114*-quit-in-game/`.
Each deployed DLL was verified against SHA-256
`65168c0de12fc8840ee90f165a42b2b4e4cf05887329285b1cea0610a199d8f9`.
Own prefix processes were verified empty and the duo released immediately afterwards.
The later Windows OS check remains in the playtest plan.

### Harness observations retained

Earlier acceptance attempts remain in `build/c3-native-attempt1.log` through `attempt5.log`; none is counted
as the final acceptance. Attempt 1 failed in `XAudio2_7.dll` at startup before the plugin heartbeat, while a
same-size newer C1 binary was still deployed. The pre-existing `copy_if_changed` size/mtime shortcut had skipped
our build; subsequent runs explicitly deployed and checked its hash. No deployment implementation changes are
part of this fix.

The remaining failures exposed input-oracle problems in the new recipes: waiting for all Wine helpers rather
than the captured Skyrim PID, pressing confirmation before the main-menu animation reached `MainConfirm`,
opening the journal before the HUD took input, and sending only the mouse press of the Yes click. The final
recipes wait for those native states and send both mouse events; the terminal release can close the command
socket before its reply. Attempt 4's main-menu run had a diagnostic manual confirmation and is excluded.
The final native pair had no intervention. The main-menu SWF sets `MAIN_CONFIRM_STATE` at frame 23; its
`MainConfirmStartAnim` label begins at frame 16 (`Skyrim - Interface.bsa`, `interface/startmenu.swf`).



## Review and integration (2026-11-01)

The user authorized review and merge into `dev`. Current `dev` (`9b47b97c`, including C1 and Windows profiling)
was integrated into the isolated C3 worktree before validation. The allocator fix, native member cleanup,
MSVC layout checks and source moves were reviewed; no remaining production defect was found in the C3 change.

Review findings corrected in the solo recipes:

- A `/proc` existence check could report success immediately on Windows while Skyrim still ran. Exit checks now
  use each platform's process adapter; Windows checks the recorded process identity, including PID reuse.
- A refused launch still entered cleanup, potentially stopping a game the recipe had not started. Both games must
  be stopped before prerequisites/deployment, and a live game is refused without cleanup.
- Windows defers assigning shared crash logs to an instance for two seconds. A solo recipe also checks new raw
  CrashLogger files after exit, before cleanup removes the process record, so an unclassified crash cannot pass.
- Cleanup ignored a failed stop. Success is now reported only after cleanup succeeds; a prior failure stays first.
- Windows harness windows are 960x540, Linux 1280x720. Quit-menu cursor positions use the configured dimensions.

Four new regression cases failed before these fixes. The harness suite now has 30 cases, including direct Linux
and Windows process-adapter checks. Logs: `build/c3-review-harness-red.log`, `build/c3-review-harness-green.log`.

The merged default profiler build also exposed an inherited xmake Tracy CMake-package issue: its manifest
exports the feature flags but omits `TRACY_ENABLE`, disabling the API declarations required by `Profile.cpp`.
`Code/client/xmake.lua` now supplies that define when profiling is enabled. The existing package library contains
the matching startup/shutdown symbols; no profiler implementation or package-cache changes were made.

Final integration validation:

- `scripts/check.sh` passes with the default profiler enabled: 668 C++ cases / 6,728 assertions, 30 harness
  cases, the comparison/baseline checks, UI fixtures, Windows DLL and launcher. Log:
  `.worktrees/c3-quit/build/c3-review-check-final.log` (48 s).
- The final terminal-input adjustment also passes all 30 harness cases. It accepts a missing reply only for the
  last input event, then still requires Skyrim to exit and checks for a new crash report.
- Both fresh solo quit paths pass on the combined build without intervention, including cleanup: 17.3 s from
  the main menu, 18.1 s from in-game. Log: `.worktrees/c3-quit/build/c3-review-native.log`.
- Deployed DLL SHA-256 verified before both launches:
  `5ff6854cbae944c56a21cbfc3f0f96d7352ad92b991b41ab9ec4342300267d32`.
- Own games stopped, both prefixes verified empty, duo released. No outstanding C3 review finding.

Native OS validation here is Linux/Proton. The Windows process adapter is covered by unit tests and the Windows
DLL is cross-compiled; the playtest plan's later Windows OS check is still required.
