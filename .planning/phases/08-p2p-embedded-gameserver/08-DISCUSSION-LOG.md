# Phase 8: P2P Embedded GameServer - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-28
**Phase:** 08-p2p-embedded-gameserver
**Areas discussed:** Server lifecycle, Hook-to-ECS bridge, Client connection flow, Verification strategy

---

## Server Lifecycle

### When should the GameServer start?

| Option | Description | Selected |
|--------|-------------|----------|
| Always-on at startup | GameServer starts when native process launches alongside relay connection. No UI toggle needed yet. | ✓ |
| On-demand via command | Native process starts as client-only, GameServer activated by CLI flag or control message. | |
| Auto-detect from config | Read a config file or env var to decide host vs client mode at startup. | |

**User's choice:** Always-on at startup
**Notes:** User confirmed this is the standard P2P pattern — "always host by default, unless you're connecting to another player"

### How should the host/join mode be determined?

| Option | Description | Selected |
|--------|-------------|----------|
| CLI flag --connect | Default is host mode. Pass --connect <ip:port> to join instead. | |
| DLL control message | DLL sends CTRL_HOST or CTRL_JOIN based on UI selection (future Phase 6). | |
| Both: flag now, DLL later | CLI flag for Phase 8 testing, designed so DLL can override mode later. | ✓ |

**User's choice:** Both: flag now, DLL later

### What port should the GameServer listen on?

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed default 10578 | Existing STR default port, overridable via --port flag. | ✓ |
| Random available port | OS picks a free port, reported to stdout/log. | |
| Fixed default + auto-fallback | Try 10578 first, if busy pick next available. | |

**User's choice:** Fixed default 10578

---

## Hook-to-ECS Bridge

### How should hook events reach the GameServer's World registry?

| Option | Description | Selected |
|--------|-------------|----------|
| Host as local player | Virtual local connection. Hook events → services → ClientMessages → GameServer::OnConsume(). Reuses all existing server handlers. | ✓ |
| Direct ECS injection | Hook events write directly into server's World registry, bypassing message layer. | |
| Shared registry | Native client and GameServer share same EnTT registry. | |

**User's choice:** Host as local player

### How should the local loopback connection work?

| Option | Description | Selected |
|--------|-------------|----------|
| In-process loopback | Virtual ConnectionId, direct OnConsume() call, callback for responses. Zero network overhead. | ✓ |
| Localhost enet6 connection | Real enet6 connection over 127.0.0.1. Tests real code path but adds latency. | |
| You decide | Claude picks based on code structure. | |

**User's choice:** In-process loopback

---

## Client Connection Flow

### What authentication model for remote players?

| Option | Description | Selected |
|--------|-------------|----------|
| Open (no auth) | Anyone with IP:port can connect. Simplest for Phase 8. | ✓ |
| Optional password | Keep existing server password cvar. Empty = open, set = required. | |
| Skip auth entirely | Remove AuthenticationRequest/Response flow. | |

**User's choice:** Open (no auth)

### How do remote players discover the host?

| Option | Description | Selected |
|--------|-------------|----------|
| Direct IP:port only | No discovery mechanism. fake_player already uses direct connect. | ✓ |
| LAN broadcast discovery | Host broadcasts on LAN, clients auto-discover. | |
| You decide | Claude picks simplest for verification. | |

**User's choice:** Direct IP:port only

---

## Verification Strategy

### Primary verification approach?

| Option | Description | Selected |
|--------|-------------|----------|
| fake_player against embedded | Extend existing fake_player to connect to embedded GameServer. Reuses Phase 5 infrastructure. | ✓ |
| test_ingame_sync.sh extension | Full in-game test with Skyrim + DLL + native + fake_player. | |
| Both: unit first, in-game stretch | Layered: fake_player as pass criteria, in-game as stretch. | |

**User's choice:** fake_player against embedded

### What should fake_player verify?

| Option | Description | Selected |
|--------|-------------|----------|
| Auth + character assign | Full handshake: connect, auth, receive assignment. | ✓ |
| Movement broadcast | Send positions, verify server broadcasts to other connections. | ✓ |
| Host state visibility | Receive host player's position/cell via server broadcasts. | ✓ |
| You decide scope | Claude determines minimum viable verification. | ✓ |

**User's choice:** All four selected (multi-select)

---

## Claude's Discretion

- GameServer::Update() integration into main loop
- ConsoleRegistry setup for embedded mode
- Loopback ConnectionId allocation strategy
- CLI argument parsing changes
- Server World initialization ordering
- GameServer shutdown handling

## Deferred Ideas

- Steam invite discovery — future phase
- LAN broadcast — deferred
- Password auth — deferred until UI (Phase 6)
- In-game test_ingame_sync.sh extension — optional stretch
