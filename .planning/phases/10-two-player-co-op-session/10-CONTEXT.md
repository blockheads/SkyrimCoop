# Phase 10: Two-Player Co-op Session - Context

**Gathered:** 2026-03-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Two Skyrim SE instances running on one Linux machine under Proton. One hosts (embedded GameServer), the other connects as client via localhost. Both players see each other's character model and sync movement in real-time. No UI, no Steam invites, no inventory/combat sync — just character presence and movement.

</domain>

<decisions>
## Implementation Decisions

### Connection Model
- **D-01:** Direct IP connection to localhost — no Steam invites, no lobby system, no discovery
- **D-02:** Host starts embedded GameServer (already implemented in Phase 8), client connects via TransportService to 127.0.0.1:10578
- **D-03:** Two separate Skyrim instances on same machine, each with its own Proton prefix/SKSE/relay DLL/native process

### Sync Scope
- **D-04:** Movement sync only — both players see each other's character model and position updates
- **D-05:** No inventory sync, no combat sync, no quest sync, no spell sync for this phase
- **D-06:** Host's loaded cell is authoritative — client must be in the same cell to see the host character

### Test Setup
- **D-07:** Two MO2 profiles or two separate Skyrim installs to avoid file locking conflicts
- **D-08:** Each instance needs its own relay DLL → native process → GameServer/client pipeline
- **D-09:** Test in Riverwood (sStartingCell=Riverwood) for both instances — known small cell with NPCs

### Claude's Discretion
- Character model selection for remote player (can use a default NPC appearance)
- Network tick rate and interpolation smoothing approach
- How to differentiate host vs client mode in the native binary (CLI flag, config file, etc.)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Architecture
- `Code/native_client/main.cpp` — Native client entry point, event loop, DispatchHookEvent
- `Code/native_client/Services/CharacterService.cpp` — Actor spawning, movement sync, position cache
- `Code/native_client/Services/CharacterService.h` — TrackedActor struct, position cache, service interface
- `Code/relay_dll/protocol.h` — HookEventPacket struct, arg layout conventions

### Networking
- `Code/server/GameServer.cpp` — Embedded server, player connection handling
- `Code/server/Services/CharacterService.cpp` — Server-side actor state, broadcast to players
- `Code/server/Services/PlayerService.cpp` — Player authentication, connection management
- `Code/encoding/Messages/` — Network message definitions (58 client, 61 server opcodes)

### Prior Phase Context
- `.planning/phases/09.1-dll-side-formid-resolution/09.1-03-SUMMARY.md` — FormId resolution verification results
- `.planning/phases/09.1-dll-side-formid-resolution/09.1-RESEARCH.md` — DLL-side formId research

### Test Infrastructure
- `tests/test_ingame_sync.sh` — Existing in-game test script (single instance)
- `tests/ci_test.sh` — CI pipeline
- `Code/tests/integration/fake_player.cpp` — Fake player for headless testing

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **GameServer**: Embedded server already works (Phase 8) — host mode with enet6 on UDP 10578
- **fake_player**: Connects to GameServer, sends mock data — proves networking works
- **TransportService**: Client-side networking already exists in original codebase
- **CharacterService (server)**: Broadcasts actor state to connected players
- **PointerTable + formId resolution**: DLL-side formId reading confirmed working (Phase 9.1)
- **test_ingame_sync.sh**: Launches Skyrim, connects native process, runs fake_player

### Established Patterns
- Hook trampolines → TCP → native process → DispatchHookEvent → services
- enet6 UDP for GameServer ↔ client communication
- EnTT ECS for entity/component management on both client and server
- Differential serialization for movement updates

### Integration Points
- Native binary needs a "client mode" (connect to remote GameServer instead of hosting)
- Client needs to receive ServerReferencesMoveRequest and spawn remote character models
- Host needs to broadcast its movement to connected clients via the existing CharacterService pipeline

</code_context>

<specifics>
## Specific Ideas

- Two Skyrim instances on the same machine — may need separate Proton prefixes to avoid file locking
- Start both in Riverwood for testing (sStartingCell injection already implemented)
- User mentioned wanting packet simulation/desync testing tools eventually — deferred to future phase

</specifics>

<deferred>
## Deferred Ideas

- **Network stress testing**: Packet drop simulation, latency injection, desync detection — user wants this but not for Phase 10
- **Steam invite system**: Replace direct IP with Steam friend invite flow
- **Combat/inventory/quest sync**: Beyond movement — full gameplay sync
- **UI overlay**: In-game connection UI, player list, health bars

</deferred>

---

*Phase: 10-two-player-co-op-session*
*Context gathered: 2026-03-28*
