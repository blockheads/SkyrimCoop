# Milestones

## v1.0 — Linux Toolchain & Relay Architecture (ABANDONED)

**Started:** 2026-02
**Abandoned:** 2026-03-30
**Reason:** Relay/native client two-process architecture was exploratory. Proved MinGW feasibility (GATE-01/GATE-02), built Address Library integration, MinHook hooking, and extensive replay test infrastructure. However, the relay approach (TCP/JSON IPC, /proc/pid/mem reads, command queue) added unnecessary complexity. The full STR client can build directly under MinGW as a single SKSE DLL — simpler, faster, and easier to debug.

**What shipped:**
- MinGW cross-compilation toolchain (clang + lld)
- SKSE plugin ABI compatibility (MinGWCompat.h, link_stubs.cpp)
- Address Library v2 parser
- MinHook integration under MinGW
- enet6 networking with GameServer
- Replay test infrastructure (NDJSON hook events + enet6 network traffic)
- 208+ test assertions across 13 replay test cases
- Host-authoritative architecture design documents
- Server cleanup (P2P embedded-only mode)

**What was abandoned:**
- Relay DLL (Code/relay_dll/) — TCP/JSON hook forwarding
- Native client (Code/native_client/) — Linux ELF with /proc/pid/mem reads
- Phases 8-18 relay-specific work (actor spawning via relay commands, etc.)

**Phases completed:** 1, 2, 3, 3.1, 4, 5, 7, 11, 12, 13, 14 (12 of 20)
**Phases abandoned:** 6, 8, 9, 9.1, 10, 15, 16, 17, 18 (partially executed or not started)
