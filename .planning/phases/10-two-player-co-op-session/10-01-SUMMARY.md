---
phase: 10-two-player-co-op-session
plan: 01
subsystem: networking
tags: [enet6, ServerMessageRouter, character-assignment, message-routing, Catch2]

requires:
  - phase: 09-in-game-p2p-integration-test
    provides: CharacterService with ECS, LoopbackTransport, embedded GameServer
provides:
  - ServerMessageRouter handles 5 opcodes (auth, move, assign, spawn, remove)
  - JoinClient routes messages through ServerMessageRouter
  - JoinClient sends AssignCharacterRequest after auth acceptance
  - CharacterService sends movement via JoinClient (enet6) in join mode
  - Host mode router handles assign/spawn/remove for join client characters
affects: [10-02, 10-03, 10-04]

tech-stack:
  added: []
  patterns:
    - "ServerMessageRouter callback pattern extended with typed callbacks per opcode"
    - "JoinClient prepends prefix byte for enet6-to-router compatibility"

key-files:
  created:
    - Code/tests/router_test.cpp
  modified:
    - Code/native_client/transport/ServerMessageRouter.h
    - Code/native_client/transport/ServerMessageRouter.cpp
    - Code/native_client/main.cpp
    - Code/native_client/Services/CharacterService.h
    - Code/native_client/Services/CharacterService.cpp

key-decisions:
  - "JoinClient OnConsume prepends zero prefix byte because enet6 Client::OnConsume does not include the prefix that Server::Send adds, but ServerMessageRouter expects it"
  - "AssignCharacterResponse passes formId=0 since the response does not contain a FormId field (server assigns it server-side)"
  - "CharacterSpawnRequest uses FormId.BaseId as the formId parameter for CharacterService::OnAssignCharacter"

patterns-established:
  - "Router callback wiring: both host and join modes wire the same set of callbacks to CharacterService"
  - "JoinClient send pattern: serialize message with prefix byte, create PacketView, call Send()"

requirements-completed: [D-01, D-02, D-03, D-04, D-05, D-06, D-14]

duration: 17min
completed: 2026-03-29
---

# Phase 10 Plan 01: JoinClient Message Routing and Character Assignment Summary

**ServerMessageRouter extended with assign/spawn/remove handlers; JoinClient routes all messages through router and sends AssignCharacterRequest after auth**

## Performance

- **Duration:** 17 min
- **Started:** 2026-03-29T05:10:35Z
- **Completed:** 2026-03-29T05:28:10Z
- **Tasks:** 4
- **Files modified:** 5

## Accomplishments
- ServerMessageRouter handles 5 opcodes total (auth, move, assign, spawn, remove) with typed callbacks
- JoinClient::OnConsume routes all server messages through ServerMessageRouter instead of just logging
- Auth acceptance automatically triggers AssignCharacterRequest so server creates character entity
- CharacterService sends movement via JoinClient (enet6) in join mode, completing the bidirectional sync path
- 3 new Catch2 unit tests for router handlers pass (all 45 test cases, 1008 assertions pass)

## Task Commits

Each task was committed atomically:

1. **Task 0: Create Wave 0 test scaffold for ServerMessageRouter** - `9f0fb094` (test)
2. **Task 1: Extend ServerMessageRouter with character assignment handlers** - `f345ab05` (feat)
3. **Task 2: Wire JoinClient OnConsume to ServerMessageRouter and add SendAssignCharacterRequest** - `09c27efb` (feat)
4. **Task 3: Wire CharacterService callbacks and JoinClient send path** - `f95dc6a1` (feat)

## Files Created/Modified
- `Code/tests/router_test.cpp` - 3 Catch2 tests for assign, spawn, remove router handlers
- `Code/native_client/transport/ServerMessageRouter.h` - AssignCallback, SpawnCallback, RemoveCallback typedefs and setters
- `Code/native_client/transport/ServerMessageRouter.cpp` - Handler branches for kAssignCharacterResponse, kCharacterSpawnRequest, kNotifyRemoveCharacter
- `Code/native_client/main.cpp` - JoinClient routes through router, sends AssignCharacterRequest, callbacks wired for both modes
- `Code/native_client/Services/CharacterService.h` - SetJoinClient() and m_pJoinClient member
- `Code/native_client/Services/CharacterService.cpp` - JoinClient movement send path in SyncMovement

## Decisions Made
- JoinClient::OnConsume prepends a zero prefix byte because enet6 strips the prefix that the server adds, but ServerMessageRouter expects it
- AssignCharacterResponse handler passes formId=0 since the response struct lacks FormId (server assigns it)
- CharacterSpawnRequest uses FormId.BaseId for the formId callback parameter

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- xmake with git worktrees requires `-P .` config flag and `XMAKE_PROJECT_DIR` env var to pick up new source files; without these, the glob patterns resolve against the main project directory instead of the worktree

## User Setup Required
None - no external service configuration required.

## Known Stubs
None - all functionality is wired end-to-end.

## Next Phase Readiness
- JoinClient can now authenticate, request character assignment, and route all server messages to CharacterService
- Movement sync is bidirectional (host via loopback, join via enet6)
- Ready for Plan 02 (CMD_SET_POSITION execution) and Plan 03 (two-player session verification)

---
*Phase: 10-two-player-co-op-session*
*Completed: 2026-03-29*
