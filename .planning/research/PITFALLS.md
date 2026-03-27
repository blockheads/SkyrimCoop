# Domain Pitfalls

**Domain:** MSVC-to-MinGW cross-compilation, Wine/Proton DLL debugging, MMO-to-co-op conversion, game sync testing
**Researched:** 2026-03-27

## Critical Pitfalls

Mistakes that cause rewrites, weeks of debugging, or fundamental architecture failures.

### Pitfall 1: MSVC Extensions Scattered Through Codebase Will Silently Miscompile Under MinGW

**What goes wrong:** The codebase uses MSVC-specific constructs that MinGW either ignores, miscompiles, or rejects outright. Confirmed in codebase: `__declspec(dllexport/dllimport)` (28+ uses), `__declspec(align(8))` (struct alignment in SaveLoad.h, BSGraphicsRenderer.h), `__declspec(noinline)` (DebugService, FaceGenSystem), `__forceinline` (likely in TiltedCore), `#pragma comment(lib, ...)` (6+ uses for auto-linking gdi32, dwmapi, d3dcompiler, version.lib), `__cdecl`/`__stdcall` calling conventions (LoadingScreen.cpp, D3D11Hook.cpp), `/SAFESEH:NO` linker flag (immersive_launcher), and `/bigobj` MSVC-only flag.

**Why it happens:** MinGW supports *some* `__declspec` variants but not all. `#pragma comment(lib)` is completely ignored by MinGW -- it is an MSVC auto-link directive. The `/bigobj` flag has no MinGW equivalent (MinGW always supports large object files). Struct alignment via `__declspec(align(N))` should use `__attribute__((aligned(N)))` or C++11 `alignas(N)` instead. The `/MT` static runtime linkage (used in 5+ xmake.lua files) has no MinGW equivalent since MinGW links against its own runtime.

**Consequences:**
- `#pragma comment(lib)` silently ignored: DLL fails to load at runtime with missing symbol errors, no compile-time warning
- `__declspec(align(8))` may produce wrong struct layout: memory corruption when reading Skyrim game structures
- Calling convention mismatches between MinGW-compiled DLL and MSVC-compiled Skyrim: stack corruption, crashes on function return
- Missing library links cause LoadLibrary failures with no useful error message

**Prevention:**
1. Create a compatibility header (`compat/msvc_compat.h`) that maps MSVC extensions to GCC equivalents:
   ```cpp
   #ifdef __GNUC__
   #define DECLSPEC_ALIGN(x) __attribute__((aligned(x)))
   #define FORCE_INLINE __attribute__((always_inline)) inline
   #define FORCE_NOINLINE __attribute__((noinline))
   #else
   #define DECLSPEC_ALIGN(x) __declspec(align(x))
   #define FORCE_INLINE __forceinline
   #define FORCE_NOINLINE __declspec(noinline)
   #endif
   ```
2. Replace all `#pragma comment(lib, ...)` with explicit linker flags in xmake.lua/CMakeLists.txt
3. Add `-Wall -Wextra -Wpedantic` to catch remaining MSVC-isms
4. Use `static_assert(sizeof(StructName) == EXPECTED)` for every struct that maps to Skyrim memory layout

**Detection:** Compile with MinGW using `-Wpedantic`. Any `#pragma comment` will produce warnings. Struct size mismatches will hit static_asserts. Missing symbols will fail at link time if libraries are properly required.

**Phase:** Milestone 1 (Linux toolchain) -- this is the first wall you will hit.

**Confidence:** HIGH -- confirmed by direct codebase grep showing 28+ `__declspec` uses, 6+ `#pragma comment(lib)` uses.

---

### Pitfall 2: MinHook and Xbyak JIT Are x86 Windows-Only -- They Cannot Cross-Compile from MinGW Trivially

**What goes wrong:** The codebase depends on MinHook (v1.3.3) for API hooking and Xbyak (v7.06) for JIT code generation. Both are Windows-native libraries that use Windows APIs (`VirtualProtect`, `FlushInstructionCache`, `VirtualAlloc`) and x86 assembly. MinHook specifically uses MSVC inline assembly or intrinsics that MinGW may not support identically. Xbyak generates x86/x64 machine code at runtime, which requires Windows memory management APIs.

**Why it happens:** These libraries are designed for in-process hooking on Windows. They compile on MSVC and assume MSVC calling conventions. Cross-compiling them with MinGW is theoretically possible (both libraries have some MinGW support) but the integration with TiltedPhoques' `FunctionHookManager` adds complexity -- it wraps MinHook with commit/rollback semantics that may use MSVC-specific thread-local storage or exception handling patterns.

**Consequences:**
- MinHook may compile but produce hooks with wrong calling conventions, causing stack corruption when hooks fire inside Skyrim
- Xbyak JIT-generated trampolines may use wrong register-saving conventions (MSVC x64 ABI vs SysV ABI confusion)
- FunctionHookManager's `TP_HOOK_COMMIT` macro may expand differently under GCC

**Prevention:**
1. Build MinHook and Xbyak separately first as a smoke test -- compile and run their own test suites under MinGW cross-compilation
2. Verify MinHook's `MH_CreateHookApi` works correctly by hooking a trivial Windows API (e.g., `GetTickCount`) in a test DLL loaded into Wine
3. Check Xbyak's `CodeGenerator` produces correct x64 Windows ABI trampolines (RCX/RDX/R8/R9 argument passing, shadow space allocation)
4. The LethalInjection project does NOT use MinHook or Xbyak -- it has its own hooking solution. Consider whether TiltedPhoques' hooking layer needs replacement or if MinHook/Xbyak genuinely cross-compile

**Detection:** Write a minimal test DLL that hooks `MessageBoxA` via MinHook, cross-compile with MinGW, inject into a Wine process, and call `MessageBoxA`. If the hook fires and returns correctly, MinHook works. If the process crashes, the calling convention is wrong.

**Phase:** Milestone 1 (Linux toolchain) -- must validate before any hooking code can be ported.

**Confidence:** MEDIUM -- MinHook's GitHub claims MinGW support, but the TiltedPhoques wrapper layer adds unknowns. Xbyak explicitly supports GCC but the Windows ABI code generation path needs verification.

---

### Pitfall 3: Static Runtime (/MT) Mismatch Between MinGW DLL and SKSE/Skyrim MSVC Runtime

**What goes wrong:** The codebase is built with `/MT` (static MSVC runtime) to avoid CRT DLL dependency conflicts with Skyrim. MinGW uses its own C runtime (mingw-w64-crt, linking to UCRT or MSVCRT). When a MinGW-compiled DLL is loaded into an MSVC-compiled process (Skyrim), each has its own heap, its own `malloc`/`free`, and its own static data for the C runtime. Passing objects that cross the DLL boundary (strings, containers, FILE handles, memory allocated in one and freed in the other) causes heap corruption.

**Why it happens:** C++ ABI incompatibility between MSVC and GCC/MinGW is fundamental. They use different name mangling, different exception handling (MSVC SEH vs GCC SJLJ/SEH), different vtable layouts, and different memory allocators. The codebase uses `mimalloc` for its own allocations (confirmed: `mi_malloc`, `mi_malloc_aligned`, `mi_malloc_size` in Memory.cpp), which helps isolate internal memory but does NOT protect against ABI mismatches at the Skyrim/SKSE interface boundary.

**Consequences:**
- Heap corruption when Skyrim allocates memory and the DLL frees it (or vice versa)
- Exception handling across DLL boundaries crashes (Skyrim throws SEH exception, MinGW DLL can't catch it)
- vtable layout differences mean virtual function calls through Skyrim object pointers dispatch to wrong methods
- RTTI type comparison fails (5,629 lines of RTTI.cpp with hardcoded Skyrim type layouts)

**Prevention:**
1. The DLL-to-game interface MUST be C-only or use raw pointers with explicit ownership. Never pass `std::string`, `std::vector`, or any standard library container across the boundary
2. Verify that the existing codebase already follows this pattern at SKSE entry points -- it likely does since SKSE uses C-style function pointers, but audit every `reinterpret_cast` that touches Skyrim memory
3. Use mimalloc exclusively for DLL-internal allocations, Skyrim's own allocator for Skyrim objects (the codebase already hooks Skyrim's memory allocator in Memory.cpp)
4. Never catch C++ exceptions from Skyrim code -- the conventions code already disables exceptions ("No exceptions allowed")
5. Add `static_assert` checks for struct sizes that MUST match Skyrim's layout (BSCriticalSection, BGSSaveLoadScrapBuffer, etc.)
6. Test with a minimal MinGW DLL that calls `SKSEPluginLoad` and accesses basic game state

**Detection:** Crashes during DLL load (immediate) or heap corruption during gameplay (delayed, hard to trace). Use Wine's `WINEDEBUG=+heap` to detect heap corruption. Use `WINEDEBUG=+seh` to detect unhandled structured exceptions.

**Phase:** Milestone 1 -- this is the fundamental feasibility question. If the SKSE interface cannot work with MinGW ABI, the entire approach needs rethinking (possibly Clang/MSVC-compatible mode instead of GCC).

**Confidence:** HIGH -- this is a well-documented incompatibility. The LethalInjection project validates that MinGW DLLs CAN be injected into Windows processes under Wine, but LethalInjection's game interface is much simpler than SKSE's.

---

### Pitfall 4: Premature P2P Refactoring Before Build System Works

**What goes wrong:** The temptation is to start the exciting architecture work (merging Server::World into client World, creating NetworkBridge, refactoring services) before the Linux build toolchain is solid. This creates a situation where you are debugging both MinGW compilation issues AND architectural regressions simultaneously, with no working baseline to compare against.

**Why it happens:** Architecture refactoring feels productive. Build system debugging feels tedious. The CONCERNS.md already documents extensive P2P refactoring plans with 5 phases. Developers naturally want to start the "real work" before the boring toolchain work is done.

**Consequences:**
- No working build to test against -- cannot determine if a crash is from MinGW compilation or P2P logic error
- Existing MSVC/Windows builds may break during refactoring, leaving zero working builds
- Test infrastructure not yet available when architectural changes happen
- Debugging P2P logic through Wine adds a layer of complexity that multiplies every bug

**Prevention:**
1. Hard gate: Milestone 1 MUST produce a MinGW-compiled DLL that loads into Skyrim via SKSE under Proton and successfully connects to the existing server -- using the CURRENT architecture, zero refactoring
2. Only after this baseline exists should P2P refactoring begin
3. Keep the MSVC build working throughout -- CI should build both MSVC and MinGW
4. P2P refactoring should be feature-flagged so either code path can run

**Detection:** If you find yourself debugging "is this a MinGW issue or a logic issue?" more than once per day, you started refactoring too early.

**Phase:** Milestone boundary between 1 (toolchain) and 2 (architecture). This is a sequencing discipline issue.

**Confidence:** HIGH -- this is a general engineering principle applied to a specific risk in the PROJECT.md roadmap.

---

### Pitfall 5: Wine/Proton Debug Tooling Is Fragile and Poorly Documented

**What goes wrong:** Attaching a debugger (GDB or LLDB) to a Wine/Proton process to debug an injected MinGW-compiled DLL involves multiple layers of complexity that each independently fail. The project already has 6 debug scripts (`attach_gdb.sh`, `attach_lldb.sh`, `attach_winedbg.sh`, `debug_symbols_lldb.sh`, `debug_wine.sh`, `debug_wine_tui.sh`) and 3 debugging guides, suggesting this has already been a significant pain point.

**Why it happens:** Wine processes are Linux processes running Windows code. GDB sees ELF binaries (Wine) but the DLL is a PE binary with DWARF debug info (when compiled with MinGW `-g`). LLDB has experimental PE+DWARF support but lacks Wine dynamic loader integration, meaning it cannot automatically discover loaded DLLs. Proton adds another layer (Steam Runtime container). Source file paths in debug info contain cross-compilation paths that don't match the host filesystem.

**Consequences:**
- Cannot set breakpoints in DLL code because debugger doesn't know the DLL is loaded
- Stack traces show Wine internals but not your code
- Single-stepping crosses Wine/Windows API boundaries and gets lost
- Debug symbols load but source mapping fails (MinGW cross-compilation paths vs Linux paths)
- Proton sandbox prevents debugger attachment without `PROTON_DUMP_DEBUG_COMMANDS`

**Prevention:**
1. Use `PROTON_DUMP_DEBUG_COMMANDS=1` to generate GDB attach scripts (documented in Proton repository)
2. Configure LLDB `target.source-map` to remap MinGW's sysroot paths to local source tree
3. Add extensive spdlog trace logging as primary debugging method -- do not rely on interactive debugging for first 80% of issues
4. Build a headless test harness (no Wine needed) for logic that doesn't touch Skyrim APIs -- test networking, serialization, state management natively on Linux
5. When interactive debugging IS needed, use winedbg in GDB mode for basic debugging, fall back to `WINEDEBUG=+relay` for API tracing
6. Consolidate the 6 existing debug scripts into one documented workflow

**Detection:** If you spend more than 30 minutes trying to set a breakpoint and it doesn't hit, your debug tooling is broken. Fall back to printf/spdlog debugging.

**Phase:** Milestone 1 -- debug workflow must be established alongside build system. The existing debug scripts suggest previous attempt(s) at this.

**Confidence:** MEDIUM -- the approaches are documented (Proton DEBUGGING-LINUX.md, werat.dev blog) but real-world reliability varies by Proton version and Wine version.

---

## Moderate Pitfalls

### Pitfall 6: XMake-to-CMake Build System Migration Complexity

**What goes wrong:** The project uses XMake 2.8.5+ with platform-specific logic (`is_plat("windows")`, `set_runtimes("MT")`), custom package repositories, and pinned dependency versions. MinGW cross-compilation in XMake is less mature than in CMake. The LethalInjection reference uses CMake with explicit toolchain files (`toolchain-mingw64.cmake`). Attempting to make XMake cross-compile for MinGW may burn weeks on build system fighting.

**Prevention:**
1. Try XMake's MinGW cross-compilation first (`xmake config --toolchain=mingw`) -- it may just work for the server and simpler targets
2. If XMake fights back for the client DLL target, create a parallel CMakeLists.txt for the client DLL only, using LethalInjection's toolchain pattern
3. Do NOT attempt to migrate the entire build system at once -- keep XMake for MSVC/Windows builds, add CMake for MinGW cross-compilation

**Detection:** If XMake cross-compilation requires patching XMake itself or forking package definitions, switch to CMake for that target.

**Phase:** Milestone 1.

**Confidence:** MEDIUM -- XMake has MinGW support but the project's heavy use of Windows-specific XMake features (`set_runtimes`, `/bigobj`, MSVC flags) suggests significant adaptation needed.

---

### Pitfall 7: CEF (Chromium Embedded Framework) Cannot Cross-Compile from MinGW

**What goes wrong:** CEF is pinned to version 100.0.24 and only builds with MSVC on Windows. It is a massive dependency (the `tp_process` subprocess, the UI rendering pipeline, D3D11 texture sharing). Attempting to cross-compile CEF with MinGW is not feasible -- Chromium's build system requires MSVC.

**Prevention:**
1. Exclude CEF, tp_process, and the entire UI layer from the MinGW build -- the xmake.lua already conditionally includes CEF only on Windows: `if is_plat("windows") then add_requires("cef 100.0.24")`
2. For the MinGW-compiled client DLL, stub out OverlayService and related CEF calls behind a compile-time flag
3. UI functionality under Wine/Proton will need to use the MSVC-compiled CEF process OR an alternative overlay approach
4. This is a scoping decision, not a bug -- document that MinGW builds produce the game logic DLL only, not the full UI stack

**Detection:** Any attempt to add CEF to a MinGW build target will fail immediately at configure time.

**Phase:** Milestone 1 (scoping decision) and Milestone 3 (UI alternative if needed).

**Confidence:** HIGH -- CEF/Chromium is known to require MSVC on Windows.

---

### Pitfall 8: Ownership Simplification Creates New Edge Cases That Don't Exist in MMO Model

**What goes wrong:** The P2P conversion assumes "host owns everything" simplifies the codebase. In practice, it creates new edge cases: What happens when the host enters a loading screen? What about host-only cells (host is in a dungeon, peer is in the overworld)? What if the host disconnects -- does everyone disconnect, or can host migration occur? What about actors near cell boundaries that the host hasn't loaded?

**Why it happens:** The MMO model's distributed ownership was complex but handled these cases (cell handoff, multiple authoritative zones). Removing that complexity also removes those solutions. The CONCERNS.md acknowledges "cell management: host's loaded cells are authoritative" but doesn't address cells the host has NOT loaded that peers need.

**Prevention:**
1. Document every edge case where "host owns everything" breaks down BEFORE starting the P2P refactor
2. Key edge cases to solve upfront:
   - Host loading screen: freeze all peers or let them continue locally?
   - Peer in unloaded cell: does the host load it on demand? Queue messages?
   - Host disconnect: immediate session end (simplest) or host migration (complex)?
   - Actor at cell boundary: which cell's authority applies?
3. Decide on simplest-possible solutions (e.g., "host disconnect = session end, period") and document them as non-negotiable for MVP

**Detection:** If P2P refactoring discussions keep circling back to "but what if the host is in a different cell," you haven't resolved the fundamental ownership model.

**Phase:** Milestone 2 (P2P architecture) -- must be resolved in design before coding begins.

**Confidence:** HIGH -- these are well-known game networking edge cases. Unity's blog on small-scale co-op games explicitly calls out shared authority as a primary challenge.

---

### Pitfall 9: Testing Game State Synchronization Requires a Purpose-Built Harness, Not Standard Unit Testing

**What goes wrong:** Developers try to test multiplayer synchronization using standard unit test patterns (assert state A == state B after operation X). Game sync is inherently temporal, non-deterministic, and depends on timing. Tests that pass locally fail in CI due to timing differences. Tests that mock the network layer don't catch the real bugs (which are in the network layer).

**Why it happens:** The existing test suite (Catch2, encoding/serialization only) tests pure functions. Extending this pattern to synchronization testing doesn't work because sync bugs are emergent from timing, ordering, and partial failures.

**Prevention:**
1. Build a purpose-built test harness that runs a real host + N simulated peers in-process, sharing a single event loop with controllable time advancement
2. The harness should:
   - Instantiate a real `World` (host mode) and N `World` instances (peer mode)
   - Connect them via in-memory transport (no real network)
   - Allow deterministic time stepping (`world.Tick(16ms)`)
   - Support injecting packet loss, reordering, and delay
   - Assert on eventual consistency (state matches within N ticks), not immediate equality
3. Test scenarios, not functions:
   - "Peer moves, host sees movement within 3 ticks"
   - "Host kills NPC, peer sees death within 5 ticks"
   - "Peer disconnects mid-inventory-transfer, no items duplicated or lost"
4. Run these as integration tests, separate from fast unit tests

**Detection:** If your sync tests have `sleep(100ms)` or `std::this_thread::sleep_for` in them, you're doing it wrong -- use deterministic time stepping.

**Phase:** Milestone 1 (test framework design), Milestone 2 (sync tests alongside P2P refactor).

**Confidence:** MEDIUM -- the general pattern is well-established in game networking, but applying it to this specific EnTT-based architecture requires custom work. No off-the-shelf framework fits.

---

### Pitfall 10: EnTT Registry State Divergence Between Host and Peers

**What goes wrong:** The host's `entt::registry` and each peer's `entt::registry` drift apart over time. Components are added/removed in different orders, entity IDs don't correspond across registries, and there's no mechanism to detect or correct divergence. The existing codebase has dual World instances (client and server) that already exhibit this problem (documented in CONCERNS.md as "Redundant Client-Server" and "Game State Validation Absent").

**Why it happens:** EnTT assigns entity IDs locally -- entity 42 on the host is NOT the same actor as entity 42 on a peer. The existing codebase uses `GameId` (ModId + BaseId) for cross-registry identity, but the mapping between GameId and local entt::entity can desync if creation/destruction messages arrive out of order.

**Prevention:**
1. Maintain a bidirectional `GameId <-> entt::entity` map that is the ONLY way to look up entities across the network boundary
2. Never send raw `entt::entity` values over the network
3. Implement periodic state hash comparison: host computes hash of all synced component states, sends to peers, peers compare and request full resync on mismatch
4. Add a "resync" command for manual recovery (already suggested in CONCERNS.md)
5. Log all entity creation/destruction with timestamps for debugging divergence

**Detection:** Peers seeing "ghost" actors (present on peer but deleted on host) or "invisible" actors (present on host but missing on peer). Health bars or names appearing on wrong actors.

**Phase:** Milestone 2 (P2P architecture refactor) and ongoing.

**Confidence:** HIGH -- entity ID mapping is a classic game networking problem, and the CONCERNS.md already flags state validation as absent.

---

## Minor Pitfalls

### Pitfall 11: MinGW C++20 Feature Support Gaps

**What goes wrong:** The codebase targets C++20 (`set_languages("cxx20")`). MinGW GCC 14.x supports most C++20 features but has gaps in `std::format` (partial), C++20 modules (not supported), and some `<ranges>` edge cases. If the codebase uses MSVC-specific C++20 extensions (like `std::format` with MSVC's extended format specs), MinGW compilation fails.

**Prevention:** Audit C++20 feature usage. The codebase uses `fmt::format` (via spdlog) rather than `std::format`, which is good -- fmt works on both compilers. Check for `std::source_location`, `std::span`, constexpr features that may differ.

**Detection:** Compile errors during Milestone 1 MinGW build attempt.

**Phase:** Milestone 1.

**Confidence:** MEDIUM -- depends on which specific C++20 features are used.

---

### Pitfall 12: DirectX Headers and COM Interfaces Under MinGW

**What goes wrong:** The client uses D3D11 (D3D11Hook.cpp, RenderSystemD3D11, DirectXTK). MinGW has DirectX headers but they may be incomplete or have interface mismatches for newer D3D11 features. COM `IUnknown` interface pointers and `HRESULT` error handling work differently.

**Prevention:** The D3D11 rendering and hooking code (CEF texture sharing, ImGui overlay) should be excluded from the MinGW build along with CEF. Only the game logic DLL needs MinGW compilation. D3D11 hooks will come from the MSVC-compiled layer if needed.

**Detection:** Immediate compile errors when DirectX headers are included.

**Phase:** Milestone 1 (scoping -- exclude D3D11 from MinGW target).

**Confidence:** HIGH -- DirectX + MinGW is a known pain point.

---

### Pitfall 13: Wine UCRT vs MSVCRT Runtime Selection

**What goes wrong:** MinGW can link against UCRT (Windows 10+) or MSVCRT (legacy). Skyrim and SKSE are MSVC-compiled and use the MSVC runtime. If the MinGW DLL links against a different CRT version than what Skyrim uses, stdio functions, locale handling, and thread-local storage may behave incorrectly when the DLL calls CRT functions that interact with Skyrim's CRT state.

**Prevention:** Configure MinGW to link against UCRT (`-D_UCRT` or use UCRT-targeting MinGW sysroot like xPack's). Minimize CRT interactions across the DLL boundary. The mimalloc usage already isolates heap allocations.

**Detection:** Subtle: `printf` output disappears, locale-dependent string formatting produces wrong results, thread-local storage corruption. Use `WINEDEBUG=+msvcrt` to trace CRT calls.

**Phase:** Milestone 1.

**Confidence:** MEDIUM -- the LethalInjection toolchain uses xPack MinGW which defaults to UCRT, suggesting this is a known consideration.

---

### Pitfall 14: Enet6 Networking Library Under Wine May Behave Differently

**What goes wrong:** The project switched from GameNetworkingSockets to enet6 (confirmed in xmake.lua comments). Enet uses raw UDP sockets which work under Wine, but Wine's Winsock implementation has known edge cases with `select()`, non-blocking socket behavior, and `WSAEventSelect`. If enet6 uses Windows-specific socket APIs that Wine implements imperfectly, networking may be unreliable under Proton.

**Prevention:** Test enet6 connectivity separately -- build a minimal enet6 client/server pair, run the server natively on Linux and the client under Wine, verify reliable packet delivery under load. Check if enet6 has a POSIX socket backend that could be used instead of Winsock when the server component runs natively.

**Detection:** Intermittent connection drops, high packet loss that doesn't correlate with network conditions, `connect()` hangs under Wine.

**Phase:** Milestone 1 (validation).

**Confidence:** LOW -- enet's Winsock usage is straightforward and Wine's socket implementation is mature. This is a "verify, don't assume" item rather than a likely blocker.

---

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| MinGW cross-compilation setup | MSVC extensions (#1), runtime mismatch (#3) | Compatibility header, struct size assertions, smoke test DLL |
| Hooking framework port | MinHook/Xbyak ABI issues (#2) | Isolated test before full port |
| XMake adaptation | Build system fighting (#6) | Fall back to CMake for cross-compile target if needed |
| CEF/UI layer | Cannot cross-compile (#7) | Exclude from MinGW build, stub interfaces |
| Debug workflow | Fragile tooling (#5) | Prioritize spdlog tracing over interactive debugging |
| P2P architecture | Premature start (#4), ownership edge cases (#8) | Working baseline first, document edge cases before coding |
| State synchronization | Registry divergence (#10), testing approach (#9) | Purpose-built harness, GameId mapping, periodic hash checks |
| C++ language features | C++20 gaps (#11), DirectX headers (#12) | Audit feature usage, exclude D3D11 from MinGW target |

## Sources

- Codebase analysis: direct grep of `__declspec`, `#pragma comment`, MinHook, Xbyak, mimalloc, CEF usage
- LethalInjection reference: `/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d5/Projects/LethalInjection/cmake/toolchain-mingw64.cmake`
- [Proton debugging documentation](https://github.com/ValveSoftware/Proton/blob/proton_10.0/docs/DEBUGGING-LINUX.md)
- [Debugging Wine with LLDB and VSCode](https://werat.dev/blog/debugging-wine-with-lldb-and-vscode/)
- [Debugging under Proton](https://apple1417.dev/posts/2023-05-18-debugging-proton)
- [Building Windows DLLs with MinGW](https://www.transmissionzero.co.uk/computing/building-dlls-with-mingw/)
- [MSVC dllexport/dllimport](https://learn.microsoft.com/en-us/cpp/cpp/dllexport-dllimport?view=msvc-170)
- [Unity blog: 8 factors of multiplayer gamedev in small-scale co-op](https://blog.unity.com/games/the-8-factors-of-multiplayer-gamedev-in-small-scale-cooperative-games-ft-breakwaters)
- [MinGW vs MSVC ABI compatibility](https://discourse.julialang.org/t/windows-c-binary-compatibility-mingw-vs-msvc/1250)
- [Clang MSVC compatibility documentation](https://releases.llvm.org/20.1.0/tools/clang/docs/MSVCCompatibility.html)
- CONCERNS.md, CONVENTIONS.md: existing technical debt documentation
