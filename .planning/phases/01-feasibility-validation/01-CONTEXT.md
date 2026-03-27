# Phase 1: Feasibility Validation - Context

**Gathered:** 2026-03-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Prove that MinGW cross-compilation can produce working SKSE plugins and function hooks on Linux before investing in full codebase migration. This phase delivers two binary feasibility gates (GATE-01, GATE-02) and initial XMake MinGW configuration (BUILD-01). No production code is modified — only test artifacts and build configuration.

</domain>

<decisions>
## Implementation Decisions

### Validation Depth
- **D-01:** Functional proof, not minimal compilation check. GATE-01 must exercise SKSE APIs (query game version, register for events), not just load and log. GATE-02 must hook a real Skyrim function at a known address and confirm the hook fires at runtime. This catches ABI mismatches that "it compiles" would miss.

### Build System Direction
- **D-02:** XMake-first approach. Add MinGW platform support to the existing XMake build (`xmake f -p mingw --mingw=/path`). Do not migrate to CMake unless XMake's MinGW support proves inadequate during this phase. The LethalInjection CMake toolchain files are a reference, not a migration target.

### Test Runtime
- **D-03:** Validate with both Proton (primary) and standalone Wine (secondary). Proton is the realistic target — what players actually use. Wine is the automation path for scripted testing and future CI (Phase 5). Running both during feasibility catches divergence early.

### Failure Handling
- **D-04:** Tiered fallback strategy. If standard MinGW (GCC) fails either gate, try LLVM-MinGW (Clang targeting MinGW ABI) before declaring the approach unviable. LLVM-MinGW has better MSVC compatibility in some edge cases. If both compilers fail, abort the MinGW approach and reassess.

### Claude's Discretion
- MinGW distribution choice (distro package vs custom build) — pick whatever is most straightforward
- Specific Skyrim function to hook for GATE-02 — any stable, well-known address works
- Test plugin project structure — whatever proves the point cleanly

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Build System
- `xmake.lua` — Current build configuration, no MinGW support yet
- `Code/immersive_launcher/xmake.lua` — MinHook usage in XMake context

### LethalInjection Reference
- `/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d5/Projects/LethalInjection/cmake/toolchain-mingw64.cmake` — Proven MinGW cross-compile toolchain
- `/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d5/Projects/LethalInjection/CMakeLists.txt` — Reference build structure

### SKSE Plugin Entry Point
- `Code/client/main.cpp` — Current SKSE plugin entry, Win32 API usage patterns to replicate minimally

### Research
- `.planning/research/PITFALLS.md` — Known risks around MinHook/XByak cross-compilation, SKSE ABI
- `.planning/research/ARCHITECTURE.md` — Architecture context including SKSE plugin structure

### Requirements
- `.planning/REQUIREMENTS.md` — GATE-01, GATE-02, BUILD-01 acceptance criteria

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `xmake.lua` root build file — add MinGW platform rules alongside existing Windows/Linux config
- `Code/immersive_launcher/stubs/FileMapping.cpp` — existing MinHook stub pattern
- LethalInjection `toolchain-mingw64.cmake` — reference for compiler flags, linker settings, ABI options

### Established Patterns
- XMake platform detection via `is_plat()` checks — MinGW config should follow this pattern
- SKSE plugin loads via `RunTiltedInit()` / `RunTiltedApp()` in `main.cpp` — minimal test plugin should mirror this entry structure
- MinHook v1.3.3 used throughout for function hooking — GATE-02 tests this specific library

### Integration Points
- `xmake.lua` root — where MinGW platform configuration gets added (BUILD-01)
- No existing MinGW toolchain files — these need to be created from scratch (or adapted from LethalInjection)
- MinHook is pulled as a dependency via XMake — need to verify it builds under MinGW toolchain

</code_context>

<specifics>
## Specific Ideas

No specific requirements — open to standard approaches for the feasibility proofs.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 01-feasibility-validation*
*Context gathered: 2026-03-27*
