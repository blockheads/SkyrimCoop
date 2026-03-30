# Phase 1: ABI Safety and Source Cleanup - Context

**Gathered:** 2026-03-30
**Status:** Ready for planning

<domain>
## Phase Boundary

Fix all known ABI hazards in the client codebase and remove v1.0 relay code before any compilation attempt. This is pure cleanup — no new features, no architecture changes. The codebase should be ready for Phase 2 (full client compilation under MinGW) when this phase completes.

</domain>

<decisions>
## Implementation Decisions

### `-flayout-compat=ms` Strategy
- **D-01:** Test the flag empirically with `x86_64-w64-mingw32-g++ -flayout-compat=ms -x c++ /dev/null -fsyntax-only`. If supported, add it to xmake.lua and re-enable `SKYRIM_STRUCT_ASSERT` macros for full ABI confidence.
- **D-02:** No CI/build-time check for GCC version compatibility — let it error naturally if someone uses an older GCC without the flag.

### `__declspec(align)` Replacement
- **D-03:** Replace all `__declspec(align(N))` uses in game headers with `alignas(N)`. Known locations: `Code/client/Games/Skyrim/SaveLoad.h` (2 uses), `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h` (1 use). Leave `Code/external/DirectXTK/` untouched (vendored dependency).

### try/catch Cleanup
- **D-04:** Replace ALL try/catch sites in Code/client/ — consistent no-exceptions policy across the entire client codebase. Known sites: `HostService.cpp`, `CrashHandler.cpp`, `TESObjectREFR.cpp`.
- **D-05:** For error-code replacement style, follow existing code patterns in the surrounding code. No strong preference on single-chain vs individual checks.

### Claude's Discretion
- Error-code replacement pattern (single bool chain vs individual checks) — match whatever pattern is established in surrounding code at each site.

### Relay Code Deletion
- **D-06:** Delete `Code/relay_dll/` entirely — remove directory and all xmake references. Git history preserves the code.
- **D-07:** Keep `Code/native_client/` in the source tree as dead code — remove it from the xmake build system only (no compilation). Phase 3 will reference `native_client/transport/ServerMessageRouter.cpp` and other transport code directly when porting LoopbackTransport. Defer full deletion of `native_client/` to Phase 3.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### ABI and Struct Layout
- `Code/client/TiltedOnlinePCH.h` — Contains `SKYRIM_STRUCT_ASSERT` macro and `-flayout-compat=ms` TODO (lines 22-30)
- `Code/client/Games/Skyrim/SaveLoad.h` — Contains `__declspec(align(8))` uses to replace (lines 145, 165)
- `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h` — Contains `__declspec(align(8))` use to replace (line 7)

### try/catch Sites
- `Code/client/Services/HostService.cpp` — try/catch around StartHosting() (line 62)
- `Code/client/CrashHandler.cpp` — try/catch around crash reporting init
- `Code/client/Games/Skyrim/TESObjectREFR.cpp` — try/catch around inventory iteration (line 681 area)

### Code to Remove/Modify
- `Code/relay_dll/` — Entire directory to delete (7 files)
- `Code/native_client/xmake.lua` — Remove from build system (keep directory as dead code)

### Build System
- Top-level `xmake.lua` or relevant build configs — Must remove relay_dll target and native_client target references

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `Code/native_client/transport/ServerMessageRouter.cpp` — Will be ported in Phase 3 for LoopbackTransport
- `Code/native_client/transport/` — Contains transport abstractions useful for Phase 3 reference

### Established Patterns
- `-mms-bitfields` already set in MinGW build config — `-flayout-compat=ms` supplements this
- `MinGWCompat.h` and `link_stubs.cpp` provide existing MSVC ABI compatibility layers
- Code follows no-exceptions style per CODE_GUIDELINES.md — try/catch sites are anomalies

### Integration Points
- `xmake.lua` build targets — relay_dll and native_client targets must be removed/modified
- `TiltedOnlinePCH.h` — Precompiled header affects all client compilation units

</code_context>

<specifics>
## Specific Ideas

No specific requirements — standard mechanical cleanup following codebase conventions.

</specifics>

<deferred>
## Deferred Ideas

- **Delete `Code/native_client/` directory** — Deferred to Phase 3 after transport code has been ported to LoopbackTransport

</deferred>

---

*Phase: 01-abi-safety-and-source-cleanup*
*Context gathered: 2026-03-30*
