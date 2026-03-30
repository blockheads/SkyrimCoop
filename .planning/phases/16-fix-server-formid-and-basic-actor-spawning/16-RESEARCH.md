# Phase 16: Fix Server FormId & Basic Actor Spawning - Research

**Researched:** 2026-03-29
**Domain:** Skyrim actor spawning pipeline, TESNPC/Actor creation, host-authoritative reconciliation
**Confidence:** HIGH

## Summary

Phase 16 is primarily a native client + relay DLL implementation phase that ports STR's proven Actor::Create pipeline to the relay DLL architecture. The server code is correct by design -- player characters intentionally have no FormIdComponent (FormId=0:0) because they use the custom TESNPC::Create path from AppearanceBuffer. The DLL must implement both spawn paths: (1) TESNPC::Create from AppearanceBuffer for players/temporaries, and (2) TESForm::GetById for world NPCs.

The phase also establishes the foundational reconciliation pattern (WorldStateSync + event-driven fast path) that all future sync systems will follow. This requires a new server message, a new opcode, and client-side reconciliation logic. The disconnect/reconnect cleanup and AI suppression (SetRemote) are essential for correctness.

The existing relay DLL CMD_SPAWN_ACTOR handler already implements the formId-based spawn path (TESForm::GetById + heap alloc + Character ctor + SpawnNewREFR). What's missing is: (1) the TESNPC::Create appearance path, (2) AI suppression, (3) forwarding AppearanceBuffer over TCP, (4) WorldStateSync reconciliation, (5) disconnect cleanup, and (6) the ServerMessageRouter needs to forward appearance data from CharacterSpawnRequest.

**Primary recommendation:** Port STR's spawn logic to the DLL in two paths, extend the JSON protocol with appearance data, add WorldStateSync as a new server message, and implement client-side reconciliation with disconnect cleanup.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** The server's CreateCharacter deliberately sends FormId=0:0 for player characters. This is STR's intended design -- player characters use the custom form pipeline (TESNPC::Create from AppearanceBuffer), not formId lookup. The server code is correct; no server-side "fix" needed.
- **D-02:** The native client must route spawn requests based on FormId value: FormId==0:0 with AppearanceBuffer -> cmd_spawn_actor with full appearance data for TESNPC::Create; non-zero FormId -> cmd_spawn_actor with formId for TESForm::GetById lookup.
- **D-03:** Port STR's full Actor::Create pipeline to the relay DLL: TESNPC::Create from AppearanceBuffer + ChangeFlags, Actor::Create(TESNPC*), SetSkipSaveFlag, ForcePosition, and all setup calls from STR's Code/client/Games/Skyrim/Actor.cpp.
- **D-04:** Implement BOTH spawn paths in the relay DLL: (1) custom TESNPC::Create for player characters (FormId=0:0), and (2) TESForm::GetById for world NPCs with known formIds. This matches STR's full CharacterSpawnRequest handling.
- **D-05:** Forward the raw AppearanceBuffer + ChangeFlags + FaceTints over TCP as a single cmd_spawn_actor payload. DLL deserializes on game thread using TESNPC::Deserialize/TESNPC::Create. No separate appearance command -- one atomic spawn.
- **D-06:** Implement AI suppression on remote actors via ActorExtension::SetRemote(true) or equivalent flag-setting in the DLL spawn path. Without this, spawned actors immediately execute Skyrim's default AI (wandering, combat).
- **D-07:** The host is always right -- single simulation, multiple renderers. Host's Skyrim is the ONE simulation. Clients receive results and render them.
- **D-08:** New server message: WorldStateSync -- sent every 1 second to each connected client. Contains complete list of synced entity serverIds + positions/rotations.
- **D-09:** Client reconciliation on every WorldStateSync receipt: spawn missing, remove orphans, correct positions.
- **D-10:** Event-driven messages remain the fast path. WorldStateSync is the safety net.
- **D-11:** Disconnect cleanup: destroy all RemoteComponent entities, send cmd_remove_actor for every tracked pointer, clear maps.
- **D-12:** Reconnect = fresh connect. Server sends full baseline. Client rebuilds from scratch.
- **D-13:** No host migration. If host disconnects, session ends.
- **D-14:** This reconciliation pattern is reusable for all future sync systems.
- **D-14b:** Unreliable sequenced channel for position updates. Spawn/remove/reconciliation on reliable ordered.
- **D-15:** Immediate deletion on disconnect (cmd_remove_actor).
- **D-16:** Relay DLL remains stateless thin shim. All identity tracking in native process.
- **D-17:** DLL reports spawn results via spawn_result hook event.
- **D-18:** Unit tests focus on native-side routing logic (Catch2).
- **D-19:** New NDJSON replay fixtures for spawn pipeline.
- **D-20:** Reconciliation tests: WorldStateSync handler, orphan removal, position correction, disconnect cleanup.
- **D-21:** Human E2E pass criteria: remote actor visible at correct position with correct race/appearance, AI suppressed.

### Claude's Discretion
- Exact address library IDs for TESNPC::Create-related game functions (IFormFactory::GetForType, BGSLoadFormBuffer, TESNPC::Initialize defaults)
- How to serialize AppearanceBuffer over JSON TCP protocol (base64, raw binary field, or chunked)
- FaceGenSystem::Setup port scope -- whether full face morph reconstruction needed for Phase 16 or simplified
- WaitingFor3D pattern -- full deferred-state-application vs simpler synchronous approach
- Error handling for spawn failures
- Which STR setup calls are strictly necessary vs nice-to-have

### Deferred Ideas (OUT OF SCOPE)
- Equipment sync -- Phase 18
- Animation sync -- Phase 18
- Actor value sync -- Phase 18
- Death/respawn sync -- Phase 18
- Weapon drawn state sync -- Phase 18
- Cell tethering -- separate phase
- SKSE task interface port -- future optimization
- Fade-out animation on actor removal -- polish for later
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SPAWN-08 | Server CreateCharacter populates FormIdComponent for player characters (non-zero formId in CharacterSpawnRequest) | Per D-01: server is CORRECT. No FormIdComponent for players is by design. The "fix" is implementing both spawn paths in the DLL. Requirement description from roadmap is outdated by CONTEXT.md decisions. |
| SPAWN-09 | Unit tests verify formId resolution for player characters, temporary forms, and world-persistent NPCs | Routing logic lives in native CharacterService. Tests verify FormId==0:0 routes to appearance path, non-zero routes to formId lookup path. DualReplayHarness + MessageSerializer available. |
| SPAWN-10 | Replay tests verify spawn pipeline: CharacterSpawnRequest -> cmd_spawn_actor with correct formId -> spawn_result handling | Existing DualReplayHarness and NDJSON fixture infrastructure. Need new fixtures with AppearanceBuffer data. |
</phase_requirements>

## Standard Stack

### Core (Already in Project)
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| EnTT | 3.10.0 | ECS for all actor/entity tracking | Already used everywhere |
| nlohmann_json | 3.11.3 | JSON wire protocol (DLL <-> native) | Already used in relay DLL and native client |
| Catch2 | 2.13.9 | Unit tests | Already used in TPTests and ReplayTests |
| spdlog | 1.13.0 | Logging | Already used everywhere |
| glm | 0.9.9+8 | 3D math | Already used for positions/rotations |
| enet6 | (project dep) | UDP networking | Already used for client-server transport |

### No New Dependencies
This phase requires no new libraries. All work uses existing dependencies.

## Architecture Patterns

### Spawn Path Routing (Critical Pattern)

The native client CharacterService must route spawns based on FormId:

```
CharacterSpawnRequest received
  |
  FormId == GameId{} (0:0)?
  |--- YES: Custom form path (player characters, temporaries)
  |    |--- Has BaseId? -> Use BaseId for TESNPC lookup + Deserialize with AppearanceBuffer
  |    |--- No BaseId?  -> TESNPC::Create from AppearanceBuffer + ChangeFlags (pure custom)
  |
  |--- NO: World NPC path
       |--- TESForm::GetById(FormId) to find existing NPC
       |--- Actor::Create(pNpc) or just set remote flags if already exists
```

Source: `Code/client/Services/Generic/CharacterService.cpp:350-423` -- STR's OnCharacterSpawn

### DLL Spawn Command Structure

Current `cmd_spawn_actor` only supports formId-based spawning. Must be extended to carry:

```json
{
  "op": "cmd_spawn_actor",
  "form_id": 0,
  "appearance": "<base64-encoded AppearanceBuffer>",
  "change_flags": 12345,
  "face_tints": "<base64-encoded FaceTints>",
  "x": 100.0, "y": 200.0, "z": 300.0,
  "rx": 0.0, "ry": 1.57
}
```

When `form_id == 0` and `appearance` is present: DLL uses custom TESNPC::Create path.
When `form_id != 0`: DLL uses existing TESForm::GetById path (already implemented).

### WorldStateSync Message Pattern

New server message added to Opcodes.h:

```cpp
// In Opcodes.h, add before kServerOpcodeMax:
kWorldStateSync,
kServerOpcodeMax
```

Message structure:
```cpp
struct WorldStateSync final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kWorldStateSync;

    struct EntityEntry {
        uint32_t ServerId;
        Vector3_NetQuantize Position;
        Rotator2_NetQuantize Rotation;
    };

    Vector<EntityEntry> Entities;

    void SerializeRaw(Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Buffer::Reader& aReader) noexcept override;
};
```

### STR Actor::Create Pipeline (Port Target)

From `Code/client/Games/Skyrim/Actor.cpp:175-208`:

```
1. Actor::New()                    -- heap alloc + Character constructor
2. SetSkipSaveFlag(true)           -- unk10 = 0xFFFF (offset 0x18)
3. GetExtension()->SetRemote(true) -- AI suppression
4. Get player for cell/worldspace context
5. SetLevelMod(4)                  -- via ExtraDataList (offset 0x70), addr 11806
6. MarkChanged(0x40000000)         -- flag actor as changed
7. SetParentCell(pCell)            -- write parentCell pointer
8. SetBaseForm(apBaseForm)         -- write baseForm at offset 0x40
9. Check cell interior bit, null if exterior
10. ModManager::Spawn()            -- SpawnNewREFR (addr 13723)
11. ForcePosition(position)        -- SetPosition call
12. InitMagicCasters(LEFT, RIGHT, OTHER) -- GetMagicCaster for each source
13. Clear flags bit 21             -- actorFlags &= 0xFFDFFFFF
```

### TESNPC::Create Pipeline (New for DLL)

From `Code/client/Games/Forms.cpp:116-127`:

```
1. IFormFactory::Create<TESNPC>()  -- needs addr 400508 (factory table)
2. pNpc->Initialize()              -- copy defaults from player base form
3. pNpc->Deserialize(buffer, flags) -- BGSLoadFormBuffer + Load()
4. pNpc->overlayRace = nullptr     -- force facegen
```

**Additional address library IDs needed:**
| Function | Address ID | Signature | Source |
|----------|-----------|-----------|--------|
| IFormFactory table | 400508 | IFormFactory*[] | `Code/client/Games/FormFactory.cpp:10` |
| BGSLoadFormBuffer | varies | Constructor + Load pattern | `Code/client/Games/Forms.cpp:38-51` |
| ActorExtension::SetRemote | N/A (flag write) | Direct memory write | `Code/client/Games/ActorExtension.cpp:30` |
| GetMagicCaster | varies | Actor thiscall | `Code/client/Games/Skyrim/Actor.cpp:201-203` |

### Recommended Project Structure for New Files

```
Code/
├── encoding/Messages/
│   └── WorldStateSync.h           # New message definition
│   └── WorldStateSync.cpp         # Serialize/Deserialize
├── encoding/Opcodes.h             # Add kWorldStateSync opcode
├── relay_dll/
│   └── relay_main.cpp             # Extend CMD_SPAWN_ACTOR for appearance path
│   └── hook_trampolines.h/cpp     # Add IFormFactory, BGSLoadFormBuffer resolvers
│   └── json_protocol.h            # Add appearance fields
├── native_client/
│   └── Services/CharacterService.cpp  # Route spawn based on FormId, reconciliation
│   └── Services/CharacterService.h    # Add WorldStateSync handler, disconnect cleanup
│   └── transport/ServerMessageRouter.cpp  # Forward AppearanceBuffer from CharacterSpawnRequest
│   └── transport/ServerMessageRouter.h    # Extend SpawnCallback with appearance data
├── server/
│   └── Services/CharacterService.cpp  # Add WorldStateSync broadcast timer
│   └─�� GameServer.cpp                 # Disconnect cleanup already exists, verify
├── tests/replay/
│   └── spawn_pipeline_tests.cpp       # New test file for spawn routing + reconciliation
└── tests/fixtures/replay/enet/
    └── player_spawn.ndjson            # Fixture: FormId=0:0 with AppearanceBuffer
    └── npc_spawn.ndjson               # Fixture: non-zero FormId
    └── spawn_lifecycle.ndjson         # Fixture: spawn + move + remove cycle
```

### Anti-Patterns to Avoid
- **Modifying server CreateCharacter for player FormIds:** The server is correct. Player characters deliberately have no FormIdComponent. Changing this breaks STR's design.
- **Sending AppearanceBuffer as separate message:** D-05 mandates atomic cmd_spawn_actor with all data. Split messages create race conditions.
- **Letting DLL track actor identity:** D-16 mandates stateless DLL. All serverId-to-pointer mapping in native process.
- **Simulating AI on remote clients:** D-07 mandates single simulation. Remote actors must have AI suppressed (SetRemote).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| AppearanceBuffer encoding | Custom binary format | Base64-encode existing STR String over JSON | STR's AppearanceBuffer is already a serialized binary blob. Base64 preserves it exactly. |
| Actor creation sequence | New spawn logic | Port STR's exact Actor::Create sequence | STR's sequence handles edge cases (interior/exterior cells, magic casters, save flags) that took years to debug |
| Reconciliation protocol | Custom diff algorithm | Simple full-state sync (all serverIds + positions) | At 50 actors x 20 bytes = 1KB/s, bandwidth is trivial. Simple > clever. |
| Position interpolation | New interpolation system | Existing InterpolationComponent + system | Already implemented in Phase 15 with lerp-based catch-up |

## Common Pitfalls

### Pitfall 1: AppearanceBuffer Corruption in JSON Transport
**What goes wrong:** Binary AppearanceBuffer contains null bytes, special characters that break JSON string fields.
**Why it happens:** JSON strings cannot contain raw binary data.
**How to avoid:** Base64-encode AppearanceBuffer before putting in JSON, decode in DLL before passing to TESNPC::Deserialize.
**Warning signs:** Garbled NPC appearance, crash in BGSLoadFormBuffer::Load.

### Pitfall 2: TESNPC::Create Without Initialize() Defaults
**What goes wrong:** Spawned NPC has no race, no combat style, crashes on AI tick.
**Why it happens:** TESNPC::Initialize() copies critical defaults from the player base form (attackDataMap, npcClass, combatStyle, race). Without these, the NPC struct is incomplete.
**How to avoid:** Always call Initialize() before Deserialize() in the custom form path, exactly as STR does in Forms.cpp:116-127.
**Warning signs:** CTD on spawned actor AI update, "null race" in logs.

### Pitfall 3: SpawnNewREFR With Wrong Cell Parameter
**What goes wrong:** Actor spawns in limbo or immediately despawns.
**Why it happens:** STR passes nullptr for cell when in exterior worldspace (bit 0 of cellFlags is NOT set). Passing the actual cell pointer for exteriors confuses the spawn system.
**How to avoid:** Check `cellFlags[0] & 1` (interior bit). If not set, pass nullptr for cell and use worldspace instead. See Actor.cpp:194.
**Warning signs:** Actor invisible after spawn, or immediate actor_removed event.

### Pitfall 4: Missing AI Suppression (SetRemote)
**What goes wrong:** Spawned remote actors wander off, enter combat with NPCs, break quest scripts.
**Why it happens:** Skyrim's default AI activates on any Actor in a loaded cell. Without SetRemote, the engine treats the spawned actor as a normal NPC.
**How to avoid:** Call SetRemote(true) immediately after Actor::Create, before any position update. In STR this is in Actor::Create itself (line 180).
**Warning signs:** Spawned player character walks away from spawn point, attacks things.

### Pitfall 5: WorldStateSync Opcode Collision
**What goes wrong:** Server sends WorldStateSync, client interprets it as a different message, crashes or corrupts state.
**Why it happens:** New opcode added to ServerOpcode enum at wrong position or without updating kServerOpcodeMax.
**How to avoid:** Add kWorldStateSync just before kServerOpcodeMax. Update ServerMessageFactory to create WorldStateSync instances.
**Warning signs:** "Unknown opcode" warnings, message deserialization failures.

### Pitfall 6: Disconnect Cleanup Race Condition
**What goes wrong:** DLL still has actor pointers that native process has already cleared, leading to dangling pointer commands.
**Why it happens:** Native sends cmd_remove_actor for each tracked pointer, but DLL may be mid-drain on a stale command.
**How to avoid:** On disconnect, clear native-side maps first, then send remove commands. DLL's stateless design means stale pointers just fail gracefully (null check).
**Warning signs:** Post-disconnect crashes, ghost actors after reconnect.

## Code Examples

### Example 1: Extended SpawnCallback with Appearance Data

The ServerMessageRouter currently strips appearance data when calling SpawnCallback. It must forward it:

```cpp
// ServerMessageRouter.h -- extended callback
using SpawnCallback = std::function<void(
    uint32_t aServerId, uint32_t aFormId,
    float aX, float aY, float aZ,
    float aRx, float aRy,
    bool aIsPlayer,
    const String& aAppearanceBuffer,
    uint32_t aChangeFlags,
    const Tints& aFaceTints)>;

// ServerMessageRouter.cpp -- forward appearance data
else if (pMessage->GetOpcode() == kCharacterSpawnRequest)
{
    auto* pSpawn = static_cast<CharacterSpawnRequest*>(pMessage.get());
    if (m_onSpawn)
    {
        m_onSpawn(pSpawn->ServerId, pSpawn->FormId.BaseId,
                  pSpawn->Position.x, pSpawn->Position.y, pSpawn->Position.z,
                  pSpawn->Rotation.x, pSpawn->Rotation.y,
                  pSpawn->IsPlayer,
                  pSpawn->AppearanceBuffer,
                  pSpawn->ChangeFlags,
                  pSpawn->FaceTints);
    }
}
```

### Example 2: Native CharacterService Spawn Routing

```cpp
// CharacterService.cpp -- route based on FormId
void CharacterService::OnSpawnCharacter(uint32_t aServerId, uint32_t aFormId,
                                         const glm::vec3& aPos, const glm::vec2& aRot,
                                         bool aIsPlayer,
                                         const std::string& aAppearanceBase64,
                                         uint32_t aChangeFlags,
                                         const std::string& aFaceTintsBase64)
{
    if (aFormId == 0 && !aAppearanceBase64.empty())
    {
        // Custom form path: TESNPC::Create from appearance
        SendSpawnActorWithAppearance(aPos, aRot, aAppearanceBase64,
                                      aChangeFlags, aFaceTintsBase64);
    }
    else if (aFormId != 0)
    {
        // World NPC path: existing formId-based spawn
        SendSpawnActor(aFormId, aPos, aRot);
    }
    // ... create RemoteComponent + InterpolationComponent as before
}
```

### Example 3: Base64 Encoding for AppearanceBuffer

```cpp
// In native_client: encode STR's binary String to base64 for JSON transport
#include <vector>
#include <string>

static const char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const void* apData, size_t aSize)
{
    std::string result;
    result.reserve(((aSize + 2) / 3) * 4);
    const auto* p = static_cast<const uint8_t*>(apData);
    // ... standard base64 encoding
    return result;
}

std::vector<uint8_t> Base64Decode(const std::string& aEncoded)
{
    // ... standard base64 decoding
}
```

### Example 4: DLL-side TESNPC::Create Sequence

```cpp
// In relay_main.cpp CMD_SPAWN_ACTOR handler, when form_id==0 and appearance present:

// 1. Get IFormFactory for TESNPC (type 43 = FormType::Npc)
auto pFactoryTable = reinterpret_cast<void**>(GetIFormFactoryTable());
auto pNpcFactory = pFactoryTable[43]; // FormType::Npc

// 2. Call virtual Create() to allocate TESNPC
// IFormFactory vtable: index 1 is Create()
auto pCreate = *reinterpret_cast<void*(**)(void*)>(
    *reinterpret_cast<uintptr_t*>(pNpcFactory) + 8); // vtable[1]
void* pNpc = pCreate(pNpcFactory);

// 3. Initialize() -- copy defaults from player base form
auto pPlayer = pGetById(0x14);
void* pPlayerBaseForm = *reinterpret_cast<void**>(
    reinterpret_cast<uintptr_t>(pPlayer) + 0x40);
// Copy attackDataMap, npcClass, combatStyle, race from player base...

// 4. Deserialize appearance buffer
// Construct BGSLoadFormBuffer, set buffer pointer, call Load() virtual

// 5. Clear overlayRace (force facegen)
// pNpc->overlayRace at appropriate offset = nullptr

// 6. Proceed with existing Actor::Create sequence (heap alloc, ctor, spawn)
```

### Example 5: WorldStateSync Server-Side Broadcast

```cpp
// In Server::CharacterService::OnUpdate, every 1 second:
void CharacterService::BroadcastWorldStateSync() const noexcept
{
    WorldStateSync sync;

    auto view = m_world.view<MovementComponent, CharacterComponent>();
    for (auto entity : view)
    {
        auto& movement = view.get<MovementComponent>(entity);
        WorldStateSync::EntityEntry entry;
        entry.ServerId = World::ToInteger(entity);
        entry.Position = movement.Position;
        entry.Rotation = {movement.Rotation.x, movement.Rotation.z};
        sync.Entities.push_back(entry);
    }

    GameServer::Get()->SendToPlayers(sync);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Phase 15 formId-only spawn | Dual-path spawn (formId + appearance) | Phase 16 | Players spawn correctly with custom appearance |
| No reconciliation | WorldStateSync 1s safety net | Phase 16 | Self-healing state sync, tolerates dropped messages |
| No disconnect cleanup | Full cleanup on enet6 disconnect | Phase 16 | Clean reconnects, no ghost actors |
| All messages on reliable ordered | Position on unreliable sequenced | Phase 16 | Lower latency for position updates |

## Open Questions

1. **IFormFactory vtable offset verification**
   - What we know: IFormFactory::Create is a virtual function, likely at vtable index 1 per the header definition
   - What's unclear: Exact vtable layout in Skyrim SE 1.6.x binary -- is Create() at offset 8 or could it differ?
   - Recommendation: Resolve via existing address library + STR's IFormFactory::GetForType (addr 400508). The factory table returns IFormFactory pointers; calling the Create virtual through the vtable is safe as long as we use the correct factory for FormType::Npc (type 43).

2. **BGSLoadFormBuffer construction in DLL**
   - What we know: STR constructs BGSLoadFormBuffer with change flags, sets buffer pointer, calls Load() virtual
   - What's unclear: Exact struct layout and constructor address for BGSLoadFormBuffer in the relay DLL (no game headers)
   - Recommendation: Research BGSLoadFormBuffer struct layout from STR headers. It's a simple struct: changeFlags, size, buffer pointer, formId, form pointer. Can be replicated as raw offsets in the DLL.

3. **FaceGenSystem complexity for Phase 16**
   - What we know: STR calls FaceGenSystem::Setup for custom forms (players/temporaries)
   - What's unclear: How much face morph data is needed for a basic recognizable player appearance
   - Recommendation: Skip FaceGenSystem::Setup for Phase 16 MVP. The base appearance (race, gender, skin color) comes from TESNPC::Deserialize. Face morphs/tints are polish. D-21 says "correct race/appearance" -- the Deserialize call handles race.

4. **AppearanceBuffer size over JSON**
   - What we know: AppearanceBuffer is a serialized binary blob (STR's String type, typically a few KB)
   - What's unclear: Maximum size in practice
   - Recommendation: Base64 encoding adds ~33% overhead. Even a 10KB appearance buffer becomes ~13KB base64 -- well within JSON line limits. Use base64.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 |
| Config file | `Code/tests/replay/xmake.lua` (ReplayTests target) |
| Quick run command | `xmake build ReplayTests && ./build/linux/x86_64/release/ReplayTests "[Spawn]"` |
| Full suite command | `xmake build ReplayTests && ./build/linux/x86_64/release/ReplayTests` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| SPAWN-08 | FormId routing: 0:0 -> appearance path, non-zero -> formId path | unit | `./ReplayTests "[SpawnRouting]"` | No -- Wave 0 |
| SPAWN-09 | Unit tests for formId resolution across player/temp/NPC cases | unit | `./ReplayTests "[SpawnRouting]"` | No -- Wave 0 |
| SPAWN-10 | Replay tests: CharacterSpawnRequest -> cmd_spawn_actor -> spawn_result | integration | `./ReplayTests "[SpawnPipeline]"` | No -- Wave 0 |
| D-20 | Reconciliation: WorldStateSync spawns missing, removes orphans, corrects position | unit | `./ReplayTests "[Reconciliation]"` | No -- Wave 0 |
| D-20 | Disconnect cleanup: all remote entities cleared | unit | `./ReplayTests "[Disconnect]"` | No -- Wave 0 |

### Sampling Rate
- **Per task commit:** `xmake build ReplayTests && ./build/linux/x86_64/release/ReplayTests "[Spawn],[Reconciliation],[Disconnect]"`
- **Per wave merge:** Full suite: `./build/linux/x86_64/release/ReplayTests`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `Code/tests/replay/spawn_pipeline_tests.cpp` -- covers SPAWN-08, SPAWN-09, SPAWN-10, D-20
- [ ] `tests/fixtures/replay/enet/player_spawn.ndjson` -- fixture: player character with FormId=0:0 + AppearanceBuffer
- [ ] `tests/fixtures/replay/enet/npc_spawn.ndjson` -- fixture: world NPC with non-zero FormId
- [ ] `tests/fixtures/replay/enet/spawn_lifecycle.ndjson` -- fixture: spawn + move + remove + reconciliation

## Sources

### Primary (HIGH confidence)
- `Code/client/Games/Skyrim/Actor.cpp:175-208` -- STR Actor::Create pipeline (exact port target)
- `Code/client/Games/Forms.cpp:116-127` -- STR TESNPC::Create implementation
- `Code/client/Games/Forms.cpp:38-51` -- STR TESNPC::Deserialize implementation
- `Code/client/Services/Generic/CharacterService.cpp:350-423` -- STR OnCharacterSpawn routing logic
- `Code/server/Services/CharacterService.cpp:389-485` -- Server CreateCharacter (FormId=0:0 is intentional)
- `Code/server/Services/CharacterService.cpp:64-100` -- Server Serialize (FormIdComponent -> CharacterSpawnRequest)
- `Code/relay_dll/relay_main.cpp:146-300` -- Existing CMD_SPAWN_ACTOR/CMD_REMOVE_ACTOR
- `Code/native_client/Services/CharacterService.cpp` -- Current native CharacterService with spawn/remove
- `Code/native_client/transport/ServerMessageRouter.cpp` -- Current message routing
- `Code/encoding/Opcodes.h` -- Current opcode enum (need to add kWorldStateSync)
- `.planning/NETWORK-ARCHITECTURE.md` -- Canonical network architecture reference

### Secondary (MEDIUM confidence)
- Address library IDs verified against STR source: 14617 (GetFormById), 68115 (HeapAllocate), 400188 (HeapInstance), 40245 (CharacterCtor), 13723 (SpawnNewREFR), 400269 (ModManager), 11806 (SetLevelMod), 400508 (IFormFactory table)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all libraries already in project, no new deps
- Architecture: HIGH -- STR source provides exact reference implementation, all patterns verified against code
- Pitfalls: HIGH -- STR's codebase documents the exact failure modes through years of debugging
- Reconciliation: HIGH -- architecture documented in NETWORK-ARCHITECTURE.md with user approval

**Research date:** 2026-03-29
**Valid until:** 2026-04-28 (stable codebase, no external dependency changes expected)
