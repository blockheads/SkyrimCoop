# Phase 2: MSVC Compatibility & Core Libraries - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

All platform-independent code (encoding, networking, common, server) compiles under MinGW/GCC and produces working static libraries. MSVC is dropped entirely as a supported compiler — no compatibility layer, no dual-compiler support. MSVC-specific constructs are replaced with portable C++20 or GCC-native equivalents. TiltedCore is inlined into the project source code, eliminating the external dependency.

</domain>

<decisions>
## Implementation Decisions

### Compiler Strategy
- **D-01:** Drop MSVC entirely. Only MinGW/GCC is a supported compiler going forward. No compat headers, no `#ifdef _MSC_VER` branches.
- **D-02:** Delete all MSVC-specific build configuration from xmake.lua — `is_plat('windows')` MSVC blocks, `/MT` flags, `/bigobj`, MSVC-specific rules. Clean break, git history preserves the old config.

### Refactoring Approach
- **D-03:** Use portable C++20 constructs where possible (`alignas()`, `[[nodiscard]]`, standard attributes). Fall back to `__attribute__` only when C++20 has no equivalent (e.g., `dllexport` → `__attribute__((visibility("default")))`, Windows calling conventions stay as-is since MinGW supports `__stdcall`/`__cdecl`).
- **D-04:** `#pragma comment(lib, ...)` directives are deleted and replaced with explicit linker flags in xmake.lua.

### Library Build Order
- **D-05:** Strict bottom-up dependency chain: TiltedCore (inline) → encoding → common → networking → server. Each layer validated before building the next. No stubbing, no shortcuts.

### Third-Party Dependencies
- **D-06:** TiltedCore is inlined into SkyrimCoop source code. The external dependency is eliminated — extract only the networking/serialization logic actually used by the project, drop the rest. It was a generic multi-game library; SkyrimCoop only needs the parts it uses.
- **D-07:** Researcher must audit all TiltedCore imports to identify the minimal set of functionality used. Also check other git branches for any prior porting work that may already exist.
- **D-08:** For other MSVC-heavy dependencies, replace with Linux-friendly alternatives rather than forking and patching. Dependencies that already support MinGW/GCC (spdlog, entt, GLM, zlib, snappy, etc.) are kept as-is.

### Validation
- **D-09:** Each library must compile, link without unresolved symbols, AND existing Catch2 encoding/serialization tests must pass when built natively on Linux. No new test infrastructure — just compile and run what already exists. Catches ABI/serialization bugs early rather than deferring to Phase 5.

### Claude's Discretion
- Specific C++20 replacement for each MSVC construct (case-by-case during implementation)
- Which Linux-friendly alternatives to use for replaced dependencies
- How to structure the inlined TiltedCore code within the project directory

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Build System
- `xmake.lua` — Current build config with MSVC paths to be removed
- `Code/encoding/xmake.lua` — Tier 1 library build rules (if exists)
- `Code/server/xmake.lua` — Server build rules

### Research
- `.planning/research/PITFALLS.md` — MSVC extension inventory (28+ `__declspec` uses, 6+ `#pragma comment(lib)`) and prevention strategies
- `.planning/research/ARCHITECTURE.md` — Architecture context, library tiers, dependency graph

### Phase 1 Context
- `.planning/phases/01-feasibility-validation/01-CONTEXT.md` — XMake-first approach (D-02), tiered fallback strategy (D-04)

### TiltedCore
- `Libraries/TiltedCore/` (or wherever submoduled) — Source to audit for extraction
- All git branches — Check for prior porting work

### Requirements
- `.planning/REQUIREMENTS.md` — BUILD-02, BUILD-03 acceptance criteria

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `Code/encoding/` — Zero MSVC-isms found, cleanest library. Should compile under MinGW with minimal or no changes.
- `Code/tests/` — Existing Catch2 tests for encoding/serialization. Can be compiled natively on Linux for validation.
- Phase 1 MinGW XMake configuration — Platform detection and MinGW toolchain setup already done.

### Established Patterns
- XMake platform detection via `is_plat()` — MinGW config already added in Phase 1
- MSVC-isms concentrated in client code (Phase 3) and externals, not in Tier 1-2 libraries
- `__stdcall`/`__cdecl` calling conventions used in hooks (WindowsHook.cpp, D3D11Hook.cpp) — these stay as-is for MinGW

### Integration Points
- `xmake.lua` root — MSVC blocks to be removed, MinGW linker flags to be added (replacing `#pragma comment(lib)`)
- TiltedCore dependency — Every library that imports from TiltedCore needs updated include paths after inlining
- Catch2 test targets — Need to be wired to compile natively on Linux

</code_context>

<specifics>
## Specific Ideas

- TiltedCore was a generic multi-game library that the user doesn't want to maintain. Only extract what SkyrimCoop actually uses (Buffer, serialization, networking primitives) and drop the rest.
- User believes prior porting work may exist on other git branches — researcher must check all branches before starting fresh.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 02-msvc-compatibility-core-libraries*
*Context gathered: 2026-03-27*
