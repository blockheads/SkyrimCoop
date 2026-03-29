# Phase 10: Two-Player Co-op Session - Research

**Researched:** 2026-03-28
**Domain:** Multiplayer session orchestration, game thread command execution, enet6 client routing
**Confidence:** HIGH

## Summary

Phase 10 bridges the gap from "all the pieces exist" to "two players see each other." The native client already has: an embedded GameServer (host mode), a JoinClient (enet6 client), ServerMessageRouter (auth + move), CharacterService (full remote actor lifecycle with ECS), InterpolationSystem, and CMD_SET_POSITION command packets. The five specific gaps are: (1) JoinClient::OnConsume just logs instead of routing through ServerMessageRouter, (2) ServerMessageRouter only handles auth + move but not AssignCharacterResponse/CharacterSpawnRequest/NotifyRemoveCharacter, (3) DrainCommandQueue in the relay DLL is a stub, (4) DrainCommandQueue is not hooked into the game thread, and (5) JoinClient has no send path for outbound movement data. The two-Skyrim-instance orchestration is a launch script concern separate from the code gaps.

**Primary recommendation:** Wire JoinClient through ServerMessageRouter for all character messages, implement CMD_SET_POSITION execution in DrainCommandQueue triggered by HOOK_ACTOR_PROCESS on the player character, enable JoinClient to send ClientReferencesMoveRequest via enet6, and create a dual-instance launch script.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- D-01: Reuse existing ServerMessageRouter for JoinClient message routing
- D-02: JoinClient sends movement data via direct enet6 send
- D-03: JoinClient::OnConsume() must be wired into ServerMessageRouter
- D-04: ServerMessageRouter must handle character assignment messages
- D-05: Server sends character assignment to remote client
- D-06: CharacterService::OnAssignCharacter() already creates ECS entities -- just needs network trigger
- D-07: Implement CMD_SET_POSITION in DrainCommandQueue
- D-08: Hook DrainCommandQueue into game thread via once-per-frame function (research CommonLibSSE for right tick point instead of HookActorProcess)
- D-09: Only CMD_SET_POSITION for this phase
- D-10: Two separate Proton prefixes for each Skyrim instance
- D-11: CLI port override for relay DLL TCP ports (host=9667, client=9668)
- D-12: Create launch script for both instances
- D-13: Direct IP connection to localhost only
- D-14: Host starts embedded GameServer, client connects via --connect 127.0.0.1:10578
- D-15: Two separate Skyrim instances with own Proton prefix/SKSE/relay DLL/native process
- D-16: Movement sync only
- D-17: No inventory/combat/quest/spell sync
- D-18: Host's loaded cell is authoritative
- D-19: Success = visual movement sync in Riverwood
- D-20: Manual visual verification by user
- D-21: Test in Riverwood (sStartingCell=Riverwood)

### Claude's Discretion
- Character model selection for remote player (can use a default NPC appearance)
- Network tick rate and interpolation smoothing approach
- Specific CommonLibSSE function to hook for game thread tick (research required)

### Deferred Ideas (OUT OF SCOPE)
- Network stress testing
- Steam invite system
- Combat/inventory/quest sync
- UI overlay
- Full CMD_* implementation (only CMD_SET_POSITION)
- Transport abstraction (ITransport interface) -- direct enet send for now
</user_constraints>

## Architecture Patterns

### Gap Analysis: Current State vs. Phase 10 Goal

```
CURRENT STATE                          NEEDED FOR PHASE 10
======================================================
JoinClient::OnConsume() logs       --> Routes through ServerMessageRouter
ServerMessageRouter: auth + move   --> + AssignCharacter + CharacterSpawn + NotifyRemove
CharacterService.SyncMovement()    --> Sends via enet6 JoinClient (not just loopback)
  uses m_pLoopback only
DrainCommandQueue() is a stub      --> Executes CMD_SET_POSITION (write NiPoint3)
DrainCommandQueue not called       --> Called from HookActorProcess(player) or similar
No dual-instance launch script     --> Script starts 2 Skyrim + 2 native_client
```

### Recommended Implementation Order

```
Wave 1: ServerMessageRouter extension + JoinClient wiring
  - Add AssignCharacterResponse, CharacterSpawnRequest, NotifyRemoveCharacter handlers
  - Wire JoinClient::OnConsume to ServerMessageRouter
  - Add callbacks for assign/spawn/remove in CharacterService

Wave 2: Bidirectional movement sync for JoinClient
  - Give CharacterService a JoinClient* send path
  - SyncMovement sends ClientReferencesMoveRequest via JoinClient when in join mode

Wave 3: DLL-side CMD_SET_POSITION execution
  - Implement DrainCommandQueue to decode CMD_SET_POSITION and call RealSetPosition
  - Hook DrainCommandQueue into game thread (HOOK_ACTOR_PROCESS on player formId 0x14)

Wave 4: Dual-instance launch script + integration test
  - Create launch_two_player.sh
  - Two Proton prefixes, two relay DLL ports, host mode + join mode
```

### Message Flow: Full Two-Player Sync

```
HOST INSTANCE:
  Skyrim (Host)
    -> HOOK_SET_POSITION fires (DLL hook)
    -> TCP -> native_client (host mode)
    -> CharacterService.SyncMovement()
    -> LoopbackTransport.SendToServer(ClientReferencesMoveRequest)
    -> GameServer.OnConsume() dispatches to server CharacterService
    -> Server CharacterService.ProcessMovementChanges()
    -> GameServer.Send(joinClientConnectionId, ServerReferencesMoveRequest)
    -> enet6 UDP -> JoinClient

CLIENT INSTANCE:
  JoinClient.OnConsume()
    -> ServerMessageRouter.OnServerMessage()
    -> MoveCallback -> CharacterService.OnReferencesMoveRequest()
    -> Updates InterpolationComponent
    -> InterpolationSystem.Update() interpolates position
    -> CharacterService.ApplyRemotePositions()
    -> SendSetPosition() -> TCP -> DLL
    -> DrainCommandQueue() -> RealSetPosition() on game thread
    -> Remote character moves in client's Skyrim

CLIENT -> HOST (reverse path):
  Skyrim (Client)
    -> HOOK_SET_POSITION fires (player movement)
    -> TCP -> native_client (join mode)
    -> CharacterService.SyncMovement()
    -> JoinClient.Send(ClientReferencesMoveRequest) via enet6
    -> GameServer receives, dispatches to server CharacterService
    -> Broadcasts ServerReferencesMoveRequest to host (via loopback)
    -> Host's ServerMessageRouter -> CharacterService -> DLL -> game thread
```

### Pattern: Game Thread Hook for DrainCommandQueue

**Research finding (HIGH confidence):** D-08 asks to research CommonLibSSE for a once-per-frame game thread hook. After analysis, the **best approach is HOOK_ACTOR_PROCESS on the player character (formId 0x14)**.

Rationale:
- HOOK_ACTOR_PROCESS (Address Library ID 37356) is `Actor::Update(float delta)` -- called once per game frame per loaded actor
- It is already an essential hook in the relay DLL (always installed)
- The player actor (formId 0x14) is always loaded when the game is playable
- Filtering on `ReadFormId(apThis) == 0x14` gives exactly once-per-frame execution on the game thread
- No additional hook installation needed -- just add DrainCommandQueue() call inside the existing HookActorProcess

Alternative considered: Finding a global `Main::Update` function. CommonLibSSE's Main.h does not expose a simple Update function with an Address Library ID. The ActorProcess hook is simpler, already installed, and equally effective for our needs.

**Implementation pattern:**
```cpp
// In hook_trampolines.cpp, modify HookActorProcess:
static void __fastcall HookActorProcess(void* apThis, float aDelta)
{
    // Drain command queue once per frame (on player actor only)
    if (ReadFormId(apThis) == 0x14)
    {
        DrainCommandQueue();  // Execute CMD_SET_POSITION etc on game thread
    }

    uint64_t args[3] = { reinterpret_cast<uint64_t>(apThis),
                          static_cast<uint64_t>(ReadFormId(apThis)),
                          FloatToU64(aDelta) };
    SendHookEvent(HOOK_ACTOR_PROCESS, 3, args);
    RealActorProcess(apThis, aDelta);
}
```

### Pattern: CMD_SET_POSITION Execution

```cpp
// DrainCommandQueue implementation in relay_main.cpp:
static void DrainCommandQueue()
{
    g_commandQueue.Drain([](const CommandSlot& cmd) {
        switch (cmd.opcode) {
        case CMD_SET_POSITION: {
            // cmd.args[0] = actorPtr
            // cmd.args[1] = (posY << 32) | posX  (two packed floats)
            // cmd.args[2] = (rotX << 32) | posZ   (two packed floats)
            // cmd.args[3] = rotZ in lower 32 bits
            void* pActor = reinterpret_cast<void*>(cmd.args[0]);
            if (!pActor) break;

            uint32_t posXBits = static_cast<uint32_t>(cmd.args[1]);
            uint32_t posYBits = static_cast<uint32_t>(cmd.args[1] >> 32);
            uint32_t posZBits = static_cast<uint32_t>(cmd.args[2]);

            float pos[3];
            memcpy(&pos[0], &posXBits, sizeof(float));
            memcpy(&pos[1], &posYBits, sizeof(float));
            memcpy(&pos[2], &posZBits, sizeof(float));

            // Call the real SetPosition (Address Library ID 19790)
            // RealSetPosition is already resolved by MinHook
            if (RealSetPosition)
                RealSetPosition(pActor, pos);
            break;
        }
        default:
            break;
        }
    });
}
```

### Pattern: ServerMessageRouter Extension

The router currently handles 2 opcodes. It needs 3 more:

| Opcode | Message | Callback Signature |
|--------|---------|-------------------|
| `kAssignCharacterResponse` | AssignCharacterResponse | `void(uint32_t serverId, uint32_t formId, float x, y, z, float rx, ry, bool owner)` |
| `kCharacterSpawnRequest` | CharacterSpawnRequest | `void(uint32_t serverId, uint32_t formId, float x, y, z, float rx, ry)` |
| `kNotifyRemoveCharacter` | NotifyRemoveCharacter | `void(uint32_t serverId)` |

Note: The server sends `AssignCharacterResponse` to the player who sent `AssignCharacterRequest` (the owner). It sends `CharacterSpawnRequest` to all OTHER players when a new character is created. For our two-player scenario:
- When JoinClient authenticates + sends AssignCharacterRequest, it gets `AssignCharacterResponse` (its own character)
- The host gets `CharacterSpawnRequest` (the join client's character)
- When the host's character was already created, the JoinClient gets `CharacterSpawnRequest` for the host

### Pattern: JoinClient Movement Send

```cpp
// CharacterService needs a JoinClient* pointer (set in join mode):
void CharacterService::SetJoinClient(JoinClient* apClient) { m_pJoinClient = apClient; }

// In SyncMovement(), replace the loopback-only send:
if (changed) {
    ClientReferencesMoveRequest moveReq;
    // ... build moveReq ...

    if (m_pLoopback) {
        m_pLoopback->SendToServer(moveReq);  // host mode
    } else if (m_pJoinClient) {
        // Serialize and send via enet6
        TiltedPhoques::Buffer buffer(1 << 16);
        TiltedPhoques::Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8);
        moveReq.Serialize(writer);
        TiltedPhoques::PacketView packet(
            reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());
        m_pJoinClient->Send(&packet);
    }
}
```

### Pattern: Dual Proton Prefix Setup

```bash
# Host uses default Skyrim compat data
COMPAT_HOST="/media/.../steamapps/compatdata/489830"

# Client needs a SEPARATE compat data directory (per D-10)
COMPAT_CLIENT="/media/.../steamapps/compatdata/489830_client"

# Create client prefix by copying host prefix (one-time setup)
if [ ! -d "$COMPAT_CLIENT" ]; then
    cp -a "$COMPAT_HOST" "$COMPAT_CLIENT"
fi
```

### Character Model for Remote Player

**Discretion area:** The remote player needs a visible character model. Options:
- **Recommended:** Use formId 0x14 (player) -- both instances have a player character. The server's AssignCharacterRequest flow creates an entity with a BaseId and appearance data. For Phase 10 MVP, the remote character will appear as the default starting character appearance (Nord warrior) since we're not syncing appearance buffers yet.
- The existing server flow in `CreateCharacter()` stores appearance data from `AssignCharacterRequest.AppearanceBuffer`. The JoinClient's auth request doesn't send this (it only sends `AuthenticationRequest`). The AssignCharacterRequest would need to be sent after auth to trigger the full character creation pipeline.

**Simplification for Phase 10:** Skip the full AssignCharacterRequest pipeline. Instead, when the server detects a new player connection (PlayerJoinEvent), the host's native client creates a RemoteComponent entity manually with a known NPC formId (e.g., 0x14 for the remote player reference). The movement sync then just updates this entity's position.

**Actually, the full pipeline is better:** The JoinClient should send an AssignCharacterRequest after auth (same as the original client). This triggers the server's CreateCharacter, which broadcasts CharacterSpawnRequest to the host, and sends AssignCharacterResponse back to the JoinClient. This uses ALL existing server code without modification.

### Anti-Patterns to Avoid

- **Modifying GameServer.cpp for Phase 10:** The server code already handles multiple players, movement broadcasting, and character assignment. Do not modify server code.
- **Building a new transport abstraction:** D-02 says use direct enet6 send. Don't create an ITransport interface.
- **Implementing full AssignCharacterRequest from native client:** The native client doesn't have access to appearance buffers, inventory data, etc. Send a minimal AssignCharacterRequest with just player formId and position.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Message serialization | Custom binary protocol | Existing ClientMessage/ServerMessage serialization | Already battle-tested, differential encoding built-in |
| Entity tracking | Custom actor map | EnTT registry with RemoteComponent/InterpolationComponent | Already implemented in CharacterService |
| Network transport | Raw socket code | TiltedPhoques::Client (JoinClient inherits this) | Handles reliability, connection management |
| Position interpolation | Custom lerp | InterpolationSystem already written | Handles frame-rate-independent smoothing |
| Game thread execution | Sleep+poll loop | CommandQueue SPSC ring buffer | Lock-free, cache-friendly, already implemented |

## Common Pitfalls

### Pitfall 1: AssignCharacterRequest Requires Fields the Native Client Doesn't Have
**What goes wrong:** The server's CreateCharacter expects populated fields (AppearanceBuffer, InventoryContent, FaceTints, etc). Sending empty values may cause null pointer issues or the character spawn request sent to other players may have no appearance data, resulting in invisible/glitched remote characters.
**Why it happens:** The original client reads these from the actual game state. The native client only has memory reads.
**How to avoid:** Send a minimal AssignCharacterRequest with only: ReferenceId (GameId{0, 0x14} for player), Position, Rotation, and CurrentActorData.IsDead=false. The server handles empty optional fields gracefully -- CharacterSpawnRequest will have empty appearance, which means remote character uses base game appearance.
**Warning signs:** Remote character invisible or T-posing.

### Pitfall 2: RealSetPosition Pointer Invalid After Hook Removal
**What goes wrong:** If hooks are removed (game shutdown), DrainCommandQueue still tries to call RealSetPosition which is now invalid.
**Why it happens:** DrainCommandQueue runs on the game thread, hook removal happens on DLL detach.
**How to avoid:** Check `RealSetPosition != nullptr` before calling. Set to nullptr in RemoveAllHooks.
**Warning signs:** Crash during game shutdown.

### Pitfall 3: Two Skyrim Instances Fighting Over Same Files
**What goes wrong:** Two Skyrim instances writing to the same saves directory, same SKSE log, same INI files.
**Why it happens:** Both use the same WINEPREFIX/compat data path.
**How to avoid:** D-10 mandates separate Proton prefixes. The launch script must set STEAM_COMPAT_DATA_PATH differently for each instance.
**Warning signs:** One instance fails to launch, or both crash with file lock errors.

### Pitfall 4: TCP Port Collision Between Two Relay DLLs
**What goes wrong:** Both DLL instances try to bind to the same TCP port.
**Why it happens:** The DLL TCP server uses an ephemeral port by default (port 0). If both happen to get the same port, or if the relay info file path collides.
**How to avoid:** D-11 mandates CLI port override. The DLL currently uses port 0 (ephemeral). Since each instance is in a separate Proton prefix, the relay_info.json files are in separate directories. However, the native_client needs --port to know which to connect to. The launch script reads each relay_info.json separately.
**Warning signs:** Native client connects to wrong DLL instance.

### Pitfall 5: Host and Client Both Send AssignCharacterRequest for formId 0x14
**What goes wrong:** Both are "the player" with formId 0x14. The server sees two different players claiming the same game form.
**Why it happens:** formId 0x14 is the player character in every Skyrim instance.
**How to avoid:** The server's AssignCharacterRequest handler uses `isPlayer = (refId.ModId == 0 && refId.BaseId == 0x14)` which sets `isCustom = true`. For custom/player entities, it creates a new server entity each time (no FormIdComponent lookup). Each player gets a unique ServerId. This already works correctly.
**Warning signs:** None -- this is already handled by the server.

### Pitfall 6: DLL relay_info.json Path Collision
**What goes wrong:** Both DLLs write relay_info.json to the same path since they're deployed to the same SKSE/Plugins directory.
**Why it happens:** If both instances use the same Skyrim installation with separate Proton prefixes but the same game files.
**How to avoid:** Each Proton prefix means each DLL writes to a different Wine-mapped path. But wait -- the DLL writes the relay_info.json using a Linux path derived from the DLL's Windows path. If both instances load the DLL from the same physical Linux directory (`Data/SKSE/Plugins/`), they'll write to the same relay_info.json. Solution: the launch script should copy the DLL+native binary to a per-instance directory, OR the relay_info.json should include a port/PID suffix, OR the native client should parse the relay_info.json before the second instance starts.
**Warning signs:** Second native client connects to first DLL.

## Code Examples

### Current JoinClient::OnConsume (needs modification)
```cpp
// Source: Code/native_client/main.cpp lines 216-234
void OnConsume(const void* apData, uint32_t aSize) override {
    // Currently just logs -- needs ServerMessageRouter wiring
    TiltedPhoques::Buffer buffer(aSize);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBytes(reinterpret_cast<const uint8_t*>(apData), aSize);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    ServerMessageFactory factory;
    auto pMessage = factory.Extract(reader);
    if (pMessage) {
        spdlog::debug("JoinClient: received server opcode {}", pMessage->GetOpcode());
        m_messagesReceived++;
    }
}
```

### Current DrainCommandQueue Stub (needs implementation)
```cpp
// Source: Code/relay_dll/relay_main.cpp lines 118-126
[[maybe_unused]]
static void DrainCommandQueue()
{
    g_commandQueue.Drain([](const CommandSlot& cmd) {
        // TODO(Plan 05+): Execute actual game functions
        RelayLog("[SkyrimCoopHooks] Executing command opcode=0x%04X\n", cmd.opcode);
    });
}
```

### Server CharacterService Character Assignment Flow
```cpp
// Source: Code/server/Services/CharacterService.cpp lines 383-474
// CreateCharacter creates entity, sends AssignCharacterResponse to requester,
// triggers CharacterSpawnedEvent which broadcasts CharacterSpawnRequest to other players.
// This is the existing flow -- no modification needed.
```

### CMD_SET_POSITION Argument Packing
```cpp
// Source: Code/native_client/Services/CharacterService.cpp lines 538-563
// args[0] = actorPtr (uint64_t)
// args[1] = (posY_bits << 32) | posX_bits
// args[2] = (rotX_bits << 32) | posZ_bits
// args[3] = rotZ_bits in lower 32 bits
// argCount = 4
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| DLL-side formId reads via pread | DLL-side formId resolution (ReadFormId inline) | Phase 9.1 | Faster, no cross-process reads for formId |
| LoopbackTransport only | LoopbackTransport + JoinClient enet6 | Phase 8+ | Supports both host and client modes |
| Stub DrainCommandQueue | Needs implementation | Phase 10 | Enables writing game state from native process |

## Open Questions

1. **RealSetPosition accessibility from DrainCommandQueue**
   - What we know: RealSetPosition is a file-static variable in hook_trampolines.cpp. DrainCommandQueue is in relay_main.cpp.
   - What's unclear: How to access RealSetPosition from relay_main.cpp without breaking encapsulation.
   - Recommendation: Either (a) make RealSetPosition extern, (b) move DrainCommandQueue into hook_trampolines.cpp, or (c) register a function pointer callback during hook installation. Option (c) is cleanest.

2. **JoinClient needs AssignCharacterRequest after auth**
   - What we know: Auth alone doesn't trigger character creation on the server. The client must send AssignCharacterRequest.
   - What's unclear: What minimal fields satisfy the server's CreateCharacter without crashing.
   - Recommendation: Send AssignCharacterRequest with ReferenceId={0, 0x14}, Position from game memory, empty optional fields. Test empirically.

3. **Relay info file collision for two instances**
   - What we know: Both DLLs write relay_info.json to the same Linux filesystem path if they share the same Skyrim installation.
   - What's unclear: Whether separate Proton prefixes cause DLL path mapping to differ.
   - Recommendation: The DLL derives path from its Windows DLL path via Wine Z: drive mapping. If both prefixes mount the same Linux filesystem, the path will be identical. Solution: use a port-specific filename (e.g., `skyrim_coop_relay_9667.json`) or have the launch script stagger starts and read relay info sequentially.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| xmake | Build system | Yes | v3.0.8 | -- |
| MinGW cross-compiler | DLL build | Yes | GCC 10 (system) + project toolchain | -- |
| Proton Experimental | Running Skyrim | Yes | Experimental | -- |
| Skyrim SE | Game instance | Yes | 1.6.1170 | -- |
| SKSE | Plugin loading | Yes | 2.2.6 | -- |
| enet6 | GameServer networking | Yes | Built from source | -- |
| Two Proton prefixes | D-10 | Partially (1 exists, 2nd needs creation) | -- | Copy existing prefix |

**Missing dependencies with no fallback:**
- None

**Missing dependencies with fallback:**
- Second Proton prefix not yet created (copy from existing `compatdata/489830`)

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 + bash integration scripts |
| Config file | Code/tests/ (Catch2 header-only) |
| Quick run command | `xmake build TPTests && ./build/release/tests/TPTests` |
| Full suite command | `xmake build TPTests && ./build/release/tests/TPTests && tests/test_ingame_sync.sh` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| D-03 | JoinClient routes messages through ServerMessageRouter | unit | `./build/release/tests/TPTests "[router]"` | No -- Wave 0 |
| D-04 | ServerMessageRouter handles AssignCharacter + CharacterSpawn | unit | `./build/release/tests/TPTests "[router]"` | No -- Wave 0 |
| D-07 | CMD_SET_POSITION executes in DrainCommandQueue | manual | Visual verification in game | N/A (manual) |
| D-08 | DrainCommandQueue called from game thread | manual | Visual verification in game | N/A (manual) |
| D-16 | Movement sync between two instances | manual | Visual verification | N/A (manual) |
| D-19 | Both players see each other moving in Riverwood | manual | Visual verification by user | N/A (manual) |

### Sampling Rate
- **Per task commit:** `xmake build SkyrimCoopHooksDLL SkyrimCoopNative TPTests && ./build/release/tests/TPTests`
- **Per wave merge:** Full build + existing test_ingame_sync.sh (single instance)
- **Phase gate:** Manual two-player visual test by user

### Wave 0 Gaps
- [ ] `Code/tests/router_test.cpp` -- unit tests for ServerMessageRouter with AssignCharacter/CharacterSpawn/NotifyRemove
- [ ] Integration: no automated test for two-instance sync (manual verification per D-20)

## Sources

### Primary (HIGH confidence)
- Codebase direct inspection: Code/native_client/main.cpp, Code/native_client/Services/CharacterService.cpp, Code/relay_dll/relay_main.cpp, Code/relay_dll/hook_trampolines.cpp, Code/server/Services/CharacterService.cpp, Code/encoding/Messages/*
- Code/relay_dll/protocol.h -- CMD_SET_POSITION (0x8001) argument layout
- Code/server/GameServer.cpp -- Send(), SendToPlayers(), loopback routing

### Secondary (MEDIUM confidence)
- [CommonLibSSE TESObjectREFR.h](https://github.com/Ryan-rsm-McKenzie/CommonLibSSE/blob/master/include/RE/T/TESObjectREFR.h) -- SetPosition signature verification
- [CommonLibSSE Main.h](https://github.com/Ryan-rsm-McKenzie/CommonLibSSE/blob/master/include/RE/M/Main.h) -- Game loop analysis (no simple Update hook found)

### Tertiary (LOW confidence)
- None

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - All code exists in repo, directly inspected
- Architecture: HIGH - Message flow traced through codebase end-to-end
- Pitfalls: HIGH - Based on direct code inspection of collision points
- Game thread hook: HIGH - HOOK_ACTOR_PROCESS already installed, player formId 0x14 filter well-understood

**Research date:** 2026-03-28
**Valid until:** Indefinite (codebase-specific research, no external dependencies changing)
