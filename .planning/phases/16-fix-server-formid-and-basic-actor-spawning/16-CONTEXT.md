# Phase 16: Fix Server FormId & Basic Actor Spawning - Context

**Gathered:** 2026-03-29
**Status:** Ready for planning

<domain>
## Phase Boundary

Fix the formId/spawn pipeline so remote player characters and world NPCs spawn correctly in-game via the relay DLL, with a host-authoritative state reconciliation model that guarantees clients never desync.

This phase absorbs Phase 17's scope (TESNPC::Create + appearance reconstruction + AI suppression). The server's FormId=0:0 for player characters is intentional (STR design — players use AppearanceBuffer, not formId lookup). The real work is:
1. Porting STR's full Actor::Create pipeline to the relay DLL (both spawn paths)
2. Adding AI suppression on remote actors
3. Implementing a 1-second WorldStateSync reconciliation message so clients always converge to host state
4. Disconnect/reconnect cleanup (clear all remote state, rebuild from host baseline)
5. Unit tests, replay tests, and human E2E verification

</domain>

<decisions>
## Implementation Decisions

### FormId Pipeline Understanding
- **D-01:** The server's CreateCharacter deliberately sends FormId=0:0 for player characters. This is STR's intended design — player characters use the custom form pipeline (TESNPC::Create from AppearanceBuffer), not formId lookup. The server code is correct; no server-side "fix" needed.
- **D-02:** The native client must route spawn requests based on FormId value: FormId==0:0 with AppearanceBuffer → cmd_spawn_actor with full appearance data for TESNPC::Create; non-zero FormId → cmd_spawn_actor with formId for TESForm::GetById lookup.

### Spawn Pipeline (Absorbs Phase 17)
- **D-03:** Port STR's full Actor::Create pipeline to the relay DLL: TESNPC::Create from AppearanceBuffer + ChangeFlags, Actor::Create(TESNPC*), SetSkipSaveFlag, ForcePosition, and all setup calls from STR's `Code/client/Games/Skyrim/Actor.cpp`.
- **D-04:** Implement BOTH spawn paths in the relay DLL: (1) custom TESNPC::Create for player characters (FormId=0:0), and (2) TESForm::GetById for world NPCs with known formIds. This matches STR's full CharacterSpawnRequest handling.
- **D-05:** Forward the raw AppearanceBuffer + ChangeFlags + FaceTints over TCP as a single cmd_spawn_actor payload. DLL deserializes on game thread using TESNPC::Deserialize/TESNPC::Create. No separate appearance command — one atomic spawn.
- **D-06:** Implement AI suppression on remote actors via ActorExtension::SetRemote(true) or equivalent flag-setting in the DLL spawn path. Without this, spawned actors immediately execute Skyrim's default AI (wandering, combat).

### Host-Authoritative State Reconciliation (Core Architecture)
- **D-07:** **The host is always right.** The host's embedded GameServer holds the authoritative entity registry. Clients mirror it. Every layer (native process, relay DLL) is rebuildable from the layer above it. This is the same architecture used by Deep Rock Galactic, Borderlands, Left 4 Dead 2, Terraria, and Helldivers 2.
- **D-08:** **New server message: `WorldStateSync`** — sent every 1 second to each connected client. Contains the complete list of synced entity serverIds + their current positions/rotations. At 50 actors × ~20 bytes each = ~1KB/s per client. Trivial bandwidth cost.
- **D-09:** **Client reconciliation on every WorldStateSync receipt:**
  - ServerId on server but not on client → spawn it (request full data if needed)
  - ServerId on client but not on server → remove the orphan immediately
  - Both exist but position differs → correct to server position
  This makes the system self-healing. Even if a spawn/remove event is missed, the next 1-second sync fixes it.
- **D-10:** **Event-driven messages remain the fast path.** CharacterSpawnRequest / NotifyRemoveCharacter / ServerReferencesMoveRequest handle immediate updates (sub-second latency). The 1-second WorldStateSync is the safety net, not the primary path.
- **D-11:** **Disconnect cleanup:** On enet6 disconnect, native process must: (1) destroy all RemoteComponent entities, (2) send cmd_remove_actor for every tracked pointer to DLL, (3) clear serverId → pointer map. Clean slate.
- **D-12:** **Reconnect = fresh connect.** No special reconnect logic. Server sends full baseline (same as WorldStateSync, plus full appearance data for each entity). Client rebuilds everything from scratch. This is what every successful co-op game does.
- **D-13:** **No host migration.** If host disconnects, session ends. No co-op game in production implements host migration for this pattern (DRG devs explicitly called it "too technically complex"). Accepted industry standard.
- **D-14:** **This pattern is reusable.** Every future sync system (inventory, combat, quests, actor values) should follow the same model: event-driven fast path + periodic reconciliation safety net. Getting it right for actor spawning establishes the template.

### Actor Lifecycle
- **D-15:** (Carried from Phase 15 D-06) Immediate deletion on disconnect. cmd_remove_actor deletes the actor from Skyrim immediately. No fade-out.
- **D-16:** (Carried from Phase 15 D-01) Relay DLL remains a stateless thin shim — executes commands and reports results. All actor identity tracking (serverId → pointer) lives in native process.
- **D-17:** DLL reports spawn results back to native via spawn_result hook event with actor pointer and formId. Native process maintains serverId → actor pointer map.

### Test Strategy
- **D-18:** Unit tests focus on native-side routing logic: verify CharacterService correctly routes FormId==0:0 to custom spawn path vs non-zero FormId to formId-based spawn. Test spawn_result → serverId-to-pointer mapping. Uses existing Catch2 framework.
- **D-19:** New dedicated NDJSON replay fixtures for spawn pipeline: (1) player character spawn (FormId=0:0, has AppearanceBuffer), (2) world NPC spawn (non-zero FormId), (3) full spawn + move + remove lifecycle. Verify cmd_spawn_actor emitted with correct data.
- **D-20:** Reconciliation tests: verify WorldStateSync handler correctly spawns missing entities, removes orphans, and corrects position mismatches. Test disconnect cleanup (all remote entities cleared). Test reconnect (fresh baseline rebuilds state).

### Human E2E Verification
- **D-21:** Pass criteria: remote actor spawns in-game at the correct position with correct race/appearance. Actor doesn't wander (AI suppressed). Fail: no actor visible, crash, or actor at 0,0,0. Equipment/animation sync not required.

### Claude's Discretion
- Exact address library IDs for TESNPC::Create, Actor::Create, ActorExtension::SetRemote, and related game functions (determined by reading STR source + SKSE source)
- How to serialize AppearanceBuffer over the JSON TCP protocol (base64-encoded blob, raw binary field, or chunked)
- FaceGenSystem::Setup port scope — whether full face morph reconstruction is needed for Phase 16 or can be simplified
- WaitingFor3D pattern — whether to implement the full deferred-state-application pattern from STR or use a simpler synchronous approach
- Error handling for spawn failures (formId not found, cell not loaded, TESNPC::Create failure)
- Which STR setup calls are strictly necessary vs nice-to-have (SetLevelMod, magic caster init, dragon/mount flags)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### STR Actor Spawning (reference implementation — PORT SOURCE)
- `Code/client/Games/Skyrim/Actor.cpp` — Actor::Create(TESNPC*) and Actor::Spawn(uint32_t). Full actor creation pipeline including SetSkipSaveFlag, SetRemote, SetLevelMod, ForcePosition, magic caster init.
- `Code/client/Games/Skyrim/TESNPC.cpp` — TESNPC::Create(AppearanceBuffer, ChangeFlags) and TESNPC::Deserialize. Appearance reconstruction from network data.
- `Code/client/Games/ModManager.cpp` — ModManager::Spawn() calls SpawnNewREFR (address lib ID 13723).
- `Code/client/Services/Generic/CharacterService.cpp` — OnCharacterSpawn() (line 350). Full handler showing both spawn paths: custom form (FormId==0:0) and world NPC (non-zero FormId).

### SKSE Source (game function signatures)
- `/home/bighass/Downloads/skse64_2_02_06 (1)/src/skse64/` — Task interface, game struct definitions, address resolution patterns.

### Current Relay DLL (modification target)
- `Code/relay_dll/relay_main.cpp` — DrainCommandQueue(). New spawn/remove command handlers go here.
- `Code/relay_dll/json_protocol.h` — JSON op constants. Needs new ops for spawn with appearance data.
- `Code/relay_dll/protocol.h` — CommandOpcode enum and CommandSlot struct.
- `Code/relay_dll/hook_trampolines.h` — Hook infrastructure. New game function pointers for TESNPC::Create, Actor::Create, ActorExtension::SetRemote go here.

### Native Client (routing logic)
- `Code/native_client/Services/CharacterService.cpp` — OnAssignCharacter(). Must route spawn commands based on FormId value.
- `Code/native_client/transport/ServerMessageRouter.cpp` — Decodes CharacterSpawnRequest with callbacks.

### Server (modification target for WorldStateSync)
- `Code/server/Services/CharacterService.cpp` — CreateCharacter() (line 389) and Serialize() (line 76). Shows FormId=0:0 is by design for player characters. ProcessMovementChanges() (line ~600) shows existing broadcast pattern — WorldStateSync follows same pattern.
- `Code/server/GameServer.cpp` — OnDisconnection() (line 684). Shows existing disconnect cleanup (removes owned entities, broadcasts NotifyRemoveCharacter).

### Message Encoding
- `Code/encoding/Messages/CharacterSpawnRequest.h` — Spawn message fields (ServerId, FormId, BaseId, Position, AppearanceBuffer, ChangeFlags, FaceTints).
- `Code/encoding/Opcodes.h` — New opcode needed for WorldStateSync message.

### Test Infrastructure
- `Code/tests/replay/` — Existing replay test patterns and DualReplayHarness.
- `tests/fixtures/replay/` — Existing NDJSON fixture files.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- STR's full Actor::Create pipeline in `Code/client/Games/Skyrim/Actor.cpp` — battle-tested code with correct address library IDs and calling conventions, direct port target
- STR's TESNPC::Create/Deserialize in `Code/client/Games/Skyrim/TESNPC.cpp` — appearance reconstruction from network buffer
- STR's OnCharacterSpawn in `Code/client/Services/Generic/CharacterService.cpp` — shows the exact routing logic (FormId==0:0 → custom, else → formId lookup)
- DualReplayHarness from Phase 14 — spawn pipeline replay tests can use existing harness infrastructure
- MessageSerializer from Phase 14 — programmatic fixture generation for spawn test scenarios

### Established Patterns
- Commands flow: native process → JSON over TCP → DLL command queue → game thread execution
- Events flow: game hooks → DLL → JSON over TCP → native process services
- Address library ID resolution via inline parser in DLL (Phase 7)
- SPSC command queue for safe game-thread execution

### Integration Points
- Native CharacterService::OnAssignCharacter() — entry point for routing spawn commands
- DLL DrainCommandQueue — new command handler for spawn with appearance data
- DLL hook_trampolines — new game function pointer resolution for TESNPC/Actor creation functions
- TCP JSON protocol — cmd_spawn_actor payload needs to carry AppearanceBuffer

</code_context>

<specifics>
## Specific Ideas

- Phase 16 now absorbs Phase 17 scope — full TESNPC::Create pipeline + appearance reconstruction + AI suppression, not just "basic actor spawning"
- The server code is correct (FormId=0:0 for players is by design) — all work is on the native client and relay DLL side
- AppearanceBuffer goes over TCP as a single atomic payload in cmd_spawn_actor — no separate appearance command
- STR source is the reference implementation to port from — read it first, then adapt for relay DLL architecture
- **Key architectural advantage over STR:** STR's server was headless (no Skyrim, no hooks, no game state) — it could only relay what clients told it. Our host IS Skyrim. The host's game state is ground truth. This enables true server authority that STR could never achieve.
- **The reconciliation pattern (event-driven + 1s sync) is the foundation for ALL future sync systems.** Inventory, combat, quests, actor values — they all follow this same template. Getting the actor spawn reconciliation right means we have a proven, reusable pattern.
- **Research basis:** This architecture matches what Deep Rock Galactic (UE4 listen server), Borderlands (UE3/4), Left 4 Dead 2 (Source Engine), Terraria (custom engine), and Helldivers 2 use. Every successful 2-4 player co-op game converges on host-authoritative with periodic reconciliation.

</specifics>

<deferred>
## Deferred Ideas

- Equipment sync (what the remote player is wearing) — Phase 18
- Animation sync (walking, combat animations) — Phase 18
- Actor value sync (health, magicka, stamina) — Phase 18
- Death/respawn sync — Phase 18
- Weapon drawn state sync — Phase 18
- SKSE task interface port (replace HookActorProcess piggyback with clean game-thread callback) — future optimization
- Fade-out animation on actor removal — polish for later

</deferred>

---

*Phase: 16-fix-server-formid-and-basic-actor-spawning*
*Context gathered: 2026-03-29*
