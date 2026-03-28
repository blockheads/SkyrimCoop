# Phase 7: Native Linux Build with SKSE TCP Relay - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-28
**Phase:** 07-native-linux-build-with-skse-tcp-relay
**Areas discussed:** Hook/command protocol, Memory reading strategy, Process lifecycle, Migration strategy

---

## Pre-Discussion: Architecture Exploration

Before formal gray area discussion, an extensive architecture exploration took place:

### Fork SKSE vs Middleware?
User asked whether to fork SKSE (embed TCP server) or build middleware that hooks into SKSE.

**Conclusion:** Middleware (SKSE plugin) is clearly better. SKSE is a moving target that updates with Skyrim versions. Forking = maintenance burden. SkyrimCoop is already an SKSE plugin.

### Game Memory Coupling Analysis
Full audit of all 15 client services revealed:
- ~149 game type references across services
- ~20 event hook types, ~15 game function call types, ~20 state read categories
- Tightest loop: 100ms movement sync. Most events are event-driven.
- All access funnels through `TESForm::GetById()` + property reads + game function calls

### Initial "Game I/O Bridge" Proposal
First proposal: thin DLL (~1000 LOC) that reads game structs, serializes events, executes commands. Native Linux process handles all logic.

**User concern:** "We'd still have code on the bridge which performs some level of logic we can't easily debug." The struct-reading code (offsets, casts, null checks) is the exact category of bugs that causes crashes — moving it to a cross-compiled binary makes it HARDER to debug, not easier.

### LethalInjection-Inspired Breakthrough
User pointed to LethalInjection project which uses `/proc/pid/mem` (ptrace + pread) to read Wine process memory from native Linux. This eliminates the need for the DLL to know game structs at all:
- DLL hooks functions, forwards raw pointer values
- Native process reads game memory via /proc/pid/mem using its own struct knowledge
- All bug-prone code (offsets, struct interpretation) runs natively with full GDB support

---

## Hook/command protocol

### Protocol Format

| Option | Description | Selected |
|--------|-------------|----------|
| Flat binary structs | Simple C structs with opcode + length + payload. Zero overhead, no deps. | ✓ |
| JSON-RPC | Human-readable, matches LethalInjection MCP. Adds JSON dep to DLL. | |
| Protobuf/FlatBuffers | Schema-driven, versioned. Overkill for localhost IPC. | |

**User's choice:** Flat binary structs
**Notes:** Keeps DLL dependency-free. Localhost-only, no need for human readability.

### Game Function Call Execution

| Option | Description | Selected |
|--------|-------------|----------|
| Game-thread command queue | Hook update loop, lock-free queue, drain each frame | ✓ |
| Synchronous call-and-wait | Block game thread on IPC. Simpler but risky. | |
| You decide | | |

**User's choice:** Game-thread command queue

### Hook Data Format

| Option | Description | Selected |
|--------|-------------|----------|
| Raw args only | DLL sends hook_id + raw uint64 function arguments. Zero game knowledge. | ✓ |
| Minimal typed extraction | DLL reads 1-2 fields from pointers before forwarding. | |
| You decide | | |

**User's choice:** Raw args only
**Notes:** Keeps DLL completely game-ignorant. Native process does all interpretation.

---

## Memory reading strategy

### Pointer Discovery

| Option | Description | Selected |
|--------|-------------|----------|
| Hook-driven pointer table | DLL hooks ActorAdded/Removed, forwards Actor* pointer. Native maintains formId→ptr map. | ✓ |
| Scan known global tables | Native walks game's form hash map via /proc/pid/mem. No DLL for discovery. | |
| Hybrid | Hooks for actors, scans for singletons (Sky, PlayerCharacter). | |

**User's choice:** Hook-driven pointer table

### Pointer Invalidation

| Option | Description | Selected |
|--------|-------------|----------|
| Hook-driven invalidation | DLL hooks ActorRemoved/Delete, notifies native to remove pointer. | ✓ |
| Validate before read | Check vtable/marker before each /proc/pid/mem read. | |
| You decide | | |

**User's choice:** Hook-driven invalidation

---

## Process lifecycle

### Startup

| Option | Description | Selected |
|--------|-------------|----------|
| DLL spawns it | SKSE→DLL→TCP server→fork/exec native binary with PID+port. | ✓ |
| User launches separately | Manual launch, PID discovery via /proc. | |
| Launcher orchestrates | External script coordinates both. | |

**User's choice:** DLL spawns it
**Notes:** Matches LethalInjection pattern. Single entry point, guaranteed ordering.

### Crash Recovery

| Option | Description | Selected |
|--------|-------------|----------|
| DLL restarts automatically | Detect broken TCP, respawn native process. ~20 lines in DLL. | ✓ |
| Continue without multiplayer | Stop forwarding hooks, play single-player. | |
| You decide | | |

**User's choice:** DLL restarts automatically

---

## Migration strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Incremental per-service | Migrate one service at a time, starting with simplest. | |
| Big bang rewrite | Rewrite all services at once against new architecture. | ✓ |
| Abstraction layer first | Wrap /proc/pid/mem with same API as direct access. | |

**User's choice:** Big bang rewrite
**Notes:** Architecture change is fundamental — incremental migration would mean maintaining two incompatible patterns.

---

## Claude's Discretion

- Lock-free queue implementation details
- TCP connection management (reconnection, keepalive)
- Exact hook and command sets
- fork/exec interaction with Wine process model
- Threading model for /proc/pid/mem reads

## Deferred Ideas

None — discussion stayed within phase scope
