# Phase 11: JSON Wire Protocol - Context

**Gathered:** 2026-03-29
**Status:** Ready for planning

<domain>
## Phase Boundary

Replace the binary struct TCP protocol (opcode + length + fixed-size uint64 args[8]) between the relay DLL and native client with newline-delimited JSON. Each hook event and command gets typed, named fields instead of positional uint64 arrays. Additionally, add a dirty-check cache so high-frequency hooks (HOOK_ACTOR_PROCESS, HOOK_SET_POSITION) only send when values actually change — eliminating redundant messages regardless of wire format.

</domain>

<decisions>
## Implementation Decisions

### JSON Library
- **D-01:** Use nlohmann/json (single-header, header-only C++). Drop `nlohmann/json.hpp` into the relay DLL source tree. No build system changes needed — just `#include`.
- **D-02:** Native client side also uses nlohmann/json for parsing incoming hook events and serializing outgoing commands.

### Schema Design
- **D-03:** Typed fields per opcode. Each hook event and command has named fields matching its semantics, not generic `args[]` arrays. Self-documenting and debuggable with tools like `jq`.
- **D-04:** Hook events (DLL -> Native) use lowercase snake_case op names: `"op":"set_position"`, `"op":"actor_added"`, etc.
- **D-05:** Commands (Native -> DLL) use `cmd_` prefix: `"op":"cmd_set_position"`, `"op":"cmd_cast_spell"`, etc.
- **D-06:** Control messages use `ctrl_` prefix: `"op":"ctrl_heartbeat"`, `"op":"ctrl_shutdown"`, `"op":"ctrl_ready"`.
- **D-07:** Newline-delimited JSON (NDJSON) — one JSON object per line, `\n` terminated. Simple framing, no length prefix needed.

### Migration Strategy
- **D-08:** Big bang cutover. Both DLL and native client switch from binary to JSON in this phase. No version negotiation, no parallel support. Both sides are built from the same repo and always deployed together — version mismatch is impossible.
- **D-09:** `protocol.h` kept as reference for opcode documentation. New `json_protocol.h` (or equivalent) defines the JSON field names/schema.

### Performance & Caching
- **D-10:** JSON serialization overhead is not a concern for localhost IPC. The real performance issue is redundant messages, not serialization cost.
- **D-11:** Add a dirty-check cache in the DLL. Track last-sent state per actor/entity. Only serialize and send a hook event when values have actually changed since the last send. This eliminates the majority of high-frequency traffic (HOOK_ACTOR_PROCESS fires per-actor per-frame even when nothing changed).
- **D-12:** Cache granularity is per-opcode-per-entity. For position hooks: cache last (x, y, z, rx, ry, rz) per formId. For actor process: cache last delta or state per formId. Only send when diff exceeds a threshold (e.g., position moved more than epsilon).

### Claude's Discretion
- Exact dirty-check thresholds (position epsilon, timing for non-spatial hooks)
- Whether to use a hash-based or value-comparison approach for the cache
- Internal data structure for the per-entity cache (flat array, unordered_map, etc.)
- Whether to batch multiple dirty events into a single NDJSON burst or send individually
- Exact JSON field names for each of the ~30 hook opcodes and ~15 command opcodes
- How to handle the HOOK_PROJECTILE special case (no formId, raw data pointer)
- Whether mock_skyrim_loader and fake_player JSON updates are in-scope or deferred

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Protocol (current binary — being replaced)
- `Code/relay_dll/protocol.h` — Current binary protocol with all opcode definitions, arg layout conventions, PacketHeader/HookEventPacket/CommandPacket structs
- `Code/relay_dll/hook_trampolines.cpp` — All ~30 hook trampolines that currently serialize to binary HookEventPacket
- `Code/relay_dll/tcp_server.cpp` — DLL-side TCP server that sends binary packets
- `Code/relay_dll/tcp_server.h` — TCP server interface

### Native client (consumer of hook events, producer of commands)
- `Code/native_client/game_bridge/tcp_client.h` — TcpClient that receives binary PacketHeader + payload
- `Code/native_client/game_bridge/tcp_client.cpp` — Binary packet receive/send implementation
- `Code/native_client/main.cpp` — Main loop that dispatches received packets by opcode
- `Code/native_client/Services/CharacterService.cpp` — Largest consumer of hook events (position, actor added/removed, spawn)

### Relay DLL (producer of hook events, consumer of commands)
- `Code/relay_dll/relay_main.cpp` — DLL entry point, DrainCommandQueue (receives commands), main loop
- `Code/relay_dll/command_queue.h` — SPSC command queue for game-thread execution

### Test infrastructure (may need updating)
- `Code/tests/integration/fake_player.cpp` — fake_player binary that connects to GameServer
- `tests/test_ingame_sync.sh` — Integration test script

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `protocol.h` opcode enums (HookOpcode, CommandOpcode, ControlOpcode) — reuse as authoritative list of ops to convert
- Arg layout comments in `protocol.h` lines 52-88 — define exactly which typed fields each JSON message needs
- `FloatToU64()` / `FloatFromU64()` helpers — can be eliminated in JSON (native float representation)

### Established Patterns
- DLL sends via `tcp_server.cpp` Send() method — single point to replace with JSON serialization
- Native receives via `TcpClient::Receive()` — single point to replace with JSON line reading
- Each hook trampoline in `hook_trampolines.cpp` constructs a HookEventPacket — each becomes a JSON object builder
- `DrainCommandQueue` in `relay_main.cpp` parses CommandPackets — becomes JSON parser

### Integration Points
- `hook_trampolines.cpp`: Each of ~30 trampolines currently fills `HookEventPacket.args[]` — must be rewritten to build JSON objects with named fields
- `tcp_server.cpp`: Binary send becomes JSON line send
- `tcp_client.cpp`: Binary receive becomes NDJSON line reader
- `main.cpp` dispatch switch: Currently switches on `header.opcode` — switches on `json["op"]` string instead

</code_context>

<specifics>
## Specific Ideas

- User specifically wants caching solved in this phase, not deferred — the redundant-message problem is the real performance issue, not wire format
- FloatToU64/U64ToFloat conversions can be eliminated — JSON natively represents floats, removing an entire class of encoding bugs

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 11-json-wire-protocol*
*Context gathered: 2026-03-29*
