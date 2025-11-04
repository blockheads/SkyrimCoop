# Listen Server Architecture Research

## Summary of Industry Practices

Research into how major game engines handle listen servers (host-as-server P2P architecture).

## What is a Listen Server?

A **listen server** is where one player's machine acts as both:
1. **Server** - Authoritative game state, processes rules
2. **Client** - Playing the game alongside remote players

This is distinguished from:
- **Dedicated Server** - Separate machine, no player on it
- **Pure P2P** - No authority, all peers equal (prone to cheating)

## Key Findings from Industry

### 1. Two Approaches Exist

#### Approach A: Localhost Networking (More Common)
**Used by:** Source Engine (Half-Life, CS:GO), many older engines

**How it works:**
- Host runs server on localhost
- Host's client connects to 127.0.0.1
- Host player goes through full network stack
- Messages serialize/deserialize even for host

**Pros:**
- ✅ Unified code path - Same logic for host and remote clients
- ✅ Easier to implement - No special cases
- ✅ Single-player can use same architecture

**Cons:**
- ❌ Overhead - Localhost serialization/deserialization
- ❌ Slight lag - Host experiences interpolation delay
- ❌ Wasted bandwidth - Messages sent to self

**Quote from Stack Overflow:**
> "If you want to seamlessly have the local client work with the local server using the same code as the remote clients...use networking because this is the common denominator."

#### Approach B: Direct Authority (Modern Optimization)
**Used by:** Some modern engines, performance-focused games

**How it works:**
- Host directly owns World state
- Host's actions apply immediately (no network)
- NetworkBridge broadcasts changes to remote clients only
- No localhost connection

**Pros:**
- ✅ Zero overhead - No localhost networking
- ✅ Instant response - Host has zero network lag
- ✅ Less complexity - One World instance

**Cons:**
- ❌ Dual code paths - Host logic differs from remote clients
- ❌ More complex - Services must handle both modes
- ❌ Testing burden - Must test host and client paths separately

### 2. Source Engine Example

From Valve Developer Community wiki:

**Listen Server Characteristics:**
- Server and client run on same machine
- Host player still goes through client network stack
- Entity interpolation causes 100ms lag **even for host**
- Server-side lag compensation corrects for this
- Different loopback buffer allocation for multiplayer vs single-player

**Key insight:** Source Engine chose unified code path over performance optimization.

### 3. Unity Netcode for GameObjects

**Listen Server Definition:**
> "A listen server acts as both a server and client on a single player's machine for multiplayer game play, meaning one player both plays the game and owns the game world while other players connect to this server."

**Architecture:**
- Host player treated as special case
- Server and client logic unified in one process
- Host has authority over game state

### 4. Unreal Engine

**Server-Authoritative Model:**
> "The server always has authority over the game state, and information will always replicate from the server to clients."

**Listen Server Traits:**
- Easy to set up for casual multiplayer
- Host has inherent advantage (lower latency)
- Less suitable for competitive games
- Convenient for co-op among small groups

**Launch command:** `GameExecutable.exe ?listen`

## Industry Consensus

### Most Games Use Localhost Networking

**Reasons:**
1. **Simpler codebase** - Same logic everywhere
2. **Easier debugging** - One code path to test
3. **Single-player support** - Can run "server" for offline play
4. **Historical precedent** - Established pattern

**Examples:**
- Minecraft (integrated server)
- Counter-Strike: Global Offensive
- Team Fortress 2
- Left 4 Dead series
- Many Source Engine games
- Many Unity/Unreal games

### Performance Optimization is Rare

**Why Direct Authority is Uncommon:**
1. **Premature optimization** - Localhost is fast enough on modern hardware
2. **Code complexity** - Not worth the maintenance burden
3. **Testing overhead** - Two paths means double the bugs
4. **Diminishing returns** - 100ms localhost lag is acceptable

### When to Use Direct Authority

**Use Case: Performance-Critical Games**
- Large player counts (50+)
- High tick rates (60+ Hz)
- Complex simulations
- Limited host hardware

**Use Case: Massive State**
- Large worlds with many entities
- Frequent state updates
- Bandwidth-constrained hosts

**Use Case: Host Advantage Matters**
- Competitive games where fairness is critical
- Games where host lag is noticeable

## Recommendation for SkyrimCoop

### Current State: Localhost Networking ✅
You're already using the **industry-standard approach** (localhost connection).

**Advantages:**
- Proven pattern
- Unified code
- Easier maintenance

**Disadvantages:**
- Overhead for host (tolerable)
- Redundant operations (StringCache duplication)

### Proposed: Direct Authority

**Why consider switching?**
1. **Complexity already exists** - You have separate client/server code
2. **Performance matters** - Large open world, many entities
3. **Co-op focused** - Small player counts, fairness less critical
4. **Already refactoring** - Good time to make architectural changes

**Why it makes sense for Skyrim:**
- Open world with thousands of actors
- Host machine already stressed (running Skyrim!)
- Co-op (2-8 players), not competitive
- Host owns NPCs/world state anyway (natural authority)

### Hybrid Approach (Recommended)

Start with **feature flag** to support both modes:

```cpp
enum class NetworkMode {
    LocalhostServer,  // Current: Host connects to localhost
    DirectAuthority   // New: Host owns World directly
};

Config::networkMode = NetworkMode::LocalhostServer; // Default: safe
```

**Migration path:**
1. Implement DirectAuthority mode alongside existing code
2. Test thoroughly with feature flag
3. Switch default once confident
4. Remove LocalhostServer mode after stable release

**Benefits:**
- Safe rollback if issues arise
- Users can opt into new mode for testing
- Gradual migration reduces risk

## Implementation Lessons from Research

### 1. Separate Server/Client Code is OK

Unreal, Unity, and Source all maintain separation between server and client logic even in listen servers. Your current structure with `Code/server/` and `Code/client/` is fine.

### 2. Authority Flag is Standard

Most engines use a boolean flag (e.g., `HasAuthority()`, `IsServer()`, `IsHost()`) to determine behavior. Your proposed `World::IsHost()` is the right approach.

### 3. NetworkBridge is a Known Pattern

Separating "game logic" from "network broadcasting" is common. Your NetworkBridge concept aligns with industry practices.

### 4. Services Need Mode Awareness

Every service that interacts with networking needs to know if it's running on the host or a remote client. This dual-mode design is standard.

## Code Examples from Research

### Unity Netcode Pattern

```csharp
if (IsServer)
{
    // Host/Server: Apply directly
    playerHealth.Value = newHealth;
}
else
{
    // Client: Send RPC to server
    UpdateHealthServerRpc(newHealth);
}
```

### Unreal Engine Pattern

```cpp
if (HasAuthority())
{
    // Server/Host: Modify directly
    Health = NewHealth;
}
else
{
    // Client: Call server function
    ServerSetHealth(NewHealth);
}
```

### Your Proposed Pattern (Matches Industry)

```cpp
if (m_world.IsHost())
{
    // Host: Modify directly, broadcast
    UpdateHealthDirect(entity, newHealth);
    m_networkBridge->BroadcastHealthUpdate(entity, newHealth);
}
else
{
    // Peer: Send to host
    m_transport.SendHealthUpdate(entity, newHealth);
}
```

## Conclusion

### You're Not Reinventing the Wheel

Your proposed P2P refactor (Direct Authority) is **less common** but not unprecedented. It's a performance optimization that makes sense for your use case.

### Industry Uses Localhost More

Most games use the localhost approach because:
- It's simpler
- Performance is "good enough"
- Unified code path reduces bugs

### Your Choice is Justified

For SkyrimCoop specifically:
- ✅ Performance matters (large world, many entities)
- ✅ Co-op focused (not competitive)
- ✅ Already complex (dual codebases exist)
- ✅ Natural authority (host owns world state)

### Key Takeaway

**Both approaches are valid.** You're choosing performance and architectural clarity over simplicity. This is a trade-off, not a mistake.

## References

1. [Unity: Listen Server Architecture](https://docs-multiplayer.unity3d.com/netcode/current/learn/listen-server-host-architecture/)
2. [Unreal: Networking Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine)
3. [Source Engine: Multiplayer Networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking)
4. [Stack Overflow: Running Server and Client in Same Process](https://gamedev.stackexchange.com/questions/32637/running-both-the-server-and-the-client-within-the-same-process)
5. [Hathora: P2P vs Client-Server](https://blog.hathora.dev/peer-to-peer-vs-client-server-architecture/)
6. [EdgeGap: Network Topologies](https://edgegap.com/blog/explainer-series-authoritative-servers-relays-peer-to-peer-understanding-networking-types-and-their-benefits-for-each-game-types)

---

**Next Steps:** Proceed with P2P refactor plan, knowing you're optimizing beyond the industry standard but with good reason.
