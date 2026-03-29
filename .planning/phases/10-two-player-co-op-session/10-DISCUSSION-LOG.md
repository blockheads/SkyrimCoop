# Phase 10: Two-Player Co-op Session - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-28
**Phase:** 10-two-player-co-op-session
**Areas discussed:** JoinClient routing, Two-instance setup, Success validation, DLL SET_POSITION

---

## JoinClient Routing

| Option | Description | Selected |
|--------|-------------|----------|
| Reuse ServerMessageRouter (Recommended) | Wire JoinClient::OnConsume into the existing ServerMessageRouter. Same code path as host loopback, already handles auth + move messages. | ✓ |
| Separate JoinMessageRouter | New router class specific to join mode. Allows different handling logic for remote clients vs loopback host. | |
| You decide | Claude picks the approach based on code fit | |

**User's choice:** Reuse ServerMessageRouter
**Notes:** None

| Option | Description | Selected |
|--------|-------------|----------|
| Direct enet send (Recommended) | JoinClient serializes ClientReferencesMoveRequest and sends via enet6 to host. Host's GameServer receives it like any other client packet. | ✓ |
| Transport abstraction | Create an ITransport interface so CharacterService doesn't care whether it's loopback or enet. More refactoring but cleaner long-term. | |
| You decide | Claude picks based on code fit | |

**User's choice:** Direct enet send
**Notes:** None

---

## Two-Instance Setup

| Option | Description | Selected |
|--------|-------------|----------|
| Two Proton prefixes (Recommended) | Each instance gets its own STEAM_COMPAT_DATA_PATH. Completely separate Wine environments, no file conflicts. | ✓ |
| Two MO2 profiles, one prefix | Single Proton prefix but separate MO2 virtual filesystems. Lighter weight but risks file locks on shared Skyrim data files. | |
| Two full Skyrim installs | Copy the entire Skyrim folder. Most reliable but wastes ~13GB disk space. | |

**User's choice:** Two Proton prefixes
**Notes:** None

| Option | Description | Selected |
|--------|-------------|----------|
| CLI port override (Recommended) | Pass different --port to each native_client instance. Host uses 9667, client uses 9668. Relay DLL already configurable. | ✓ |
| Auto-detect free port | Relay DLL scans for available ports and writes chosen port to a file. Native process reads it. More complex but zero-config. | |
| You decide | Claude picks based on relay DLL code | |

**User's choice:** CLI port override
**Notes:** None

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, launch script (Recommended) | Shell script that launches host Skyrim + native_client host mode, then client Skyrim + native_client join mode. Documents the whole flow. | ✓ |
| Manual launch | Document the commands in a README but no automation. Keeps phase scope minimal. | |
| You decide | Claude picks based on phase scope | |

**User's choice:** Launch script
**Notes:** None

---

## Success Validation

| Option | Description | Selected |
|--------|-------------|----------|
| Visual movement sync (Recommended) | Both players see each other's character model moving in real-time in Riverwood. Walk around, see the other player follow. Screenshot/video proof. | ✓ |
| Log-based proof only | Confirm via spdlog that position updates flow host→server→client and back. No visual verification required — just packet flow. | |
| Static presence only | See the other player's character standing still at a position. Movement sync is a bonus, not required. | |

**User's choice:** Visual movement sync
**Notes:** None

| Option | Description | Selected |
|--------|-------------|----------|
| Screenshot + log excerpt | Take screenshots of both Skyrim windows showing both characters. Include key spdlog lines showing packet flow. | |
| Video recording | Screen-record both instances side by side. More compelling proof but harder to capture. | |
| Logs only | Just spdlog output confirming bidirectional position sync. Fastest to capture. | |

**User's choice:** Other — "I can just verify this manually"
**Notes:** Manual visual verification, no formal documentation artifacts needed

---

## DLL SET_POSITION

| Option | Description | Selected |
|--------|-------------|----------|
| Implement in Phase 10 (Recommended) | Wire DrainCommandQueue into the game thread, implement CMD_SET_POSITION to call RealSetPosition. Minimal — just one command. | ✓ |
| Implement all commands | While wiring the command queue, implement all defined CMD_* opcodes. More work but gets the full pipeline working. | |
| Skip DLL writes, log only | Phase 10 proves the network path works but doesn't visually move actors. DLL command execution becomes Phase 11. | |

**User's choice:** Full pipeline in Phase 10 (after understanding full gap analysis)
**Notes:** User asked about character assignment pipeline — confirmed it's also missing (ServerMessageRouter doesn't handle AssignCharacter). Agreed to implement full pipeline in one phase.

| Option | Description | Selected |
|--------|-------------|----------|
| HookActorProcess (Recommended) | Drain once per frame from the first HookActorProcess call. Already hooked, guaranteed game thread. | |
| Separate game loop hook | Hook a different Skyrim function that fires exactly once per frame. Cleaner but requires new address. | |
| You decide | Claude picks based on DLL code | |

**User's choice:** Other — research CommonLibSSE TESObjectREFR.cpp to find the right function
**Notes:** User wants to find the proper once-per-frame tick function from CommonLibSSE rather than piggybacking on HookActorProcess. This becomes a research item for the planner.

---

## Claude's Discretion

- Character model selection for remote player
- Network tick rate and interpolation smoothing
- Specific CommonLibSSE function for game thread tick (research required)

## Deferred Ideas

- Network stress testing (packet drop simulation, latency injection)
- Steam invite system (replace direct IP)
- Combat/inventory/quest sync
- UI overlay (connection UI, player list, health bars)
- Full CMD_* implementation (only CMD_SET_POSITION for now)
- Transport abstraction (ITransport interface — do direct enet send for now)
