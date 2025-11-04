# P2P Architecture Design

## Overview

This document describes the true peer-to-peer (P2P) architecture for SkyrimCoop, where the host player directly owns the authoritative game state instead of running a separate server they connect to.

## Current Problem: Localhost Client-Server Model

Currently, SkyrimCoop uses a "localhost client-server" approach:

```
Host Process:
├─ GameServer (embedded) - Authoritative World
└─ Client - Connects to localhost, sends/receives updates

Problem: Host client treats itself as a remote player!
- Host detects change → sends to localhost → receives back
- Double work, double bandwidth, double latency
- Two separate World instances (client + server)
```

This is **not true P2P** - it's just a local server with the overhead of client-server networking.

## True P2P Architecture

In true P2P, the host **IS** the authority, not a client of a local server:

```
Host Process:
├─ World (THE authoritative state)
├─ Game Systems (directly modify World)
└─ NetworkBridge (broadcasts to peers, receives peer inputs)

Peer Process:
├─ World (local simulation/prediction)
├─ Game Systems (update from host messages)
└─ NetworkClient (sends inputs to host, receives updates)
```

### Key Principles

1. **Host owns World directly** - No self-networking
2. **Host's changes are instant** - Applied locally, then broadcast
3. **Peers send inputs** - Host applies them and broadcasts results
4. **One source of truth** - Host's World is authoritative

## Detailed Architecture

### Host Process

```
┌─────────────────────────────────────────┐
│           Host Process                  │
│                                         │
│  ┌──────────────────────────────────┐  │
│  │  World (Authority)               │  │
│  │  ├─ entt::registry               │  │
│  │  ├─ CharacterService             │  │
│  │  ├─ InventoryService             │  │
│  │  ├─ MagicService                 │  │
│  │  ├─ StringCache                  │  │
│  │  └─ ... all game systems         │  │
│  └──────────────────────────────────┘  │
│         ▲                   │           │
│         │                   │           │
│    Local input          Broadcast       │
│    (direct)            changes only     │
│         │                   ▼           │
│  ┌──────────────────────────────────┐  │
│  │  NetworkBridge                   │  │
│  │  ├─ Listen for peer connections  │  │
│  │  ├─ Receive peer inputs          │  │
│  │  ├─ Apply to World               │  │
│  │  └─ Broadcast World deltas       │  │
│  └──────────────────────────────────┘  │
│                                         │
└─────────────────────────────────────────┘
```

**How Host Works:**

1. **Local Game Loop (No Networking)**
   ```
   Host presses 'W' to move forward:
   ├─ InputService detects keypress
   ├─ CharacterService updates position DIRECTLY in World
   ├─ Position changes immediately (no network delay!)
   └─ NetworkBridge detects change, broadcasts to peers
   ```

2. **Broadcasting Changes**
   ```
   NetworkBridge::Update():
   ├─ Check what changed in World (dirty flags/events)
   ├─ Create ServerMessage (ActorMove, SpawnNPC, etc.)
   ├─ FOR EACH connected peer:
   │   └─ Send(message)
   └─ Host's World already updated (no loopback!)
   ```

3. **Receiving Peer Inputs**
   ```
   Peer presses 'E' to interact:
   ├─ NetworkBridge receives ClientMessage
   ├─ Validates action (anti-cheat, range checks)
   ├─ Applies to World (peer's character interacts)
   ├─ World systems process (InventoryService, etc.)
   └─ Broadcasts result to all peers
   ```

### Peer Process

```
┌─────────────────────────────────────────┐
│          Peer Process                   │
│                                         │
│  ┌──────────────────────────────────┐  │
│  │  World (Local View)              │  │
│  │  ├─ entt::registry (replica)     │  │
│  │  ├─ CharacterService             │  │
│  │  ├─ RemoteComponent (most actors)│  │
│  │  ├─ LocalComponent (own player)  │  │
│  │  └─ Interpolation/prediction     │  │
│  └──────────────────────────────────┘  │
│         ▲                   │           │
│         │                   │           │
│    Receive updates      Send inputs     │
│    from host            to host         │
│         │                   ▼           │
│  ┌──────────────────────────────────┐  │
│  │  NetworkClient                   │  │
│  │  ├─ Connect to host IP           │  │
│  │  ├─ Send player inputs/actions   │  │
│  │  ├─ Receive world updates        │  │
│  │  └─ Apply to local World         │  │
│  └──────────────────────────────────┘  │
│                                         │
└─────────────────────────────────────────┘
```

**How Peer Works:**

1. **Local Prediction**
   ```
   Peer presses 'W' to move:
   ├─ InputService detects keypress
   ├─ Predict movement locally (instant feedback)
   ├─ NetworkClient sends input to host
   └─ Wait for authoritative update from host
   ```

2. **Receiving Host Updates**
   ```
   NetworkClient receives ServerMessage:
   ├─ Deserialize message
   ├─ Apply to local World
   │   ├─ Spawn NPC
   │   ├─ Update actor position
   │   └─ Apply damage/effects
   └─ Reconcile prediction (correct if wrong)
   ```

3. **Sending Inputs**
   ```
   Every frame:
   ├─ Collect player actions (move, attack, interact)
   ├─ Create ClientMessage
   └─ Send to host
   ```

## Message Flow Examples

### Example 1: Host Casts Spell

**Old (Localhost Client-Server):**
```
1. Host's MagicService detects cast
2. Creates ClientMessage
3. Sends to localhost:10578
4. Server's MagicService receives
5. Server updates World
6. Server creates ServerMessage
7. Sends to host + peers
8. Host client applies (redundant!)
```

**New (True P2P):**
```
1. Host's MagicService detects cast
2. Updates World DIRECTLY (instant)
3. NetworkBridge detects change
4. Creates ServerMessage
5. Sends ONLY to peers (no loopback)
```

### Example 2: Peer Casts Spell

**Old (via localhost server):**
```
1. Peer's MagicService detects cast
2. Creates ClientMessage → sends to server
3. Server validates and updates World
4. Server broadcasts to all clients
```

**New (via host NetworkBridge):**
```
1. Peer's MagicService detects cast
2. Creates ClientMessage → sends to host
3. Host's NetworkBridge validates
4. Host's World updates (authoritative)
5. Host's NetworkBridge broadcasts to peers
```

**Key insight:** Peers work the same! Just connected to host instead of dedicated server.

### Example 3: StringCache Sync

**Old (with localhost loopback):**
```
1. Host encounters new string "Lydia"
2. Client adds to local StringCache
3. Sends string to localhost server
4. Server adds to server StringCache
5. Server broadcasts StringCacheUpdate
6. Host client receives update (already has it!)
```

**New (unified cache):**
```
1. Host encounters new string "Lydia"
2. Adds DIRECTLY to StringCache (one instance)
3. NetworkBridge detects dirty cache
4. Broadcasts StringCacheUpdate to peers only
5. Host's cache already updated (no network)
```

## Code Structure Changes

### Host-Specific Code

```cpp
// Host's World owns everything
class World : entt::registry {
    // Game systems (existing)
    CharacterService m_characters;
    InventoryService m_inventory;
    MagicService m_magic;
    StringCache m_stringCache; // Or use singleton

    // NEW: Network layer for P2P
    NetworkBridge m_network; // Replaces GameServer for host

    bool m_isHost{false}; // Flag to differentiate behavior
};

// NEW: Lightweight broadcast layer
class NetworkBridge {
    void Initialize(uint16_t port, uint8_t maxPlayers);
    void Shutdown();

    // Accept peer connections
    void AcceptConnections();

    // Handle peer messages
    void OnPeerMessage(ConnectionId_t peerId, ClientMessage& msg);

    // Broadcast world changes
    void BroadcastUpdate(const ServerMessage& msg);
    void BroadcastToAllExcept(const ServerMessage& msg, ConnectionId_t excludeId);

private:
    World& m_world; // Reference to authoritative World
    Map<ConnectionId_t, PeerConnection> m_peers;
};
```

### Peer-Specific Code

```cpp
// Peer's World is a simulated replica
class World : entt::registry {
    // Same game systems, but in "receive mode"
    CharacterService m_characters;
    InventoryService m_inventory;

    // Connection to host
    NetworkClient m_network; // Replaces TransportService

    bool m_isHost{false}; // Always false for peers
};

// Peer's network client
class NetworkClient {
    void Connect(String hostAddress, uint16_t port);
    void Disconnect();

    // Send player inputs to host
    void SendInput(const ClientMessage& msg);

    // Receive and apply host updates
    void OnHostMessage(const ServerMessage& msg);

private:
    World& m_world; // Reference to local World
    ConnectionId_t m_hostConnection;
};
```

## Benefits of True P2P

### Performance
- ✅ **Zero localhost latency for host** - Direct World modifications
- ✅ **50% less network traffic** - No loopback messages
- ✅ **Lower CPU usage** - No redundant serialization/deserialization

### Architecture
- ✅ **Single source of truth** - Host's World is THE World
- ✅ **Simpler code paths** - No "if hosting" checks scattered everywhere
- ✅ **Natural data flow** - Changes propagate outward from host

### Scalability
- ✅ **Easier to optimize** - Focus on efficient delta broadcasting
- ✅ **Less memory** - One World instance, not two
- ✅ **Clearer ownership** - Host owns NPCs, world state, quest progress

## Migration Path

### Phase 1: Identify Redundancy
- [x] Identify systems that do localhost networking (StringCache)
- [ ] Map all ClientMessage/ServerMessage flows
- [ ] Find all places where host treats itself as remote

### Phase 2: Refactor Core Systems
- [ ] Create NetworkBridge to replace GameServer for embedded mode
- [ ] Add `World::IsHost()` flag and routing logic
- [ ] Refactor services to use direct World access when hosting

### Phase 3: Remove Localhost Networking
- [ ] Remove TransportService localhost connection for host
- [ ] Remove server-side World instance (host's World is the server)
- [ ] Update message handlers to skip host in broadcasts

### Phase 4: Optimize
- [ ] Add delta compression for broadcasts
- [ ] Implement proper prediction/reconciliation for peers
- [ ] Add interest management (only send nearby updates)

## Open Questions

1. **How do we handle "host player" entity?**
   - Option A: Host player exists in World like any other player
   - Option B: Host player is implicit, only peers have entities
   - **Recommendation:** Option A (cleaner, uniform treatment)

2. **What about GameServer scripting (Lua)?**
   - Lua scripts should work on NetworkBridge
   - Host can run server-side scripts on their World
   - **Recommendation:** Keep scripting, just runs on host's World

3. **Migration strategy for existing saves/code?**
   - Gradual refactor, not rewrite
   - Keep message formats compatible
   - **Recommendation:** Feature flag for old vs new mode during transition

## Conclusion

True P2P means the host **IS** the authority, not a client of a local server. This eliminates redundant networking, reduces latency for the host, and simplifies the architecture. The key mental shift is: **Host owns World directly, peers connect to them.**
