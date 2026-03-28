# Phase 8: P2P Embedded GameServer - Research

**Researched:** 2026-03-28
**Domain:** Embedded server architecture, in-process loopback networking, P2P host mode
**Confidence:** HIGH

## Summary

Phase 8 embeds the existing `Server::GameServer` into the native `skyrim-coop` process so it simultaneously acts as relay bridge AND multiplayer server. The GameServer class already works (proven by Phase 5 tests and fake_player), is already linked as a dependency in the native client's xmake.lua, and the fake_player binary already connects to it over enet6. The core challenge is wiring an in-process loopback so the host player's native services can inject ClientMessages directly into `GameServer::OnConsume()` without going through an enet6 socket, and routing server responses back without enet6 Send.

The existing code has explicit TODO markers in `CharacterService.cpp` (lines 306-307, 325) that say "TODO(Plan 08+): Build ClientReferencesMoveRequest and send via network transport" -- these are the exact integration points for the loopback. The native services already track actor state via GameReader/ProcMemReader and produce hook events; they just need a transport layer to encode those as ClientMessages and deliver them to the embedded GameServer.

**Primary recommendation:** Create a `LoopbackTransport` class that implements a virtual ConnectionId for the host player, exposes `SendToServer(ClientMessage&)` which serializes and calls `GameServer::OnConsume()` directly, and receives ServerMessages via a callback from a patched `GameServer::Send()` path. This avoids touching the enet6 stack for the host while keeping all server message handlers unchanged.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Host mode is the default. GameServer starts when the native process launches alongside the relay connection. No UI toggle needed (Phase 6 handles UI). Matches the standard P2P pattern: always host unless connecting to someone else.
- **D-02:** Join mode (connecting to another player's host) is triggered by a `--connect <ip:port>` CLI flag for Phase 8 testing. The startup path is designed so the DLL can override mode later (Phase 6 UI sends CTRL_HOST/CTRL_JOIN). If `--connect` is present, skip GameServer instantiation and connect as client instead.
- **D-03:** GameServer listens on the existing STR default port 10578 (already a cvar in GameServer.cpp). Overridable via `--port` CLI flag.
- **D-04:** Host player is treated as a "local player" connected to its own GameServer. Hook events flow through native services -> encode as ClientMessages -> delivered to GameServer via in-process loopback. Server treats host identically to any remote player. This reuses ALL existing server message handlers unchanged.
- **D-05:** In-process loopback connection. A virtual ConnectionId is created for the host player. Services serialize ClientMessages and call GameServer::OnConsume() directly -- no actual enet6 socket for the host. Server responds via a callback instead of enet6 send. Zero latency, zero overhead.
- **D-06:** Open authentication (no password) for Phase 8. Anyone who knows the IP:port can connect. Password/Steam auth deferred to future phases when UI exists.
- **D-07:** Direct IP:port only for remote player discovery. No LAN broadcast, no server browser.
- **D-08:** Primary verification is fake_player connecting to the embedded GameServer in the native process. Reuses Phase 5 infrastructure.
- **D-09:** fake_player verification scope: Auth + character assignment, Movement broadcast, Host state visibility.
- **D-10:** Claude has discretion on minimum viable verification scope beyond the above three areas.

### Claude's Discretion
- How to integrate GameServer::Update() into the existing main loop (separate thread vs inline in frame loop)
- ConsoleRegistry setup for embedded mode (minimal vs full)
- Whether the loopback uses a special ConnectionId constant or allocates dynamically
- Exact changes to main.cpp argument parsing for --connect / --port flags
- Server World initialization and service startup ordering
- How to handle GameServer shutdown when native process exits

### Deferred Ideas (OUT OF SCOPE)
- Steam invite discovery -- future phase, requires Steam SDK integration
- LAN broadcast discovery -- deferred, Steam invites will replace this
- Password authentication -- deferred until UI exists (Phase 6)
- In-game test_ingame_sync.sh extension -- optional stretch, not required for Phase 8 pass criteria
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| P2P-01 | Server embedded in host player's client process | GameServer already linked in native_client/xmake.lua via `add_deps("SkyrimTogetherServer")`; constructor instantiation pattern proven in fake_player self-test |
| P2P-02 | Host-authoritative world state | GameServer::OnUpdate triggers dispatcher->UpdateEvent which feeds all server services; EnforceProximity already references host player |
| P2P-03 | Simplified actor ownership | Host owns all non-player actors; server CharacterService + PlayerManager already implement ownership model |
| P2P-04 | Cell management uses host's loaded cells | PlayerManager::GetHostPlayer() exists; EnforceProximity() already teleports wandering players back to host |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| SkyrimTogetherServer | in-tree | Complete GameServer + World + Services | Already compiled and linked; proven by Phase 5 tests |
| enet6 | in-tree | UDP networking for remote clients | Already used by TiltedPhoques::Server base class |
| EnTT | v3.10.0 | ECS registry for both server and native client | Both World classes already use it |
| spdlog | v1.13.0 | Logging | Already used throughout |
| ConsoleRegistry | in-tree | Required by GameServer constructor | Already compiled in dependency chain |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| rpmalloc | in-tree | Allocator | Already initialized in native main.cpp |
| Catch2 | 2.13.9 | Unit/integration tests | For loopback transport tests |
| MockClient | in-tree | Headless enet6 client | For fake_player verification |

**Installation:** No new dependencies. Everything is already in the dependency chain via `add_deps("SkyrimTogetherServer")` and `add_packages("enet6")` in `Code/native_client/xmake.lua`.

## Architecture Patterns

### Recommended Project Structure
```
Code/native_client/
  main.cpp                    # Modified: add --connect/--port flags, GameServer lifecycle
  Services/
    ...                       # Existing 8 services (modified to send via loopback)
  game_bridge/
    ...                       # Existing ProcMemReader, PointerTable, etc.
  transport/
    LoopbackTransport.h       # NEW: Virtual connection to embedded GameServer
    LoopbackTransport.cpp     # NEW: Serialize ClientMessages, route to OnConsume
    ServerMessageRouter.h     # NEW: Route ServerMessages from GameServer back to native services
    ServerMessageRouter.cpp   # NEW: Deserialize + dispatch to native CharacterService etc.
Code/tests/
  loopback_transport_tests.cpp  # NEW: Unit tests for loopback
```

### Pattern 1: In-Process Loopback Transport
**What:** A class that bridges native client services to the embedded GameServer without enet6.
**When to use:** Host mode (default). The loopback creates a virtual ConnectionId, serializes ClientMessages into the same binary format GameServer::OnConsume expects, and calls it directly.
**Key insight from code:** `GameServer::OnConsume(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId)` reads raw bytes through `ViewBuffer` -> `Buffer::Reader` -> `ClientMessageFactory::Extract`. The loopback must produce the same byte stream that enet6 would deliver after stripping the packet type byte (kPayload prefix is stripped by `Server::HandleMessage` before calling OnConsume).

**Example:**
```cpp
// Source: Code/server/GameServer.cpp line 617-647 (OnConsume pattern)
// Source: Code/tests/mock_client.cpp line 50-58 (SendGameMessage serialization)
class LoopbackTransport {
public:
    static constexpr ConnectionId_t kHostConnectionId = 1;  // Reserved for host

    LoopbackTransport(Server::GameServer& aServer);

    // Send a ClientMessage to the embedded GameServer
    void SendToServer(const ClientMessage& acMessage);

    // Callback: GameServer calls this instead of enet6 Send for kHostConnectionId
    // Route incoming ServerMessages back to native services
    using ServerMessageCallback = std::function<void(const void*, uint32_t)>;
    void SetServerMessageCallback(ServerMessageCallback aCallback);

private:
    Server::GameServer& m_server;
    ServerMessageCallback m_onServerMessage;
};

void LoopbackTransport::SendToServer(const ClientMessage& acMessage) {
    // Same serialization as MockClient::SendGameMessage
    TiltedPhoques::Buffer buffer(1 << 16);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Opcode byte
    acMessage.Serialize(writer);

    // Feed directly into GameServer, skipping enet6 entirely
    // Note: Skip the first byte (packet type) -- OnConsume expects data after kPayload strip
    m_server.OnConsume(buffer.GetWriteData() + 1, writer.Size() - 1, kHostConnectionId);
}
```

### Pattern 2: GameServer::Send Override for Loopback
**What:** Intercept `GameServer::Send(ConnectionId_t, ServerMessage&)` to detect when the target is the host's virtual ConnectionId. Instead of serializing to enet6, serialize into a buffer and deliver via callback.
**When to use:** Always in host mode. The server's broadcast methods (`SendToPlayers`, `SendToLoaded`) iterate PlayerManager and call `pPlayer->Send()`, which eventually calls `GameServer::Send(connectionId, msg)`. The loopback must intercept at that level.

**Critical finding:** `GameServer::Send()` at line 707-722 calls `TiltedPhoques::Server::Send(aConnectionId, &packet)` which calls `enet_peer_send`. The loopback ConnectionId won't have a real peer in `m_peers`, so `Server::Send` will silently fail (line 197: `if (it == m_peers.end()) return;`). This means we need either:
- (a) Override or wrap `GameServer::Send` to check for loopback ConnectionId before calling base
- (b) Insert a virtual peer entry (unsafe, enet6 will crash on null peer)
- (c) Make `GameServer::Send` virtual (it's currently non-virtual `const`)

**Recommendation:** Option (a) -- Wrap `GameServer::Send`. Add a `LoopbackTransport*` pointer to GameServer. Before calling `TiltedPhoques::Server::Send`, check if `aConnectionId == kHostConnectionId` and route to the loopback callback instead. This is the minimal-invasive approach.

```cpp
// In GameServer::Send (both overloads):
void GameServer::Send(ConnectionId_t aConnectionId, const ServerMessage& acServerMessage) const
{
    // Check loopback first
    if (m_pLoopback && aConnectionId == LoopbackTransport::kHostConnectionId) {
        // Serialize and deliver via callback (same serialization, skip enet6)
        Buffer buffer(1 << 20);
        Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8);
        acServerMessage.Serialize(writer);
        m_pLoopback->DeliverToHost(buffer.GetWriteData(), writer.Size());
        return;
    }

    // ... existing enet6 path unchanged
}
```

### Pattern 3: Host Authentication via Loopback
**What:** The host player must authenticate with its own GameServer just like any remote client. Send an `AuthenticationRequest` through the loopback at startup.
**When to use:** During initialization after GameServer and LoopbackTransport are created.
**Key finding:** `HandleAuthenticationRequest` at line 799 does version check, max player count, SKSE/MO2 checks, then creates a Player in PlayerManager. The host must pass the same flow to get a Player entity assigned. Version must match BUILD_COMMIT.

### Pattern 4: Main Loop Integration
**What:** Call `GameServer::Update()` inline in the existing frame loop alongside service updates.
**Why inline, not threaded:** The native client's main loop already runs at 60 FPS (16ms frame time, line 214). `Server::Update()` has its own internal rate limiter via `m_timeBetweenUpdates` (default 100ms for 10Hz, or 16ms for 60Hz premium). Calling `GameServer::Update()` in the same loop is safe -- it checks elapsed time and only processes events when enough time has passed. A separate thread would introduce synchronization complexity for the shared LoopbackTransport with no real benefit.

```
while (s_running) {
    // Phase 1: Receive TCP packets from DLL (existing)
    // Phase 2: Service updates (existing) -- now also produce ClientMessages
    // Phase 3: Systems (existing)
    // Phase 4: Send ClientMessages via LoopbackTransport (NEW)
    // Phase 5: GameServer::Update() -- processes enet6 + loopback messages (NEW)
    // Phase 6: Route ServerMessages back to native services (NEW)
    // Phase 7: Heartbeat + Frame pacing (existing)
}
```

### Anti-Patterns to Avoid
- **Don't create a real enet6 connection for the host:** Decision D-05 explicitly requires in-process loopback. A localhost enet6 socket would add latency and complexity.
- **Don't run GameServer on a separate thread:** The loopback shares data (messages, callbacks) with the native services. Thread safety would require locks on every message send/receive. Inline update is simpler and sufficient.
- **Don't modify TiltedPhoques::Server base class:** It's in a library subdirectory and used by other code. Intercept at the GameServer level.
- **Don't skip the auth handshake for the host:** The server's PlayerManager needs a Player entity for the host. Skipping auth means no Player, which means CharacterService/PartyService/etc. won't work.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Message serialization | Custom binary protocol | MockClient::SendGameMessage pattern | Already proven correct, same format OnConsume expects |
| Server message deserialization | Custom parser | ServerMessageFactory::Extract | Same factory used by MockClient::OnConsume |
| Authentication flow | Custom auth bypass | AuthenticationRequest/Response as-is | Server needs a Player entity; full auth flow creates it |
| Connection management | Custom connection tracker | GameServer's existing PlayerManager | Already tracks players by ConnectionId |
| Test client | New test harness | MockClient + fake_player | Already works with enet6 GameServer from Phase 5 |

**Key insight:** The entire encode/decode/dispatch pipeline already exists and is tested. The loopback just needs to produce the same byte streams and consume the same byte streams.

## Common Pitfalls

### Pitfall 1: GameServer Singleton Conflict
**What goes wrong:** `GameServer` constructor asserts `s_pInstance == nullptr` (line 156). If any test or previous instantiation left a stale pointer, the process will abort.
**Why it happens:** GameServer uses a global singleton pattern (`GameServer::Get()`).
**How to avoid:** Ensure the native process creates exactly one GameServer instance. In tests, use the TestFixture::SharedServerHolder pattern (one per process). In the native process, construct after argument parsing and destroy on shutdown.
**Warning signs:** "Server instance already exists?" assertion failure at startup.

### Pitfall 2: ConsoleRegistry Static Item Consumption
**What goes wrong:** ConsoleRegistry consumes static `Setting` and `Command` objects on first construction. A second ConsoleRegistry instance may find them already consumed, leading to empty settings.
**Why it happens:** Static global Setting/Command objects (lines 25-59 of GameServer.cpp) register themselves once.
**How to avoid:** Create exactly one ConsoleRegistry per process, before constructing GameServer. Same pattern as fake_player (line 175-176) and TestFixture.
**Warning signs:** Server starts with default settings, ignoring cvars.

### Pitfall 3: ConnectionId for Loopback Must Be Recognized
**What goes wrong:** If the loopback uses an arbitrary ConnectionId that doesn't map to a real enet6 peer, `GameServer::Send()` will silently drop messages via the `m_peers.find()` check in `TiltedPhoques::Server::Send()`.
**Why it happens:** The base Server class only knows about enet6 peers.
**How to avoid:** Intercept at the `GameServer::Send()` level (before it reaches `Server::Send()`). Check for the loopback ConnectionId and route to the callback instead of enet6.
**Warning signs:** Host connects and authenticates but never receives server broadcasts.

### Pitfall 4: Message Byte Offset
**What goes wrong:** `OnConsume` expects data AFTER the packet-type byte. `Server::HandleMessage` strips the first byte (`pData[0]` is kPayload/kCompressedPayload) before calling `OnConsume(pData + 1, aSize - 1, ...)`. If the loopback includes this byte, the opcode will be misread.
**Why it happens:** The enet6 transport path prepends a packet type byte. OnConsume never sees it.
**How to avoid:** When calling `OnConsume` from the loopback, serialize like MockClient (WriteBits(0, 8) for the opcode byte), then skip that first byte: `OnConsume(buffer + 1, size - 1, connId)`.
**Warning signs:** "Couldn't parse packet" errors from OnConsume.

### Pitfall 5: OnConsume is Protected
**What goes wrong:** `GameServer::OnConsume` is `protected` (line 122 of GameServer.h), so external code cannot call it directly.
**Why it happens:** It's a virtual override of TiltedPhoques::Server.
**How to avoid:** Either (a) make LoopbackTransport a friend of GameServer, (b) add a public method `InjectMessage(const void*, uint32_t, ConnectionId_t)` that calls OnConsume, or (c) make LoopbackTransport a member of GameServer. Option (b) is cleanest -- one public method addition.
**Warning signs:** Compile error "cannot access protected member."

### Pitfall 6: OnConnection Callback for Loopback
**What goes wrong:** When a real enet6 client connects, `Server::Update()` triggers `OnConnection(handle)`. The loopback bypasses this. If `OnConnection` does required setup, the host won't be properly initialized.
**Why it happens:** `OnConnection` is called by the enet6 event loop, not by GameServer code.
**How to avoid:** Check what `GameServer::OnConnection` does (line 649-653): it just logs and calls `UpdateTitle()`. Not critical. But the loopback setup should still trigger this for completeness (or call it manually after creating the loopback connection).
**Warning signs:** Title shows 0 players even though host is connected.

### Pitfall 7: ScriptService Initialization Reads Filesystem
**What goes wrong:** `GameServer::Initialize()` calls `m_pWorld->GetScriptService().Initialize(*m_pResources)` which attempts to load Lua scripts from the filesystem. If the script directory doesn't exist, it may log errors.
**Why it happens:** Server assumes a dedicated server directory structure with Data/ and scripts.
**How to avoid:** For the embedded host, ModPolicy is disabled by default (`bEnableModCheck = false`), and ScriptService should handle empty resource paths gracefully. Verify during integration that Initialize() doesn't crash with no scripts directory. The fake_player self-test already calls `server.Initialize()` successfully without scripts.
**Warning signs:** spdlog errors about missing script files at startup.

### Pitfall 8: Native Service -> ClientMessage Translation Layer
**What goes wrong:** The native client services currently log movement changes but don't produce ClientMessages (see TODO markers in CharacterService.cpp lines 306-307). This translation layer must be built.
**Why it happens:** Phase 7 deferred network transport integration to Phase 8.
**How to avoid:** Add a `LoopbackTransport*` reference to CharacterService. In `SyncMovement()`, where it currently logs, construct `ClientReferencesMoveRequest` and send via loopback. Similarly for animation sync.
**Warning signs:** Host connects to its own server but server never sees movement updates.

## Code Examples

### Example 1: GameServer Instantiation in Native Process
```cpp
// Source: Code/tests/integration/fake_player.cpp lines 172-178 (proven pattern)
// Source: Code/tests/test_server_fixture.h lines 25-33

// In native_client/main.cpp, after argument parsing:
if (!spdlog::get("ConOut"))
    spdlog::stdout_color_mt("ConOut");

ServerConsole::ConsoleRegistry console("ConOut");
Server::GameServer server(console);
server.Initialize();
// server now listening on port 10578 (or next available)
```

### Example 2: Message Serialization for Loopback
```cpp
// Source: Code/tests/mock_client.cpp lines 50-58 (MockClient::SendGameMessage)
void LoopbackTransport::SendToServer(const ClientMessage& acMessage) {
    TiltedPhoques::Buffer buffer(1 << 16);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // First byte reserved for packet type
    acMessage.Serialize(writer);

    // OnConsume expects data AFTER the packet-type byte
    // Source: Code/libraries/networking/Server.cpp line 325 (HandleMessage strips pData[0])
    m_server.InjectMessage(
        buffer.GetWriteData() + 1,
        static_cast<uint32_t>(writer.Size()) - 1,
        kHostConnectionId);
}
```

### Example 3: Server Message Routing Back to Native Services
```cpp
// Source: Code/tests/mock_client.cpp lines 84-112 (OnConsume deserialization)
void ServerMessageRouter::OnServerMessage(const void* apData, uint32_t aSize) {
    TiltedPhoques::Buffer buffer(aSize);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBytes(reinterpret_cast<const uint8_t*>(apData), aSize);
    TiltedPhoques::Buffer::Reader reader(&buffer);

    ServerMessageFactory factory;
    auto pMessage = factory.Extract(reader);
    if (!pMessage) {
        spdlog::warn("ServerMessageRouter: failed to extract server message");
        return;
    }

    // Dispatch based on opcode
    switch (pMessage->GetOpcode()) {
    case kAuthenticationResponse:
        HandleAuthResponse(static_cast<AuthenticationResponse*>(pMessage.get()));
        break;
    case kServerReferencesMoveRequest:
        HandleMoveBroadcast(static_cast<ServerReferencesMoveRequest*>(pMessage.get()));
        break;
    // ... other handlers
    }
}
```

### Example 4: Host Authentication Through Loopback
```cpp
// Source: Code/tests/integration/fake_player.cpp lines 196-214
AuthenticationRequest authReq;
authReq.Version = BUILD_COMMIT;
authReq.Token = "";
authReq.Username = "HostPlayer";
authReq.SKSEActive = true;
authReq.MO2Active = false;
authReq.DiscordId = 0;
authReq.Level = 1;

loopback.SendToServer(authReq);
// Then call server.Update() to process the message
server.Update();
// ServerMessageRouter will receive AuthenticationResponse via callback
```

### Example 5: CharacterService Movement -> ClientMessage
```cpp
// Source: Code/native_client/Services/CharacterService.cpp lines 306-307 (TODO marker)
// Replace the TODO with actual message construction:
if (changed) {
    ClientReferencesMoveRequest moveReq;
    moveReq.Tick = m_server.GetTick();

    ReferenceUpdate update;
    update.UpdatedMovement.Position.x = currentPos.x;
    update.UpdatedMovement.Position.y = currentPos.y;
    update.UpdatedMovement.Position.z = currentPos.z;
    // Rotation, direction, variables...

    moveReq.Updates[comp.formId] = update;
    m_pLoopback->SendToServer(moveReq);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| GameNetworkingSockets | enet6 | Phase 5 (2026-03) | UDP transport already enet6-based |
| Dedicated server binary | Embedded in native process | Phase 8 (this phase) | No separate server executable needed |
| SKSE-direct client | Relay DLL + Native process | Phase 7 (2026-03) | Server runs as Linux ELF, not Windows DLL |

## Open Questions

1. **Thread safety of OnConsume call from main loop**
   - What we know: The main loop is single-threaded. GameServer::Update() processes enet6 events and calls OnConsume for each. Our loopback also calls OnConsume (or InjectMessage) from the same thread.
   - What's unclear: Whether OnConsume / message handlers assume they're only called from within Server::Update(). Inspection shows no such assumption -- handlers just read the message and dispatch events.
   - Recommendation: Call loopback SendToServer and GameServer::Update() in the same thread. No synchronization needed. **LOW risk.**

2. **GameServer Send path for Player::Send()**
   - What we know: `SendToPlayers`, `SendToLoaded` iterate PlayerManager and call `pPlayer->Send(msg)`. Player::Send likely calls GameServer::Send with the player's ConnectionId.
   - What's unclear: The exact implementation of Player::Send -- is it a direct call or does it go through GameServer?
   - Recommendation: Trace Player::Send implementation to confirm it calls GameServer::Send(connectionId, msg). The loopback intercept must work at whatever layer Player uses. **MEDIUM risk -- verify during implementation.**

3. **ServerMessage delivery timing**
   - What we know: When the loopback calls OnConsume, the server processes the message immediately (within the same call). If the server broadcasts in response, the callback fires immediately too.
   - What's unclear: Whether native services can safely process the server's response within the same frame as the request.
   - Recommendation: Should be fine -- it's equivalent to zero-latency networking. Native services update their state, send to server, server responds, services process response, all within one frame tick.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| SkyrimTogetherServer lib | Embedded GameServer | Yes | in-tree | -- |
| enet6 | Remote client connections | Yes | in-tree | -- |
| xmake | Build system | Yes | v3.0.8 | -- |
| g++ | Native compilation | Yes | 11.4.0 | -- |
| fake_player binary | Verification | Yes | built in releasedbg | Rebuild |
| TPTests binary | Unit tests | Yes | built in releasedbg | Rebuild |

**Missing dependencies with no fallback:** None.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 (header-only) |
| Config file | Code/tests/xmake.lua (TPTests target) |
| Quick run command | `xmake run TPTests "[Loopback]"` |
| Full suite command | `xmake run TPTests` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| P2P-01 | GameServer instantiates in native process | integration | `xmake run fake_player --port 10578` (external mode) | Partial -- fake_player exists, native embedding is new |
| P2P-02 | Host-authoritative world state | integration | `xmake run TPTests "[Loopback]"` | Wave 0 |
| P2P-03 | Actor ownership via host | unit | `xmake run TPTests "[Loopback]"` | Wave 0 |
| P2P-04 | Cell management via host | integration | `xmake run fake_player --port 10578` | Partial |
| LOOPBACK-01 | LoopbackTransport serializes and delivers ClientMessage | unit | `xmake run TPTests "[Loopback]"` | Wave 0 |
| LOOPBACK-02 | GameServer::Send routes to loopback for host ConnectionId | unit | `xmake run TPTests "[Loopback]"` | Wave 0 |
| LOOPBACK-03 | Host authenticates through loopback | unit | `xmake run TPTests "[Loopback]"` | Wave 0 |
| LOOPBACK-04 | fake_player connects to embedded server and receives host state | integration | `xmake run fake_player --port 10578` | Existing |

### Sampling Rate
- **Per task commit:** `xmake build TPTests && xmake run TPTests "[Loopback]" -x`
- **Per wave merge:** `xmake build TPTests && xmake run TPTests`
- **Phase gate:** Full suite green + fake_player external mode connects to native process embedded server

### Wave 0 Gaps
- [ ] `Code/tests/loopback_transport_tests.cpp` -- covers LOOPBACK-01, LOOPBACK-02, LOOPBACK-03
- [ ] `Code/native_client/transport/LoopbackTransport.h` -- new transport class
- [ ] `Code/native_client/transport/LoopbackTransport.cpp` -- implementation
- [ ] `Code/native_client/transport/ServerMessageRouter.h` -- server message dispatcher
- [ ] `Code/native_client/transport/ServerMessageRouter.cpp` -- implementation

## Sources

### Primary (HIGH confidence)
- Code/server/GameServer.h -- GameServer class definition, constructor, message handlers, Send methods
- Code/server/GameServer.cpp -- Full implementation, auth flow, OnConsume, cvars
- Code/libraries/networking/Server.hpp -- TiltedPhoques::Server base (Host, Update, Send, OnConsume interface)
- Code/libraries/networking/Server.cpp -- enet6 event loop, message handling, Send via enet_peer_send
- Code/native_client/main.cpp -- Current main loop structure, service initialization
- Code/native_client/Services/CharacterService.cpp -- TODO markers for transport integration (lines 306, 325)
- Code/tests/mock_client.cpp -- Proven serialization/deserialization patterns
- Code/tests/integration/fake_player.cpp -- Proven GameServer instantiation + MockClient connection
- Code/tests/test_server_fixture.h -- SharedServerHolder pattern for tests
- Code/server/World.h -- Server World class, service accessors
- Code/server/Game/PlayerManager.h -- Player creation, GetHostPlayer, GetByConnectionId

### Secondary (MEDIUM confidence)
- Code/relay_dll/protocol.h -- TCP protocol between DLL and native (for understanding hook event flow)
- Code/native_client/xmake.lua -- Build config showing SkyrimTogetherServer already linked

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - all components already exist in the codebase and are proven by Phase 5 tests
- Architecture: HIGH - loopback pattern is straightforward; all integration points identified in code with line numbers
- Pitfalls: HIGH - identified 8 specific pitfalls with code references and mitigations
- Verification: HIGH - fake_player and MockClient infrastructure already exist and work

**Research date:** 2026-03-28
**Valid until:** 2026-04-28 (stable -- in-tree code, no external dependency changes expected)
