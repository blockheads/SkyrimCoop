---
phase: 03-client-dll-cross-compilation
verified: 2026-03-27T23:50:00Z
status: human_needed
score: 1/3 must-haves verified (2/3 artifact-ready; runtime unverified)
re_verification:
  previous_status: gaps_found
  previous_score: 1/3
  gaps_closed:
    - "DLL linking gap: SkyrimTogetherClient.dll (PE32+ 12MB) now exists in build tree"
    - "Smoke test DLL auto-detection: finds DLL in build tree without manual DLL_PATH override"
    - "SKSE entry points: SKSEPlugin_Load and SKSEPlugin_Version exported from DLL"
  gaps_remaining:
    - "BUILD-05: DLL loads into Skyrim SE under Proton without crashing — requires human runtime verification"
    - "Character sync: blocked until Truth 2 is human-verified"
  regressions: []
human_verification:
  - test: "Run tests/smoke_test_client.sh — it should auto-detect the DLL and report PE32+ OK, then deploy and launch Skyrim"
    expected: "Script reports PE32+ format OK, deploys DLL to SKSE plugins dir, launches Skyrim SE under Proton, and within 90s the client log shows plugin loaded. Game does not crash within 30s of reaching the main menu."
    why_human: "Requires Skyrim SE + SKSE installed under a Proton prefix. Cannot be verified programmatically without the full game runtime."
  - test: "With Skyrim loaded and local server running, verify connection establishment"
    expected: "Client log shows OnConnected or 'connected to server' and server log shows 'player connected' or 'new connection'. Basic character position visible on both clients."
    why_human: "Requires two Skyrim instances (or one + a headless server) and live network traffic. No automated equivalent exists."
---

# Phase 3: Client DLL Cross-Compilation Verification Report

**Phase Goal:** MinGW produces a SkyrimTogetherClient.dll that loads into Skyrim SE under Proton and connects to a server
**Verified:** 2026-03-27T23:50:00Z
**Status:** human_needed
**Re-verification:** Yes — after gap closure (Plan 04 closed DLL linking gap from initial verification)

## Re-verification Summary

The initial verification (2026-03-27T22:00:00Z) found status `gaps_found` with score 1/3: the static library compiled but no DLL artifact existed. Plan 04 closed that gap. This re-verification confirms the gap closure and identifies remaining blockers.

| Gap from Previous Verification | Resolved? | Evidence |
|---|---|---|
| No `.dll` artifact (only `.a`) | YES | `build/mingw/x86_64/releasedbg/SkyrimTogetherClient.dll` 12MB PE32+ |
| Smoke test auto-detection broken | YES | `find build/ -name 'SkyrimTogetherClient.dll' -not -path '*/cache/*'` returns valid path |
| No SKSE entry points exported | YES | `objdump -p` shows `SKSEPlugin_Load` and `SKSEPlugin_Version` in export table |

---

## Goal Achievement

### Observable Truths (from ROADMAP.md Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | The client DLL compiles under MinGW with CEF and DirectXTK excluded (stubbed or replaced) | VERIFIED | `libSkyrimTogetherClient.a` (1.5GB, 136 objects) built under MinGW GCC 10; `SkyrimTogetherClient.dll` (12MB PE32+) produced by `SkyrimTogetherClientDLL` target; HAS_CEF/HAS_DISCORD/HAS_DIRECTXTK guards confirmed; zero mimalloc references in client source |
| 2 | The MinGW-compiled DLL loads into Skyrim SE via SKSE under Proton without crashing | ARTIFACT-READY / UNVERIFIED | `SkyrimTogetherClient.dll` is valid PE32+ with correct SKSE exports. Smoke test auto-detects it. Runtime load in Skyrim under Proton has NOT been executed — requires human. |
| 3 | A client using the MinGW-built DLL can connect to a locally running server and see basic character sync | UNVERIFIED | Blocked until Truth 2 is confirmed. Requires live game runtime + running server. |

**Score:** 1/3 truths verified programmatically. 2/3 are artifact-ready; 2 require human runtime verification.

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `Code/client/xmake.lua` | Feature-gated client build config; SkyrimTogetherClientDLL shared target | VERIFIED | HAS_CEF=1/HAS_DISCORD=1/HAS_DIRECTXTK=1 in `not is_plat("mingw")` block; `SkyrimTogetherClientDLL` target with `set_kind("shared")`, `set_basename("SkyrimTogetherClient")`, `before_link` stale-.dll.a cleanup |
| `Code/client/skse_entry.cpp` | SKSE plugin entry points bridging to RunTiltedInit/RunTiltedApp | VERIFIED | Exports `SKSEPlugin_Version` and `SKSEPlugin_Load` via `__attribute__((dllexport))`; `DllMain` saves HINSTANCE; `extern RunTiltedInit/RunTiltedApp` declared; confirmed in main.cpp |
| `Code/client/link_stubs.cpp` | Linker stubs for runtime-resolved Skyrim engine symbols and MSVC intrinsics | VERIFIED | File exists; guarded `#ifndef __MINGW32__`; stubs for ActorValueOwner, IAnimationGraphManagerHolder vtables; MSVC UCRT intrinsics (`__intrinsic_setjmpex`, etc.) |
| `build/mingw/x86_64/releasedbg/SkyrimTogetherClient.dll` | MinGW-compiled client DLL, PE32+ | VERIFIED | File exists (12MB); `file` reports `PE32+ executable (DLL) (console) x86-64, for MS Windows`; objdump confirms `SKSEPlugin_Load` and `SKSEPlugin_Version` exports |
| `tests/smoke_test_client.sh` | Smoke test with DLL auto-detection, PE32+ check, log-polling | VERIFIED | 200 lines; executable; `set -euo pipefail`; PE32+ check; log-polling loop; TIMEOUT variable; auto-detection finds DLL via `find build/ -name 'SkyrimTogetherClient.dll' -not -path '*/cache/*'` |
| `Code/client/MinGWCompat.h` | Calling convention macro management | VERIFIED | Undefs `__fastcall`, `__stdcall`, `__cdecl` under `__GNUC__ && __x86_64__` |
| `.toolchain/bin/` | Wrapper scripts for posix-threaded MinGW | VERIFIED | 7 wrapper scripts: `x86_64-w64-mingw32-{ar,g++,gcc,ld,ranlib,strip,windres}` |
| `Code/client/Games/Memory.cpp` | rpmalloc-based memory hooks, no mimalloc | VERIFIED | `RpmallocAllocator`, `rpmemalign(aAlignment, aSize)`, `_initterm_e` guarded by `#ifdef _MSC_VER`; zero mimalloc references |
| `Code/client/Services/OverlayService.h` | No-op stub under `#else HAS_CEF` | VERIFIED | `#ifdef HAS_CEF` on line 3; `#include <include/internal/cef_ptr.h>` inside guard |
| `Code/client/Services/DiscordService.h` | No-op stub under `#else HAS_DISCORD` | VERIFIED | `#ifdef HAS_DISCORD` on line 3; `#include <discord.h>` inside guard |
| `Code/client/Systems/RenderSystemD3D11.h` | Entire file wrapped in HAS_DIRECTXTK | VERIFIED | `#ifdef HAS_DIRECTXTK` at line 3 |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `Code/client/xmake.lua (SkyrimTogetherClientDLL)` | `Code/client/xmake.lua (SkyrimTogetherClient)` | `add_deps` + `before_link` stale-.dll.a cleanup | WIRED | `add_deps("SkyrimTogetherClient")` pulls in static lib; `before_link` removes stale `.dll.a` import libs that would shadow the `.a` archive |
| `Code/client/skse_entry.cpp` | `Code/client/main.cpp` | `extern void RunTiltedInit` / `extern void RunTiltedApp` declarations | WIRED | `skse_entry.cpp` declares `extern RunTiltedInit` and `extern RunTiltedApp`; `main.cpp` defines both (confirmed grep); linker resolves at link time via `--whole-archive` dep |
| `tests/smoke_test_client.sh` | `build/.../SkyrimTogetherClient.dll` | `find` command auto-detection | WIRED | `find "$PROJECT_ROOT/build" -name 'SkyrimTogetherClient.dll' -not -path '*/cache/*'` returns `build/mingw/x86_64/releasedbg/SkyrimTogetherClient.dll` |
| `Code/client/World.cpp` | `Code/client/Services/OverlayService.h` | `ctx().emplace<OverlayService>` compiles with stub | WIRED | OverlayService emplace unconditional; stub header always provides valid struct definition |
| `Code/client/TiltedOnlineApp.cpp` | `Code/client/Systems/RenderSystemD3D11.h` | RenderSystemD3D11 creation guarded by HAS_DIRECTXTK | WIRED | 5 HAS_DIRECTXTK guards confirmed in TiltedOnlineApp.cpp |

---

### Data-Flow Trace (Level 4)

Not applicable. This phase produces C++ compilation artifacts (DLL, static library, build scripts), not data-rendering components.

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| DLL is valid PE32+ x86-64 | `file build/.../SkyrimTogetherClient.dll` | `PE32+ executable (DLL) (console) x86-64, for MS Windows` | PASS |
| DLL exports SKSE entry points | `objdump -p ... \| grep SKSEPlugin` | `SKSEPlugin_Load`, `SKSEPlugin_Version` in export table | PASS |
| DLL is substantive (12MB, not stub) | `ls -lh ...dll` | 12MB — contains all client code from 1.5GB static lib via dep linking | PASS |
| Smoke test DLL auto-detection | `find build/ -name 'SkyrimTogetherClient.dll' -not -path '*/cache/*'` | Returns valid path | PASS |
| No mimalloc in client source | `grep -rl mimalloc Code/client/ \| wc -l` | 0 | PASS |
| HAS_CEF guard in xmake.lua | `grep -c "HAS_CEF" Code/client/xmake.lua` | 1 | PASS |
| CEF include inside guard | `head -5 OverlayService.h` | `#ifdef HAS_CEF` then `#include <cef_ptr.h>` | PASS |
| Discord include inside guard | `head -5 DiscordService.h` | `#ifdef HAS_DISCORD` then `#include <discord.h>` | PASS |
| RunTiltedInit defined in main.cpp | `grep "RunTiltedInit" Code/client/main.cpp` | Line 36: function definition | PASS |
| DLL loads in Skyrim under Proton | Run `tests/smoke_test_client.sh` with Skyrim installed | Not runnable without game runtime | SKIP — requires human |
| Server connection established | Observe logs with running server | Not testable without live runtime | SKIP — requires human |

---

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| BUILD-04 | 03-01, 03-02, 03-03 | Tier 3 client DLL compiles under MinGW (CEF/DirectXTK excluded, replaced by ImGui) | SATISFIED | `libSkyrimTogetherClient.a` (1.5GB, 136 objects); `SkyrimTogetherClient.dll` (12MB PE32+); all feature guards in place; REQUIREMENTS.md marks `[x]` |
| BUILD-05 | 03-03, 03-04 | MinGW-compiled DLL loads into Skyrim SE under Proton without crash | NEEDS HUMAN | DLL artifact exists with correct SKSE exports. Runtime load not yet verified. REQUIREMENTS.md marks `[ ]` pending. Smoke test infrastructure ready. |

**Orphaned requirements check:** No additional Phase 3 requirements in REQUIREMENTS.md beyond BUILD-04 and BUILD-05. No orphans.

---

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h` | 3 | `#include <d3d11.h>` with no HAS_DIRECTXTK guard in the header itself | Info | Build succeeds because MinGW-w64 provides d3d11.h natively. Inconsistent with the guarding pattern but not a blocker. |
| `tests/smoke_test_client.sh` | 55 | Error message says `xmake build SkyrimTogetherClient` (static target) instead of `SkyrimTogetherClientDLL` (DLL target) | Info | Confusing to user if DLL not found; correct command should be `xmake build SkyrimTogetherClientDLL` |

No blocker anti-patterns found. The stale-import-library issue from Plan 04 is handled by the `before_link` cleanup hook.

---

### Human Verification Required

#### 1. DLL Load in Skyrim SE Under Proton

**Test:** Run `tests/smoke_test_client.sh` from the project root. No arguments needed — it auto-detects the DLL.
**Expected:**
- Step 1: Script reports `PE32+ x86-64 OK`
- Step 2: DLL deployed to `$SKYRIM_DIR/Data/SKSE/Plugins/SkyrimTogetherClient.dll`
- Step 4: Skyrim SE launches under Proton without error
- Within 90s: Client log at `Data/SKSE/Plugins/SkyrimTogether.log` contains a plugin-loaded message
- Game remains stable for at least 30 seconds after reaching the main menu (no crash)
**Why human:** Requires Skyrim SE + SKSE 2.2+ installed under a Proton prefix. The smoke test automates deployment and log-polling but cannot execute without the game runtime.

#### 2. Server Connection and Basic Character Sync

**Test:** With Skyrim loaded via the DLL: start a local `SkyrimTogetherServer`, connect from the in-game menu, open a second client, and observe that both clients appear in the game world.
**Expected:** Client log shows `connected to server` or `OnConnected`. Server log shows `player connected`. A second player character is visible in-game.
**Why human:** Requires two game instances (or one client + a server process) running concurrently with a real Skyrim session. No headless equivalent exists for this phase.

---

### Gaps Summary

**Gap closure confirmed:** Plan 04 successfully closed the DLL linking gap identified in the initial verification. The `SkyrimTogetherClient.dll` (12MB PE32+ x86-64) is a real artifact containing all 136 compiled translation units from `libSkyrimTogetherClient.a`, linked via the `SkyrimTogetherClientDLL` shared target. The SKSE entry points (`SKSEPlugin_Load`, `SKSEPlugin_Version`) are exported and correctly bridge to `RunTiltedInit`/`RunTiltedApp` in `main.cpp`.

**Remaining work:** BUILD-05 (DLL loads in Skyrim under Proton) requires a human to execute the smoke test against a real Skyrim installation. All automated preconditions are met:

- DLL artifact: exists, correct format, correct exports
- Smoke test: auto-detects DLL, handles PE32+ check, deploys to SKSE plugins, polls logs for connection
- SKSE entry point wiring: correct (skse_entry.cpp → RunTiltedInit/RunTiltedApp → main.cpp)
- Link stubs: Skyrim engine vtables and MSVC intrinsics stubbed for runtime resolution

The DLL may require additional runtime fixes once loaded (Skyrim ABI edge cases, address library resolution, Wine-specific behaviors) but these cannot be identified without attempting the load. The smoke test is the correct next step.

---

_Verified: 2026-03-27T23:50:00Z_
_Verifier: Claude (gsd-verifier)_
