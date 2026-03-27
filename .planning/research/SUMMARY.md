# Research Summary: SkyrimCoop Linux Toolchain & Cross-Compilation

**Domain:** Linux-first game mod development toolchain (cross-compilation, Wine debugging, CI/CD, automated testing)
**Researched:** 2026-03-27
**Overall confidence:** HIGH for toolchain stack, MEDIUM for testing strategy, LOW for LLDB Wine debugging

## Executive Summary

The Linux cross-compilation stack for SkyrimCoop is well-defined and largely proven. The xPack MinGW-w64 GCC 14.3.0 toolchain is already installed on the development machine and validated in the LethalInjection reference project. XMake has first-class MinGW platform support via `xmake f -p mingw`, which avoids a costly migration to CMake. The critical path is not choosing tools -- it is making the existing 60+ dependency codebase compile under MinGW, particularly resolving MSVC-specific extensions scattered through the code.

Debugging is the area with the most uncertainty. GDB is the recommended primary debugger because it natively understands DWARF symbols produced by MinGW. The existing `attach_gdb.sh` script provides a working foundation. Wine must be upgraded from 6.0.3 to 9.0+ for modern debugging support and NTSYNC performance. The custom LLDB 21.1.5 build exists but lacks Wine DYLD patches (which target LLVM 14.x and would require significant effort to rebase). The recommendation is to defer LLDB Wine work entirely.

For CI/CD, the existing GitHub Actions infrastructure extends naturally. A new `linux-cross.yml` workflow downloads the xPack MinGW toolchain, configures XMake, and builds the client DLL. Native Linux tests run in parallel without Wine. The two biggest blockers for MinGW compilation -- CEF and DirectXTK -- are both MSVC-only and should be excluded from the MinGW build target entirely, with the game logic DLL produced separately from the UI overlay.

Automated testing of game state synchronization has no off-the-shelf solution. The recommended approach is a layered strategy: (1) native Linux unit tests for encoding/serialization, (2) a custom integration test harness that instantiates a real GameServer + mock clients using enet6 on Linux without Wine or Skyrim, (3) eventual deterministic sync scenario tests. The mock client pattern using Catch2 + Trompeloeil for mocking the transport layer is the most pragmatic starting point.

## Key Findings

**Stack:** xPack MinGW GCC 14.3.0 + XMake `-p mingw` + GDB for debugging + Wine 9.0+ -- all proven, minimal new tooling needed.
**Architecture:** LethalInjection's dual-build pattern (native tests + MinGW DLL from same sources) maps directly to SkyrimCoop's tiered component structure.
**Critical pitfall:** MSVC extensions (`__declspec`, `#pragma comment(lib)`, `/MT` runtime) are scattered through 12k+ LOC and will silently miscompile or fail to link under MinGW. Must create compatibility layer before anything else.

## Implications for Roadmap

Based on research, suggested phase structure:

1. **XMake MinGW Platform Config + Compatibility Layer** - Foundation for everything
   - Addresses: Build system configuration, MSVC-to-GCC compatibility header
   - Avoids: Pitfall #1 (MSVC extensions), Pitfall #6 (build system fighting)
   - Deliverable: `xmake f -p mingw` configures without errors, Tier 1 libs compile

2. **Tier 1-2 Libraries Compile Under MinGW** - Platform-independent core
   - Addresses: encoding, networking, server logic all building with MinGW
   - Avoids: Pitfall #4 (premature P2P refactoring -- keep building on existing architecture)
   - Deliverable: All static libraries link, native Linux tests pass

3. **Tier 3 Client DLL Compiles (excluding CEF/DirectXTK)** - The hard part
   - Addresses: Win32 hooks, MinHook/Xbyak validation, SKSE interface
   - Avoids: Pitfall #2 (hooking ABI), Pitfall #3 (runtime mismatch), Pitfall #7 (CEF exclusion)
   - Deliverable: SkyrimTogetherClient.dll produced by MinGW, loads in Skyrim under Proton

4. **Debug Workflow + Wine Upgrade** - Development experience
   - Addresses: GDB attach with DWARF symbols, Wine 9.0+ upgrade, consolidated debug scripts
   - Avoids: Pitfall #5 (fragile debug tooling)
   - Deliverable: One-command GDB attach, breakpoints hit in DLL code

5. **CI Pipeline: linux-cross.yml** - Automated verification
   - Addresses: MinGW build on every push, native test execution, artifact upload
   - Deliverable: Green CI badge for MinGW builds

6. **Integration Test Harness** - Testing infrastructure
   - Addresses: Server + mock client testing, message round-trip verification
   - Avoids: Pitfall #9 (wrong testing approach for sync)
   - Deliverable: Catch2 test suite exercising real networking stack

**Phase ordering rationale:**
- Phases 1-3 are strictly sequential (each depends on the previous)
- Phase 4 (debugging) can run in parallel with Phase 3 once Tier 1-2 compile
- Phase 5 (CI) requires Phase 2 at minimum, can start before Phase 3 is complete
- Phase 6 (testing) requires Phase 2 for native server tests, benefits from Phase 5 for CI integration

**Research flags for phases:**
- Phase 3: HIGH risk -- MinHook/Xbyak cross-compilation, SKSE ABI compatibility, CEF exclusion strategy all need validation
- Phase 4: MEDIUM risk -- Wine upgrade may break existing Proton setup, GDB behavior varies by Wine version
- Phase 6: MEDIUM risk -- custom harness design, no precedent for this specific architecture
- Phases 1, 2, 5: LOW risk -- standard toolchain work with good documentation

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Cross-compile toolchain | HIGH | xPack MinGW 14.3.0 already installed, validated in LethalInjection, XMake has MinGW support |
| Build configuration | HIGH | LethalInjection provides exact flags and patterns to copy |
| Debugging (GDB) | MEDIUM | GDB + DWARF is standard, but Wine process debugging adds complexity. Existing scripts provide foundation. |
| Debugging (LLDB) | LOW | Custom build exists but lacks Wine DYLD patches. Rebasing from LLVM 14.x to 21.x is uncharted. |
| CI/CD | HIGH | Existing GitHub Actions, straightforward extension |
| Testing (unit) | HIGH | Catch2 already in use, native Linux compilation straightforward |
| Testing (integration) | MEDIUM | Custom harness needed, no off-the-shelf solution, but pattern is well-understood |
| Testing (E2E sync) | LOW | No precedent, requires purpose-built deterministic simulation |
| MSVC-to-MinGW compat | MEDIUM | Known problem space with known solutions, but 12k LOC audit is significant work |
| CEF/DirectXTK exclusion | HIGH | Both are definitively MSVC-only; exclusion strategy is clear |

## Gaps to Address

- **MinHook cross-compilation validation:** Must build and test MinHook with MinGW before committing to this path. If MinHook fails, may need alternative hooking (LethalInjection's approach or manual trampolines).
- **SKSE ABI smoke test:** Need a minimal "hello world" SKSE plugin compiled with MinGW that loads and prints a log message. This validates the fundamental feasibility before investing in full codebase migration.
- **Wine 9.0+ compatibility with Proton:** Upgrading system Wine may conflict with Steam's Proton runtime. Need to verify they coexist or decide which to use for debugging vs. playing.
- **XMake package cross-compilation:** XMake claims to auto-cross-compile packages for MinGW, but 60+ packages with pinned versions and custom configs may surface edge cases. First attempt will reveal the real scope.
- **Catch2 v3 migration:** Current v2.13.9 works but v3.x has better partitioned headers and CMake integration. Migration is low-risk but adds noise if done during toolchain migration.

## Sources

All sources documented in individual research files:
- `.planning/research/STACK.md` -- Technology recommendations with versions and rationale
- `.planning/research/FEATURES.md` -- Feature landscape for toolchain and gameplay
- `.planning/research/ARCHITECTURE.md` -- Component boundaries, build tiers, LethalInjection mapping
- `.planning/research/PITFALLS.md` -- 14 catalogued pitfalls with prevention strategies

---

*Summary: 2026-03-27*
