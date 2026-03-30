---
phase: 01-abi-safety-and-source-cleanup
plan: 01
subsystem: client
tags: [abi, alignment, exceptions, no-exceptions, mingw, toolchain]
dependency_graph:
  requires: []
  provides: [abi-safe-headers, no-exceptions-client, toolchain-note]
  affects: [Code/client/TiltedOnlinePCH.h, Code/client/Games/Skyrim/SaveLoad.h, Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h, Code/client/Services/HostService.cpp, Code/client/CrashHandler.cpp]
tech_stack:
  added: []
  patterns: [alignas-over-declspec, no-exceptions-policy, explicit-error-codes]
key_files:
  created: []
  modified:
    - Code/client/TiltedOnlinePCH.h
    - Code/client/Games/Skyrim/SaveLoad.h
    - Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h
    - Code/client/Services/HostService.cpp
    - Code/client/CrashHandler.cpp
decisions:
  - "alignas(N) replaces __declspec(align(N)) across all game headers in Code/client/ — C++11 standard syntax compiles on both MSVC and MinGW"
  - "HostService::StartHosting uses IsListening() return-value check instead of try/catch — no exception to catch from non-throwing Win32/ENet API"
  - "CrashHandler minidump path runs without try/catch — Win32 C APIs don't throw, existing hDumpFile null guard is sufficient"
  - "-flayout-compat=ms TODO replaced with factual note: GCC 10 doesn't support the flag, tail-padding differences accepted for now"
metrics:
  duration: "~5 minutes"
  completed: "2026-03-30"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 5
---

# Phase 01 Plan 01: ABI Safety Fixes and Source Cleanup Summary

Replace `__declspec(align(N))` with `alignas(N)` in three game headers, remove all try/catch blocks from Code/client/ using explicit error-code returns, and replace the stale GCC 12+ TODO with a factual note about the installed GCC 10 toolchain.

## Tasks Completed

| # | Task | Commit | Files |
|---|------|--------|-------|
| 1 | Replace __declspec(align) with alignas, update toolchain note | e0bfbf2f | TiltedOnlinePCH.h, SaveLoad.h, BSGraphicsRenderer.h |
| 2 | Remove all try/catch blocks from Code/client/ | 6357fb92 | HostService.cpp, CrashHandler.cpp |

## What Was Done

**Task 1 — ABI Alignment and Toolchain Note:**
- `Code/client/Games/Skyrim/SaveLoad.h`: Replaced `__declspec(align(8))` with `alignas(8)` on `BGSSaveLoadScrapBuffer` (line 145) and `BGSLoadFormData` (line 165)
- `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h`: Replaced `__declspec(align(8))` with `alignas(8)` on `BSCriticalSection` (line 7)
- `Code/client/TiltedOnlinePCH.h`: Replaced the single-line TODO `// TODO: Upgrade to GCC 12+ and use -flayout-compat=ms for proper MSVC ABI compat.` with a 3-line factual note documenting that `-flayout-compat=ms` is not available on GCC 10, and that `-mms-bitfields` handles bitfield layout
- `SKYRIM_STRUCT_ASSERT` suppression block left completely unchanged (per D-01)

**Task 2 — No-Exceptions Policy:**
- `Code/client/Services/HostService.cpp`: Removed `try { ... } catch (const std::exception& e)` wrapper from `StartHosting()`. Added explicit `if (!m_pGameServer->IsListening())` check after `Initialize()` with `spdlog::error`, `m_pGameServer.reset()`, `m_pConsole.reset()`, and `return false` — mirrors what the catch block did but without exceptions
- `Code/client/CrashHandler.cpp`: Removed `try { ... } catch (...) // Mini-dump is best effort only.` wrapper from the minidump section of `VectoredExceptionHandler`. Dedented body by one level. Preserved `HANDLE hDumpFile = NULL` declaration and the existing `if (!hDumpFile)` null check as the error guard. Win32 C APIs (`CreateFileA`, `MiniDumpWriteDump`) do not throw C++ exceptions

## Verification Results

All 5 plan-level checks passed:

1. `grep -rn '__declspec(align' Code/client/` — 0 matches (exit 1)
2. `grep -rn '\btry\b' Code/client/ --include='*.cpp' --include='*.h' | grep -v '//'` — 0 matches (exit 1)
3. `grep -rn '\bcatch\b' Code/client/ --include='*.cpp' --include='*.h' | grep -v '//'` — 0 matches (exit 1)
4. `grep -c 'alignas(8)' Code/client/Games/Skyrim/SaveLoad.h` — 2 matches
5. `grep -c 'alignas(8)' Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h` — 1 match

## Deviations from Plan

None - plan executed exactly as written.

## Known Stubs

None.

## Self-Check: PASSED

Files exist and commits verified:
- `Code/client/TiltedOnlinePCH.h` — exists, contains "flayout-compat=ms (GCC 12+) is not available"
- `Code/client/Games/Skyrim/SaveLoad.h` — exists, contains 2x `alignas(8)`
- `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h` — exists, contains `alignas(8) BSCriticalSection`
- `Code/client/Services/HostService.cpp` — exists, contains `IsListening()` check
- `Code/client/CrashHandler.cpp` — exists, contains `MiniDumpWriteDump`, no try/catch
- Commit e0bfbf2f — Task 1
- Commit 6357fb92 — Task 2
