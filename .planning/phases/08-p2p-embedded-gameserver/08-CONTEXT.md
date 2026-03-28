# Phase 8: P2P Embedded GameServer - Context

**Gathered:** 2026-03-28
**Status:** Ready for planning

<domain>
## Phase Boundary

The native `skyrim-coop` process hosts an enet6 GameServer alongside the relay connection so other players can connect. When Skyrim boots with the relay DLL, the native process starts an embedded GameServer on localhost, accepts client connections via enet6, and bridges hook events into the server's ECS via an in-process loopback. fake_player and real remote clients can connect, authenticate, and sync.

</domain>

<decisions>
## Implementation Decisions

### Server Lifecycle
- **D-01:** Host mode is the default. GameServer starts when the native process launches alongside the relay connection. No UI toggle needed (Phase 6 handles UI). Matches the standard P2P pattern: always host unless connecting to someone else.
- **D-02:** Join mode (connecting to another player's host) is triggered by a `--connect <ip:port>` CLI flag for Phase 8 testing. The startup path is designed so the DLL can override mode later (Phase 6 UI sends CTRL_HOST/CTRL_JOIN). If `--connect` is present, skip GameServer instantiation and connect as client instead.
- **D-03:** GameServer listens on the existing STR default port 10578 (already a cvar in GameServer.cpp). Overridable via `--port` CLI flag.

### Hook-to-ECS Bridge
- **D-04:** Host player is treated as a "local player" connected to its own GameServer. Hook events flow through native services → encode as ClientMessages → delivered to GameServer via in-process loopback. Server treats host identically to any remote player. This reuses ALL existing server message handlers unchanged.
- **D-05:** In-process loopback connection. A virtual ConnectionId is created for the host player. Services serialize ClientMessages and call GameServer::OnConsume() directly — no actual enet6 socket for the host. Server responds via a callback instead of enet6 send. Zero latency, zero overhead.

### Client Connection Flow
- **D-06:** Open authentication (no password) for Phase 8. Anyone who knows the IP:port can connect. Password/Steam auth deferred to future phases when UI exists. The AuthenticationRequest/Response flow is kept intact but the server accepts all connections.
- **D-07:** Direct IP:port only for remote player discovery. No LAN broadcast, no server browser. fake_player already uses direct connect. Steam invite discovery is future work.

### Verification Strategy
- **D-08:** Primary verification is fake_player connecting to the embedded GameServer in the native process. Reuses Phase 5 infrastructure.
- **D-09:** fake_player verification scope:
  - Auth + character assignment (full handshake)
  - Movement broadcast (fake_player sends positions, server broadcasts to other connections)
  - Host state visibility (fake_player receives host player's position/cell via server broadcasts)
- **D-10:** Claude has discretion on minimum viable verification scope beyond the above three areas.

### Claude's Discretion
- How to integrate GameServer::Update() into the existing main loop (separate thread vs inline in frame loop)
- ConsoleRegistry setup for embedded mode (minimal vs full)
- Whether the loopback uses a special ConnectionId constant or allocates dynamically
- Exact changes to main.cpp argument parsing for --connect / --port flags
- Server World initialization and service startup ordering
- How to handle GameServer shutdown when native process exits

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Existing GameServer
- `Code/server/GameServer.h` — Server class definition, extends TiltedPhoques::Server
- `Code/server/GameServer.cpp` — Server implementation, cvars, message handlers, auth flow
- `Code/server/World.h` — Server-side ECS World registry
- `Code/server/Services/` — All server services (PlayerService, CharacterService, etc.)

### Networking Layer
- `Code/libraries/networking/Server.hpp` — TiltedPhoques::Server base class (enet6 Host/Update/Send)
- `Code/libraries/networking/Server.cpp` — enet6 connection management, packet dispatch
- `Code/libraries/networking/Client.hpp` — TiltedPhoques::Client base class (for understanding connect flow)

### Native Client (to be modified)
- `Code/native_client/main.cpp` — Current native process entry point (relay-only, no GameServer)
- `Code/native_client/xmake.lua` — Build config (already links SkyrimTogetherServer)
- `Code/native_client/Services/` — Native service implementations that will feed the loopback

### Protocol & Bridge
- `Code/relay_dll/protocol.h` — DLL-to-native TCP protocol (hook events, commands)
- `Code/native_client/game_bridge/` — GameReader, ProcMemReader, PointerTable, TcpClient APIs

### Testing Infrastructure
- `Code/tests/integration/fake_player.cpp` — Existing fake player binary
- `Code/tests/mock_client.h` — MockClient for headless testing
- `Code/tests/mock_client.cpp` — MockClient implementation
- `Code/tests/server_roundtrip_tests.cpp` — Existing server round-trip tests

### Encoding
- `Code/encoding/Messages/` — All ClientMessage/ServerMessage definitions
- `Code/encoding/Opcodes.h` — Opcode enums (kClientOpcodeMax, kServerOpcodeMax)

### Prior Phase Context
- `.planning/phases/05-testing-ci-pipeline/05-CONTEXT.md` — Server cleanup decisions, fake_player design
- `.planning/phases/07-native-linux-build-with-skse-tcp-relay/07-CONTEXT.md` — Relay architecture, all service rewrite decisions

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **GameServer class** (`Code/server/GameServer.h`): Already functional, extends TiltedPhoques::Server with enet6. Just needs to be instantiated in native process.
- **SkyrimTogetherServer library**: Already linked in native client's xmake.lua (`add_deps("SkyrimTogetherServer")`). No build changes needed.
- **fake_player** (`Code/tests/integration/fake_player.cpp`): Existing test binary that connects to GameServer, sends scripted movements. Reuse directly.
- **MockClient** (`Code/tests/mock_client.h`): Headless enet6 client for testing. Used by fake_player and server round-trip tests.
- **ConsoleRegistry** (`Code/components/console/`): Required by GameServer constructor. Already compiled in the dependency chain.

### Established Patterns
- GameServer uses `TiltedPhoques::Server::Host(port, tickRate)` to start listening
- `GameServer::OnConsume()` dispatches incoming packets to message handlers indexed by opcode
- `GameServer::Send()` serializes ServerMessages to enet6 packets via connection ID
- Services register message handlers in their constructors via the handler array pattern
- Main loop pattern in native_client/main.cpp: frame-paced with delta time, service updates, systems

### Integration Points
- `main.cpp` main loop needs GameServer::Update() call added
- GameServer constructor needs a ConsoleRegistry instance
- Loopback needs to inject into GameServer::OnConsume() path
- Native services need to produce ClientMessages for the loopback (currently they only process hook events)

</code_context>

<specifics>
## Specific Ideas

- Host-by-default with --connect override matches the standard P2P game pattern: "you're always hosting unless you're joining someone"
- The DLL should be able to override host/join mode later via control message (Phase 6 UI)
- In-process loopback avoids the overhead of localhost enet6 while reusing all server message handlers unchanged
- Port 10578 is the existing STR default — familiar to anyone who's used Skyrim Together before

</specifics>

<deferred>
## Deferred Ideas

- **Steam invite discovery** — future phase, requires Steam SDK integration
- **LAN broadcast discovery** — deferred, Steam invites will replace this
- **Password authentication** — deferred until UI exists (Phase 6)
- **In-game test_ingame_sync.sh extension** — optional stretch, not required for Phase 8 pass criteria

</deferred>

---

*Phase: 08-p2p-embedded-gameserver*
*Context gathered: 2026-03-28*
