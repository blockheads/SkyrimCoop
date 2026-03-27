# Phase 1: Feasibility Validation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-27
**Phase:** 01-feasibility-validation
**Areas discussed:** Validation depth, Build system direction, Test runtime, Failure handling

---

## Validation Depth

| Option | Description | Selected |
|--------|-------------|----------|
| Minimal proof | DLL loads + writes log line; MinHook compiles + hooks test function in standalone exe | |
| Functional proof | DLL loads + calls SKSE APIs (query version, register events); MinHook hooks real Skyrim function and fires | ✓ |
| Stress proof | Functional proof plus survive 5+ min gameplay, hook multiple functions, verify no memory corruption | |

**User's choice:** Functional proof
**Notes:** Catches ABI mismatches that compilation-only checks would miss, without pulling in Phase 2/3 scope.

---

## Build System Direction

| Option | Description | Selected |
|--------|-------------|----------|
| XMake-first | Add MinGW platform support to existing XMake; CMake only if XMake fails | ✓ |
| CMake migration | Port to CMake following LethalInjection toolchain files; abandon XMake | |
| Parallel evaluation | Build test plugin with both XMake+MinGW and CMake+MinGW; pick winner | |

**User's choice:** XMake-first
**Notes:** Codebase already uses XMake, and XMake documents MinGW platform support. Migration only if forced.

---

## Test Runtime

| Option | Description | Selected |
|--------|-------------|----------|
| Proton (Steam) | Most realistic; harder to script; CI-unfriendly | |
| Standalone Wine | Full control; easier to automate; may diverge from Proton behavior | |
| Both, Proton primary | Validate with Proton for realism, Wine for automation; catch divergence early | ✓ |

**User's choice:** Both, Proton primary
**Notes:** Proton is the real player target. Wine enables future CI automation (Phase 5). Testing both during feasibility surfaces divergence before it becomes a problem.

---

## Failure Handling

| Option | Description | Selected |
|--------|-------------|----------|
| Binary abort | Either gate fails → full stop, reassess entire MinGW approach | |
| Tiered fallback | GCC fails → try LLVM-MinGW before declaring failure | ✓ |
| Scoped pivot | Define failure categories with different responses per category | |

**User's choice:** Tiered fallback
**Notes:** LLVM-MinGW is a low-cost second attempt with better MSVC compatibility in edge cases. Full scoped pivot matrix is over-engineering for a feasibility phase.

---

## Claude's Discretion

- MinGW distribution choice
- Specific Skyrim function to hook for GATE-02
- Test plugin project structure

## Deferred Ideas

None — discussion stayed within phase scope
