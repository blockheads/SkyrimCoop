# Phase 16: Fix Server FormId & Basic Actor Spawning - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-29
**Phase:** 16-fix-server-formid-and-basic-actor-spawning
**Areas discussed:** FormId fix strategy, What formId to spawn, Test strategy, Human E2E verification

---

## FormId Fix Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Use client's FormId field | AssignCharacterRequest has FormId field with TESNPC base form. Emplace FormIdComponent using this when isCustom is true. | |
| Hardcode a known NPC formId | Use vanilla NPC formId (e.g. 0x1A6A4) for Phase 16 only. | |
| You decide | Claude picks based on codebase analysis. | |

**User's choice:** Asked "what does TSR do?" — led to codebase investigation.

**Investigation result:** STR intentionally sends FormId 0:0 for player characters. Players use TESNPC::Create from AppearanceBuffer on the receiving client, not formId lookup. The server code is working as designed.

**Follow-up question:** How to approach this given the relay DLL needs spawn data?

| Option | Description | Selected |
|--------|-------------|----------|
| Hardcode placeholder NPC formId | Send vanilla NPC formId in cmd_spawn_actor. Phase 17 replaces with TESNPC::Create. | |
| Port TESNPC::Create to relay DLL now | Full custom form pipeline in relay DLL. | ✓ |
| Use player's base race formId | Extract race from AppearanceBuffer, spawn generic NPC of correct race. | |

**User's choice:** Port TESNPC::Create to relay DLL now

**Scope impact question:** This pulls in Phase 17 scope.

| Option | Description | Selected |
|--------|-------------|----------|
| Absorb Phase 17 into Phase 16 | Merge both phases. Bigger but fewer handoffs. | ✓ |
| Keep phases separate, use placeholder | Phase 16 = placeholder, Phase 17 = TESNPC::Create. | |
| Split differently | Phase 16 = TESNPC::Create minimal, Phase 17 = full appearance. | |

**User's choice:** Absorb Phase 17 into Phase 16

---

## What FormId to Spawn

| Option | Description | Selected |
|--------|-------------|----------|
| Forward raw AppearanceBuffer over TCP | Single cmd_spawn_actor payload with full appearance data. DLL deserializes on game thread. | ✓ |
| Send formId only, appearance later | Separate cmd_set_appearance follows spawn. Adds flash of incorrect NPC. | |
| You decide | Claude picks based on STR patterns. | |

**User's choice:** Forward raw AppearanceBuffer over TCP

| Option | Description | Selected |
|--------|-------------|----------|
| Both spawn paths | Custom TESNPC::Create for players AND formId lookup for world NPCs. | ✓ |
| Player character path only | Only TESNPC::Create. World NPC spawning deferred. | |
| You decide | Claude picks based on success criteria. | |

**User's choice:** Both paths (Recommended)

---

## Test Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Test native-side routing logic | Unit test CharacterService routing: FormId==0:0 → custom spawn, non-zero → formId spawn. Test spawn_result mapping. | ✓ |
| Test server-side serialization too | Also verify Serialize() populates CharacterSpawnRequest correctly. | |
| Replay tests only | Skip unit tests, rely on NDJSON replay fixtures. | |

**User's choice:** Test native-side routing logic (Recommended)

| Option | Description | Selected |
|--------|-------------|----------|
| New dedicated spawn fixtures | Create new NDJSON fixtures for spawn pipeline scenarios. | ✓ |
| Extend existing fixtures | Add spawn events to existing two-player fixtures. | |
| You decide | Claude picks based on test patterns. | |

**User's choice:** New dedicated spawn fixtures (Recommended)

---

## Human E2E Verification

| Option | Description | Selected |
|--------|-------------|----------|
| Visible actor at correct position | Remote actor spawns at correct position with correct race. AI suppressed. No equipment/animation needed. | ✓ |
| Visible actor + basic movement | Above plus position updates when other player moves. | |
| Just visible, any position | Minimal bar, any actor anywhere. | |

**User's choice:** Visible actor at correct position (Recommended)

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, implement AI suppression | Port SetRemote(true) to DLL spawn path. | ✓ |
| Defer AI suppression | Spawn without AI suppression. Actor wanders off. | |
| You decide | Claude evaluates criticality. | |

**User's choice:** Yes, implement AI suppression (Recommended)

---

## Claude's Discretion

- Exact address library IDs for all game functions
- AppearanceBuffer serialization over TCP protocol
- FaceGenSystem::Setup port scope
- WaitingFor3D pattern implementation
- Error handling for spawn failures
- Which STR setup calls are necessary vs nice-to-have

## Deferred Ideas

- Equipment sync — Phase 18
- Animation sync — Phase 18
- Actor value sync — Phase 18
- Death/respawn sync — Phase 18
- SKSE task interface port — future optimization
- Fade-out on removal — polish
