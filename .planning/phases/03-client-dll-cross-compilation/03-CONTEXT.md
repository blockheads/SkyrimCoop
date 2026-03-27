# Phase 3: Client DLL Cross-Compilation - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

MinGW produces a SkyrimTogetherClient.dll that loads into Skyrim SE under Proton and connects to a locally running server. CEF, DirectXTK, and Discord SDK are excluded via compile-time guards (Phase 6 replaces CEF with ImGui). MinHook and Xbyak are cross-compiled as-is, with small patches if needed. Client-specific MSVC-isms are cleaned up using Phase 2's established patterns.

</domain>

<decisions>
## Implementation Decisions

### CEF/DirectXTK/Discord Exclusion
- **D-01:** Compile-time exclusion using per-feature preprocessor guards: `HAS_CEF`, `HAS_DISCORD`, `HAS_DIRECTXTK`. Not a single umbrella flag — each feature is independently toggleable.
- **D-02:** OverlayService, OverlayClient, RenderSystemD3D11, and DiscordService become no-ops when their respective guards are undefined. The DLL loads but has no UI overlay.
- **D-03:** SkyrimCoopUI and SkyrimCoopUIProcess targets are excluded from the MinGW build via XMake platform/config guards.
- **D-04:** ImGui (already vendored in `Code/external/imgui`) stays wired in the build for Phase 6 readiness.

### MinHook/Xbyak Cross-Compilation
- **D-05:** Cross-compile MinHook and Xbyak as-is first. Both have some MinGW support upstream. GATE-02 (Phase 1) already validated basic MinHook hooking under MinGW.
- **D-06:** If compilation fails, apply small targeted patches (ifdef guards, type fixes, asm syntax). If patches grow too large or complex, reconsider and evaluate alternatives (Detours for MinHook, AsmJit for Xbyak). No hard line count — use judgment on when "too large" means rethinking the approach.

### DLL Validation Scope
- **D-07:** Validation target is **Load + Connect**: DLL loads into Skyrim SE via SKSE under Proton, and establishes a connection to a locally running server. Full character sync is NOT required in Phase 3.
- **D-08:** A smoke test script is included in scope. The script launches Skyrim under Wine/Proton, spins up a local server, and verifies the client establishes a connection. Returns pass/fail. Catches ABI and linking issues before manual testing.
- **D-09:** Manual testing supplements the smoke test for edge cases. Automated testing infrastructure comes in Phase 5.

### Client MSVC-isms Cleanup
- **D-10:** Extend Phase 2 patterns to client code: portable C++20 where possible, `__attribute__` fallbacks, delete MSVC build blocks. Skyrim struct alignments use `alignas()` with `static_assert` for layout verification.
- **D-11:** Switch client from mimalloc to rpmalloc, consistent with Phase 2 decision. Memory.cpp updated to use rpmalloc APIs.
- **D-12:** Windows syslinks (version, dbghelp, kernel32) kept as-is — MinGW provides import libraries for all standard Windows DLLs. No changes needed.

### Claude's Discretion
- Specific MinHook/Xbyak patches if needed (case-by-case during implementation)
- Smoke test script implementation details (shell script vs Python, log parsing approach)
- Order of client source file cleanup (which files to tackle first)
- How to structure the per-feature guard macros in XMake configuration

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Build System
- `xmake.lua` — Root build config with package declarations and platform gates
- `Code/client/xmake.lua` — Client target build rules, dependencies (CEF, Discord, MinHook, etc.)
- `Code/libraries/xmake.lua` — Tier 3 library targets (SkyrimCoopReverse, SkyrimCoopHooks, SkyrimCoopUI)

### Client Code
- `Code/client/main.cpp` — SKSE plugin entry point
- `Code/client/Services/OverlayService.h` — CEF overlay integration (to be guarded)
- `Code/client/Services/OverlayClient.h` — CEF client (to be guarded)
- `Code/client/Systems/RenderSystemD3D11.cpp` — D3D11/DirectXTK rendering (to be guarded)
- `Code/client/Services/Generic/DiscordService.cpp` — Discord SDK usage (to be guarded)
- `Code/client/Services/HostService.cpp` — Embedded server for P2P hosting

### Phase 2 Patterns (apply to client)
- `.planning/phases/02-msvc-compatibility-core-libraries/02-CONTEXT.md` — D-01 through D-09 decisions on MSVC cleanup approach
- Phase 2 established: MSVC dropped entirely, portable C++20, rpmalloc over mimalloc

### Research
- `.planning/research/PITFALLS.md` — MinHook/Xbyak cross-compilation risks, MSVC extension inventory
- `.planning/research/ARCHITECTURE.md` — Library tiers, dependency graph

### Codebase Maps
- `.planning/codebase/ARCHITECTURE.md` — Layer architecture, service dependencies
- `.planning/codebase/CONCERNS.md` — Known architectural debt

### Requirements
- `.planning/REQUIREMENTS.md` — BUILD-04, BUILD-05 acceptance criteria

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- Phase 2 MSVC compat patterns — same approach extends to client code
- `Code/external/imgui/` — Already vendored, stays in build for Phase 6
- Phase 1 MinGW XMake configuration — Platform detection and toolchain setup
- Existing smoke test patterns from Phase 1 (GATE-01/GATE-02 Wine validation)

### Established Patterns
- Per-feature compile guards used elsewhere in codebase (e.g., `TP_VIVOX` in client xmake.lua)
- Tier 3 targets already gated behind `is_plat("windows") or is_plat("mingw")` in libraries/xmake.lua
- rpmalloc as default allocator (Phase 2 decision)
- `static_assert(sizeof(...))` for struct layout verification (Phase 2 pitfall prevention)

### Integration Points
- `Code/client/xmake.lua` — CEF, Discord, DirectXTK dependencies to be conditionally excluded
- `Code/libraries/xmake.lua` — SkyrimCoopUI/UIProcess targets to be excluded from MinGW builds
- `Code/client/TiltedOnlineApp.cpp` — References OverlayService, likely needs guards
- `Code/client/World.cpp` / `World.h` — Service initialization, needs guards around CEF services

</code_context>

<specifics>
## Specific Ideas

- Smoke test should spin up a local server and verify client connection, not just check DLL load — catches networking stack issues early.
- MinHook/Xbyak fallback is judgment-based, not line-count-based: if patches start feeling like a rewrite, reconsider alternatives.
- Per-feature flags (HAS_CEF, HAS_DISCORD, HAS_DIRECTXTK) rather than umbrella flag — Phase 6 needs to selectively re-enable ImGui without CEF.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 03-client-dll-cross-compilation*
*Context gathered: 2026-03-27*
