# Requirements: SkyrimCoop

**Defined:** 2026-03-27
**Core Value:** Two friends can drop into Skyrim together on Linux with zero server setup

## v1 Requirements

Requirements for Milestone 1: Linux Build, Debug & UI Port. Each maps to roadmap phases.

### Build System

- [x] **BUILD-01**: XMake configures for MinGW cross-compilation (`xmake f -p mingw --mingw=/path`)
- [x] **BUILD-02**: MSVC compatibility header resolves all `__declspec`, `#pragma comment(lib)`, struct alignment, and MSVC-specific extensions for GCC
- [x] **BUILD-03**: Tier 1-2 libraries (encoding, networking, common, server) compile under MinGW and produce working static libraries
- [x] **BUILD-04**: Tier 3 client DLL compiles under MinGW (CEF and DirectXTK excluded, replaced by ImGui)
- [x] **BUILD-05**: MinGW-compiled DLL loads into Skyrim SE under Proton without crash

### Debug Tooling

- [ ] **DEBUG-01**: GDB attaches to Wine/Proton Skyrim process and hits breakpoints in SkyrimCoop DLL code
- [ ] **DEBUG-02**: MinGW build produces DWARF debug symbols that GDB reads correctly
- [ ] **DEBUG-03**: One-command debug attach script (consolidating existing debug_wine.sh, attach_gdb.sh, etc.)
- [ ] **DEBUG-04**: Wine upgraded to 9.0+ for modern debugging support and NTSYNC performance

### Testing Infrastructure

- [ ] **TEST-01**: Existing Catch2 unit tests compile and pass natively on Linux (no Wine required)
- [ ] **TEST-02**: Headless mock client connects to GameServer on Linux without Skyrim or Wine
- [ ] **TEST-03**: Message round-trip tests verify serialization/deserialization across mock client and server
- [ ] **TEST-04**: CI pipeline (GitHub Actions) builds MinGW DLL and runs native Linux tests on every push
- [ ] **TEST-05**: Message fuzz testing sends malformed/random messages to server without crashes

### Validation Gates

- [ ] **GATE-01**: Minimal "hello world" SKSE plugin compiled with MinGW loads in Skyrim under Proton (ABI feasibility proof)
- [ ] **GATE-02**: MinHook produces correct x64 Windows ABI hooks when cross-compiled with MinGW GCC

### UI Port (CEF to ImGui)

- [ ] **UI-01**: CEF overlay system replaced with ImGui rendering via existing DirectX hooks
- [ ] **UI-02**: Connect/disconnect dialog functional in ImGui (server address input, connect button, status)
- [ ] **UI-03**: Party panel displays connected players with host indicator
- [ ] **UI-04**: In-game text chat functional in ImGui (send/receive messages)
- [ ] **UI-05**: Settings panel functional in ImGui (network settings, keybinds)
- [ ] **UI-06**: ImGui UI fully compilable with MinGW and debuggable via GDB from Linux
- [ ] **UI-07**: CEF dependency (`tp_process`, Chromium Embedded Framework) removed from build

### Native Client Architecture (SKSE TCP Relay)

- [x] **RELAY-01**: Flat binary TCP protocol (opcode + length + payload) for DLL-to-native IPC with zero external dependencies
- [x] **RELAY-02**: Minimal MinGW relay DLL (~500 LOC) with ~30 hook trampolines forwarding raw uint64 arguments over TCP
- [x] **RELAY-03**: Lock-free SPSC command queue enabling native process to execute game functions on the game thread
- [x] **RELAY-04**: DLL spawns native ELF process via CreateProcess/Wine, auto-restarts on crash
- [x] **RELAY-05**: Native process reads game memory via /proc/pid/mem with ptrace SEIZE + pread
- [x] **RELAY-06**: Hook-driven pointer table maps formId to game pointers from ActorAdded/Removed events
- [x] **RELAY-07**: GameReader API provides typed memory reads using game struct offsets for native services
- [ ] **RELAY-08**: All 8 client services (Weather, Calendar, Quest, Combat, Inventory, ActorValue, Magic, Character) rewritten against GameReader API
- [x] **RELAY-09**: XMake build system has separate targets for relay DLL (MinGW, minimal deps) and native client (Linux ELF, full deps)
- [ ] **RELAY-10**: End-to-end integration validated: DLL loads in Skyrim, native process connects, hook events flow

## v2 Requirements

Deferred to Milestone 2+: Co-op Architecture & Gameplay.

### P2P Architecture

- **P2P-01**: Server embedded in host player's client process (no separate server executable)
- **P2P-02**: Host-authoritative world state (NPCs, quests, time, weather owned by host)
- **P2P-03**: Simplified actor ownership (host owns all non-player actors)
- **P2P-04**: Cell management uses host's loaded cells as authoritative

### Session Management

- **SESS-01**: Drop-in 2-4 player sessions via Steam friend invite
- **SESS-02**: Zero-setup hosting (host clicks "host," friend joins, no IP/port config)
- **SESS-03**: Reconnection with state recovery after disconnect
- **SESS-04**: Mod-list enforcement on connect with clear mismatch error messages

### Gameplay Improvements

- **PLAY-01**: Improved quest sync reliability (host-authoritative eliminates distributed ownership bugs)
- **PLAY-02**: Direct item trading UI (offer/accept dialog, replaces broken drop-item workaround)
- **PLAY-03**: Player visibility system (compass markers for party members)
- **PLAY-04**: Fix ActorValueService crash bug
- **PLAY-05**: Fix BehaviorVar animation hash collisions
- **PLAY-06**: Fix combat death resolution (enemies stuck at 0 HP)
- **PLAY-07**: Re-enable AMD GPU support
- **PLAY-08**: Desync detection and recovery (state hash comparison, warning UI)

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Dedicated server support | Contradicts P2P co-op model; STR already does this |
| Server browser / server list | MMO feature, not co-op; use Steam invite |
| More than 4 players | Skyrim's systems break at scale; co-op not MMO |
| PvP combat | Broken in STR, not needed for co-op |
| Voice chat integration | Use Discord/Steam voice |
| Automatic mod downloading | Legal liability, bandwidth costs, mod author permissions |
| Follower sync | AI breaks in multiplayer; you have human companions |
| DLC home sync | Low value, high complexity |
| Native Linux SKSE port | SKSE stays Windows, runs under Proton |
| LLDB Wine debugging | GDB is sufficient; LLDB Wine patches require LLVM 14.x rebase |
| Lua scripting API for end users | Maintenance burden; co-op host doesn't need custom scripts |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| GATE-01 | Phase 1 | Pending |
| GATE-02 | Phase 1 | Pending |
| BUILD-01 | Phase 1 | Complete |
| BUILD-02 | Phase 2 | Complete |
| BUILD-03 | Phase 2 | Complete |
| BUILD-04 | Phase 3 | Complete |
| BUILD-05 | Phase 3 | Complete |
| DEBUG-01 | Phase 4 | Pending |
| DEBUG-02 | Phase 4 | Pending |
| DEBUG-03 | Phase 4 | Pending |
| DEBUG-04 | Phase 4 | Pending |
| TEST-01 | Phase 5 | Pending |
| TEST-02 | Phase 5 | Pending |
| TEST-03 | Phase 5 | Pending |
| TEST-04 | Phase 5 | Pending |
| TEST-05 | Phase 5 | Pending |
| UI-01 | Phase 6 | Pending |
| UI-02 | Phase 6 | Pending |
| UI-03 | Phase 6 | Pending |
| UI-04 | Phase 6 | Pending |
| UI-05 | Phase 6 | Pending |
| UI-06 | Phase 6 | Pending |
| UI-07 | Phase 6 | Pending |
| RELAY-01 | Phase 7 | Complete |
| RELAY-02 | Phase 7 | Complete |
| RELAY-03 | Phase 7 | Complete |
| RELAY-04 | Phase 7 | Complete |
| RELAY-05 | Phase 7 | Complete |
| RELAY-06 | Phase 7 | Complete |
| RELAY-07 | Phase 7 | Complete |
| RELAY-08 | Phase 7 | Pending |
| RELAY-09 | Phase 7 | Complete |
| RELAY-10 | Phase 7 | Pending |

**Coverage:**
- v1 requirements: 33 total
- Mapped to phases: 33
- Unmapped: 0

---
*Requirements defined: 2026-03-27*
*Last updated: 2026-03-28 after Phase 7 planning*
