# Technology Stack

**Analysis Date:** 2026-03-27

## Languages

**Primary:**
- C++20 - Core multiplayer networking, client plugin, server executable, game integration
- TypeScript 5.1+ - UI framework (Angular), type-safe frontend development
- Lua 5.1 - Server-side scripting engine for game logic customization (Sol2 bindings)
- C99 - Legacy support code

## Runtime

**Environment:**
- Windows (Client/Server development)
- Linux x86_64 + ARM64/v8 (Server deployment)
- Docker containerization for server distribution

**Package Manager:**
- XMake 2.8.5+ - Primary build orchestration for C++, servers, and tests
- pnpm - Angular UI package manager
- Node 18.x (implied by toolchain)

## Frameworks

**Core Networking:**
- ENet6 (UDP-based multiplayer protocol) - Replaces GameNetworkingSockets for P2P simplification
- TiltedConnect (custom networking wrapper) - Packet handling and reliability
- TiltedCore v0.2.7 - Core networking and utility library

**Game Integration:**
- SKSE (Skyrim Script Extender) - Plugin API for hooking Skyrim systems
- Skyrim Address Library - Runtime address resolution for SKSE hooks

**Entity Component System:**
- EnTT v3.10.0 - ECS framework for actors, components, systems architecture

**UI Framework:**
- Angular 16.1.2 - Web-based overlay framework
- Chromium Embedded Framework (CEF) 100.0.24 - Browser runtime for in-game UI rendering
- RxJS 7.8.1 - Reactive programming for UI state management
- NgELF (Elf Store) 2.3.2 - Angular reactive state management library
- Transloco 4.3.0 - Internationalization/localization

**Testing:**
- Catch2 2.13.9 - C++ unit test framework (header-only)
- GTest v1.14.0 - Google Test framework for C++ tests
- Ngx-Playwright v0.4.2 - E2E testing for Angular (browser-based)

**Build/Dev:**
- CMake 3.30.2 (legacy support via xmake)
- Clang-format - C++ code formatting
- Prettier 2.8.8 - TypeScript/JSON formatting
- ESLint 8.43.0 - TypeScript linting with Angular-specific rules
- Visual Studio Project generation via XMake

## Key Dependencies

**Critical:**
- enet6 - Replaces GameNetworkingSockets, UDP-based P2P networking
- libuv v1.48.0 - Event-driven I/O, async operations (underlying enet6 dependency)
- Sentry-Native v0.7.1 - Crash reporting with Crashpad backend (both client and server)
- Discord SDK 3.2.1 (Windows only) - Discord presence, overlay integration
- OpenSSL 1.1.1-w - TLS/cryptography (dependency via sentry-native)
- CryptoC++ 8.9.0 - Cryptographic algorithms for message integrity

**Infrastructure:**
- spdlog v1.13.0 - Structured logging (both client and server)
- GLM 0.9.9+8 - 3D mathematics (vectors, matrices, quaternions)
- cpp-httplib 0.14.0 (vendored) - Lightweight HTTP server for admin panel
- Zlib v1.3.1 - Compression for network payloads
- Mimalloc 2.2.4 - High-performance memory allocator
- Hopscotch-map v2.3.1 - Fast hash map implementation
- Snappy 1.1.10 - Fast compression library (network optimization)
- Recast Navigation v1.6.0 - Navigation mesh generation
- MinHook v1.3.3 - Function hooking (SKSE integration)
- XByak v7.06 - x86/x64 assembler code generator

**UI Dependencies:**
- @angular/animations, @angular/cdk, @angular/common, @angular/compiler - Core Angular
- @angular/forms, @angular/router, @angular/platform-browser - Angular modules
- @fortawesome/fontawesome-svg-core v6.4.0, @fortawesome/angular-fontawesome v0.13.0 - Icon library
- @ngneat/elf-devtools v1.3.0 - Redux DevTools integration
- @ngneat/elf-entities v4.4.4 - Entity management store
- @ngneat/loadoff v2.1.0 - Angular loading state management
- @ngneat/reactive-forms v5.0.2 - Reactive form utilities
- tslib 2.5.3 - TypeScript runtime library
- zone.js 0.13.1 - Angular zone polyfill

**Dev/Test Dependencies:**
- @angular-devkit/build-angular 16.1.1 - Angular build system
- @angular/cli 16.1.1 - Angular command-line interface
- @angular/compiler-cli 16.1.2 - Angular template compiler
- @typescript-eslint/eslint-plugin v5.60.0 - TypeScript ESLint rules
- @angular-eslint plugins (6 plugins) - Angular-specific linting
- ts-node 10.9.1 - TypeScript runtime for Node
- Mem 1.0.0 - Memory utilities
- ImGui v1.89.7 (Windows, vendored in Code/external) - Debug UI rendering
- DirectXTK (Windows, vendored in Code/external) - Direct3D utilities for overlay

## Configuration

**Environment:**
- Platform detection: Windows vs. Linux via XMake `is_plat()` checks
- Build modes: `debug`, `releasedbg`, `release` via XMake mode system
- Static runtime linking on Windows (MT flag for zero dependency on MSVC runtime)
- Target: Windows 10+ (0x0A00 via `_WIN32_WINNT`)
- Unity builds supported via `--unitybuild=y` config flag

**Build Settings:**
- SIMD optimization: SSE, SSE2, SSE3, SSSE3, NEON vector extensions enabled
- Large object files: `/bigobj` flag for Windows MSVC
- Full debug info: PDB paths embedded for debugger integration
- Warnings: All enabled (`set_warnings("all")`)

**C++ Standard:**
- Language: C++20 standard with modern features
- Compilation targets: x64 architecture exclusively

## Platform Requirements

**Development:**
- Windows 10+ (Visual Studio 2019+ recommended via XMake project generation)
- Linux development support (XMake on Linux)
- Git with recursive submodule support
- XMake 2.8.5 or later installed

**Production:**
- **Client:** Windows 10+ with SKSE 2.2+ installed, Skyrim Special Edition license
- **Server:** Ubuntu 22.04 (Docker deployable) or Windows server
  - UDP port 10578 open (default networking port)
  - 2+ GB RAM recommended for concurrent players
  - Multi-architecture Docker support (amd64/x86_64 and arm64/v8)

## Symbol Management

- **Symbol Upload:** Sentry CLI for uploading .pdb (Windows) and .debug (Linux) files
- **Automatic Crash Reporting:** Sentry Integration with Crashpad backend, auto-session tracking disabled
- **Debug Symbols:** BuildInfo.h auto-generated with branch and commit hash for telemetry

---

*Stack analysis: 2026-03-27*
