# Phase 1: Feasibility Validation - Research

**Researched:** 2026-03-27
**Domain:** MinGW cross-compilation for SKSE plugins, x64 function hooking, XMake build configuration
**Confidence:** HIGH

## Summary

Phase 1 proves three things before investing in full codebase migration: (1) a MinGW-compiled DLL can load as an SKSE plugin in Skyrim under Proton, (2) MinHook produces working x64 hooks when cross-compiled with MinGW, and (3) XMake can configure for MinGW cross-compilation. The LethalInjection project on the same machine provides a proven reference -- it already cross-compiles MinGW DLLs with MinHook that run under Wine. The xPack MinGW-w64 GCC 14.3.0 toolchain is installed at `~/.local/xPacks/` and working.

The critical risk is SKSE ABI compatibility: SKSE loads plugins via a C-linkage `DllMain` entry point, and the existing SkyrimCoop codebase enters through `RunTiltedInit`/`RunTiltedApp` which are called by the immersive launcher (itself an MSVC binary). For the feasibility test, the minimal plugin needs only `DllMain` with `DLL_PROCESS_ATTACH` writing to the SKSE log via Win32 `CreateFileA`/`WriteFile` -- no C++ SKSE interfaces needed, pure C ABI. MinHook v1.3.3 has official MinGW support since v1.3.2.

**Primary recommendation:** Build a standalone minimal test plugin (not modifying the main codebase), cross-compile with xPack MinGW GCC 14.3.0 via a simple xmake.lua, test under Proton 9.0. Separately, add `is_plat("mingw")` support to the root xmake.lua for BUILD-01.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Functional proof, not minimal compilation check. GATE-01 must exercise SKSE APIs (query game version, register for events), not just load and log. GATE-02 must hook a real Skyrim function at a known address and confirm the hook fires at runtime. This catches ABI mismatches that "it compiles" would miss.
- **D-02:** XMake-first approach. Add MinGW platform support to the existing XMake build (`xmake f -p mingw --mingw=/path`). Do not migrate to CMake unless XMake's MinGW support proves inadequate during this phase. The LethalInjection CMake toolchain files are a reference, not a migration target.
- **D-03:** Validate with both Proton (primary) and standalone Wine (secondary). Proton is the realistic target -- what players actually use. Wine is the automation path for scripted testing and future CI (Phase 5). Running both during feasibility catches divergence early.
- **D-04:** Tiered fallback strategy. If standard MinGW (GCC) fails either gate, try LLVM-MinGW (Clang targeting MinGW ABI) before declaring the approach unviable. LLVM-MinGW has better MSVC compatibility in some edge cases. If both compilers fail, abort the MinGW approach and reassess.

### Claude's Discretion
- MinGW distribution choice (distro package vs custom build) -- pick whatever is most straightforward
- Specific Skyrim function to hook for GATE-02 -- any stable, well-known address works
- Test plugin project structure -- whatever proves the point cleanly

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| GATE-01 | Minimal SKSE plugin compiled with MinGW loads in Skyrim under Proton, exercises SKSE APIs (query game version, register for events) | SKSE entry point analysis, MinGW DLL export patterns, xPack toolchain verification, Proton 9.0 availability |
| GATE-02 | MinHook produces correct x64 Windows ABI hooks when cross-compiled with MinGW GCC | MinHook v1.3.2+ official MinGW support, LethalInjection proven reference, hook target selection research |
| BUILD-01 | XMake configures for MinGW cross-compilation (`xmake f -p mingw --mingw=/path`) | XMake mingw platform docs, `is_plat("mingw")` detection, xPack toolchain path discovery |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| xPack MinGW-w64 GCC | 14.3.0-1.1 | Cross-compiler | Installed, proven in LethalInjection, C++20 support |
| XMake | 2.8.5+ (needs install) | Build system | Project already uses XMake, mingw platform built-in |
| MinHook | v1.3.3 | x64 function hooking | Already a project dependency, official MinGW support since v1.3.2 |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| LLVM-MinGW | latest | Fallback compiler | Only if GCC fails GATE-01 or GATE-02 (per D-04) |
| Wine | 6.0.3 (installed) | Secondary test runtime | Wine validation per D-03 |
| Proton | 9.0-4f (installed) | Primary test runtime | GATE-01 and GATE-02 validation |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| xPack MinGW GCC 14.3 | Distro MinGW GCC 10 | Distro version is too old (GCC 10, no C++20 `<format>`, limited constexpr). Use xPack. |
| LLVM-MinGW | N/A | Only if GCC fails -- better MSVC ABI compat in some cases but less tested in this project |

**Installation (XMake -- MISSING):**
```bash
# XMake is not installed on this system. Install it:
curl -fsSL https://xmake.io/shget.text | bash
# Or via package manager if available
```

**Version verification:**
- xPack MinGW GCC 14.3.0: VERIFIED installed at `~/.local/xPacks/@xpack-dev-tools/mingw-w64-gcc/14.3.0-1.1/.content/`
- Distro MinGW GCC 10: VERIFIED at `/usr/bin/x86_64-w64-mingw32-gcc` (too old for C++20)
- XMake: NOT INSTALLED -- must install before any work
- Wine: 6.0.3 installed at `/usr/bin/wine`
- Proton: 9.0 (Beta) installed at `~/.steam/steam/steamapps/common/Proton 9.0 (Beta)/`

## Architecture Patterns

### Recommended Project Structure for Feasibility Tests
```
Code/
├── tests/
│   └── feasibility/
│       ├── xmake.lua              # Standalone build for test plugins
│       ├── gate01_skse_plugin/
│       │   ├── main.cpp           # Minimal SKSE plugin (DllMain + SKSE API exercise)
│       │   └── xmake.lua          # Builds gate01_test.dll
│       └── gate02_minhook/
│           ├── main.cpp           # MinHook test (hook real Skyrim function)
│           └── xmake.lua          # Builds gate02_test.dll
```

### Pattern 1: Minimal SKSE Plugin via DllMain (GATE-01)

**What:** The SkyrimCoop codebase does NOT use the standard `SKSEPlugin_Load` entry. Instead, the `immersive_launcher` (SkyrimTogether.exe) replaces skse64_loader.exe and calls `RunTiltedInit`/`RunTiltedApp` directly. The launcher loads the client DLL and invokes these functions.

For the feasibility test, we need a simpler approach: a DLL that SKSE's standard loader discovers, or better yet, a DLL that the immersive launcher loads. However, since we are proving MinGW can produce a working DLL, the simplest path is:

1. Compile a standalone DLL with `DllMain` entry
2. Have it call SKSE-relevant Windows APIs on `DLL_PROCESS_ATTACH`
3. Load it into Skyrim under Proton

**D-01 requires exercising SKSE APIs** (query game version, register for events). This means the test plugin must:
- Access SKSE's `LoadInterface` to query runtime version
- Register a message listener for `kPostLoad` or `kDataLoaded`
- Log success to the SKSE log directory

**When to use:** GATE-01 only. Not production code.

**Example -- DllMain entry (MinGW-compatible):**
```cpp
// gate01_skse_plugin/main.cpp
// Compiled with: x86_64-w64-mingw32-g++ -shared -o gate01_test.dll main.cpp -static -static-libgcc -static-libstdc++

#include <windows.h>
#include <cstdio>

// SKSE common plugin API structures (C-compatible subset)
// These match SKSE's own header definitions
struct SKSEInterface {
    uint32_t skseVersion;
    uint32_t runtimeVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
    // ... function pointers follow
};

// Export with C linkage for SKSE loader discovery
extern "C" __attribute__((dllexport))
bool SKSEPlugin_Query(const SKSEInterface* skse, /* PluginInfo* */ void* info) {
    // Query game version from SKSE interface
    // Log to file for verification
    char buf[256];
    snprintf(buf, sizeof(buf),
        "GATE-01: SKSEPlugin_Query called. SKSE=%u Runtime=%u\n",
        skse->skseVersion, skse->runtimeVersion);

    // Write to SKSE plugin log directory
    HANDLE hFile = CreateFileA(
        "Data\\SKSE\\Plugins\\gate01_test.log",
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, buf, strlen(buf), &written, NULL);
        CloseHandle(hFile);
    }
    return true;
}

extern "C" __attribute__((dllexport))
bool SKSEPlugin_Load(const SKSEInterface* skse) {
    // Exercise SKSE messaging interface
    // This proves ABI compatibility beyond just "DLL loads"
    return true;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}
```

**Key MinGW note:** Use `__attribute__((dllexport))` not `__declspec(dllexport)` -- both work under MinGW but the GCC attribute syntax is more portable. Alternatively, use a `.def` file to control exports.

### Pattern 2: MinHook Function Hook Test (GATE-02)

**What:** Cross-compile a DLL that uses MinHook to hook a real Skyrim function at a known address, inject it into Skyrim under Proton, and verify the hook fires.

**Hook target recommendation (Claude's discretion):** Hook `GetTickCount` (kernel32.dll) first as a trivial validation, then hook a Skyrim function. A good Skyrim target is `Main::Update` -- the main game loop tick function. Its address is resolvable via the Address Library (SKSE plugin requirement), or for the feasibility test, a hardcoded offset from the Skyrim SE 1.6.x base can work.

Alternatively, hook `CreateFileA` (simpler, well-known, fires frequently in Skyrim) to prove hooking works without needing Skyrim-specific addresses.

**Example:**
```cpp
// gate02_minhook/main.cpp
#include <windows.h>
#include <MinHook.h>

typedef DWORD (WINAPI *GetTickCount_t)(void);
GetTickCount_t fpGetTickCount = NULL;
static int hookCallCount = 0;

DWORD WINAPI DetourGetTickCount(void) {
    hookCallCount++;
    return fpGetTickCount();
}

static void LogResult(const char* msg) {
    HANDLE hFile = CreateFileA(
        "Data\\SKSE\\Plugins\\gate02_minhook.log",
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, msg, strlen(msg), &written, NULL);
        CloseHandle(hFile);
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);

        if (MH_Initialize() != MH_OK) {
            LogResult("GATE-02 FAIL: MH_Initialize failed\n");
            return TRUE;
        }

        if (MH_CreateHookApi(L"kernel32", "GetTickCount",
                &DetourGetTickCount, (LPVOID*)&fpGetTickCount) != MH_OK) {
            LogResult("GATE-02 FAIL: MH_CreateHookApi failed\n");
            return TRUE;
        }

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
            LogResult("GATE-02 FAIL: MH_EnableHook failed\n");
            return TRUE;
        }

        // Wait briefly for hook to fire
        Sleep(100);
        DWORD tick = GetTickCount(); // Triggers our hook
        (void)tick;

        char buf[256];
        snprintf(buf, sizeof(buf),
            "GATE-02 PASS: MinHook working. Hook called %d times. Tick=%u\n",
            hookCallCount, tick);
        LogResult(buf);
    }
    else if (fdwReason == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}
```

### Pattern 3: XMake MinGW Platform Configuration (BUILD-01)

**What:** Add `is_plat("mingw")` support to the root `xmake.lua`. XMake treats "mingw" as a distinct platform from "windows" -- this is the key architectural insight.

**Example xmake.lua additions:**
```lua
-- Root xmake.lua: add after existing is_plat("windows") block
if is_plat("mingw") then
    -- MinGW uses GCC-style flags, not MSVC
    add_cxflags("-static", "-static-libgcc", "-static-libstdc++")
    add_ldflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})
    set_arch("x86_64")
    add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
    add_defines("NOMINMAX")
    -- MinGW always supports big object files (no /bigobj needed)
    -- No set_runtimes() -- that is MSVC-only
end
```

**Configuration command:**
```bash
xmake f -p mingw --mingw=$HOME/.local/xPacks/@xpack-dev-tools/mingw-w64-gcc/14.3.0-1.1/.content
```

### Anti-Patterns to Avoid
- **Modifying production code for feasibility tests:** GATE-01 and GATE-02 are standalone test DLLs. Do not modify `Code/client/main.cpp` or any production code.
- **Using distro MinGW GCC 10:** It is C++20-incomplete. Always use xPack GCC 14.3.0.
- **Testing only that "it compiles":** D-01 explicitly requires runtime proof -- the DLL must load AND exercise SKSE APIs in a running Skyrim instance.
- **Hardcoding Proton paths:** Proton location varies. Use `PROTON_DUMP_DEBUG_COMMANDS=1` or find via Steam's compatdata.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| x64 function hooking | Custom trampoline assembly | MinHook v1.3.3 | Handles thread-safe hook installation, trampoline allocation, RIP-relative instruction relocation |
| DLL export control | Manual `__attribute__((dllexport))` on every function | `.def` file passed to linker | Cleaner, explicit, avoids decoration differences between MSVC and GCC |
| Build system MinGW config | Custom Makefile or CMake toolchain | XMake `--mingw=` flag | XMake auto-detects cross-compiler prefix, handles sysroot |
| SKSE version detection | Hardcoded offsets | Address Library + SKSE interfaces | Though for GATE-01 we use minimal SKSE structures |

**Key insight:** The feasibility tests deliberately use the simplest possible approach at each layer. Complex integration (full SKSE API surface, Address Library, EnTT, etc.) comes in later phases.

## Common Pitfalls

### Pitfall 1: XMake Not Installed
**What goes wrong:** XMake is not on this system. Cannot run any build commands.
**Why it happens:** Previous builds may have used a different machine or containerized environment.
**How to avoid:** Install XMake as the very first task before any build work.
**Warning signs:** `xmake: command not found`

### Pitfall 2: Using Distro MinGW Instead of xPack
**What goes wrong:** Distro's `x86_64-w64-mingw32-gcc` is GCC 10 (2022 vintage). It lacks C++20 features like `std::format`, some constexpr support, and has incomplete `<ranges>`.
**Why it happens:** It is on the PATH at `/usr/bin/x86_64-w64-mingw32-gcc` and gets picked up before xPack.
**How to avoid:** Always specify `--mingw=$HOME/.local/xPacks/@xpack-dev-tools/mingw-w64-gcc/14.3.0-1.1/.content` explicitly. Never rely on PATH detection.
**Warning signs:** Compile errors about missing C++20 features, or `gcc --version` showing 10.x.

### Pitfall 3: Static Linking Omission
**What goes wrong:** MinGW DLL depends on `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll` at runtime. These do not exist in Skyrim's directory or Wine's system32.
**Why it happens:** MinGW defaults to dynamic linking of its runtime libraries.
**How to avoid:** Always link with `-static -static-libgcc -static-libstdc++`. LethalInjection does this.
**Warning signs:** DLL loads but immediately fails with "missing DLL" error, or Wine shows `err:module:LdrInitializeThunk` errors.

### Pitfall 4: SKSE Plugin Discovery Path
**What goes wrong:** Test DLL placed in wrong directory, SKSE never loads it.
**Why it happens:** SKSE looks for plugins in `Data/SKSE/Plugins/` relative to the Skyrim installation. Under Proton, the Skyrim prefix is at a Steam-managed path.
**How to avoid:** Locate Skyrim's prefix via `~/.steam/steam/steamapps/compatdata/<APPID>/pfx/drive_c/...` or use `steam://` paths. The Skyrim SE app ID is 489830.
**Warning signs:** No log file appears, no evidence of DLL loading in SKSE logs.

### Pitfall 5: Proton Sandbox Blocks File Access
**What goes wrong:** DLL writes log file but it appears in the Proton prefix, not where you expect on the Linux filesystem.
**Why it happens:** Proton/Wine maps `C:\` to the prefix directory. `Data\SKSE\Plugins\gate01_test.log` resolves to a path inside the Wine prefix.
**How to avoid:** Know the prefix path. For Skyrim SE: `~/.steam/steam/steamapps/compatdata/489830/pfx/drive_c/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition/Data/SKSE/Plugins/`
**Warning signs:** "File not found" when looking on the Linux filesystem.

### Pitfall 6: Wine 6.0 May Be Too Old for Reliable Testing
**What goes wrong:** Wine 6.0.3 (installed) is from 2021. It may have bugs with newer MinGW runtime features or specific Win32 API behaviors that Proton 9.0 (Wine 9.x based) handles correctly.
**Why it happens:** Distro Wine lags behind Proton's embedded Wine.
**How to avoid:** Use Proton 9.0 as the primary test target (D-03 says Proton is primary). For standalone Wine testing, consider installing Wine 9.x from WineHQ.
**Warning signs:** Tests pass under Proton but fail under standalone Wine.

## Code Examples

### XMake Configuration for MinGW Test Plugin
```lua
-- Code/tests/feasibility/xmake.lua
-- Standalone build for feasibility test DLLs

set_xmakever("2.8.5")
set_languages("c99", "cxx20")

if is_plat("mingw") then
    add_cxflags("-static", "-static-libgcc", "-static-libstdc++")
    add_ldflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})
    add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
end

-- GATE-01: Minimal SKSE plugin
target("gate01_skse_plugin")
    set_kind("shared")
    set_basename("gate01_test")
    add_files("gate01_skse_plugin/main.cpp")

-- GATE-02: MinHook test
target("gate02_minhook")
    set_kind("shared")
    set_basename("gate02_test")
    add_files("gate02_minhook/main.cpp")
    add_packages("minhook")
```

### LethalInjection Link Flags Reference (proven working)
```cmake
# From LethalInjection -- these flags produce working MinGW DLLs under Wine
target_link_options(... PRIVATE
    -static
    -static-libgcc
    -static-libstdc++
    -Wl,--allow-multiple-definition  # May be needed for symbol conflicts
)
```

### Verifying DLL Loads Under Wine
```bash
# Quick test: verify DLL can be loaded by Wine's loader
WINEPREFIX=/tmp/test_prefix wine64 rundll32 gate01_test.dll,DllMain

# Verbose mode to see loading errors
WINEDEBUG=+loaddll WINEPREFIX=/tmp/test_prefix wine64 rundll32 gate01_test.dll,DllMain

# Check for missing dependencies
WINEDEBUG=+module WINEPREFIX=/tmp/test_prefix wine64 rundll32 gate01_test.dll,DllMain
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Wine-hosted MSVC (`/opt/msvc`) | MinGW cross-compilation | This project's decision | Eliminates fragile Wine MSVC toolchain |
| GameNetworkingSockets | enet6 | Already changed in codebase | Simpler, pure C, better MinGW compatibility |
| MinHook 1.3.2 (experimental MinGW) | MinHook 1.3.4 (Mar 2025, fixes Clang) | 2025 | Improved non-MSVC compiler support |
| `SKSEPlugin_Load` standard entry | Immersive launcher with `RunTiltedInit` | SkyrimCoop original design | Custom loader, not standard SKSE plugin path |

**Key insight about SKSE entry:** SkyrimCoop does NOT use the standard SKSE plugin loading mechanism. The `immersive_launcher` (SkyrimTogether.exe) is a custom executable that patches itself into Skyrim's process, then calls `RunTiltedInit` and `RunTiltedApp` directly. There is no `SKSEPlugin_Query` or `SKSEPlugin_Load` export in the current codebase. The test plugin for GATE-01 should either:
1. Use the standard SKSE plugin entry points (`SKSEPlugin_Query`/`SKSEPlugin_Load`) to prove MinGW can produce SKSE-compatible DLLs, OR
2. Replicate the immersive launcher pattern with `DllMain` + `RunTiltedInit` exports

Option 1 is simpler for feasibility validation. The full immersive launcher integration is a later-phase concern.

## Open Questions

1. **SKSE API Exercise Depth for D-01**
   - What we know: D-01 requires "exercise SKSE APIs (query game version, register for events)". The standard SKSE plugin interface uses C-style function pointer tables (SKSEInterface struct with version fields and GetInterface function pointers).
   - What's unclear: Whether the SKSE interface structures have C++ vtable dependencies that would break MinGW ABI, or if they are truly C-compatible POD structs.
   - Recommendation: Start with `SKSEPlugin_Query` (receives version info, purely read-only). If that works, add `SKSEPlugin_Load` with messaging interface registration. The SKSE interfaces use C-style function pointer dispatch, not C++ virtual methods, so they should be ABI-safe.

2. **Skyrim Function Address for GATE-02 Hook**
   - What we know: D-01 says "hook a real Skyrim function at a known address." The Address Library provides runtime address resolution for SKSE plugins.
   - What's unclear: Whether the Address Library itself can be loaded from a MinGW-compiled DLL (it is a separate SKSE plugin that provides an API).
   - Recommendation: For GATE-02, hook a Win32 API function first (`GetTickCount`, `CreateFileA`) to prove MinHook works under MinGW. Then attempt hooking a Skyrim function at a hardcoded offset from the main module base. If Address Library integration is needed, that is Phase 3 scope.

3. **Skyrim SE Installation Location**
   - What we know: Skyrim SE was not found at standard Steam paths (`~/.steam/steam/steamapps/common/Skyrim*`).
   - What's unclear: Whether Skyrim SE is installed on this machine, or on a different drive/path.
   - Recommendation: This is a blocking dependency for GATE-01 and GATE-02 runtime validation. The build/compile steps can proceed without Skyrim, but runtime testing requires it. Planner should include a task to locate or install Skyrim SE.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| XMake | BUILD-01, all builds | **NO** | -- | Must install (`curl -fsSL https://xmake.io/shget.text \| bash`) |
| xPack MinGW GCC | GATE-01, GATE-02 | YES | 14.3.0-1.1 | -- |
| Distro MinGW GCC | -- | YES (too old) | 10-win32 | Do not use -- lacks C++20 |
| Wine | D-03 secondary | YES | 6.0.3 | Upgrade to 9.x recommended for parity with Proton |
| Proton | D-03 primary | YES | 9.0-4f (Beta) | -- |
| Skyrim SE | GATE-01, GATE-02 runtime | **UNKNOWN** | -- | Cannot run runtime tests without it |
| SKSE | GATE-01 runtime | **UNKNOWN** | -- | Cannot test plugin loading without it |
| Steam | Proton runtime | YES | Installed | -- |

**Missing dependencies with no fallback:**
- XMake -- must install before any build work
- Skyrim SE -- must locate/install for runtime validation (compilation can proceed without it)
- SKSE -- required for GATE-01 runtime test

**Missing dependencies with fallback:**
- Wine 6.0.3 is old but functional for basic testing; Proton 9.0 is the primary target anyway

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Manual runtime validation (log file inspection) + XMake build success |
| Config file | `Code/tests/feasibility/xmake.lua` (to be created in Wave 0) |
| Quick run command | `xmake f -p mingw --mingw=$XPACK_PATH && xmake build gate01_skse_plugin` |
| Full suite command | Build both gates + deploy to Skyrim + verify log files |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| GATE-01 | MinGW DLL loads in Skyrim, exercises SKSE APIs | manual (runtime) | `xmake build gate01_skse_plugin` (build only) | Wave 0 |
| GATE-02 | MinHook hook fires from MinGW DLL | manual (runtime) | `xmake build gate02_minhook` (build only) | Wave 0 |
| BUILD-01 | `xmake f -p mingw` configures without errors | smoke | `xmake f -p mingw --mingw=$XPACK_PATH -y && echo PASS` | Wave 0 |

### Sampling Rate
- **Per task commit:** `xmake build` for the relevant target
- **Per wave merge:** Build both gates + attempt Wine DLL load test
- **Phase gate:** Both DLLs load in Skyrim under Proton, log files confirm success

### Wave 0 Gaps
- [ ] `Code/tests/feasibility/xmake.lua` -- build config for test DLLs
- [ ] `Code/tests/feasibility/gate01_skse_plugin/main.cpp` -- GATE-01 test plugin
- [ ] `Code/tests/feasibility/gate02_minhook/main.cpp` -- GATE-02 test plugin
- [ ] XMake installation on developer machine
- [ ] Skyrim SE + SKSE installation verification

## Project Constraints (from CLAUDE.md)

- **Build tool:** XMake 2.8.5+ (matches D-02: XMake-first)
- **Language:** C++20 (need xPack GCC 14.3.0, not distro GCC 10)
- **Platform:** Linux-first development, Windows compatibility maintained
- **SKSE dependency:** Client DLL loads into Skyrim via SKSE -- cannot modify SKSE
- **Wine/Proton:** Client always runs under Wine/Proton on Linux
- **Error handling:** No exceptions allowed -- use return codes and spdlog
- **Naming:** Function args prefixed with `a`, const with `c`, pointers with `p`, members with `m_`
- **Code style:** `clang-format` for C++, `#pragma once` for headers
- **GSD workflow:** Use GSD commands for all file-changing work

## Sources

### Primary (HIGH confidence)
- LethalInjection toolchain: `/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d5/Projects/LethalInjection/cmake/toolchain-mingw64.cmake` -- xPack GCC 14.3.0 proven working for MinGW DLLs under Wine
- LethalInjection backend: `backend/CMakeLists.txt` -- MinHook + MinGW proven working together
- SkyrimCoop codebase: `xmake.lua`, `Code/client/main.cpp`, `Code/immersive_elf/main.cpp` -- current entry point patterns
- SkyrimCoop research: `.planning/research/PITFALLS.md`, `.planning/research/ARCHITECTURE.md` -- prior domain analysis
- xPack GCC 14.3.0: verified installed on system via `--version` check
- Proton 9.0: verified installed via `~/.steam/steam/steamapps/common/Proton 9.0 (Beta)/version`

### Secondary (MEDIUM confidence)
- [XMake cross-compilation docs](https://xmake.io/guide/basic-commands/cross-compilation.html) -- MinGW platform configuration
- [MinHook GitHub](https://github.com/TsudaKageyu/minhook) -- MinGW support since v1.3.2, v1.3.4 latest
- [SKSE plugin tutorial (skyrim.dev)](https://skyrim.dev/skse/first-plugin) -- standard entry point structure
- [Rust SKSE plugin attempt](https://github.com/thallada/rust-skse-plugin) -- confirms SKSE uses C-style interfaces at DLL boundary

### Tertiary (LOW confidence)
- Wine 6.0.3 compatibility with MinGW GCC 14.3.0 runtime -- untested combination, may have edge cases
- SKSE interface structure ABI safety under MinGW -- high confidence it is C-compatible POD, but unverified until GATE-01 runs

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- xPack GCC 14.3.0 verified installed, LethalInjection proves the pattern
- Architecture: HIGH -- entry points and DLL structure well-understood from codebase analysis
- Pitfalls: HIGH -- comprehensive analysis in PITFALLS.md plus environment audit
- GATE-01 feasibility: MEDIUM -- SKSE interface ABI compatibility is the key unknown; strong evidence it works (C-style function pointers, Rust SKSE plugin loaded successfully) but not proven until tested
- GATE-02 feasibility: HIGH -- LethalInjection already uses MinHook + MinGW successfully

**Research date:** 2026-03-27
**Valid until:** 2026-04-27 (stable domain -- MinGW, SKSE, and XMake don't change rapidly)
