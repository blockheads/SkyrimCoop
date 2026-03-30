# Feature Research

**Domain:** Skyrim co-op mod — MinGW full client with host-authoritative P2P (Milestone v2.0)
**Researched:** 2026-03-30
**Confidence:** HIGH (direct codebase analysis, architecture docs, active phase context)

## Context: What This Research Answers

This milestone builds the full STR client under MinGW as a single SKSE DLL with host-authoritative P2P
architecture. The question is not "what features should a Skyrim co-op mod have" (that was answered in the
prior feature survey). The question is:

1. What are the **table stakes for a working two-player session** — the minimum to validate the architecture?
2. What STR services **work as-is** vs **need modification** for host-authoritative P2P?
3. What are **anti-features** to refuse before they enter scope?

MinGW compilation is a largely-solved build concern (Phases 1-3 complete). The remaining
challenge is the architecture: host-authoritative P2P requires removing the localhost loopback and
ensuring services behave correctly when the host IS the simulation.

---

## Feature Landscape

### Table Stakes (Users Expect These)

Features that must work for a two-player session to feel like co-op, not a demo. Missing any of
these and the session is not a co-op game — it is a tech proof.

| Feature | Why Expected | Complexity | STR Service | Notes |
|---------|--------------|------------|-------------|-------|
| See the other player's character | Co-op means you see your friend | HIGH | `CharacterService` (client+server) | Central to all sync. Actor spawn pipeline (Phases 15-17). |
| Character position and movement sync | Friend must move, not stand still | MEDIUM | `CharacterService` | Interpolation system exists. Works under loopback. Direct-authority path needs host bypass. |
| Correct appearance (race, face, armor) | T-posed grey blob is not a co-op session | HIGH | `CharacterService` + spawn data | TESNPC::Create from AppearanceBuffer — Phase 16-17 scope. |
| AI suppression on remote actors | Remote character must not wander off | MEDIUM | `CharacterService` (ActorExtension::SetRemote) | Required so peer's character stays where host put it. Phase 16. |
| Character removal on disconnect | Session cleanup when peer drops | LOW | `CharacterService` + `PlayerService` | NotifyRemoveCharacter + WorldStateSync reconciliation. Phase 16. |
| Actor values (health, magicka, stamina) | Combat without HP numbers is broken | MEDIUM | `ActorValueService` | Health change events, death state sync. Phase 18. |
| Direct IP connect (no Steam invite) | Two players must be able to connect | LOW | `TransportService` + `HostService` | HostService exists and starts GameServer. Direct IP is already the target. |
| Host embeds server in-process | Zero server setup | MEDIUM | `HostService` | Already implemented as localhost loopback. Direct-authority refactor is the target. |
| Inventory/equipment visible on spawn | See what friend is wearing | MEDIUM | `InventoryService` + spawn data | Equipment serialized in CharacterSpawnRequest. Works under current message model. |
| WorldStateSync reconciliation | State heals itself after missed events | MEDIUM | Server `CharacterService` | BroadcastWorldStateSync() exists. Client-side reconciliation needed. Phase 16. |

### Differentiators (Competitive Advantage)

Features that justify SkyrimCoop over Skyrim Together Reborn (STR). Each maps to a specific
architectural advantage of the host-authoritative model.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Host-authoritative ownership (no distributed NPC ownership) | STR's #1 desync cause: two clients fighting over who owns an NPC. Eliminated architecturally. | LOW (architectural, not feature work) | Direct consequence of single-simulation model. Host always owns NPCs. No handoff. |
| Quest sync that does not break | STR's biggest complaint. Distributed ownership causes quest desync when NPCs change owners. Host always runs quests — no conflict. | HIGH | `QuestService` header says "currently not in use." Needs resurrection with host-authoritative semantics. Phase post-18. |
| Weather/time follows host exactly | In STR, party leader controls weather but other clients can drift. Host-authoritative means one source of truth. | LOW | `WeatherService` + `CalendarService` already server-authoritative. Works as-is. |
| Combat resolution host-adjudicated | No ambiguity about who killed what. Host's Skyrim computes damage, result is authoritative. | MEDIUM | Host's `CombatService` already runs the real Havok physics. Clients trust outcomes. |
| No dedicated server setup | STR requires running a separate exe on a VPS or locally. SkyrimCoop: host clicks "host." | LOW (infrastructure) | HostService exists. The UX benefit is automatic once the architecture works. |
| Linux-first development toolchain | Attracts contributors who game on Linux/Steam Deck. Unique among Skyrim mods. | HIGH (toolchain, not feature) | Already proven through Phase 1-11. Maintained as development constraint. |

### Anti-Features (Commonly Requested, Often Problematic)

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| Host migration | "What if the host leaves?" | No production co-op game implements this for the host-as-simulation pattern. DRG's devs called it "too complex to transfer." The simulation state is inside Skyrim.exe — you cannot move it to another process mid-session. | Session ends on host disconnect. Document this clearly upfront. |
| Dedicated server support | "I want to run a persistent world" | Contradicts the single-simulation P2P model. Standalone server cannot run Skyrim's engine. STR already does this better. Adding it re-introduces distributed ownership bugs. | Use Skyrim Together Reborn for that use case. SkyrimCoop is co-op only. |
| More than 4 players | "More friends = more fun" | Skyrim's AI, pathfinding, and script systems degrade above 4 players. The co-op experience is 2-4, not MMO-scale. Each additional player multiplies sync traffic quadratically. | Hard cap at 4. Optimize for 2. |
| PvP combat | "We want to fight each other" | STR explicitly abandoned PvP. Attack validation across the network is a separate engineering effort with no co-op gameplay value. Adds anti-cheat surface. | Disable player-vs-player attacks entirely. |
| Direct authority (no loopback) as MVP requirement | "Loopback is wrong, fix it first" | Loopback is correct for MVP. All services work unchanged under loopback. Direct authority is an optimization (removes ~50% host-side traffic, eliminates StringCache redundancy). Blocking the first working session on this refactor is premature. | Ship loopback first. Refactor to direct authority after two players sync successfully. See WHY_DIRECT_AUTHORITY_IS_REQUIRED.md. |
| Steam invite / lobby system | "Users need easy discovery" | Not needed for the first working session. Direct IP connect (127.0.0.1 on localhost, LAN IP for same-network) is sufficient to prove the architecture. Steam lobby is a UX layer on top of working P2P, not a prerequisite. | Direct IP first. Steam invites after co-op session is stable. |
| CEF/Angular overlay UI | "Players need a UI to connect" | CEF does not compile under MinGW. Building a replacement UI before the co-op session works is wasted effort. Character sync is observable in logs. | Defer all UI until Phase 6. Use spdlog output for developer verification. ImGui if quick toggle is needed. |
| Lua scripting API for co-op sessions | "Hosts want custom game rules" | Server-side Lua was designed for dedicated-server environments. Co-op hosts don't need custom scripts. Adds maintenance surface. | Freeze the Lua API. Keep ScriptService compilable but non-exposed. |

---

## STR Services: Works As-Is vs Needs Modification

### Architecture Basis

The current HostService uses localhost loopback: host starts a GameServer, host's client connects to
127.0.0.1 like any remote client. This means all services work without modification — they cannot tell
the difference between local and remote.

The target is direct authority (see `WHY_DIRECT_AUTHORITY_IS_REQUIRED.md`): host's World is updated
directly, NetworkBridge broadcasts only to peers (no self-networking). This is the right target but
requires per-service code changes.

**Recommended sequencing:** Loopback first (all services work unchanged), validate two-player sync,
then refactor to direct authority service by service.

### Service Analysis

| Service | Loopback Works? | Direct Authority Changes Needed | Phase |
|---------|-----------------|--------------------------------|-------|
| `HostService` | Partial — starts server, but host also connects as a client | Add `IsHost()` flag to bypass client connection; broadcast only to peers | Phase 16+ |
| `TransportService` | YES — no changes needed | Add `IsHost()` mode to skip sending messages to localhost | Phase 16+ |
| `CharacterService` (client) | YES — sends/receives messages normally | Host path: update World directly, skip self-notification | Phase 16-17 |
| `CharacterService` (server) | YES — validates and broadcasts | Add host bypass: skip broadcasting back to host | Phase 16 |
| `InventoryService` | YES — event-driven, message-based | Minor: host path skips localhost roundtrip | Phase 18 |
| `ActorValueService` | YES — event-driven, message-based | Minor: host path skips localhost roundtrip | Phase 18 |
| `CombatService` | YES — event-driven, message-based | Minor: host path skips localhost roundtrip | Phase 18 |
| `MagicService` | YES — event-driven, message-based | Minor: host path skips localhost roundtrip | Post-18 |
| `QuestService` | PARTIAL — header says "currently not in use" | Full resurrection needed with host-authoritative semantics. Note: PROJECT.md lists "Quest sync (host-authoritative)" as active v2.0 milestone target, but this is correctly phased post-18 — actor spawn and state sync must be proven first. | Post-18 |
| `WeatherService` | YES — party leader (host) pushes weather | Host is always leader; no ownership ambiguity | Phase 18 |
| `PartyService` | YES — but designed for MMO party invites | Simplify: host is always leader, peers auto-join on connect | Post-18 |
| `StringCacheService` | YES under loopback — host sends string, server echoes back | Broken under direct authority: WHY_DIRECT_AUTHORITY_IS_REQUIRED.md shows triple-update bug | Refactor with direct authority |
| `CalendarService` | YES — server sends time, clients accept | No changes needed | Works as-is (verify in Phase 18) |
| `ObjectService` | YES — container sync is message-based | No changes needed (but container sync is deferred — no milestone requirement yet) | Deferred |
| `PapyrusService` | Unverified — Papyrus hooks are complex | Needs audit; likely deferred beyond Phase 18 | Deferred |
| `DiscordService` | NO — Discord SDK is Windows-only, MinGW excluded | Compiled out via `#ifndef HAS_DISCORD`. Skip entirely for Linux. | N/A (excluded) |
| `OverlayService` / CEF | NO — CEF cannot compile under MinGW | Compiled out via `#ifndef HAS_CEF`. Replace with ImGui in Phase 6. | Phase 6 |

### Services Blocked by Architecture Gaps

| Service | Blocker | Required Fix |
|---------|---------|--------------|
| `CharacterService` appearance sync | TESNPC::Create pipeline not yet ported to relay DLL | Phase 16-17: DLL-side TESNPC::Create from AppearanceBuffer |
| `QuestService` | Commented out ("currently not in use") | Phase post-18: audit quest hooks and enable with host-authoritative semantics |
| `ObjectService` (container sync) | Not in current milestone targets | Deferred — players expect shared loot but not blocked for initial two-player session |

---

## Feature Dependencies

```
Direct IP connect
    └──requires──> HostService (start embedded GameServer) ✓
    └──requires──> TransportService connect() ✓

Two players see each other
    └──requires──> CharacterService AssignCharacter flow ✓
    └──requires──> CharacterSpawnRequest + DLL CMD_SPAWN_ACTOR (Phase 15-16)
    └──requires──> TESNPC::Create from AppearanceBuffer (Phase 16-17)
    └──requires──> AI suppression SetRemote(true) (Phase 16)

Combat feels right
    └──requires──> ActorValueService health sync (Phase 18)
    └──requires──> Death/respawn state sync (Phase 18)
    └──requires──> CombatService projectile sync ✓ (works under loopback)

Inventory visible on spawn
    └──requires──> Two players see each other (above)
    └──requires──> CharacterSpawnRequest equipment payload (Phase 18)

Quest sync reliability
    └──requires──> QuestService resurrection (post-18)
    └──requires──> Host-authoritative ownership (architectural — Phase 16+ refactor)

Weather/time sync ✓ (works as-is under current model)

Party system (host is always leader)
    └──requires──> PartyService simplification (post-18)
    └──requires──> WeatherService uses host-as-leader (works as-is)

Direct authority (no loopback)
    └──requires──> Two players sync working (validate first)
    └──requires──> StringCacheService refactor
    └──requires──> CharacterService host-bypass path
    └──requires──> TransportService IsHost() routing
```

### Dependency Notes

- **TESNPC::Create requires AppearanceBuffer serialization:** The server must have serialized the
  remote player's appearance at connect time. This happens through AssignCharacterRequest which
  carries ActorData. The full appearance reconstruction pipeline is Phase 16-17.

- **AI suppression requires WaitingFor3D:** Setting remote flag on an actor before 3D loads causes
  issues. STR's WaitingFor3D pattern defers state application until mesh is loaded. This is part of
  Phase 16.

- **Direct authority conflicts with loopback:** Cannot run both simultaneously. Services must know
  their mode. The `IsHost()` flag on TransportService is the switch. Loopback is the MVP path;
  direct authority is the refactor target.

- **ObjectService (containers) is a table-stakes gap:** Players expect to share loot from the same
  chest. ObjectService exists on both client and server but is not in current milestone requirements.
  First working session does not require it, but it is needed before "real" co-op play.

---

## MVP Definition

### Launch With (Two-Player Session Proof)

Minimum to validate: "two players can co-op in Skyrim via the MinGW DLL."

- [ ] Direct IP connect (host starts, client connects to host IP) — HostService + TransportService
- [ ] Remote player spawns at correct position with AI suppressed — CharacterService + TESNPC::Create
- [ ] Remote player's movement syncs in real-time (interpolation) — ServerReferencesMoveRequest
- [ ] Remote player appears with correct appearance (race, face, basic equipment) — AppearanceBuffer
- [ ] Disconnect removes remote player cleanly — NotifyRemoveCharacter + reconciliation
- [ ] WorldStateSync 1-second reconciliation keeps state consistent — BroadcastWorldStateSync

This matches Phase 16 success criteria exactly.

### Add After Validation (v1.x)

- [ ] Actor values (health, magicka, stamina) on spawn and update — ActorValueService wired (Phase 18)
- [ ] Equipment visible (weapons, armor) during play — InventoryService spawn data (Phase 18)
- [ ] Death and respawn sync — ActorValueService death state (Phase 18)
- [ ] Weather and time sync — CalendarService + WeatherService enabled (Phase 18)
- [ ] Combat sync (projectiles, hit events) — CombatService wired (Phase 18)
- [ ] Direct authority refactor (remove localhost loopback) — post-Phase 18

### Future Consideration (v2+)

- [ ] Quest sync resurrection — QuestService, requires host-authoritative semantics
- [ ] Non-leader NPC interaction (merchants, trainers) — NPC classification system
- [ ] ImGui connect/party overlay — Phase 6 (already in roadmap)
- [ ] Steam invite / lobby discovery — requires Steam SDK integration
- [ ] Desync detection with state hash comparison — requires state serialization
- [ ] Container/loot sync (ObjectService) — table stakes gap not yet scheduled
- [ ] Reconnection with state resync — session management improvement

---

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| Remote player spawns (correct appearance) | HIGH | HIGH | P1 |
| Movement sync | HIGH | LOW (exists) | P1 |
| AI suppression on remote actors | HIGH | MEDIUM | P1 |
| Disconnect cleanup (WorldStateSync) | HIGH | MEDIUM | P1 |
| Health/actor values sync | HIGH | MEDIUM | P2 |
| Equipment visible on spawn | HIGH | MEDIUM | P2 |
| Death/respawn sync | HIGH | MEDIUM | P2 |
| Combat (projectiles, hits) | MEDIUM | LOW (exists) | P2 |
| Weather/time sync | LOW | LOW (exists) | P2 |
| Direct authority (no loopback) | LOW (invisible to user) | HIGH | P3 |
| Quest sync | HIGH | HIGH | P3 |
| Steam invites | HIGH | HIGH | P3 |
| ImGui overlay | MEDIUM | MEDIUM | P3 |
| Container/loot sync | HIGH | MEDIUM | P3 |

---

## Competitor Feature Analysis (STR vs SkyrimCoop)

| Feature | Skyrim Together Reborn | SkyrimCoop v2.0 |
|---------|------------------------|-----------------|
| Architecture | Dedicated server + multiple clients | Host-embedded server, direct P2P |
| Quest sync | Leader-only NPC interaction, frequent desync | Host-authoritative (target), not yet implemented |
| NPC ownership | Distributed (handoffs cause desync) | Host owns all — no handoffs |
| Combat resolution | Distributed (inconsistent results) | Host adjudicates — authoritative |
| Server setup | Separate exe required | Zero setup — embedded in DLL |
| Linux dev toolchain | Windows-only builds | Linux-native MinGW cross-compile |
| Player cap | 8+ players | 4 players (co-op focus) |
| Overlay UI | Angular/CEF (complex) | ImGui (deferred, Phase 6) |
| Quest service status | Active but fragile | Commented out ("not in use") — needs resurrection |
| Weather/time | Party leader controls | Host controls (same effect, simpler semantics) |

---

## Sources

- Direct codebase analysis: `Code/client/Services/`, `Code/server/Services/` — HIGH confidence
- `.planning/PROJECT.md` — HIGH confidence, canonical milestone scope
- `.planning/NETWORK-ARCHITECTURE.md` — HIGH confidence, canonical architecture
- `P2P_ARCHITECTURE.md` — HIGH confidence, direct authority design
- `LISTEN_SERVER_ARCHITECTURE.md` — HIGH confidence, embedded server design
- `WHY_DIRECT_AUTHORITY_IS_REQUIRED.md` — HIGH confidence, architectural rationale
- `.planning/ROADMAP.md` — HIGH confidence, phase dependencies
- `Code/client/Services/QuestService.h` line 14: "This service is currently not in use" — HIGH confidence
- `Code/client/Services/MagicService.h`: comment warns magic sync is "easily the ugliest part of the codebase" — HIGH confidence
- [Skyrim Together Reborn wiki](https://wiki.tiltedphoques.com/tilted-online/) — MEDIUM confidence
- [DRG host migration quote](https://store.steampowered.com/app/548430/Deep_Rock_Galactic/) — LOW confidence (community attribution, not official dev statement)

---
*Feature research for: Skyrim co-op mod — MinGW full client, host-authoritative P2P*
*Researched: 2026-03-30*
