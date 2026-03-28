---
phase: 03-client-dll-cross-compilation
verified: 2026-03-28T03:30:00Z
status: human_needed
score: 1/3 must-haves verified programmatically (all artifacts verified; Truths 2-3 require runtime)
re_verification:
  previous_status: human_needed
  previous_score: 1/3
  gaps_closed:
    - "DLL load crash (0x3E6 ERROR_NOACCESS): removed --noinhibit-exec and added comprehensive stubs for all 20 unresolved Skyrim engine symbols — DLL now links cleanly without null function pointers"
    - "IAnimationGraphManagerHolder vtable: 16 sub_ entries stubbed (sub_3 through sub_F plus GetVariableFloat/Int/Bool)"
    - "BGSKeywordForm vtable: Contains and sub_5 stubbed"
    - "RipAllocateN: 1MB static fallback pool provided"
    - "g_SharedWindowIcon: global variable definition provided"
    - "_ReturnAddress: MSVC intrinsic mapped to __builtin_return_address(0)"
  gaps_remaining:
    - "Truth 2 (BUILD-05): DLL loads into Skyrim SE under Proton without crashing — root cause of previous crash is fixed, but runtime UAT has not been re-executed after Plan 05"
    - "Truth 3: Server connection + character sync — blocked until Truth 2 is confirmed"
  regressions: []
human_verification:
  - test: "Run tests/smoke_test_client.sh from project root (auto-detects DLL, no arguments needed)"
    expected: "Script reports PE32+ x86-64 OK, deploys DLL to Data/SKSE/Plugins/, Skyrim SE launches under Proton via SKSE, client log shows plugin loaded within 90s, game does not crash within 30s of main menu"
    why_human: "Requires Skyrim SE + SKSE 2.2+ installed under a Proton prefix. Cannot be verified programmatically without the full game runtime."
  - test: "With Skyrim loaded via the DLL: start a local SkyrimTogetherServer, connect from the in-game menu"
    expected: "Client log shows OnConnected or 'connected to server'. Server log shows 'player connected'. A second player character is visible in-game when a second client joins."
    why_human: "Requires two Skyrim instances (or one client + a headless server) and live network traffic. No automated equivalent exists."
---

# Phase 3: Client DLL Cross-Compilation Verification Report

**Phase Goal:** MinGW produces a SkyrimTogetherClient.dll that loads into Skyrim SE under Proton and connects to a server
**Verified:** 2026-03-28T03:30:00Z
**Status:** human_needed
**Re-verification:** Yes — after Plan 05 gap closure (removed --noinhibit-exec, added comprehensive linker stubs)

## Re-verification Summary

The previous verification (2026-03-27T23:50:00Z) had `status: human_needed` after Plan 04 produced the DLL artifact. The UAT (03-UAT.md) subsequently identified a blocker: DLL load crash 0x3E6 (ERROR_NOACCESS) caused by `--noinhibit-exec` filling unresolved symbols with null pointers that static constructors dereference. Plan 05 (commit `32491303`) closed that gap. This re-verification confirms Plan 05's changes are correct and no regressions were introduced.

| Gap from Previous UAT | Resolved? | Evidence |
|---|---|---|
| DLL crashes on load: 0x3E6 ERROR_NOACCESS | YES — root cause fixed | `--noinhibit-exec` removed from `xmake.lua`; 20 unresolved symbols now have real stubs in `link_stubs.cpp` (210 lines) |
| IAnimationGraphManagerHolder vtable incomplete | YES | 16 sub_ entries + GetVariableFloat/Int/Bool stubbed |
| BGSKeywordForm vtable missing | YES | Contains and sub_5 stubbed |
| RipAllocateN undefined | YES | 1MB static fallback pool |
| g_SharedWindowIcon undefined | YES | Global nullptr definition |
| _ReturnAddress MSVC intrinsic missing | YES | Mapped to `__builtin_return_address(0)` |
| Runtime DLL load in Skyrim | NOT YET | Requires re-run of UAT after Plan 05 changes |

---

## Goal Achievement

### Observable Truths (from ROADMAP.md Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | The client DLL compiles under MinGW with CEF and DirectXTK excluded (stubbed or replaced) | VERIFIED | `SkyrimTogetherClient.dll` 13MB PE32+ exists; `--noinhibit-exec` absent from `xmake.lua`; HAS_CEF/HAS_DISCORD/HAS_DIRECTXTK guards intact; DLL links cleanly with zero undefined references; no MinGW runtime DLL dependencies |
| 2 | The MinGW-compiled DLL loads into Skyrim SE via SKSE under Proton without crashing | ARTIFACT-READY — UNVERIFIED | DLL is valid PE32+ with correct SKSE exports and comprehensive linker stubs. Root cause of previous crash (0x3E6) is fixed. Runtime re-test in Skyrim under Proton has not been executed post-Plan 05. |
| 3 | A client using the MinGW-built DLL can connect to a locally running server and see basic character sync | UNVERIFIED | Blocked until Truth 2 is confirmed via human UAT. |

**Score:** 1/3 truths verified programmatically. 2/3 require human runtime verification.

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `Code/client/xmake.lua` | SkyrimTogetherClientDLL shared target, no --noinhibit-exec | VERIFIED | `SkyrimTogetherClientDLL` target exists at line 111; `add_shflags` contains `-static-libgcc`, `-static-libstdc++`, `--allow-multiple-definition` — no `--noinhibit-exec` anywhere in file (grep returns 0 matches) |
| `Code/client/link_stubs.cpp` | Comprehensive stubs for all unresolved Skyrim engine symbols and MSVC intrinsics | VERIFIED | 210 lines; stubs for ActorValueOwner (8 virtuals), IAnimationGraphManagerHolder (16 sub_ entries + 3 GetVariable methods), BGSKeywordForm (2 entries), RipAllocateN (1MB pool), g_SharedWindowIcon, _ReturnAddress, and MSVC UCRT intrinsics |
| `build/mingw/x86_64/releasedbg/SkyrimTogetherClient.dll` | Clean-linked MinGW client DLL | VERIFIED | 13MB; `file` reports `PE32+ executable (DLL) (console) x86-64, for MS Windows`; objdump confirms `SKSEPlugin_Load` and `SKSEPlugin_Version` exports; import table contains only Windows system DLLs (KERNEL32, msvcrt, WS2_32, etc.) — no libgcc, libstdc++, or libwinpthread |
| `Code/client/skse_entry.cpp` | SKSE entry points bridging to RunTiltedInit/RunTiltedApp | VERIFIED (from previous) | `SKSEPlugin_Version` and `SKSEPlugin_Load` exported; `extern void RunTiltedInit` and `extern void RunTiltedApp` declared; both called in `SKSEPlugin_Load` handler |
| `tests/smoke_test_client.sh` | Smoke test with DLL auto-detection and PE32+ check | VERIFIED (from previous) | 200 lines; `set -euo pipefail`; auto-detects DLL via `find build/ -name 'SkyrimTogetherClient.dll' -not -path '*/cache/*'` |
| `Code/client/MinGWCompat.h` | Calling convention macro management | VERIFIED (from previous) | Undefs `__fastcall`, `__stdcall`, `__cdecl` under MinGW |
| `.toolchain/bin/` | Wrapper scripts for posix-threaded MinGW | VERIFIED (from previous) | 7 wrapper scripts present |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `Code/client/xmake.lua (SkyrimTogetherClientDLL)` | `Code/client/link_stubs.cpp` | `add_files("skse_entry.cpp", "link_stubs.cpp")` line 117 | WIRED | link_stubs.cpp explicitly listed in DLL target's add_files call |
| `Code/client/xmake.lua (SkyrimTogetherClientDLL)` | `Code/client/xmake.lua (SkyrimTogetherClient)` | `add_deps("SkyrimTogetherClient")` | WIRED | Static client lib pulled in via dep; before_link removes stale .dll.a import libs |
| `Code/client/link_stubs.cpp` | Skyrim engine headers | `#include` + stub definitions matching mangled names | WIRED | Includes `ActorValueOwner.h`, `IAnimationGraphManagerHolder.h`, `BGSKeywordForm.h`; stubs match class virtual function signatures |
| `Code/client/skse_entry.cpp` | `Code/client/main.cpp` | `extern void RunTiltedInit` / `extern void RunTiltedApp` declarations resolved at link time | WIRED | skse_entry.cpp declares externs; main.cpp defines them (confirmed from previous verification) |

---

### Data-Flow Trace (Level 4)

Not applicable. This phase produces C++ compilation artifacts (DLL, static library, build scripts), not data-rendering components. No dynamic data flow to trace.

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| DLL is valid PE32+ x86-64 | `file build/.../SkyrimTogetherClient.dll` | `PE32+ executable (DLL) (console) x86-64, for MS Windows` | PASS |
| DLL exports SKSE entry points | `objdump -p ... \| grep SKSEPlugin` | `[0] SKSEPlugin_Load`, `[1] SKSEPlugin_Version` | PASS |
| DLL is substantive (13MB) | `ls -lh ...dll` | 13M (exceeds 10MB threshold) | PASS |
| No --noinhibit-exec in xmake.lua | `grep -c noinhibit-exec Code/client/xmake.lua` | 0 | PASS |
| No MinGW runtime DLL dependencies | `objdump -p ... \| grep "DLL Name"` | Only system DLLs: KERNEL32, msvcrt, WS2_32, GDI32, USER32, etc. | PASS |
| link_stubs.cpp included in DLL target | `grep link_stubs Code/client/xmake.lua` | Line 117: `add_files("skse_entry.cpp", "link_stubs.cpp")` | PASS |
| link_stubs.cpp is substantive | `wc -l Code/client/link_stubs.cpp` | 210 lines (20 symbols covered) | PASS |
| Plan 05 commit exists in git | `git show 32491303 --stat` | Commit confirmed: removes --noinhibit-exec, adds IAnimationGraphManagerHolder/BGSKeywordForm/RipAllocateN/g_SharedWindowIcon/_ReturnAddress stubs | PASS |
| DLL loads in Skyrim under Proton | Run `tests/smoke_test_client.sh` with Skyrim installed | Not runnable without game runtime | SKIP — requires human |
| Server connection established | Observe logs with running server | Not testable without live runtime | SKIP — requires human |

---

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| BUILD-04 | 03-01, 03-02, 03-03, 03-05 | Tier 3 client DLL compiles under MinGW (CEF/DirectXTK excluded, replaced by ImGui) | SATISFIED | `SkyrimTogetherClient.dll` 13MB PE32+; all feature guards in place; links cleanly without --noinhibit-exec; REQUIREMENTS.md marks `[x]` |
| BUILD-05 | 03-03, 03-04, 03-05 | MinGW-compiled DLL loads into Skyrim SE under Proton without crash | NEEDS HUMAN | DLL artifact valid with correct SKSE exports; crash root cause (null ptrs from --noinhibit-exec) fixed in Plan 05; runtime re-test pending; REQUIREMENTS.md marks `[x]` (optimistically, based on Plan 05 fix) |

**Orphaned requirements check:** Phase 3 in REQUIREMENTS.md maps only to BUILD-04 and BUILD-05. No orphaned requirements exist. REQUIREMENTS.md lines 106-107 confirm both are marked "Complete" in the tracker table.

---

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h` | 3 | `#include <d3d11.h>` without HAS_DIRECTXTK guard | Info | Build succeeds because MinGW-w64 provides d3d11.h natively. Inconsistent with the feature-guard pattern but not a blocker. |
| `tests/smoke_test_client.sh` | ~55 | Error message references `xmake build SkyrimTogetherClient` (static target) instead of `xmake build SkyrimTogetherClientDLL` (DLL target) | Info | Confusing to user if DLL not found; does not affect correctness of auto-detection path |

No blocker anti-patterns found. The null-pointer-at-load issue from Plan 04 has been resolved by Plan 05.

---

### Human Verification Required

#### 1. DLL Load in Skyrim SE Under Proton (BUILD-05 re-test)

**Test:** Run `tests/smoke_test_client.sh` from the project root. No arguments needed — it auto-detects the DLL in `build/mingw/x86_64/releasedbg/`.
**Expected:**
- Step 1: Script reports `PE32+ x86-64 OK`
- Step 2: DLL deployed to `$SKYRIM_DIR/Data/SKSE/Plugins/SkyrimTogetherClient.dll`
- Step 4: Skyrim SE launches under Proton via SKSE without an error dialog
- Within 90s: Client log at `Data/SKSE/Plugins/SkyrimTogether.log` contains a plugin-loaded message
- Game remains stable for at least 30 seconds after reaching the main menu (no 0x3E6 crash)

**Why human:** Requires Skyrim SE + SKSE 2.2+ installed under a Proton prefix. Plan 05 fixed the root cause of the previous 0x3E6 crash, but the fix must be confirmed against the actual game runtime — SKSE's address library patching and Wine ABI behavior cannot be simulated programmatically.

#### 2. Server Connection and Basic Character Sync

**Test:** With Skyrim loaded via the DLL: start a local `SkyrimTogetherServer`, connect from the in-game UI, open a second client, and observe that both clients appear in the game world.
**Expected:** Client log shows `connected to server` or `OnConnected`. Server log shows `player connected`. A second player character is visible in-game.
**Why human:** Requires two game instances (or one client + headless server) running concurrently with a real Skyrim session. No headless equivalent exists for this phase.

---

### Gaps Summary

**Plan 05 gap closure confirmed:** All automated preconditions for BUILD-05 now pass:

- `--noinhibit-exec` is absent from `Code/client/xmake.lua` (grep returns 0)
- `Code/client/link_stubs.cpp` (210 lines) provides stubs for all 20 previously unresolved symbols across 5 categories: ActorValueOwner, IAnimationGraphManagerHolder, BGSKeywordForm, globals (g_SharedWindowIcon), allocators (RipAllocateN), and MSVC intrinsics (_ReturnAddress, __intrinsic_setjmpex, etc.)
- DLL artifact is 13MB PE32+ x86-64 with correct SKSE exports and only system DLL dependencies
- `link_stubs.cpp` is wired into the DLL target at xmake.lua line 117

**Remaining work:** The UAT that identified the 0x3E6 crash (03-UAT.md test 5) must be re-run against the Plan 05 DLL. If the load succeeds, BUILD-05 and Truth 2 can be marked VERIFIED. Truth 3 (server connection) must be tested immediately after. The smoke test (`tests/smoke_test_client.sh`) automates deployment and log-polling and is the correct next step.

---

_Verified: 2026-03-28T03:30:00Z_
_Verifier: Claude (gsd-verifier)_
