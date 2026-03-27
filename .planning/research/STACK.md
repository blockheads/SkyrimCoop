# Technology Stack: Linux Cross-Compilation & Debug Toolchain

**Project:** SkyrimCoop
**Researched:** 2026-03-27
**Focus:** Cross-compiling Windows SKSE plugin DLL from Linux, debugging under Wine/Proton, CI/CD, automated testing

## Recommended Stack

### Cross-Compilation Toolchain

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| xPack MinGW-w64 GCC | 14.3.0-1.1 | C++20 cross-compiler producing Windows x64 PE/DLLs | Already installed at `~/.local/xPacks/`, proven in LethalInjection project, full C++20 support, reproducible binary distribution. Upstream distro MinGW (GCC 10) is too old for C++20. | HIGH |
| XMake | 2.9.8+ | Build orchestration with `-p mingw` platform | Already used by project, has first-class MinGW cross-compilation support (`xmake f -p mingw --mingw=/path`), auto-detects toolchain prefix, handles package cross-compilation. Avoids CMake migration. | HIGH |
| ccache | 4.x (system) | Compilation cache for incremental rebuilds | 5-10x speedup on incremental builds, works with MinGW cross-compilers via `CMAKE_C_COMPILER_LAUNCHER` or XMake equivalent. LethalInjection already uses it. | HIGH |
| DWARF debug symbols | (via `-g -gdwarf-4`) | Debug info in cross-compiled DLLs | MinGW produces DWARF (not PDB). GDB reads DWARF natively. This is the critical difference from MSVC builds -- no PDB conversion needed when using GDB. | HIGH |

### Build Configuration

| Setting | Value | Why |
|---------|-------|-----|
| C++ Standard | C++20 (`-std=gnu++20`) | Match existing codebase. Use `gnu++20` (not `c++20`) for MinGW SEH extensions, same as LethalInjection uses for C++23. |
| Static linking | `-static -static-libgcc -static-libstdc++` | Zero runtime dependencies on target machine. LethalInjection pattern. Eliminates need to ship MinGW runtime DLLs. |
| Architecture | x86_64 only | Skyrim SE is x64 only. No 32-bit target needed. |
| Optimization | `-O2 -g` for releasedbg, `-O0 -g` for debug | Need debug symbols in all dev builds for Wine debugging. `-O2` over `-O3` to reduce code size and improve debuggability. |
| Windows target | `-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00` | Match existing MSVC config (Windows 10+). |
| SSE/SSE2/SSE3 | `-msse -msse2 -msse3 -mssse3` | Match existing SIMD config. MinGW GCC supports these natively. |

### Debugging

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| GDB | 15.x+ (build from source or PPA) | Primary debugger for Wine process attachment | GDB understands DWARF in MinGW-compiled DLLs natively. `gdb -p <linux_pid>` attaches to Wine process, `info proc mappings` finds DLL load address, `add-symbol-file` loads DWARF symbols. System GDB 12.1 works but 15+ has better MinGW type support. | HIGH |
| winedbg (GDB proxy mode) | Ships with Wine | Alternative: winedbg as GDB server | `winedbg --gdb --attach <windows_pid>` exposes a GDB-compatible remote protocol. Understands Wine's PE loader, sees Windows modules. Connect with `gdb -ex "target remote :port"`. | MEDIUM |
| LLDB (Wine-patched) | 21.1.5 (custom build exists) | Secondary debugger, VSCode integration | Already built at `/Projects/llvm-project-21.1.5-build/bin/lldb`. Needs werat/llvm-project-wine DYLD plugin patches to see PE modules. Without patches, only sees `wine64-preloader`. Patches are based on LLVM 14.x and need rebasing for 21.x -- this is non-trivial work. | LOW |
| Wine | 9.0+ (upgrade from 6.0.3) | Windows compatibility layer | Current system Wine 6.0.3 is very old (2021). Wine 9.0+ has significantly better debugging support, improved PE module loading, and NTSYNC (kernel 6.14+) for performance. Wine 9/10/11 dramatically improves DLL loading and debug symbol resolution. | HIGH |

**Debugger recommendation:** Use GDB as primary. The existing debug scripts (`attach_gdb.sh`) already work. GDB with MinGW DWARF symbols is the path of least resistance -- no PDB conversion, no custom LLDB patches, no winedbg quirks. Upgrade Wine to 9.0+ for better process debugging support.

**Do NOT invest in LLDB Wine patches** until GDB-based workflow is proven insufficient. The werat patches target LLVM 14.x and rebasing to 21.x is significant effort for uncertain benefit.

### CI/CD

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| GitHub Actions | (existing) | CI runner platform | Already configured with linux.yml and windows.yml workflows. Extend, don't replace. | HIGH |
| `ubuntu-24.04` runner | Latest LTS | CI build environment | 22.04 is current but 24.04 has newer system packages. MinGW will be installed via xPack regardless. | MEDIUM |
| xPack MinGW (CI-installed) | 14.3.0 | Cross-compiler on CI | Download xPack tarball in CI step (no apt). Reproducible across dev machine and CI. Cache in `actions/cache`. | HIGH |
| XMake | 2.9.8 | Build system on CI | Already using `xmake-io/github-action-setup-xmake@v1`. Add MinGW platform config step. | HIGH |
| Docker | (for server) | Server build container | Existing Dockerfile already works. No changes needed for server-only builds. | HIGH |

**CI workflow strategy:** Add a new `linux-cross.yml` workflow that:
1. Downloads and caches xPack MinGW 14.3.0
2. Runs `xmake f -p mingw --mingw=$XPACK_ROOT -m releasedbg`
3. Builds client DLL with `xmake -y`
4. Runs native (Linux) unit tests separately
5. Uploads `.dll` artifact

### Testing

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| Catch2 | 3.x (upgrade from 2.13.9) | Unit tests for encoding, serialization, math | Already in use (2.13.9). Catch2 3.x is current, has better CMake integration and partitioned headers. Upgrade is low-risk. | HIGH |
| Catch2 + Trompeloeil | 47+ | Mocking for service-level integration tests | Catch2 has no built-in mocking. Trompeloeil is the standard C++ mocking framework, integrates cleanly with Catch2. Use for mocking TransportService, network layer in CharacterService tests. | MEDIUM |
| Native Linux test binary | (via XMake) | Run unit tests without Wine | Build encoding, common, and server code natively on Linux for testing. These modules have no Windows dependencies. Only the SKSE client integration code requires Windows. | HIGH |
| Wine headless | 9.0+ | E2E: run cross-compiled test binary under Wine | For integration tests that exercise Windows-specific codepaths (message serialization with Windows types), run the MinGW-compiled test binary under `wine64`. Works in CI with `xvfb-run` for display. | MEDIUM |
| Custom test harness | (build ourselves) | E2E host+client sync testing | No off-the-shelf framework for game mod sync testing. Build a lightweight harness: spawn embedded server + 2 mock clients (using the real networking code), verify state convergence. Run natively on Linux (server code is Linux-native). | LOW |

**Testing strategy layers:**
1. **Unit tests (native Linux):** All encoding, math, serialization, ECS component logic. These compile natively, no Wine needed. Run on every commit.
2. **Unit tests (MinGW via Wine):** Windows-specific code paths. Run MinGW-compiled test binary under `wine64` in CI. Catches ABI/linkage issues.
3. **Integration tests (native Linux):** Server + mock client using real networking stack. Spawn `GameServer` + connect `TransportService` client in same process or two processes. Verify message round-trips.
4. **Sync validation tests:** Deterministic test scenarios (spawn actor, move, verify position converges on both sides). Hardest to build, highest value.

### Supporting Infrastructure

| Technology | Version | Purpose | Why | Confidence |
|------------|---------|---------|-----|------------|
| spdlog | 1.13.0 (existing) | Logging | Already used. No change needed. | HIGH |
| Sentry | 0.7.1 (existing) | Crash reporting | Already used. MinGW builds need DWARF upload instead of PDB. Use `sentry-cli upload-dif` with DWARF files. | MEDIUM |
| compile_commands.json | (via XMake) | IDE/clangd integration | `xmake project -k compile_commands` generates for clangd. Works with MinGW cross-compile (clangd understands cross-compile flags). | HIGH |

## Alternatives Considered

| Category | Recommended | Alternative | Why Not |
|----------|-------------|-------------|---------|
| Cross-compiler | xPack MinGW GCC 14.3 | System MinGW (apt) | System MinGW is GCC 10 (2020), no C++20 support. xPack is self-contained, version-pinned, proven in LethalInjection. |
| Cross-compiler | xPack MinGW GCC 14.3 | llvm-mingw (Clang) | Clang cross-compilation works but XMake's MinGW support assumes GCC. LethalInjection uses GCC. Switching compilers introduces ABI risk with existing MSVC-compiled SKSE headers. |
| Cross-compiler | xPack MinGW GCC 14.3 | Zig cc | Zig's C/C++ compiler can cross-compile to Windows, but C++20 template-heavy code with SKSE headers is untested with Zig's Clang frontend. Too experimental for this codebase. |
| Build system | XMake (extend) | CMake (migrate) | LethalInjection uses CMake but SkyrimCoop already uses XMake with 60+ configured deps. Migration cost is enormous. XMake has native MinGW platform support. |
| Build system | XMake (extend) | Dual: CMake for MinGW, XMake for MSVC | Maintaining two build systems is a maintenance nightmare. XMake can do both. |
| Debugger | GDB | LLDB (Wine-patched) | Custom LLDB build already exists but Wine DYLD patches are for LLVM 14.x, not 21.x. Rebasing is significant work. GDB works today with MinGW DWARF. |
| Debugger | GDB | winedbg standalone | winedbg's CLI is non-standard, poor C++ support, no scripting. Use winedbg only as GDB server backend. |
| Debugger | GDB | Visual Studio remote debugging | Requires Windows. Defeats the Linux-first goal. |
| Wine version | 9.0+ (upgrade) | Keep Wine 6.0.3 | Wine 6 is 5 years old. Missing critical debugging improvements, PE module handling, and NTSYNC performance. |
| Test mock | Trompeloeil | FakeIt | Trompeloeil is more actively maintained, better C++17/20 support, more expressive expectation syntax. |
| Test mock | Trompeloeil | GoogleMock | Already have GTest v1.14.0 which includes GMock. Could use GMock instead, but Catch2 + Trompeloeil is a cleaner pairing since tests already use Catch2. |
| E2E testing | Custom harness | Selenium/Playwright for UI | UI testing is secondary. Core sync correctness matters more. Build game-logic E2E first. |

## Installation

### Development Machine Setup

```bash
# 1. xPack MinGW (already installed)
# Verify: ~/.local/xPacks/@xpack-dev-tools/mingw-w64-gcc/14.3.0-1.1/
$HOME/.local/xPacks/@xpack-dev-tools/mingw-w64-gcc/14.3.0-1.1/.content/bin/x86_64-w64-mingw32-g++ --version

# 2. XMake (install if missing)
curl -fsSL https://xmake.io/shget.text | bash

# 3. Wine 9.0+ (upgrade from 6.0.3)
# Option A: WineHQ repository
sudo dpkg --add-architecture i386
sudo mkdir -pm755 /etc/apt/keyrings
sudo wget -O /etc/apt/keyrings/winehq-archive.key https://dl.winehq.org/wine-builds/winehq.key
sudo wget -NP /etc/apt/sources.list.d/ https://dl.winehq.org/wine-builds/ubuntu/dists/$(lsb_release -cs)/winehq-$(lsb_release -cs).sources
sudo apt update && sudo apt install --install-recommends winehq-stable

# 4. GDB (system or build from source for 15.x)
sudo apt install gdb
# Or build GDB 15+ from source for better MinGW type support

# 5. ccache
sudo apt install ccache

# 6. Trompeloeil (for test mocking)
# Add to xmake.lua: add_requires("trompeloeil")
```

### XMake MinGW Configuration

```bash
# Set global MinGW path (one-time)
xmake g --mingw=$HOME/.local/xPacks/@xpack-dev-tools/mingw-w64-gcc/14.3.0-1.1/.content

# Configure for MinGW cross-compilation
xmake f -p mingw -a x86_64 -m releasedbg

# Build
xmake -y

# Or explicitly specify SDK
xmake f -p mingw --mingw=$HOME/.local/xPacks/@xpack-dev-tools/mingw-w64-gcc/14.3.0-1.1/.content --sdk=$HOME/.local/xPacks/@xpack-dev-tools/mingw-w64-gcc/14.3.0-1.1/.content -a x86_64 -m releasedbg
```

### CI Installation (GitHub Actions)

```yaml
# In linux-cross.yml workflow
- name: Install xPack MinGW
  run: |
    mkdir -p $HOME/.local/xPacks
    curl -L https://github.com/xpack-dev-tools/mingw-w64-gcc-xpack/releases/download/v14.3.0-1/xpack-mingw-w64-gcc-14.3.0-1-linux-x64.tar.gz \
      | tar xz -C $HOME/.local/xPacks/

- name: Cache xPack MinGW
  uses: actions/cache@v4
  with:
    path: ~/.local/xPacks
    key: xpack-mingw-14.3.0-1
```

## Key Compatibility Risks

### MSVC-to-MinGW ABI Concerns

The existing codebase compiles with MSVC. Switching to MinGW GCC introduces ABI differences:

1. **SKSE headers:** SKSE is built with MSVC. The SKSE plugin DLL interface is C-style (`extern "C"` exports), so ABI is compatible. Internal C++ objects from SKSE headers may have different layouts -- test thoroughly.

2. **Exception handling:** MSVC uses SEH, MinGW uses DWARF or SJLJ. Use `-fexceptions -municode` and SEH-capable MinGW (xPack uses SEH by default for x64).

3. **Name mangling:** Different between MSVC and GCC. Only matters for C++ exports. SKSE plugin entry point is `extern "C"` so this is fine for the plugin interface.

4. **Static runtime:** `-static -static-libgcc -static-libstdc++` eliminates MinGW runtime DLL dependencies, matching MSVC's `/MT` static runtime behavior.

5. **Third-party deps:** All XMake-managed deps will rebuild with MinGW. Vendored deps (imgui, DirectXTK, cpp-httplib) need manual verification. DirectXTK will NOT compile with MinGW -- it requires MSVC. This is a client-only dep used for D3D11 overlay rendering.

### DirectXTK / D3D11 Overlay

DirectXTK is MSVC-only. Two options:
- **Option A:** Strip DirectXTK dependency, use imgui with MinGW-compatible D3D11 headers (MinGW has d3d11.h).
- **Option B:** Keep MSVC build path for UI overlay components only, cross-compile everything else.

This is the single biggest technical risk in the MinGW migration.

### CEF (Chromium Embedded Framework)

CEF binary distribution is built with MSVC. Linking MinGW-compiled code against MSVC-built CEF may require import library generation (`gendef` + `dlltool`). This is a known MinGW pattern but adds complexity.

## Sources

- [xPack MinGW-w64 GCC releases](https://github.com/xpack-dev-tools/mingw-w64-gcc-xpack) - Toolchain distribution
- [XMake Cross Compilation docs](https://xmake.io/guide/basic-commands/cross-compilation.html) - MinGW platform configuration
- [XMake Toolchain Configuration](https://xmake.io/guide/project-configuration/toolchain-configuration.html) - Advanced toolchain setup
- [werat/llvm-project-wine](https://github.com/werat/llvm-project-wine) - LLDB Wine debugging patches
- [Debugging Wine with LLDB and VSCode](https://werat.dev/blog/debugging-wine-with-lldb-and-vscode/) - LLDB Wine DYLD plugin details
- [drmingw](https://github.com/jrfonseca/drmingw) - MinGW postmortem debugging tools
- LethalInjection reference project - Proven MinGW cross-compile pattern (local: `/Projects/LethalInjection/`)
- Existing SkyrimCoop debug scripts - `attach_gdb.sh`, `debug_wine.sh`, `WINE_DEBUGGING_GUIDE.md` (local)

---

*Stack research: 2026-03-27*
