# Codebase Concerns

**Analysis Date:** 2026-03-27

## Architecture Debt: Redundant Client-Server in P2P Model

**Issue:** Host player runs embedded server and connects to it via localhost networking, creating duplicate state and unnecessary serialization overhead.

**Files:**
- `Code/client/main.cpp` - Entry point, initializes both client and embedded server
- `Code/client/Services/HostService.cpp` - Embeds GameServer, host still uses TransportService to connect
- `Code/client/Services/Generic/TransportService.cpp` - Host connects to localhost:10578
- `Code/server/GameServer.cpp` - Full game server running inside client process
- `Code/server/World.h/cpp` - Separate authoritative World maintained by server
- `Code/client/World.h` - Separate client-side World with duplicate state

**Impact:**
- Host experiences higher latency (localhost loopback vs direct state access)
- CPU overhead from redundant serialization/deserialization for host
- Memory bloat from maintaining two World instances simultaneously
- Complex message handling across process boundaries within same executable
- StringCache and state synchronization requires bidirectional messaging even on same machine
- All services duplicated (client version + server version) with different logic paths

**Fix approach:**
Full P2P architecture refactor documented in `P2P_REFACTOR_PLAN.md`:
1. Merge Server::World into single World with `m_isHost` flag
2. Replace GameServer with lightweight NetworkBridge that references World (not owns it)
3. Host does NOT connect to TransportService; instead broadcasts via NetworkBridge
4. Refactor all services (CharacterService, InventoryService, QuestService, etc.) for dual host/peer modes
5. Remove Server namespace classes and consolidate into client Services

**Priority:** Critical - This is the foundational architectural issue blocking performance optimization.

---

## Incomplete P2P Conversion

**Issue:** The codebase is mid-transition to P2P but still relies on old client-server patterns. HostService exists but services haven't been refactored for dual host/peer modes.

**Files:**
- `Code/client/Services/HostService.cpp` (lines 48-49) - Creates embedded GameServer, tries to work with host's World
- All service files in `Code/client/Services/Generic/` - Still assume peer-only behavior
- `Code/server/Services/*` - Full server-side logic that should be consolidated into client services

**Impact:**
- Host runs unnecessary server code path alongside client code path
- Services don't check `World::IsHost()` to branch behavior (not implemented yet)
- Message handlers only exist for client-receive, not host-broadcast
- Can't fully disable networking overhead for host without major refactoring
- Testing complicated by dual code paths

**Fix approach:**
- Implement Phase 1-2 of P2P_REFACTOR_PLAN: Add `World::m_isHost` flag and create NetworkBridge stub
- Begin Phase 3 refactoring services one by one to handle both modes
- Add test coverage for host vs peer modes
- Feature flag approach to run both old/new code during transition

**Priority:** High - Blocks cleanup and optimization work.

---

## Message Handler Complexity: 40+ Unregistered Handlers

**Issue:** 20+ services register event handlers in constructors, creating tight coupling and difficult-to-trace message flow.

**Files:**
- `Code/client/Services/Generic/CharacterService.cpp` (lines 71-101) - Registers 30+ event handlers in constructor
- `Code/client/Services/Generic/InventoryService.cpp` - Multiple handlers
- `Code/client/Services/Generic/TransportService.cpp` (lines 45-58) - Factory-based handler registration
- All service constructors - Implicit registration via entt::dispatcher sinks

**Impact:**
- Hard to understand which service handles which event
- No centralized message routing registry
- Adding new message types requires modifying multiple service constructors
- Debugging message flow requires grepping for handler connections
- Tests must instantiate entire service to test single handler

**Fix approach:**
- Create central MessageHandlerRegistry that services register into
- Build dependency injection to pass registry to services
- Document message flow in architecture diagram
- Extract handler logic to static functions where possible for easier testing
- Add trace logging for all message dispatches in debug mode

**Priority:** Medium - Impacts code maintainability and debugging velocity.

---

## Nine Critical TODOs in Core Systems

**Issue:** Numerous TODO/FIXME comments in critical hot paths indicate incomplete or workaround implementations.

**Files:**
- `Code/client/Services/Generic/CharacterService.cpp` (lines 141, 307, 741, 953, 1072, 1530) - Movement sync, entity iteration, actor ownership issues
- `Code/client/Services/Generic/ActorValueService.cpp` (line 71) - Crashes sometimes, no investigation documented
- `Code/client/Services/Generic/TransportService.cpp` (line 125) - No user opt-out for Discord ID collection
- `Code/server/Services/ObjectService.cpp` (line 29) - Cell handling needs overhaul
- `Code/server/GameServer.h` (line 32) - "eventually refactor this" - vague commitment

**Impact:**
- ActorValueService crashes silently with no clear fix
- Entity iteration inefficiencies in CharacterService during actor movement
- Incomplete discord privacy feature
- Object service cell management is fragile
- Unknown scope of GameServer refactoring needs

**Fix approach:**
- Review each TODO with original author to clarify scope
- Convert vague comments to specific GitHub issues with reproduction steps
- Prioritize ActorValueService crash - add instrumentation and bounds checking
- Create tracking issue for CharacterService entity iteration optimization
- Document Discord opt-out requirement for privacy compliance

**Priority:** High - Some are crash-prone or privacy-related.

---

## Animation System Fragility

**Issue:** Custom behavior modification (BehaviorVar.cpp) has unhandled cases for animation variable duplication and complex animation synchronization logic.

**Files:**
- `Code/client/ModCompat/BehaviorVar.cpp` (line 307) - "doesn't handle case of acNewHash already existing"
- `Code/client/Services/CharacterService.h` (line 115) - "TODO: revamp this, read the local anim var like vampire lord?"
- `Code/client/Games/Animation.cpp` (line 20) - "TODO: make scoped override"
- `Code/client/Services/Generic/CharacterService.cpp` (line 141) - Animation initialization issues

**Impact:**
- Animation variable collisions can cause undefined behavior
- Vampire lord and werewolf form animations may not sync properly
- Complex animation systems with mods may have missing sync for edge cases
- Scoped animation overrides not properly implemented
- README-ANIMATION-MODS.md documents workarounds for unsupported mods

**Fix approach:**
- Add hash collision detection and resolution in BehaviorVar.cpp
- Create animation state machine test suite with Nemesis and Pandora patches
- Document all supported animation mod combinations in compatibility matrix
- Implement scoped animation override context manager
- Test with popular animation mods (1ED, Brute Force UH, etc.)

**Priority:** Medium - Affects users with custom animation mods.

---

## String Cache Synchronization Overhead (Pre-P2P Refactor)

**Issue:** StringCacheService maintains string-to-ID mapping with complex bidirectional synchronization required between host and embedded server.

**Files:**
- `Code/common/StringCache.h/cpp` - Dual-mode cache logic
- Message handlers in CharacterService and other services - StringCache updates
- `P2P_REFACTOR_PLAN.md` (lines 273-325) - Describes current inefficiency

**Impact:**
- Host experiences double serialization: strings cached locally, then sent to embedded server, then broadcast to peers
- Peer logic for "wanted" strings adds complexity
- Embedded server needs to detect dirty cache entries and broadcast
- Migration required when P2P refactor complete

**Fix approach:**
- Current state is acceptable until P2P refactor (Phase 2)
- During Phase 2, consolidate to single StringCache owned by World
- Host directly adds strings with IDs, broadcasts deltas to peers
- Peers receive full cache snapshots periodically
- See P2P_REFACTOR_PLAN.md lines 273-325 for detailed strategy

**Priority:** Low - Deferred until P2P architecture refactor.

---

## Networking Resilience Gaps

**Issue:** Error handling for network failures is minimal; dropped packets and disconnects may cause silent desynchronization.

**Files:**
- `Code/client/Services/Generic/TransportService.cpp` (lines 96-110) - Malformed packet silently logged and ignored
- `Code/client/Services/Generic/TransportService.cpp` (line 105) - "Couldn't parse packet from server" just returns
- No timeout handling visible for stalled connections
- No reconnect backoff strategy observed

**Impact:**
- Malformed packets silently drop without retry or user notification
- Peer may be unaware of desync after packet loss
- No exponential backoff for reconnection attempts
- Disconnects don't trigger state validation/resync

**Fix approach:**
- Implement packet loss detection and retransmission for critical messages
- Add connection heartbeat with timeout detection
- Implement exponential backoff (1s → 2s → 4s → 8s max) for reconnection
- Queue failed sends and retry with user notification
- Add "resync state" command for manual recovery from desync
- Log and report network error events to Sentry with context

**Priority:** Medium - Impacts reliability in poor network conditions.

---

## Large Complex Files Obscuring Intent

**Issue:** Several service implementation files exceed 1,000+ lines with multiple responsibilities intermingled.

**Files:**
- `Code/client/Services/Generic/CharacterService.cpp` - 1,605 lines
- `Code/client/Games/Skyrim/RTTI.cpp` - 5,629 lines (reverse engineering data)
- `Code/client/Games/Skyrim/Actor.cpp` - 1,335 lines
- `Code/client/Games/Skyrim/TESObjectREFR.cpp` - 1,131 lines

**Impact:**
- Difficult to locate specific functionality
- Higher cognitive load when understanding service behavior
- Increased chance of missing edge cases
- Makes refactoring riskier
- Test coverage harder to reason about

**Fix approach:**
- Split CharacterService.cpp into focused modules:
  - CharacterMovementService (movement sync, interpolation)
  - CharacterAnimationService (animation handling, BehaviorVar)
  - CharacterAttributeService (stats, health, respawn)
  - CharacterStateService (mounted, beast form, etc.)
- Each module ~300-400 LOC, single responsibility
- Use dependency injection to maintain service relationships
- Update P2P_REFACTOR_PLAN to include this refactoring as Phase 3.5

**Priority:** Medium - Quality-of-life improvement, deferred during active P2P work.

---

## Skyrim RTTI Data Fragility

**Issue:** Reverse-engineered Skyrim type information (RTTI.cpp) is brittle to game updates and not well-documented.

**Files:**
- `Code/client/Games/Skyrim/RTTI.cpp` - 5,629 lines of hardcoded type definitions
- `Code/client/Games/Skyrim/RTTI.h` - Auto-generated type structs
- Depends on Skyrim Address Library for function pointers

**Impact:**
- Game updates require Address Library refresh and RTTI regeneration
- Misaligned memory structures cause hard crashes
- No runtime validation of memory layout
- Limited documentation of which types are mission-critical

**Fix approach:**
- Add runtime validation checks: walk object vtables, check field offsets
- Document required RTTI types (Player, Actor, Item, Cell) vs optional
- Create fallback implementations for types that may change
- Add assertions if Address Library is missing or outdated
- Monitor for Skyrim patches and trigger RTTI regeneration
- Consider runtime introspection library (metatype) if feasible

**Priority:** Low - Address Library maintainers actively support this.

---

## AMD GPU Support Explicitly Disabled

**Issue:** D3D11 support detection explicitly ignores AMD GPUs.

**Files:**
- `Code/immersive_launcher/oobe/SupportChecks.cpp` (line 21) - "FIXME(Force): Ignore amd for now"

**Impact:**
- AMD GPU users get unsupported hardware warning
- No error reporting on why AMD is excluded
- Implies rendering issues or crash on AMD hardware
- Users with AMD GPUs blocked from playing

**Fix approach:**
- Investigate original AMD failure mode (driver issue? rendering bug?)
- Test current Skyrim + AMD driver combination
- Re-enable AMD support if issues resolved
- Document hardware requirements and known limitations if AMD remains unsupported
- Add specific error message explaining AMD exclusion reason

**Priority:** Low-Medium - Blocks AMD users, but likely few affected.

---

## Missing Export Validation

**Issue:** Lua scripting bindings and admin interface code paths exist but may not be fully tested for P2P environment.

**Files:**
- `Code/server/Scripting/` - Lua bindings (Sol2)
- `Code/server/Services/AdminService.cpp` - Admin panel HTTP server
- `Code/admin/` - Admin panel UI

**Impact:**
- Lua scripting designed for dedicated server, behavior untested in P2P host context
- Admin console commands may fail or behave unexpectedly when host is also playing
- AdminService state management unclear for embedded server scenario
- ServerConsole commands may not have UI for P2P hosting

**Fix approach:**
- Define Lua API surface for P2P hosts (what scripts can do)
- Test admin commands with host player active
- Document which server features work in P2P mode vs dedicated only
- Implement P2P-specific admin UI (in-game overlay, not web panel?)
- See P2P_REFACTOR_PLAN.md questions section (lines 457-471) for detailed discussion

**Priority:** Low - Not blocking initial P2P launch, can be refined post-launch.

---

## Test Coverage Gaps

**Issue:** Automated test coverage is minimal; only unit tests for encoding/serialization exist.

**Files:**
- `Code/tests/` - Contains only Catch2 tests for encoding and network quantization
- `Code/tests/main.cpp` - Test runner (minimal)
- No integration tests for multiplayer scenarios
- No E2E tests for host + peer interactions

**Impact:**
- No automated validation that P2P refactoring doesn't break existing services
- Difficult to prevent regressions during 6-8 week refactoring
- Manual testing required for each service change
- No CI/CD pipeline visible for test execution

**Fix approach:**
- Create integration test framework simulating host + N peers
- Tests for:
  - Character spawning and movement sync
  - Inventory changes across peers
  - Quest progression sync
  - Combat damage/health updates
  - Cell load/unload handling
  - Disconnection and reconnection scenarios
- Add to xmake build system: `xmake build TPIntegrationTests`
- Run tests in CI on every push to `dev` branch
- Target 70%+ code coverage for client Services

**Priority:** High - Essential for P2P refactoring confidence.

---

## MO2 USVFS Compatibility Incomplete

**Issue:** File redirection under Mod Organizer 2 has known gaps and unresolved interactions.

**Files:**
- `Code/immersive_launcher/stubs/FileMapping.cpp` (lines 246-248) - "Further analysis of what is under MO2 USVFS and what is needed"
- `Code/immersive_launcher/stubs/FileMapping.cpp` (lines 321-323) - "we need some check if usvfs already fucked with this?"
- Commented-out LdrGetDllFullName hooking

**Impact:**
- MO2 users with heavily modded setups may experience file resolution issues
- Launcher may crash or hang when USVFS virtualizes certain files
- Unclear which mod operations trigger which code paths
- Some file mapping stubs potentially deactivated to work around crashes

**Fix approach:**
- Establish MO2 test environment with 100+ mods
- Document current USVFS interaction patterns
- Enable/test commented-out LdrGetDllFullName hooking
- Add logging to FileMapping operations for debugging
- Create list of confirmed incompatible MO2 mod combinations
- Consider adding MO2 version detection and conditional behavior

**Priority:** Medium - Affects MO2 users (large subset of player base).

---

## CEF (Chromium) Integration Complexity

**Issue:** UI rendering via Chromium Embedded Framework adds significant complexity and potential stability issues.

**Files:**
- `Code/tp_process/main.cpp` - CEF worker process
- `Code/libraries/ui/` - UI rendering and input handling (D3D11 integration)
- `Code/libraries/ui_process/` - IPC bridge to CEF subprocess
- IPC messaging between C++ and V8 JavaScript

**Impact:**
- CEF process crashes may hang game or leave orphaned processes
- D3D11 rendering integration complex (texture sharing, format conversion)
- Input handling conflicts between game and CEF overlay
- Memory overhead from embedded Chromium
- Security surface from JavaScript → C++ IPC

**Fix approach:**
- Add CEF crash handler with auto-restart logic
- Implement process watchdog to clean up orphaned processes
- Separate input routing (game input vs UI input) more clearly
- Profile CEF memory usage, consider LazyInstall for minimal builds
- Audit JavaScript → C++ IPC message types for injection vulnerabilities
- Document known CEF versions and crashes with workarounds

**Priority:** Medium - Stability concern, but widely used feature.

---

## Game State Validation Absent

**Issue:** No runtime validation that game state (player position, inventory, etc.) remains consistent with network messages.

**Files:**
- `Code/client/Services/Generic/CharacterService.cpp` - Applies received positions without validation
- `Code/client/Services/Generic/InventoryService.cpp` - Adds items without ownership check
- `Code/client/Services/Generic/ActorValueService.cpp` - Sets health without bounds checking

**Impact:**
- Peer can cause peer positions to jump to invalid locations (OOB in dungeons)
- Inventory can be corrupted if message ordering isn't guaranteed
- Health values set above 100% or below 0% causing undefined behavior
- No detection of malicious or corrupted packets from peers
- Silent desync difficult to diagnose

**Fix approach:**
- Add validation layer in all service message handlers:
  - CharacterService: Validate positions within loaded cells, interpolate if jump > threshold
  - InventoryService: Verify item owner, max stack sizes
  - ActorValueService: Clamp health/magicka/stamina to valid ranges
- Log validation failures with context (packet source, expected vs actual)
- Add state hash checksums to detect desync (send world hash periodically)
- Implement "rollback" for peer if state diverges too far (re-send from host)
- Add telemetry to Sentry for desync events

**Priority:** High - Affects data integrity and user experience.

---

## Dependency Version Pinning Fragile

**Issue:** Many dependencies pinned to specific versions without documented compatibility ranges; updating risky.

**Files:**
- `xmake.lua` (lines 43-73) - Direct version pins for EnTT, spdlog, Catch2, etc.
- `Code/external/` - Bundled ImGui, DirectXTK, cpp-httplib

**Impact:**
- Can't easily update to security patches
- New developer must use pinned versions (may be old)
- Unclear if higher versions break compatibility
- xmake package ecosystem fragmentation
- Wine MSVC has special debug symbol handling (line 32) - may break with xmake upgrades

**Fix approach:**
- Document compatibility matrix for each major dependency
- Test against next minor/major version for each dependency before pinning
- Use version ranges where possible: `"spdlog ^1.13.0"` instead of `"spdlog v1.13.0"`
- Create update strategy: quarterly dependency security review
- Maintain separate build variants (minimal, full) for dependency subset testing

**Priority:** Low - Current approach works, cleanup can happen post-launch.

---

## Platform-Specific Code Duplication

**Issue:** Windows-only client plugin with Linux server creates divergent code paths for shared logic.

**Files:**
- `Code/client/Games/Skyrim/` - Windows-only RTTI, hooks, SKSE plugin
- `Code/server/` - Theoretically portable, but embedded into Windows client
- Build system: Windows builds everything, Linux builds server-only

**Impact:**
- Same game logic tested on Windows only
- Server behavior may diverge from client behavior over time
- Linux users can't test locally
- Dedicated server Linux users can't replicate client-side bugs

**Fix approach:**
- After P2P refactor: Create client-agnostic World/service layer
- Linux: Build test client simulator that can connect to server
- Implement test suite runnable on Linux without SKSE
- Document which features are Windows-only (none theoretically, but SKSE is Windows-only)
- Platform detection compile guards for Skyrim-specific code

**Priority:** Low-Medium - Deferred until P2P architecture solidified.

---

## Performance Optimization Opportunities Blocked

**Issue:** P2P refactoring must complete before optimizations can be applied; current architecture prevents standard P2P patterns.

**Files:**
- All service files - Can't optimize until dual host/peer modes implemented
- `P2P_REFACTOR_PLAN.md` - Documents performance as "Success Criteria" item

**Impact:**
- Host has preventable latency (localhost loopback)
- Host CPU overhead from redundant serialization
- No path to optimize for common case (host as player)
- Performance regression vs Tilted Online (dedicated server model) until refactor complete

**Fix approach:**
- Prioritize P2P refactoring (Phase 1-3 of plan)
- Once complete, implement performance optimizations:
  - Direct state access for host (zero-copy)
  - Delta encoding optimizations (only changed fields)
  - Peer interpolation smoothing
  - Network bandwidth reduction (quantization, compression)
- Create performance regression test suite (see Test Coverage Gaps section)

**Priority:** Medium - Important for launch quality, deferred until architecture ready.

