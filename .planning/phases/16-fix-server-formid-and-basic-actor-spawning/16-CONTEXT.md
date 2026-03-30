# Phase 16: Fix Server FormId & Basic Actor Spawning - Context

**Gathered:** 2026-03-29
**Status:** Ready for planning

<domain>
## Phase Boundary

Fix the formId/spawn pipeline so remote player characters and world NPCs spawn correctly in-game via the relay DLL. This phase absorbs Phase 17's scope (TESNPC::Create + appearance reconstruction + AI suppression). The server's FormId=0:0 for player characters is intentional (STR design — players use AppearanceBuffer, not formId lookup). The real work is porting STR's full Actor::Create pipeline to the relay DLL, implementing both spawn paths (custom form via TESNPC::Create, and world NPC via TESForm::GetById), adding AI suppression, and validating with unit tests, replay tests, and human E2E verification.

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

### Actor Lifecycle
- **D-07:** (Carried from Phase 15 D-06) Immediate deletion on disconnect. cmd_remove_actor deletes the actor from Skyrim immediately. No fade-out.
- **D-08:** (Carried from Phase 15 D-01) Relay DLL remains a stateless thin shim — executes commands and reports results. All actor identity tracking (serverId → pointer) lives in native process.
- **D-09:** DLL reports spawn results back to native via spawn_result hook event with actor pointer and formId. Native process maintains serverId → actor pointer map.

### Test Strategy
- **D-10:** Unit tests focus on native-side routing logic: verify CharacterService correctly routes FormId==0:0 to custom spawn path vs non-zero FormId to formId-based spawn. Test spawn_result → serverId-to-pointer mapping. Uses existing Catch2 framework.
- **D-11:** New dedicated NDJSON replay fixtures for spawn pipeline: (1) player character spawn (FormId=0:0, has AppearanceBuffer), (2) world NPC spawn (non-zero FormId), (3) full spawn + move + remove lifecycle. Verify cmd_spawn_actor emitted with correct data.

### Human E2E Verification
- **D-12:** Pass criteria: remote actor spawns in-game at the correct position with correct race/appearance. Actor doesn't wander (AI suppressed). Fail: no actor visible, crash, or actor at 0,0,0. Equipment/animation sync not required.

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

### Server (understanding — NOT modifying)
- `Code/server/Services/CharacterService.cpp` — CreateCharacter() (line 389) and Serialize() (line 76). Shows FormId=0:0 is by design for player characters.

### Message Encoding
- `Code/encoding/Messages/CharacterSpawnRequest.h` — Spawn message fields (ServerId, FormId, BaseId, Position, AppearanceBuffer, ChangeFlags, FaceTints).

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
