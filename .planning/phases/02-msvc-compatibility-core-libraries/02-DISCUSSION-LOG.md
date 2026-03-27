# Phase 2: MSVC Compatibility & Core Libraries - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-27
**Phase:** 02-msvc-compatibility-core-libraries
**Areas discussed:** Refactoring approach, Library build order, Third-party deps, Validation depth

---

## Pre-Discussion: MSVC Necessity

Before gray area selection, user questioned whether MSVC support is needed at all.

| Option | Description | Selected |
|--------|-------------|----------|
| Dual-compiler compat header | Map MSVC-isms to GCC equivalents, keep both compilers | |
| Drop MSVC, go pure MinGW/GCC | Replace all MSVC-isms with portable code, single compiler | ✓ |

**User's choice:** Drop MSVC entirely, only support MinGW/GCC
**Notes:** This fundamentally simplified the phase — no compatibility layer needed, just refactor to portable code.

---

## Refactoring Approach

| Option | Description | Selected |
|--------|-------------|----------|
| Portable C++20 (Recommended) | Use alignas(), standard attributes where possible, __attribute__ as fallback | ✓ |
| GCC __attribute__ everywhere | Consistent GCC-native style, ties to GCC/Clang | |
| You decide | Claude picks per-pattern | |

**User's choice:** Portable C++20
**Notes:** None

| Option | Description | Selected |
|--------|-------------|----------|
| Delete MSVC paths (Recommended) | Remove MSVC blocks from xmake.lua entirely | ✓ |
| Keep but disable | Comment out, guard behind flag | |
| You decide | Claude determines | |

**User's choice:** Delete MSVC paths
**Notes:** Clean break, git history preserves old config.

---

## Library Build Order

| Option | Description | Selected |
|--------|-------------|----------|
| Dependency chain (Recommended) | TiltedCore → encoding → common → networking → server, strict bottom-up | ✓ |
| Easiest first, stub deps | Start with encoding, stub TiltedCore temporarily | |
| You decide | Claude determines | |

**User's choice:** Dependency chain
**Notes:** None

---

## Third-Party Dependencies

| Option | Description | Selected |
|--------|-------------|----------|
| Fork and patch (Recommended) | Fork non-compiling deps, patch for GCC | |
| Wrapper/shim layer | Don't modify third-party code, wrap at boundary | |
| Replace problematic deps | Swap MSVC-tied deps for Linux-friendly alternatives | ✓ (partial) |
| You decide per-dep | Claude evaluates individually | |

**User's choice:** Replace MSVC-heavy deps with Linux-friendly alternatives. For TiltedCore specifically: inline the used code into SkyrimCoop source and drop the external dependency entirely.
**Notes:** User stated TiltedCore was a generic multi-game library they don't want to maintain. Only extract networking logic actually used. User believes prior porting work may exist on other git branches — researcher must check.

| Option | Description | Selected |
|--------|-------------|----------|
| Researcher audits TiltedCore (Recommended) | Trace all imports, identify minimal extraction set | ✓ |
| User specifies components | User lists which parts matter | |

**User's choice:** Researcher audits it, also check other branches for prior porting work
**Notes:** User recalls doing some porting work previously that may have been scrapped.

---

## Validation Depth

| Option | Description | Selected |
|--------|-------------|----------|
| Compile + link + existing tests (Recommended) | Build and run existing Catch2 tests natively on Linux | ✓ |
| Compile + link only | Just verify clean build, defer tests to Phase 5 | |
| You decide | Claude determines per-library | |

**User's choice:** Compile + link + existing tests
**Notes:** None

---

## Claude's Discretion

- Specific C++20 replacement for each MSVC construct
- Which Linux-friendly alternatives for replaced dependencies
- How to structure inlined TiltedCore code within project directory

## Deferred Ideas

None — discussion stayed within phase scope.
