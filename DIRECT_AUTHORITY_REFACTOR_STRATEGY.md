# Direct Authority Refactor Strategy

**Goal:** Transform SkyrimCoop from a relay-based architecture to a direct P2P authority model with client-side physics prediction and reconciliation, creating a smooth, well-synchronized co-op experience.

---

## Table of Contents

1. [Current Architecture Problems](#current-architecture-problems)
2. [Target Architecture](#target-architecture)
3. [Key Architectural Changes](#key-architectural-changes)
4. [Physics Reconciliation Strategy](#physics-reconciliation-strategy)
5. [Implementation Phases](#implementation-phases)
6. [Technical Details](#technical-details)
7. [Expected Improvements](#expected-improvements)

---

## Current Architecture Problems

### Issue 1: Relay Server Overhead

```
Host Client → Localhost Server → Network → Remote Clients
```

**Problems:**
- Unnecessary localhost loopback (serialize → deserialize for same process)
- Three copies of state (Skyrim engine, Client World, Server World)
- Server World doesn't run Skyrim logic, just stores data
- Added latency from extra hop

### Issue 2: Disabled Physics on Remote Clients

**Current Code ([Actor.cpp:1233-1238](Code/client/Games/Skyrim/Actor.cpp#L1233-L1238)):**
```cpp
char TP_MAKE_THISCALL(HookActorProcess, Actor, float a2)
{
    if (apThis->GetExtension()->IsRemote())
        return 0;  // ❌ DISABLE all physics/AI for remote actors

    return TiltedPhoques::ThisCall(RealActorProcess, apThis, a2);
}
```

**Result:**
- Remote NPCs are "puppets" following network positions
- No local Havok physics simulation
- Linear interpolation between snapshots (unnatural)
- Looks like "sliding" instead of natural movement
- Collision detection disabled

### Issue 3: 300ms Display Latency

**Current Code ([CharacterService.cpp:1389](Code/client/Services/Generic/CharacterService.cpp#L1389)):**
```cpp
void CharacterService::RunRemoteUpdates() noexcept
{
    // Delay by 300ms to let interpolation system accumulate points
    const auto tick = m_transport.GetClock().GetCurrentTick() - 300;

    // Display remote actors 300ms behind reality
    InterpolationSystem::Update(pActor, interpolationComponent, tick);
}
```

**Result:**
- Remote clients always see world 300ms behind host
- NPCs appear to lag during fast movement
- Unnecessary for co-op (buffer designed for jittery connections)

---

## Target Architecture

### Direct Authority Model

```
┌─────────────────────────────────────────────────┐
│        Skyrim.exe (Host Process)                │
│                                                 │
│  ┌───────────────────────────────────────────┐ │
│  │     Skyrim Engine (AUTHORITY)             │ │
│  │     - Physics (Havok)                     │ │
│  │     - AI (Bethesda)                       │ │
│  │     - Quests (Papyrus)                    │ │
│  │     - Combat (engine)                     │ │
│  │     - World state (authoritative)         │ │
│  └───────────────────────────────────────────┘ │
│               ↓ (SKSE hooks)                    │
│  ┌───────────────────────────────────────────┐ │
│  │  SkyrimTogetherClient.dll                 │ │
│  │                                           │ │
│  │  World (entt ECS) - Single mirror        │ │
│  │  NetworkBridge - Direct to peers         │ │
│  │  NO localhost connection!                 │ │
│  └───────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
                     ↓ (Direct P2P)
┌─────────────────────────────────────────────────┐
│        Skyrim.exe (Remote Client)               │
│                                                 │
│  ┌───────────────────────────────────────────┐ │
│  │     Skyrim Engine (REPLICA)               │ │
│  │     - Runs FULL physics locally           │ │
│  │     - Predicts NPC movement               │ │
│  │     - Reconciles with host authority      │ │
│  └───────────────────────────────────────────┘ │
│               ↓ (SKSE hooks)                    │
│  │  World (entt ECS) - Remote mirror        │ │
│  │  NetworkClient - Receives from host      │ │
│  │  ReconciliationSystem - Corrects drift   │ │
│  └───────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
```

---

## Key Architectural Changes

### Change 1: Remove Localhost Server Loop

**Before:**
```cpp
// Host's client
TransportService::Connect("127.0.0.1", 10578);  // Connect to self

// Embedded server
GameServer::Start(10578);  // Listen on localhost
```

**After:**
```cpp
// Host
if (m_isHost) {
    // World is directly authoritative
    // No localhost connection
    NetworkBridge::StartHosting(port);  // Listen for peers only

    // Detect local actors by checking World directly
    for (auto [entity, localComp] : m_world.view<LocalComponent>()) {
        // This is authoritative, broadcast to peers
    }
}
```

**Result:**
- Single World instance per process
- No serialize/deserialize to localhost
- Direct peer-to-peer broadcasts
- Zero localhost overhead

---

### Change 2: Enable Physics on Remote Clients

**Before:**
```cpp
char TP_MAKE_THISCALL(HookActorProcess, Actor, float a2)
{
    if (apThis->GetExtension()->IsRemote())
        return 0;  // Skip physics

    return TiltedPhoques::ThisCall(RealActorProcess, apThis, a2);
}
```

**After:**
```cpp
char TP_MAKE_THISCALL(HookActorProcess, Actor, float a2)
{
    // Let ALL actors run physics (local and remote)
    return TiltedPhoques::ThisCall(RealActorProcess, apThis, a2);
}
```

**Result:**
- Remote clients run Havok physics for all NPCs
- Natural movement (falling, walking, collisions)
- Prediction matches reality most of the time
- Divergence corrected by reconciliation system

---

### Change 3: Implement Client-Side Prediction with Reconciliation

**New System:**
```cpp
class ReconciliationSystem {
public:
    void Update(Actor* apActor, const Vector3& aHostAuthority, uint64_t aTick) {
        // Current local simulation result (from Skyrim's physics)
        Vector3 localPosition = apActor->position;
        Vector3 localVelocity = apActor->GetVelocity();

        // Host's authoritative state
        Vector3 hostPosition = aHostAuthority;

        // Calculate prediction error
        float error = glm::distance(localPosition, hostPosition);

        if (error > HARD_SNAP_THRESHOLD) {
            // Significant desync (e.g., 5+ units)
            // Hard snap to host position
            apActor->ForcePosition(hostPosition);
            apActor->SetVelocity(hostVelocity);

            spdlog::warn("Hard snap for actor {:X}, error: {}m",
                         apActor->formID, error);
        }
        else if (error > SOFT_CORRECTION_THRESHOLD) {
            // Small drift (e.g., 0.5 - 5 units)
            // Soft correction: blend over time
            Vector3 corrected = glm::mix(localPosition, hostPosition,
                                         CORRECTION_BLEND_FACTOR);
            apActor->ForcePosition(corrected);

            spdlog::debug("Soft correction for actor {:X}, error: {}m",
                          apActor->formID, error);
        }
        else {
            // error < 0.5 units: prediction is good!
            // Keep local physics result (smoother than network position)
            // No correction needed
        }
    }
};
```

**Parameters:**
```cpp
// Thresholds
constexpr float HARD_SNAP_THRESHOLD = 5.0f;      // 5 units = teleport
constexpr float SOFT_CORRECTION_THRESHOLD = 0.5f; // 0.5 units = blend
constexpr float CORRECTION_BLEND_FACTOR = 0.2f;   // 20% correction per frame
```

---

## Physics Reconciliation Strategy

### Deterministic Cases (Low Divergence)

These will rarely need correction:

1. **Gravity**
   - Same across all clients
   - Deterministic fall trajectories
   - **Expected error:** < 0.1 units

2. **Walking/Running**
   - Animation-driven movement
   - Speed synchronized via animation variables
   - **Expected error:** < 0.5 units

3. **Simple Collisions**
   - Static geometry (walls, floors)
   - Mostly deterministic
   - **Expected error:** < 1.0 units

### Non-Deterministic Cases (High Divergence)

These will need frequent corrections:

1. **Ragdoll Physics**
   - Chaotic system (butterfly effect)
   - Floating-point differences amplified
   - **Expected error:** 5+ units after 1 second
   - **Strategy:** Host authority, frequent updates (20Hz)

2. **Complex Collisions**
   - Multi-body interactions
   - Accumulated floating-point errors
   - **Expected error:** 2-5 units
   - **Strategy:** Soft corrections every 100ms

3. **AI-Driven Movement**
   - Pathfinding decisions
   - Random elements in AI
   - **Expected error:** Variable
   - **Strategy:** Trust host's AI, reconcile position only

### Reconciliation Frequency

```cpp
// Different update rates for different cases

// Simple movement (walking, falling)
constexpr auto SIMPLE_MOVEMENT_UPDATE_RATE = 100ms;  // 10 Hz

// Combat/complex physics (ragdolls, knockbacks)
constexpr auto COMPLEX_PHYSICS_UPDATE_RATE = 50ms;   // 20 Hz

// Critical events (teleports, mounts)
constexpr auto IMMEDIATE_UPDATE = 0ms;  // Instant
```

---

## Implementation Phases

### Phase 1: Simplify to Relay (1-2 days)

**Goal:** Prove relay concept works before adding complexity.

**Tasks:**
1. Strip validation from server message handlers
2. Make server a pure forwarder (no ownership checks beyond authentication)
3. Test that basic replication works

**Files to modify:**
- `Code/server/Services/CharacterService.cpp` - Remove validation logic
- `Code/server/Services/CombatService.cpp` - Remove damage validation
- `Code/server/GameServer.cpp` - Simplify to relay mode

**Success criteria:**
- Host → Server → Client flow works
- No desyncs from server "corrections"
- Multiplayer functional (even if separate server process)

---

### Phase 2: Enable Client-Side Physics (2-3 days)

**Goal:** Remove physics blocking, let remote clients simulate.

**Tasks:**
1. Remove `IsRemote()` early return in `HookActorProcess`
2. Remove `IsRemote()` blocking in other physics hooks
3. Observe divergence (expect significant desync initially)
4. Add logging to measure prediction error

**Files to modify:**
- `Code/client/Games/Skyrim/Actor.cpp` - Remove physics blocking
  - `HookActorProcess` (line 1233)
  - `HookSetPosition` (line 909) - Allow physics to move actors
  - `HookAddDeathItems` (line 1246) - Allow death processing
- `Code/client/Games/Skyrim/TESObjectREFR.cpp` - Check for other blocks

**Testing:**
- Spawn NPC on cliff edge
- Watch it fall on both host and client
- Measure position divergence over time
- Log: "Host pos: (X,Y,Z), Client pos: (X,Y,Z), Error: Nm"

**Expected results:**
- Simple physics (gravity): < 1 unit divergence
- Complex physics (ragdoll): 5+ units divergence
- Need reconciliation system!

---

### Phase 3: Implement Reconciliation System (3-4 days)

**Goal:** Correct divergence while maintaining smooth physics.

**Tasks:**
1. Create `ReconciliationSystem` class
2. Replace `InterpolationSystem` with reconciliation logic
3. Implement error thresholds (hard snap vs. soft blend)
4. Add velocity reconciliation (not just position)
5. Tune parameters (thresholds, blend factors)

**New files:**
- `Code/client/Systems/ReconciliationSystem.h`
- `Code/client/Systems/ReconciliationSystem.cpp`

**Modified files:**
- `Code/client/Services/Generic/CharacterService.cpp`
  - Replace `InterpolationSystem::Update` calls with `ReconciliationSystem::Update`
  - Remove 300ms delay (line 1389)
  - Use current tick instead of delayed tick

**Reconciliation algorithm:**
```cpp
void ReconciliationSystem::Update(Actor* apActor,
                                    const MovementUpdate& aHostUpdate,
                                    uint64_t aCurrentTick)
{
    // 1. Let Skyrim's physics run this frame (already happened)
    Vector3 predictedPosition = apActor->position;

    // 2. Get host's authoritative position for this tick
    Vector3 authorityPosition = aHostUpdate.Position;

    // 3. Calculate error
    float error = glm::distance(predictedPosition, authorityPosition);

    // 4. Apply correction based on error magnitude
    if (error > HARD_SNAP_THRESHOLD) {
        // Teleport (unrecoverable desync)
        apActor->ForcePosition(authorityPosition);
        apActor->rotation.x = aHostUpdate.Rotation.x;
        apActor->rotation.z = aHostUpdate.Rotation.y;

        // Reset velocity to match host
        if (apActor->currentProcess && apActor->currentProcess->middleProcess) {
            apActor->currentProcess->middleProcess->direction = aHostUpdate.Direction;
        }
    }
    else if (error > SOFT_CORRECTION_THRESHOLD) {
        // Blend smoothly
        Vector3 corrected = glm::mix(predictedPosition, authorityPosition,
                                     CORRECTION_BLEND_FACTOR);
        apActor->ForcePosition(corrected);

        // Also blend rotation
        auto currentRot = glm::vec3(apActor->rotation.x, 0.f, apActor->rotation.z);
        auto targetRot = glm::vec3(aHostUpdate.Rotation.x, 0.f, aHostUpdate.Rotation.y);
        auto blendedRot = glm::mix(currentRot, targetRot, CORRECTION_BLEND_FACTOR);

        apActor->SetRotation(blendedRot.x, blendedRot.y, blendedRot.z);
    }
    // else: prediction is good, keep local physics!

    // 5. Update animation variables (always sync these)
    apActor->LoadAnimationVariables(aHostUpdate.Variables);
}
```

**Tuning parameters:**
- Start with aggressive corrections (high blend factor)
- Measure average error in logs
- Gradually reduce corrections as system stabilizes
- Different thresholds for different actor types?

---

### Phase 4: Direct Authority (Embed Server) (5-7 days)

**Goal:** Remove separate server process, host owns World directly.

**Tasks:**
1. Create `HostService` to manage peer connections
2. Detect host vs. client mode at startup
3. Bypass localhost connection for host
4. Broadcast directly to peers (no server middleman)
5. Add UI for "Host Game" / "Join Game"

**New files:**
- `Code/client/Services/HostService.h`
- `Code/client/Services/HostService.cpp`

**Modified files:**
- `Code/client/main.cpp` - Initialize host mode
- `Code/client/Services/TransportService.h` - Add host mode flag
- `Code/client/Services/TransportService.cpp` - Detect localhost vs. peer
- `Code/client/Services/CharacterService.cpp` - Check if host vs. client
- `Code/skyrim_ui/src/app/` - Add host/join UI buttons

**HostService design:**
```cpp
class HostService {
public:
    HostService(World& aWorld, entt::dispatcher& aDispatcher);

    void StartHosting(uint16_t aPort);
    void StopHosting();

    bool IsHost() const { return m_isHost; }

    // Broadcast to all connected peers
    template<typename T>
    void BroadcastTopeers(const T& aMessage);

private:
    World& m_world;
    bool m_isHost{false};

    // Peer connection management (Steam P2P or EOS)
    Vector<PeerConnection*> m_peers;

    // Network bridge (direct P2P, no localhost)
    std::unique_ptr<NetworkBridge> m_networkBridge;
};
```

**Host vs. Client logic:**
```cpp
// Host
void CharacterService::OnUpdate(const UpdateEvent& acEvent) {
    if (m_hostService.IsHost()) {
        // Authoritative: Broadcast local actors to peers
        BroadcastLocalActorUpdates();
    } else {
        // Client: Receive updates from host
        ApplyHostUpdates();
    }
}

void CharacterService::BroadcastLocalActorUpdates() {
    auto localActors = m_world.view<LocalComponent, MovementComponent>();

    ServerReferencesMoveRequest message;
    message.Tick = GetCurrentTick();

    for (auto [entity, local, movement] : localActors.each()) {
        message.Updates[local.Id] = {
            .Position = movement.Position,
            .Rotation = movement.Rotation,
            .Variables = movement.Variables,
            .Direction = movement.Direction
        };
    }

    // Direct broadcast (no localhost!)
    m_hostService.BroadcastToPeers(message);
}
```

**No localhost flow:**
```cpp
// OLD (localhost)
Host: World → Serialize → Localhost Socket → Server World → Broadcast

// NEW (direct)
Host: World → Broadcast directly to peers (no server World!)
```

---

### Phase 5: Optimization & Tuning (Ongoing)

**Goal:** Reduce bandwidth, improve feel, minimize corrections.

**Tasks:**
1. Differential encoding (only send changed values)
2. Adaptive update rates (faster during combat, slower when idle)
3. Interest management (only sync nearby actors)
4. Extrapolation (predict between updates)
5. Compression (quantize positions, rotations)

**Bandwidth optimization:**
```cpp
// Current: Send full state every 100ms
struct MovementUpdate {
    Vector3 Position;      // 12 bytes
    Rotator2 Rotation;     // 8 bytes
    AnimationVariables;    // 64 bytes
    Direction;             // 4 bytes
    // Total: ~88 bytes per actor per update
    // 10 actors × 10 Hz = 8.8 KB/s
};

// Optimized: Send deltas only
struct DeltaMovementUpdate {
    uint8_t ChangedFlags;  // 1 byte (which fields changed?)
    Vector3_NetQuantize Position;  // 8 bytes (quantized)
    // Only if changed...
    // Total: ~10-30 bytes per actor (when moving)
    // 10 actors × 10 Hz = 1-3 KB/s
};
```

**Adaptive update rates:**
```cpp
void CharacterService::DetermineUpdateRate(Actor* apActor) {
    if (apActor->IsInCombat()) {
        return 50ms;  // 20 Hz for combat
    }
    else if (apActor->IsMoving()) {
        return 100ms;  // 10 Hz for movement
    }
    else {
        return 500ms;  // 2 Hz for idle
    }
}
```

---

## Technical Details

### Network Message Flow

#### Current (Relay)
```
HOST:
  Skyrim engine → SKSE hooks → CharacterService detects change
    → ClientReferencesMoveRequest → TransportService
    → Serialize → Localhost TCP socket
    → GameServer receives → Deserialize
    → Validate ownership → Store in Server World
    → ServerReferencesMoveRequest → Serialize
    → TCP socket to remote clients

REMOTE:
  TCP socket → TransportService → Deserialize
    → CharacterService::OnReferencesMoveRequest
    → Add to InterpolationComponent queue (300ms delay)
    → InterpolationSystem::Update (every frame)
    → Lerp between two points → ForcePosition
```

**Total latency:** Network RTT + 300ms buffer + localhost overhead

#### Target (Direct Authority)
```
HOST:
  Skyrim engine → SKSE hooks → CharacterService detects change
    → ServerReferencesMoveRequest → NetworkBridge
    → Serialize ONCE → Direct P2P to peers

REMOTE:
  P2P socket → NetworkBridge → Deserialize
    → CharacterService::OnReferencesMoveRequest
    → ReconciliationSystem::Update (immediate, no queue)
    → Compare with local physics → Correct if needed
```

**Total latency:** Network RTT only (no buffer, no localhost)

---

### Data Structures

#### Host Authority State
```cpp
// Host's World (authoritative)
struct LocalComponent {
    uint32_t Id;  // Server ID (assigned by host)
};

struct MovementComponent {
    Vector3 Position;           // Authoritative
    glm::vec3 Rotation;        // Authoritative
    AnimationVariables Variables;
    Vector3 Direction;
    uint64_t LastBroadcastTick;  // When we last sent this
};
```

#### Remote Client State
```cpp
// Remote client's World (replica)
struct RemoteComponent {
    uint32_t Id;           // Server ID (from host)
    uint32_t CachedRefId;  // Local Skyrim formID
};

struct PredictionComponent {
    Vector3 LastHostPosition;      // Last known authority
    uint64_t LastHostTick;         // When host sent it

    Vector3 PredictedPosition;     // Our local physics result
    float AccumulatedError;        // Running error metric

    uint32_t CorrectionCount;      // How many corrections this second
    CorrectionMode Mode;           // TRACKING, SOFT_CORRECT, HARD_SNAP
};
```

---

### Reconciliation Modes

```cpp
enum class CorrectionMode {
    TRACKING,      // Prediction is good (< 0.5 units error)
    SOFT_CORRECT,  // Small drift (0.5 - 5 units), blend gently
    HARD_SNAP      // Major desync (> 5 units), teleport
};

void ReconciliationSystem::DetermineCorrectionMode(
    PredictionComponent& aPrediction,
    float aError)
{
    if (aError > HARD_SNAP_THRESHOLD) {
        aPrediction.Mode = CorrectionMode::HARD_SNAP;
    }
    else if (aError > SOFT_CORRECTION_THRESHOLD) {
        aPrediction.Mode = CorrectionMode::SOFT_CORRECT;

        // Track correction frequency
        aPrediction.CorrectionCount++;

        // If correcting too often, might need hard snap
        if (aPrediction.CorrectionCount > 10) {  // 10 corrections/sec
            spdlog::warn("Actor {:X} needs frequent corrections, forcing hard snap",
                         actor->formID);
            aPrediction.Mode = CorrectionMode::HARD_SNAP;
            aPrediction.CorrectionCount = 0;
        }
    }
    else {
        aPrediction.Mode = CorrectionMode::TRACKING;
        aPrediction.CorrectionCount = 0;  // Reset
    }
}
```

---

### Error Metrics & Logging

```cpp
struct ReconciliationMetrics {
    // Per-actor metrics
    float AverageError;
    float MaxError;
    uint32_t HardSnaps;
    uint32_t SoftCorrections;
    uint32_t FramesInSync;

    // Global metrics
    static float GlobalAverageError;
    static uint32_t TotalActorsTracked;
};

void ReconciliationSystem::LogMetrics() {
    static auto lastLog = std::chrono::steady_clock::now();
    constexpr auto LOG_INTERVAL = 5s;

    auto now = std::chrono::steady_clock::now();
    if (now - lastLog < LOG_INTERVAL)
        return;

    lastLog = now;

    auto view = m_world.view<RemoteComponent, PredictionComponent>();

    float totalError = 0.f;
    uint32_t totalSnaps = 0;
    uint32_t totalCorrections = 0;

    for (auto [entity, remote, prediction] : view.each()) {
        totalError += prediction.AccumulatedError;
        totalSnaps += prediction.HardSnaps;
        totalCorrections += prediction.CorrectionCount;
    }

    float avgError = totalError / view.size_hint();

    spdlog::info("Reconciliation metrics:");
    spdlog::info("  Actors: {}", view.size_hint());
    spdlog::info("  Avg error: {:.2f} units", avgError);
    spdlog::info("  Hard snaps: {}", totalSnaps);
    spdlog::info("  Soft corrections: {}", totalCorrections);

    // If average error is high, suggest tuning
    if (avgError > 2.0f) {
        spdlog::warn("High average error detected. Consider:");
        spdlog::warn("  - Increasing update rate (currently {}ms)", UPDATE_RATE);
        spdlog::warn("  - Reducing SOFT_CORRECTION_THRESHOLD");
        spdlog::warn("  - Checking for packet loss");
    }
}
```

---

## Expected Improvements

### 1. Visual Quality

**Before (Puppet Mode):**
- NPCs "slide" between positions
- No natural physics arcs (falling looks linear)
- Sudden weapon draw/sheath
- Collision glitches (walk through walls briefly)

**After (Physics + Reconciliation):**
- NPCs fall naturally (Havok physics)
- Smooth collisions with environment
- Natural movement arcs
- Occasional minor corrections (imperceptible)

### 2. Latency

**Before:**
- 300ms intentional delay
- Localhost serialization overhead
- Total: ~350-400ms behind host

**After:**
- Network RTT only (~30-100ms depending on connection)
- No buffer delay
- Total: ~30-100ms behind host

**Improvement:** 250-350ms reduction in perceived lag

### 3. Desync Stability

**Before:**
- Rare desyncs (puppets always follow exactly)
- When desyncs happen: catastrophic (entire world wrong)
- No recovery mechanism

**After:**
- Small divergence expected (physics prediction)
- Continuous reconciliation (never catastrophic)
- Self-correcting system
- Gradual drift prevention

### 4. Bandwidth

**Before:**
- Full state every 100ms
- ~88 bytes per actor per update
- 10 actors = 8.8 KB/s

**After (Phase 5 optimizations):**
- Delta encoding + quantization
- ~15-30 bytes per actor per update
- 10 actors = 1.5-3 KB/s

**Improvement:** 66-83% bandwidth reduction

### 5. Architecture Cleanliness

**Before:**
- 3 copies of state (Skyrim, Client World, Server World)
- Confusing ownership model
- Localhost loopback overhead

**After:**
- 1 copy per process (Skyrim → World mirror)
- Clear authority (host owns NPCs)
- Direct P2P communication

---

## Risk Mitigation

### Risk 1: Physics Divergence is Too High

**Symptom:** Constant hard snaps, looks worse than puppet mode.

**Mitigation:**
1. Start with conservative thresholds (allow more error)
2. Measure divergence per physics type (gravity vs. ragdoll)
3. Fall back to puppet mode for high-divergence cases:
   ```cpp
   if (actor->IsRagdoll() && avgError > 5.0f) {
       // Ragdolls are too chaotic, use puppet mode
       disableLocalPhysics = true;
   }
   ```

### Risk 2: Bandwidth Explosion

**Symptom:** Network saturated, high latency.

**Mitigation:**
1. Implement differential encoding immediately (Phase 5)
2. Adaptive update rates (combat vs. idle)
3. Interest management (only sync nearby actors)
4. Monitor bandwidth in metrics:
   ```cpp
   spdlog::info("Bandwidth: {} KB/s", bytesPerSecond / 1024.0f);
   ```

### Risk 3: Reconciliation Feels Janky

**Symptom:** Visible corrections, stuttering.

**Mitigation:**
1. Tune blend factor (start at 0.1, increase if too slow)
2. Use velocity reconciliation (not just position)
3. Extrapolation between updates:
   ```cpp
   Vector3 extrapolated = lastHostPos + hostVelocity * timeSinceUpdate;
   ```

### Risk 4: Host Migration Complexity

**Symptom:** Can't handle host disconnect.

**Mitigation:**
1. Phase 1-3: Don't implement host migration (accept host must stay)
2. Phase 4+: Add host migration:
   - Elect new host (lowest ping player)
   - Transfer World authority
   - All actors become remote for brief moment
   - New host claims ownership

---

## Testing Strategy

### Unit Tests

```cpp
TEST_CASE("ReconciliationSystem::SoftCorrection", "[reconciliation]") {
    Vector3 predicted(100.0f, 50.0f, 20.0f);
    Vector3 authority(100.5f, 50.2f, 20.0f);

    float error = glm::distance(predicted, authority);
    REQUIRE(error < HARD_SNAP_THRESHOLD);
    REQUIRE(error > SOFT_CORRECTION_THRESHOLD);

    Vector3 corrected = glm::mix(predicted, authority, 0.2f);

    // Should move toward authority
    REQUIRE(glm::distance(corrected, authority) < error);
}
```

### Integration Tests

1. **Gravity Test**
   - Spawn NPC on cliff edge
   - Measure position every frame on host and client
   - Assert: Error < 1.0 units throughout fall

2. **Combat Test**
   - Trigger ragdoll on host
   - Measure divergence over 5 seconds
   - Assert: Average error < 3.0 units (with reconciliation)

3. **Latency Test**
   - Simulate 100ms network delay
   - Move player rapidly
   - Assert: Remote sees movement within 150ms (100ms network + 50ms reconciliation)

### Manual Test Cases

1. **Visual Smoothness**
   - [ ] NPC falls off cliff (looks natural on both clients)
   - [ ] NPC walks up stairs (no stuttering)
   - [ ] Combat ragdoll (not identical, but reasonable)
   - [ ] Mount/dismount (synced within 100ms)

2. **Desync Recovery**
   - [ ] Force teleport (admin command)
   - [ ] Check hard snap occurs
   - [ ] Verify no permanent desync

3. **Bandwidth**
   - [ ] Monitor network traffic
   - [ ] Assert: < 10 KB/s per client for 10 NPCs

4. **Latency**
   - [ ] Measure time from host action → remote sees it
   - [ ] Assert: < 150ms on good connection

---

## Success Criteria

### Phase 1 (Relay Simplification)
- ✅ Multiplayer works without desyncs
- ✅ No server "corrections" causing jitter
- ✅ Code complexity reduced (validation removed)

### Phase 2 (Enable Physics)
- ✅ Remote NPCs run physics locally
- ✅ Divergence measured and logged
- ✅ Simple physics (gravity) diverges < 1 unit

### Phase 3 (Reconciliation)
- ✅ Average error < 2.0 units
- ✅ < 5 hard snaps per minute per actor
- ✅ Visual smoothness acceptable in manual testing
- ✅ No permanent desyncs

### Phase 4 (Direct Authority)
- ✅ Host mode functional (no localhost connection)
- ✅ Peer connections work (Steam P2P or EOS)
- ✅ Single World instance per process
- ✅ Latency reduced by 250ms+

### Phase 5 (Optimization)
- ✅ Bandwidth < 3 KB/s per client (10 actors)
- ✅ Differential encoding working
- ✅ Adaptive update rates implemented
- ✅ Interest management (only nearby actors synced)

---

## References

- [WHY_DIRECT_AUTHORITY_IS_REQUIRED.md](WHY_DIRECT_AUTHORITY_IS_REQUIRED.md) - Architectural reasoning
- [Code/client/Games/Skyrim/Actor.cpp](Code/client/Games/Skyrim/Actor.cpp) - Physics hooks
- [Code/client/Systems/InterpolationSystem.cpp](Code/client/Systems/InterpolationSystem.cpp) - Current interpolation
- [Code/server/Services/CharacterService.cpp](Code/server/Services/CharacterService.cpp) - Server relay logic
- [Valve's Source Engine Networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking) - Industry best practices
- [Gabriel Gambetta's Client-Server Game Architecture](https://www.gabrielgambetta.com/client-server-game-architecture.html) - Theory

---

## Conclusion

This refactor transforms SkyrimCoop from a relay-based puppet system to a true client-side prediction architecture:

1. **Direct P2P** - Remove localhost overhead
2. **Enable physics** - Natural movement on all clients
3. **Reconciliation** - Correct divergence smoothly
4. **Lower latency** - 250-350ms improvement
5. **Better feel** - Physics-based, not interpolation-based

**The result:** A smooth, well-synchronized Skyrim co-op experience that feels natural and responsive.

---

**Last Updated:** 2025-11-03
**Author:** Claude (with SkyrimCoop developer)
**Status:** Planning Complete, Ready for Implementation
