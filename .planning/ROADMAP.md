# Roadmap: SkyrimCoop

## Overview

Milestone 1 takes SkyrimCoop from a Windows-only MSVC build to a fully Linux-native development toolchain. The critical path runs through MinGW cross-compilation feasibility validation, progressive library compilation (Tier 1-2 then Tier 3 client DLL), and ultimately loading a MinGW-built DLL into Skyrim under Proton. Debug tooling, testing infrastructure, and the CEF-to-ImGui UI port build on that foundation. By the end, the entire develop-build-debug-test cycle runs on Linux without touching Windows.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [ ] **Phase 1: Feasibility Validation** - Prove MinGW cross-compilation is viable before investing in full migration
- [ ] **Phase 2: MSVC Compatibility & Core Libraries** - Platform-independent libraries compile under MinGW
- [ ] **Phase 3: Client DLL Cross-Compilation** - MinGW produces a client DLL that loads in Skyrim under Proton
- [ ] **Phase 4: Linux Debug Workflow** - One-command GDB attach with full symbol resolution for the cross-compiled DLL
- [ ] **Phase 5: Testing & CI Pipeline** - Native Linux tests, mock client harness, and automated CI on every push
- [ ] **Phase 6: ImGui UI Port** - Replace CEF overlay with MinGW-compilable ImGui UI

## Phase Details

### Phase 1: Feasibility Validation
**Goal**: Confirm that MinGW cross-compilation can produce working SKSE plugins and function hooks before investing in full codebase migration
**Depends on**: Nothing (first phase)
**Requirements**: GATE-01, GATE-02, BUILD-01
**Success Criteria** (what must be TRUE):
  1. A minimal SKSE plugin compiled with MinGW loads in Skyrim under Proton and writes to the SKSE log
  2. MinHook produces working x64 function hooks when cross-compiled with MinGW GCC
  3. `xmake f -p mingw --mingw=/path` configures the SkyrimCoop project without errors
**Plans**: 2 plans
Plans:
- [x] 01-01-PLAN.md -- Install XMake, add MinGW platform config, scaffold feasibility test plugins
- [x] 01-02-PLAN.md -- Cross-compile gate DLLs, Wine smoke test, Proton runtime validation

### Phase 2: MSVC Compatibility & Core Libraries
**Goal**: All platform-independent code (encoding, networking, common, server) compiles under MinGW and produces working libraries
**Depends on**: Phase 1
**Requirements**: BUILD-02, BUILD-03
**Success Criteria** (what must be TRUE):
  1. An MSVC compatibility header resolves all `__declspec`, `#pragma comment(lib)`, and struct alignment differences so source files compile unchanged under GCC
  2. Tier 1-2 static libraries (encoding, common, server, networking) build successfully with MinGW and link without unresolved symbols
  3. Existing Catch2 unit tests for encoding/serialization pass when compiled natively on Linux against the MinGW-built libraries
**Plans**: TBD

### Phase 3: Client DLL Cross-Compilation
**Goal**: MinGW produces a SkyrimTogetherClient.dll that loads into Skyrim SE under Proton and connects to a server
**Depends on**: Phase 2
**Requirements**: BUILD-04, BUILD-05
**Success Criteria** (what must be TRUE):
  1. The client DLL compiles under MinGW with CEF and DirectXTK excluded (stubbed or replaced)
  2. The MinGW-compiled DLL loads into Skyrim SE via SKSE under Proton without crashing
  3. A client using the MinGW-built DLL can connect to a locally running server and see basic character sync
**Plans**: TBD

### Phase 4: Linux Debug Workflow
**Goal**: Developer can attach GDB to a running Skyrim/Proton process and debug SkyrimCoop DLL code with full source-level inspection
**Depends on**: Phase 2 (needs compilable libraries; can run in parallel with Phase 3)
**Requirements**: DEBUG-01, DEBUG-02, DEBUG-03, DEBUG-04
**Success Criteria** (what must be TRUE):
  1. GDB attaches to the Wine/Proton Skyrim process and hits breakpoints set in SkyrimCoop DLL source files
  2. DWARF debug symbols produced by the MinGW build resolve correctly in GDB (variables, stack traces, type info)
  3. A single `./debug_attach.sh` script finds the Skyrim process, attaches GDB, loads symbols, and drops into a debug session
  4. Wine 9.0+ is installed and Skyrim runs under it with NTSYNC performance improvements
**Plans**: TBD

### Phase 5: Testing & CI Pipeline
**Goal**: Automated test suite runs natively on Linux covering serialization, networking, and server logic, with CI enforcing green builds on every push
**Depends on**: Phase 2 (needs compilable libraries; CI benefits from Phase 3 for DLL build verification)
**Requirements**: TEST-01, TEST-02, TEST-03, TEST-04, TEST-05
**Success Criteria** (what must be TRUE):
  1. All existing Catch2 unit tests compile and pass natively on Linux without Wine
  2. A headless mock client connects to a real GameServer instance on Linux and completes a handshake
  3. Message round-trip tests verify that every message type serializes on one side and deserializes correctly on the other
  4. GitHub Actions CI builds the MinGW DLL and runs the native Linux test suite on every push to dev
  5. Fuzz testing sends 1000+ malformed messages to the server without crashes or hangs
**Plans**: TBD

### Phase 6: ImGui UI Port
**Goal**: All user-facing UI functionality works through ImGui instead of CEF, fully compilable with MinGW and debuggable from Linux
**Depends on**: Phase 3 (needs client DLL structure for DirectX hook integration)
**Requirements**: UI-01, UI-02, UI-03, UI-04, UI-05, UI-06, UI-07
**Success Criteria** (what must be TRUE):
  1. ImGui renders an overlay inside Skyrim via the existing DirectX hooks, replacing the CEF rendering path
  2. Player can enter a server address, connect, and see connection status through the ImGui connect dialog
  3. Connected players appear in a party panel with the host clearly indicated
  4. Players can send and receive text chat messages through the ImGui overlay
  5. Settings (network config, keybinds) are accessible and modifiable through an ImGui settings panel
**Plans**: TBD
**UI hint**: yes

## Progress

**Execution Order:**
Phases execute in numeric order. Phases 4 and 5 can run in parallel with Phase 3 (they depend on Phase 2, not Phase 3).

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Feasibility Validation | 0/2 | Planning complete | - |
| 2. MSVC Compatibility & Core Libraries | 0/0 | Not started | - |
| 3. Client DLL Cross-Compilation | 0/0 | Not started | - |
| 4. Linux Debug Workflow | 0/0 | Not started | - |
| 5. Testing & CI Pipeline | 0/0 | Not started | - |
| 6. ImGui UI Port | 0/0 | Not started | - |
