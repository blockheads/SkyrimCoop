# Phase 14: enet6 Network Traffic Replay - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-29
**Phase:** 14-enet6-network-traffic-replay
**Areas discussed:** Recording format, Capture points & dual recording, Replay harness design, Test scope & assertions

---

## Recording Format

| Option | Description | Selected |
|--------|-------------|----------|
| NDJSON (Recommended) | Serialize ClientMessage/ServerMessage fields to JSON lines. Consistent with Phase 12/13 tooling, human-readable, grep-able. | ✓ |
| Raw binary capture | Dump exact bytes from GameServer::Send/OnConsume. Perfect fidelity but opaque. | |
| Binary + JSON sidecar | Record raw binary for replay fidelity, plus parallel JSON log for inspection. | |

**User's choice:** NDJSON
**Notes:** Consistency with existing Phase 12/13 tooling was the deciding factor.

### Follow-up: Message Key Format

| Option | Description | Selected |
|--------|-------------|----------|
| Type names (Recommended) | Human-readable type names as primary key, opcode as metadata | ✓ |
| Numeric opcodes only | Matches binary wire format, compact but requires opcode table | |

**User's choice:** Type names
**Notes:** None

---

## Capture Points & Dual Recording

| Option | Description | Selected |
|--------|-------------|----------|
| GameServer Send/OnConsume (Recommended) | Tap at application layer. Captures fully deserialized messages. | ✓ |
| enet6 transport layer | Tap raw packets. Captures everything including handshakes/retransmits. | |
| Both layers | Application-layer for normal, transport-layer optionally for debugging. | |

**User's choice:** GameServer Send/OnConsume
**Notes:** User asked about value of raw packets. Analysis showed transport layer adds significant noise (handshakes, keepalives, fragmentation, ACKs, retransmissions) that has zero value for regression testing. Application layer is the right abstraction.

### Follow-up: Dual Stream Organization

| Option | Description | Selected |
|--------|-------------|----------|
| Single interleaved file (Recommended) | One NDJSON file with "dir" field per line. Preserves temporal ordering. | |
| Separate files per direction | host_to_client.ndjson + client_to_host.ndjson. Cleaner per-direction but loses interleaving. | |
| Separate files per peer | One file per connected player. Scales for multi-client. | ✓ |

**User's choice:** Separate files per peer
**Notes:** None

### Follow-up: Per-Peer File Content

| Option | Description | Selected |
|--------|-------------|----------|
| Both directions per peer (Recommended) | Each peer's file has both c2s and s2c, tagged with "dir" field. Complete picture per player. | ✓ |
| Send-only per peer | Each file only has messages FROM that peer. Requires cross-referencing. | |

**User's choice:** Both directions per peer
**Notes:** None

---

## Replay Harness Design

| Option | Description | Selected |
|--------|-------------|----------|
| New ServerReplayHarness (Recommended) | Separate class for server-side replay. Clean separation from NDJSON hook-event harness. | |
| Extend existing ReplayHarness | Add enet6 capability to existing class. Mixes two different service stacks. | |
| Combined dual harness | Wraps BOTH ReplayHarness (client) and server component. Most powerful, most complex. | ✓ |

**User's choice:** Combined dual harness
**Notes:** None

### Follow-up: Wiring Between Sides

| Option | Description | Selected |
|--------|-------------|----------|
| Independent replay (Recommended) | Each side replays from own recorded file. Deterministic, debuggable. | ✓ |
| Live wired | Server output feeds client input in real-time. Non-deterministic. | |
| Wired with snapshots | Live wired with recorded snapshots as checkpoints. Most complex. | |

**User's choice:** Independent replay
**Notes:** None

---

## Test Scope & Assertions

### Priority Message Flows (multiSelect)

| Option | Description | Selected |
|--------|-------------|----------|
| Auth + AssignCharacter | Connection handshake validation | ✓ |
| CharacterSpawn + movement | Core gameplay sync | ✓ |
| Inventory + equipment | Item sync | ✓ |
| All opcodes (comprehensive) | Record and test whatever the live session produces | ✓ |

**User's choice:** All four selected — comprehensive coverage
**Notes:** None

### Assertion Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Hardcoded assertions (Recommended) | Same Catch2 REQUIRE/CHECK pattern as Phase 12/13 | ✓ |
| Snapshot comparison | Serialize state to JSON, diff against golden snapshot | |

**User's choice:** Hardcoded assertions
**Notes:** None

---

## Claude's Discretion

- NDJSON serialization approach (manual per-message or reflection-based)
- ServerReplayHarness internal architecture and stub dependencies
- Combined harness coordination design (one class or thin coordinator)
- NDJSON field schema for enet6 messages
- Recording trigger mechanism (flag, config, or automatic)
- Fixture organization and naming conventions
- Test file organization

## Deferred Ideas

None — discussion stayed within phase scope
