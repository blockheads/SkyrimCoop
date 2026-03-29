# Phase 14: enet6 Network Traffic Replay - Research

**Researched:** 2026-03-29
**Domain:** Network message recording, server-side replay harness, NDJSON serialization of binary protocol messages
**Confidence:** HIGH

## Summary

Phase 14 extends the existing Phase 12/13 NDJSON replay infrastructure to capture and replay full enet6 network traffic (ClientMessage/ServerMessage) at the application layer. The two main workstreams are: (1) recording taps in GameServer::Send() and GameServer::OnConsume() that serialize messages to NDJSON, and (2) a ServerReplayHarness that instantiates the server World and services with stubbed dependencies, feeding recorded ClientMessages and capturing ServerMessage output for assertion.

The codebase already has all the building blocks: ClientMessageFactory::Visit/ServerMessageFactory::Visit provide compile-time iteration over all message types for JSON serialization, GameServer::InjectMessage() feeds raw binary into OnConsume(), GameServer::SetLoopbackRoute() captures outgoing ServerMessages, and the ReplayTests xmake target already links SkyrimTogetherServer. The main challenge is JSON serialization of ~100 message types (46 client + 53 server) and constructing a server World without ESLoader/ScriptService/enet6 dependencies.

**Primary recommendation:** Use the existing Visit() template pattern on ClientMessageFactory/ServerMessageFactory to generate message-to-JSON and JSON-to-binary converters at compile time, serialize each message field to named JSON keys. Build ServerReplayHarness by constructing a minimal World with a stub ConsoleRegistry and bypassing ESLoader/ScriptService initialization.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- D-01: NDJSON format for enet6 traffic, consistent with Phase 12/13 hook-event recordings. Each line is a JSON object with message type name, opcode (metadata), direction, timestamp, and serialized payload fields.
- D-02: Message type names (e.g. "AuthenticationRequest", "NotifySpawnData") as the primary identifier per record. Numeric opcode included as metadata but type name is the primary key. Human-readable, self-documenting fixtures.
- D-03: Recording taps at the application layer -- GameServer::Send() for outgoing ServerMessages and GameServer::OnConsume() for incoming ClientMessages. NOT at the enet6 transport layer.
- D-04: Separate NDJSON file per connected peer. Each file contains both directions (c2s + s2c) for that peer, tagged with a "dir" field. Self-contained session transcript per player.
- D-05: Combined dual harness that wraps both the existing ReplayHarness (native_client hook events) and a new server-side component (GameServer + server World + server services with stubs). One harness class can replay both NDJSON hook events and enet6 messages.
- D-06: Independent replay -- each side replays from its own recorded file. Server gets recorded ClientMessages, client gets recorded ServerMessages. No live wiring between sides.
- D-07: Comprehensive opcode coverage -- record and test whatever the live session produces.
- D-08: Hardcoded Catch2 assertions with explicit expected values, same pattern as Phase 12/13.

### Claude's Discretion
- How to serialize ClientMessage/ServerMessage fields to JSON (manual per-message or reflection-based approach using existing Serialize/Deserialize methods)
- ServerReplayHarness internal architecture -- how to stub enet6 connections for the server World while keeping services functional
- Whether the combined harness is one class or a thin coordinator over two independent harness classes
- NDJSON field schema for enet6 messages (exact field names, how to represent binary payload fields in JSON)
- How to trigger recording (--record-enet flag, config option, or automatic when --record is active)
- Fixture organization (subdirectory per session, naming convention for per-peer files)
- Which server services need stub/mock dependencies for replay mode
- Test file organization (one test file per message category vs grouped)

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope
</user_constraints>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| nlohmann_json | v3.11.3 | NDJSON parsing and generation | Already used by ReplayTests (Phase 12), available via xmake packages |
| Catch2 | 2.13.9 | Test assertions | Already used by all existing replay tests |
| entt | v3.10.0 | Server World ECS registry | Core dependency of server services |
| spdlog | v1.13.0 | Logging in recording/replay code | Already linked everywhere |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| TiltedPhoques::Buffer | (vendored) | Binary serialization/deserialization of messages | Converting between binary wire format and JSON |
| sol2 | v3.3.0 | ScriptService dependency | Required by server World, but ScriptService::Initialize() can be skipped |

**Installation:** No new packages needed. All dependencies already in xmake.lua.

## Architecture Patterns

### Recommended Project Structure
```
Code/
├── server/
│   ├── Recording/
│   │   ├── EnetRecorder.h          # Recording tap inserted into GameServer
│   │   ├── EnetRecorder.cpp        # NDJSON file writer, per-peer file management
│   │   ├── MessageSerializer.h     # ClientMessage/ServerMessage -> JSON conversion
│   │   └── MessageSerializer.cpp   # Visit()-based compile-time dispatch
│   └── ...existing server files...
├── tests/
│   └── replay/
│       ├── server_replay_harness.h      # ServerReplayHarness class
│       ├── server_replay_harness.cpp    # Server World + stub setup + message injection
│       ├── enet_replay_tests.cpp        # Auth + character spawn + movement tests
│       ├── enet_inventory_tests.cpp     # Inventory/equipment replay tests
│       ├── enet_integration_tests.cpp   # Full session aggregate tests
│       └── ...existing replay test files...
tests/
└── fixtures/
    └── replay/
        ├── enet/                        # enet6 message recordings (per-peer NDJSON)
        │   ├── session_001_peer_1.ndjson
        │   └── session_001_peer_2.ndjson
        └── ...existing hook event fixtures...
```

### Pattern 1: Visit()-Based Message Serialization

**What:** Use the existing `ClientMessageFactory::Visit()` and `ServerMessageFactory::Visit()` template to generate a type-name-to-opcode mapping and per-message JSON serialization at compile time.

**When to use:** Recording and replay -- serializing any ClientMessage/ServerMessage to JSON and deserializing JSON back to binary.

**How it works:** The Visit pattern iterates all message types at compile time. For each type T, extract `T::Opcode` and use the class name (via a constexpr string or macro) as the JSON "type" field. Serialize by: (1) take the typed message, (2) use its SerializeRaw() to write to a TiltedPhoques::Buffer, (3) base64-encode the binary payload into JSON. Alternatively, write per-message ToJson/FromJson functions using field names.

**Recommendation:** Use a hybrid approach. For the initial implementation, serialize the binary payload as base64 in the JSON (fast to implement, covers all messages immediately). Add human-readable field-level JSON for high-value messages (AuthenticationRequest, AssignCharacterRequest, ClientReferencesMoveRequest, CharacterSpawnRequest) in a second pass.

```cpp
// Binary-payload approach (covers all messages immediately)
// Source: Code/encoding/Messages/ClientMessageFactory.h Visit() pattern
struct MessageSerializer {
    // Serialize any ClientMessage to JSON with base64 binary payload
    static nlohmann::json ToJson(const ClientMessage& acMessage);
    // Serialize any ServerMessage to JSON with base64 binary payload
    static nlohmann::json ToJson(const ServerMessage& acMessage);

    // Deserialize JSON back to binary buffer for InjectMessage()
    static std::vector<uint8_t> ClientFromJson(const nlohmann::json& acJson);
    static std::vector<uint8_t> ServerFromJson(const nlohmann::json& acJson);
};
```

### Pattern 2: ServerReplayHarness Architecture

**What:** A test harness that creates a server World with all services functional, but without enet6 network, ESLoader file system access, or ScriptService Lua initialization.

**When to use:** Replaying recorded ClientMessages through server services and asserting on server state/output.

**Key insight from code analysis:** The server World constructor (World.cpp:23-54) does three problematic things for test mode:
1. Creates AdminService and adds it as spdlog sink -- harmless, can keep
2. Emplaces all services via ctx().emplace -- these all just take (World&, dispatcher&), fine
3. Runs ESLoader::ESLoader().BuildRecordCollection() -- this reads files from Data/ directory
4. Creates ScriptService which initializes Lua state

**Recommendation:** Create a `TestWorld` subclass or factory function that constructs the World but skips ESLoader and defers ScriptService. The GameServer constructor also calls Host() to bind a port -- must be bypassed in test mode.

```cpp
// ServerReplayHarness manages a stripped-down GameServer
class ServerReplayHarness {
public:
    ServerReplayHarness();

    // Feed a recorded ClientMessage (from NDJSON) into the server
    size_t ReplayFile(const std::string& aPath);

    // Access for assertions
    Server::World& GetWorld();
    std::vector<nlohmann::json>& GetCapturedServerMessages();

private:
    // Stub ConsoleRegistry (no-op commands)
    ServerConsole::ConsoleRegistry m_console;
    // GameServer with port binding skipped
    std::unique_ptr<Server::GameServer> m_server;
    // Captured outgoing ServerMessages as JSON
    std::vector<nlohmann::json> m_captured;
};
```

### Pattern 3: EnetRecorder Tap

**What:** A recording component inserted into GameServer::Send() and GameServer::OnConsume() that writes NDJSON lines to per-peer files.

**When to use:** During live multiplayer sessions to capture traffic for fixture generation.

**NDJSON line schema (recommended):**
```json
{"type":"AuthenticationRequest","opcode":0,"dir":"c2s","peer":1,"ts":1234567890.123,"payload":"base64..."}
{"type":"CharacterSpawnRequest","opcode":4,"dir":"s2c","peer":1,"ts":1234567890.456,"payload":"base64..."}
```

Fields:
- `type` (string): Message class name -- primary key (D-02)
- `opcode` (int): Numeric opcode -- metadata for cross-reference
- `dir` (string): "c2s" (client-to-server) or "s2c" (server-to-client) (D-04)
- `peer` (int): Peer/connection ID within session (D-04)
- `ts` (float): Timestamp in seconds since session start
- `payload` (string): Base64-encoded binary serialization of the message

### Pattern 4: Combined Dual Harness

**What:** A coordinator class that holds both a ReplayHarness (client-side hook events) and a ServerReplayHarness (server-side enet6 messages), allowing tests to replay both sides from their respective NDJSON files.

**Recommendation:** Thin coordinator, not inheritance. Each side remains independently testable.

```cpp
class DualReplayHarness {
public:
    ReplayHarness& Client() { return m_client; }
    ServerReplayHarness& Server() { return m_server; }

    // Replay client hook events and server enet messages independently
    void ReplayClientFile(const std::string& aPath);
    void ReplayServerFile(const std::string& aPath);

private:
    ReplayHarness m_client;
    ServerReplayHarness m_server;
};
```

### Anti-Patterns to Avoid
- **Serializing at transport layer:** Don't tap enet6 packet send/recv -- too noisy with ACKs, keepalives, fragmentation. Tap at application layer per D-03.
- **Hand-writing JSON for each message:** 100+ message types. Use Visit() or base64-binary to avoid massive boilerplate.
- **Constructing real GameServer in tests:** GameServer constructor calls Host() to bind a port, uses global CVars. Must bypass or stub.
- **Modifying Message.h to add JSON support:** Pollutes the encoding layer. Keep JSON serialization in a separate translation unit.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Per-message JSON serialization | 100+ manual ToJson/FromJson functions | Base64 binary payload via SerializeRaw/DeserializeRaw | Covers all messages immediately, field-level JSON added incrementally |
| Message type name lookup | String switch/if chain | Visit() compile-time map from opcode to type name | Already exists, type-safe, auto-updates when messages added |
| Binary-to-JSON encoding | Custom hex encoding | Base64 from a lightweight encoder (or nlohmann::json binary) | Standard, compact, well-tested |
| Server test isolation | Mock every service | Construct real World, skip ESLoader/ScriptService init | Services are self-contained once World exists |

**Key insight:** The binary payload approach means recording works for ALL messages from day one. Human-readable field-level JSON can be added per-message as needed for debugging, without blocking the recording/replay pipeline.

## Common Pitfalls

### Pitfall 1: GameServer Constructor Side Effects
**What goes wrong:** GameServer() calls Host() which binds a port via enet6. In tests, this either fails (port in use) or creates unnecessary network resources.
**Why it happens:** GameServer was designed as a singleton with network initialization in the constructor.
**How to avoid:** Create a test-mode factory or subclass that skips Host() call. Alternatively, construct GameServer with a special flag, or directly instantiate World + BindMessageHandlers() without the network setup.
**Warning signs:** Tests fail with "port already in use" or "enet6 initialization failed."

### Pitfall 2: ESLoader File System Dependency
**What goes wrong:** Server::World constructor runs ESLoader::ESLoader().BuildRecordCollection() which reads Skyrim mod files from a Data/ directory that doesn't exist in CI.
**Why it happens:** World assumes a game installation is present.
**How to avoid:** Create a TestWorld that skips ESLoader, or set bEnableModCheck=false before construction (CheckMoPo() will bypass the check). Inspect the flow: World constructor calls ESLoader unconditionally, so need to either (a) subclass World, (b) add a test-mode constructor, or (c) ensure ESLoader handles missing Data/ gracefully (returns nullptr RecordCollection, which is already checked).
**Warning signs:** Segfault or "Failed to start: ModPolicy's ModCheck is enabled" errors in test output.

### Pitfall 3: ScriptService Lua State
**What goes wrong:** ScriptService constructor initializes a full Lua VM and binds all server functions. Destructor order matters (documented in ScriptService.h).
**Why it happens:** ScriptService is always created by World constructor.
**How to avoid:** ScriptService construction is fine (just creates state). The issue is Initialize() which loads script files. Since Initialize() is called separately by GameServer::Initialize() (not World constructor), replay mode just skips calling GameServer::Initialize().
**Warning signs:** "No such file or directory" errors from script loading.

### Pitfall 4: PlayerManager::GetByConnectionId Returns nullptr
**What goes wrong:** BindMessageHandlers() default handler calls GetByConnectionId(). For non-auth messages, if no Player exists, it kicks the connection.
**Why it happens:** Server expects auth before other messages.
**How to avoid:** In replay, must inject messages in order: AuthenticationRequest first to create Player, then subsequent messages. The recording captures this naturally since auth always comes first.
**Warning signs:** "Connection {:x} is not associated with a player" errors, messages not processed.

### Pitfall 5: Message Type Name Strings at Compile Time
**What goes wrong:** C++ doesn't have runtime reflection. Getting "AuthenticationRequest" as a string from the type requires explicit mapping.
**Why it happens:** No built-in type_name that gives clean class names across compilers.
**How to avoid:** Generate a static map using Visit() -- each MetaMessage<T> can use a macro or constexpr string to register the type name. Alternatively, use a simple `#define REGISTER_MESSAGE_NAME(T)` pattern. The Visit() lambda already has the type via `MetaMessage<T>::Type`.
**Warning signs:** Type names showing as mangled GCC names in NDJSON output.

## Code Examples

### Recording Tap in GameServer::Send()
```cpp
// Source: GameServer.cpp line ~719, modified to add recording
void GameServer::Send(const ConnectionId_t aConnectionId, const ServerMessage& acServerMessage) const
{
    // Recording tap (Phase 14)
    if (m_pRecorder)
        m_pRecorder->RecordServerMessage(aConnectionId, acServerMessage);

    // ... existing send logic unchanged ...
}
```

### Recording Tap in GameServer::OnConsume()
```cpp
// Source: GameServer.cpp line ~617, modified to add recording
void GameServer::OnConsume(const void* apData, const uint32_t aSize, const ConnectionId_t aConnectionId)
{
    ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    const ClientMessageFactory factory;
    auto pMessage = factory.Extract(reader);
    if (!pMessage) { /* error handling */ return; }

    // Recording tap (Phase 14)
    if (m_pRecorder)
        m_pRecorder->RecordClientMessage(aConnectionId, *pMessage);

    m_messageHandlers[pMessage->GetOpcode()](pMessage, aConnectionId);
}
```

### Visit()-Based Type Name Map
```cpp
// Build a static map from opcode -> type name string
#include <Messages/ClientMessageFactory.h>
#include <array>
#include <string_view>

// Use a macro to stringify the type name
#define MSG_NAME(T) {T::Opcode, #T}

// Or generate via Visit():
inline std::array<const char*, kClientOpcodeMax> BuildClientOpcodeNames()
{
    std::array<const char*, kClientOpcodeMax> names{};
    ClientMessageFactory::Visit([&names](auto& meta) {
        using T = typename std::remove_reference_t<decltype(meta)>::Type;
        names[T::Opcode] = __PRETTY_FUNCTION__; // Extract type name
        // Better: use a traits class with explicit name string
        return false;
    });
    return names;
}
```

### Message-to-JSON via Binary Payload
```cpp
// Serialize any ClientMessage to NDJSON-ready JSON object
nlohmann::json MessageSerializer::ToJson(const ClientMessage& acMessage)
{
    // Serialize to binary
    Buffer buffer(1 << 16);
    Buffer::Writer writer(&buffer);
    acMessage.SerializeRaw(writer);

    // Base64 encode
    auto* data = buffer.GetWriteData();
    auto size = writer.Size();
    std::string b64 = Base64Encode(data, size);

    nlohmann::json j;
    j["type"] = GetClientMessageName(acMessage.GetOpcode());
    j["opcode"] = static_cast<int>(acMessage.GetOpcode());
    j["payload"] = b64;
    return j;
}
```

### ServerReplayHarness Message Injection
```cpp
// Feed a recorded ClientMessage into the server
void ServerReplayHarness::InjectClientMessage(const nlohmann::json& acJson, ConnectionId_t aPeer)
{
    // Decode base64 payload back to binary
    auto binary = Base64Decode(acJson["payload"].get<std::string>());

    // Prepend opcode byte (OnConsume expects it)
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(acJson["opcode"].get<int>()));
    packet.insert(packet.end(), binary.begin(), binary.end());

    // Use existing InjectMessage() -- already routes to OnConsume()
    m_server->InjectMessage(packet.data(), packet.size(), aPeer);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Manual per-message JSON | Visit() compile-time dispatch | Available since codebase has ClientMessageFactory::Visit | Covers all 100+ messages without per-type code |
| Transport-layer capture | Application-layer recording | D-03 decision | Clean fixtures without enet6 noise |
| Single recording file | Per-peer NDJSON files | D-04 decision | Self-contained player transcripts |

## Open Questions

1. **Base64 vs Field-Level JSON**
   - What we know: Base64 covers all messages immediately. Field-level JSON is more debuggable but requires per-message code.
   - What's unclear: How much debugging value do field-level JSON fixtures provide vs the implementation cost?
   - Recommendation: Start with base64 for universal coverage. Add field-level JSON for the 5-10 most important messages (auth, assign, spawn, move) in a follow-up. The NDJSON format supports both -- just add optional field keys alongside the payload.

2. **GameServer Test Construction**
   - What we know: Constructor calls Host() and creates World. InjectMessage() and SetLoopbackRoute() exist.
   - What's unclear: Can we construct GameServer without calling Host()? Need to inspect TiltedPhoques::Server base class constructor.
   - Recommendation: Subclass GameServer with a test constructor that skips Host(), or extract the message handling into a separate testable component. Alternatively, use a mock Server base that no-ops Host().

3. **Type Name Extraction at Compile Time**
   - What we know: Visit() gives us the type T. We need a string name.
   - What's unclear: Best cross-compiler approach for clean type names.
   - Recommendation: Define a `MessageName<T>` traits template specialized for each message type. Can be generated with a simple macro: `DEFINE_MESSAGE_NAME(AuthenticationRequest)`. Boilerplate but reliable.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 |
| Config file | Code/tests/replay/xmake.lua |
| Quick run command | `xmake build ReplayTests && $(xmake show -t ReplayTests projectdir 2>/dev/null)/build/linux/x86_64/releasedbg/ReplayTests "[Enet6]"` |
| Full suite command | `xmake build ReplayTests && $(find build -name ReplayTests -type f) --order rand` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| (no formal IDs) | Recording taps capture messages as NDJSON | integration | `ReplayTests "[Enet6][Recording]"` | Wave 0 |
| (no formal IDs) | Server replay harness processes auth flow | unit | `ReplayTests "[Enet6][Auth]"` | Wave 0 |
| (no formal IDs) | Server replay harness processes character assign | unit | `ReplayTests "[Enet6][Character]"` | Wave 0 |
| (no formal IDs) | Server replay harness processes movement | unit | `ReplayTests "[Enet6][Movement]"` | Wave 0 |
| (no formal IDs) | Full enet6 session replay (aggregate) | integration | `ReplayTests "[Enet6][Integration]"` | Wave 0 |
| (no formal IDs) | Dual harness replays both hook events and enet6 | integration | `ReplayTests "[DualReplay]"` | Wave 0 |

### Sampling Rate
- **Per task commit:** `xmake build ReplayTests && find build -name ReplayTests -exec {} "[Enet6]" \;`
- **Per wave merge:** `xmake build ReplayTests && find build -name ReplayTests -exec {} \;`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `Code/server/Recording/EnetRecorder.h/cpp` -- recording tap component
- [ ] `Code/server/Recording/MessageSerializer.h/cpp` -- message-to-JSON conversion
- [ ] `Code/tests/replay/server_replay_harness.h/cpp` -- server-side replay harness
- [ ] `Code/tests/replay/enet_replay_tests.cpp` -- enet6 message replay tests
- [ ] `tests/fixtures/replay/enet/` directory -- enet6 NDJSON fixture files
- [ ] Base64 encode/decode utility (lightweight, can use nlohmann_json binary or a small header)

## Project Constraints (from CLAUDE.md)

- Build system: XMake, C++20
- Error handling: No exceptions, return bool or result codes, log via spdlog
- Naming: PascalCase classes, camelCase variables, `a` prefix for args, `m_` for members
- Headers: `#pragma once`, no `#ifndef` guards
- Recording code must compile on Linux (server is cross-platform)
- Test code links against SkyrimTogetherServer and SkyrimEncoding
- nlohmann_json already available in the build (used by ReplayTests)
- Existing fixture pattern: curated golden files in `tests/fixtures/replay/`, from live recordings

## Sources

### Primary (HIGH confidence)
- `Code/server/GameServer.h` and `GameServer.cpp` -- recording tap points, InjectMessage(), SetLoopbackRoute()
- `Code/server/World.h` and `World.cpp` -- server World construction, service emplacement
- `Code/encoding/Messages/ClientMessageFactory.h` -- Visit() compile-time dispatch pattern
- `Code/encoding/Messages/ServerMessageFactory.h` -- Visit() for server messages
- `Code/encoding/Opcodes.h` -- 46 client opcodes, 53 server opcodes
- `Code/encoding/Messages/Message.h` -- ClientMessage/ServerMessage base with Serialize/Deserialize
- `Code/native_client/replay/replay_harness.h/cpp` -- existing replay harness pattern to mirror
- `Code/tests/replay/xmake.lua` -- build configuration for replay tests
- `Code/tests/replay/replay_tests.cpp` -- existing Catch2 test patterns

### Secondary (MEDIUM confidence)
- `Code/server/Services/ScriptService.h` -- Lua state initialization (can be deferred)
- `Code/native_client/transport/LoopbackTransport.h` -- in-process message routing reference

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - all libraries already in the project, no new dependencies
- Architecture: HIGH - recording taps are straightforward insertions, Visit() pattern is proven
- Pitfalls: HIGH - code inspection reveals exact constructor side effects and dependencies
- Serialization strategy: MEDIUM - base64 approach is safe but field-level JSON needs per-message work

**Research date:** 2026-03-29
**Valid until:** 2026-04-28 (stable codebase, no external dependency changes expected)
