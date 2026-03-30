# Requirements: SkyrimCoop

**Defined:** 2026-03-30
**Core Value:** Two friends can drop into Skyrim together on Linux with zero server setup

## v2.0 Requirements

Requirements for MinGW full client build with loopback-based embedded server. Each maps to roadmap phases.

### Build & ABI Safety

- [ ] **BUILD-01**: Full Code/client/ compiles under MinGW as a single SKSE DLL (CEF/DirectXTK/Discord excluded via preprocessor guards)
- [ ] **BUILD-02**: ABI safety fixes applied — `__declspec(align)` replaced with `alignas()`, try/catch UB removed from HostService, struct layout correctness verified with MinGW
- [x] **BUILD-03**: Code/relay_dll/ and Code/native_client/ deleted from source tree and build system
- [ ] **BUILD-04**: SKSE interaction abstracted behind ISkyrimBridge interface — real implementation calls SKSE APIs, test implementation replays recorded events for headless execution without Skyrim

### Embedded Server (Loopback)

- [ ] **HOST-01**: LoopbackTransport delivers client messages to embedded GameServer and server responses back to client services without network round-trip
- [ ] **HOST-02**: TransportService operates in dual mode — loopback path for host player, enet6 network path for joining clients
- [ ] **HOST-03**: HostService::StartHosting() constructs embedded GameServer in DLL process with start/stop/cleanup lifecycle

### Verification

- [ ] **VERIFY-01**: Two-player co-op session via direct IP:port connect — both players visible in-game using existing STR sync
- [ ] **VERIFY-02**: Existing STR character sync (spawn, movement, animation) works through loopback bridge
- [ ] **VERIFY-03**: Existing STR inventory/equipment sync works through loopback bridge
- [ ] **VERIFY-04**: Disconnect/reconnect cleanup — remote actors removed on disconnect, rejoin produces fresh state
- [ ] **VERIFY-05**: Linux cross-compile and debug workflow validated — build from Linux via MinGW, deploy to Proton, attach gdb with DWARF symbols

## Future Milestone Requirements

Deferred to future milestones. Tracked but not in v2.0 roadmap.

### Host Authority Rewrite (v3.0+)

Rewrite existing STR client-server code to use proper host-authoritative logic instead of loopback bridge. The host's Skyrim engine IS the simulation — client services should read engine state directly, not send messages to themselves through the server.

**Reference docs:**
- `.planning/NETWORK-ARCHITECTURE.md` — "Single Simulation, Multiple Renderers" canonical design
- `P2P_ARCHITECTURE.md` — direct authority model, no localhost loopback
- `WHY_DIRECT_AUTHORITY_IS_REQUIRED.md` — why direct authority is mandatory

- **AUTH-01**: CharacterService reads host player position/state directly from engine (no self-messaging)
- **AUTH-02**: InventoryService reads host equipment directly from engine
- **AUTH-03**: All services implement host-bypass path (read engine) vs client path (receive from network)
- **AUTH-04**: StringCache triple-update bug fixed by removing host self-routing
- **AUTH-05**: Server services access game state directly when running in host process

### Full Game State Sync (v4.0+)

Extend sync to cover ALL game systems — physics, AI, scripting, weather, lighting, etc. Currently STR skips many systems (physics engine, Havok state, Papyrus scripts, etc.). The ultimate goal: every game system is host-authoritative, with state sent to clients and interpolated. This is essentially rewriting Skyrim's single-player systems to be multiplayer-aware.

**Pattern:** Host simulates -> state captured -> sent to clients -> clients interpolate/apply. This is the pattern for EVERYTHING.

- **GSYNC-01**: Physics/Havok state sync (host-authoritative ragdolls, object physics)
- **GSYNC-02**: AI state sync (NPC behavior, combat AI, pathfinding decisions)
- **GSYNC-03**: Papyrus script state sync (quest scripts, world scripts)
- **GSYNC-04**: Weather/lighting/time sync (host-authoritative environment)
- **GSYNC-05**: Sound/music state sync
- **GSYNC-06**: Cell/worldspace transition sync

**Note:** This will likely require multiple milestones. We are essentially making a single-player game engine multiplayer-aware — the scope is massive but this IS the ultimate goal of the port.

## Out of Scope

| Feature | Reason |
|---------|--------|
| CEF overlay UI | Not needed for core multiplayer; add after sync works |
| DirectXTK debug UI | Not needed for core multiplayer |
| Discord SDK integration | Polish, not core |
| Steam invite system | Direct IP first; Steam invites are polish |
| Dedicated server hosting | Co-op only, no MMO infrastructure |
| More than 4 players | Keep intimate co-op scale |
| Native Linux SKSE port | SKSE stays Windows, runs under Proton |
| Host migration | Industry standard for co-op: host leaves = session ends |
| Direct authority refactor | Deferred to v3.0+ milestone (loopback works for MVP) |
| New sync systems (physics, AI, etc.) | Deferred to v4.0+ milestone |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| BUILD-01 | Phase 2 | Pending |
| BUILD-02 | Phase 1 | Pending |
| BUILD-03 | Phase 1 | Complete |
| BUILD-04 | Phase 2.5 | Pending |
| HOST-01 | Phase 3 | Pending |
| HOST-02 | Phase 3 | Pending |
| HOST-03 | Phase 3 | Pending |
| VERIFY-01 | Phase 5 | Pending |
| VERIFY-02 | Phase 4 | Pending |
| VERIFY-03 | Phase 4 | Pending |
| VERIFY-04 | Phase 4 | Pending |
| VERIFY-05 | Phase 5 | Pending |

**Coverage:**
- v2.0 requirements: 12 total
- Mapped to phases: 12
- Unmapped: 0

---
*Requirements defined: 2026-03-30*
*Last updated: 2026-03-30 — traceability filled after v2.0 roadmap created*
