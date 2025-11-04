# Why Direct Authority is REQUIRED for SkyrimCoop

## TL;DR

**SkyrimCoop MUST use direct authority architecture (host owns World directly, no localhost connection) because we are modding a closed-source game engine, not building our own.**

Unlike Unity/Unreal games where developers control the engine, we cannot run a separate server process that executes Skyrim's game logic. The host's Skyrim.exe IS the authority, and our mod is just a sync layer on top of it.

---

## The Fundamental Difference: Open vs Closed Source

### Traditional Game Engines (Unity, Unreal, Source)

```
Your Game (You Own The Code):
├─ Game Engine Code ← YOU WROTE THIS
│   ├─ Server Logic (your code)
│   ├─ Client Logic (your code)
│   ├─ Physics simulation
│   ├─ AI systems
│   └─ Game rules
└─ You control everything, can choose localhost or direct authority
```

**Key point:** Developers have **full access to engine code**. They can:
- Run server logic in a separate process
- Execute game simulation on the server
- Run headless servers (no rendering)
- Duplicate game logic between client and server

**Choice:** Localhost networking is **viable** because the server can run the game.

### SkyrimCoop (Modding Closed-Source Game)

```
Skyrim.exe (Bethesda's Code):
├─ Skyrim Engine ← CLOSED SOURCE, YOU DON'T CONTROL THIS
│   ├─ Physics (Havok)
│   ├─ AI (Bethesda's navmesh + behavior)
│   ├─ Quests (Papyrus scripts)
│   ├─ Combat (engine calculations)
│   └─ World state (cells, actors, objects)
├─ SKSE ← LIMITED HOOKS into closed engine
└─ SkyrimTogetherClient.dll ← YOUR MOD
    └─ Can only observe and inject via SKSE hooks
```

**Key point:** You have **zero access to Skyrim's engine source code**. You can:
- Hook into events via SKSE (OnActorMove, OnCombatStart, etc.)
- Read game state (positions, health, inventory)
- Inject data (spawn actors, teleport players)
- **CANNOT:** Recreate Skyrim's engine in a separate process

**Conclusion:** Localhost networking is **NOT viable** because you can't run Skyrim's game logic in a server process.

---

## Why Separate Server Process is IMPOSSIBLE

### Option A: Standalone Server Executable (Tried by Tilted Online)

**Concept:**
```
SkyrimTogetherServer.exe (Standalone):
├─ Receives player inputs
├─ Simulates game world
├─ Sends updates to clients
└─ Runs authoritative game logic
```

**Required Capabilities:**
- Load Skyrim's assets (.esm/.esp files)
- Run Skyrim's AI (behavior trees, navmesh pathfinding)
- Execute Skyrim's scripts (Papyrus VM)
- Simulate Skyrim's physics (Havok)
- Process Skyrim's combat calculations
- Manage Skyrim's quest system
- Handle Skyrim's dialogue trees
- Render nothing (headless mode)

**Problems:**
1. ❌ **Skyrim's engine is closed source** - Can't recreate it
2. ❌ **No headless mode exists** - Skyrim requires full rendering
3. ❌ **Asset loading requires Skyrim.exe** - Can't load outside engine
4. ❌ **Papyrus VM is proprietary** - Can't execute scripts
5. ❌ **Physics tied to engine** - Can't simulate without Havok integration
6. ❌ **AI requires engine** - Behavior trees not exposed
7. ❌ **Years of development** - Would need to reverse-engineer entire engine

**Verdict:** Impossible without recreating Skyrim from scratch.

**Why Tilted Online Did It:**
- They built a **limited** server (didn't run full Skyrim)
- Server only tracked positions, basic state
- Server didn't run AI, quests, physics
- Worked for MMO-style (persistent world, no complex simulation)
- **Didn't work well for co-op** (needed full game simulation)

### Option B: Run Two Skyrim.exe Instances

**Concept:**
```
Host Machine:
├─ Skyrim.exe #1 (Server)
│   └─ Listen on port 10578
│   └─ Run game simulation
│   └─ No rendering? (not supported)
└─ Skyrim.exe #2 (Client)
    └─ Connect to localhost:10578
    └─ Render for host player
```

**Problems:**
1. ❌ **Steam DRM** - Prevents running two instances simultaneously
2. ❌ **Resource usage** - Skyrim uses ~4-8GB RAM, double = 16GB!
3. ❌ **CPU load** - Skyrim is heavy, running two = unplayable
4. ❌ **No headless mode** - Can't run server without rendering
5. ❌ **Mod conflicts** - Which instance has which mods?
6. ❌ **SKSE injection** - How to inject different DLLs into each?
7. ❌ **Save files** - Both would try to access same saves
8. ❌ **User experience** - Insane to require users to do this

**Verdict:** Technically possible but utterly impractical and terrible UX.

### Option C: Direct Authority (ONLY VIABLE OPTION)

**Concept:**
```
Skyrim.exe (Host):
├─ Skyrim Engine ← THE REAL AUTHORITY (runs the game)
├─ SkyrimTogetherClient.dll ← YOUR MOD
│   ├─ SKSE hooks capture Skyrim events
│   ├─ World (entt ECS) mirrors Skyrim state
│   └─ NetworkBridge broadcasts to peers
└─ NO separate server process needed
```

**How It Works:**
1. ✅ **Skyrim engine runs the game** (AI, quests, physics, scripts)
2. ✅ **SKSE hooks capture events** (OnActorSpawn, OnCombatHit, etc.)
3. ✅ **Your ECS mirrors state** (positions, health, inventory)
4. ✅ **NetworkBridge broadcasts deltas** (what changed since last tick)
5. ✅ **Peers receive and apply** (reconstruct state in their Skyrim)

**Advantages:**
- ✅ Single Skyrim instance (normal resource usage)
- ✅ Full game simulation (Skyrim does it)
- ✅ All mods work (running on real Skyrim)
- ✅ No DRM issues (one legitimate instance)
- ✅ Simple UX (just click "Host Game")

**This is not optional - it's the ONLY way to make it work.**

---

## Current Architecture is Fundamentally Flawed

### What You Have Now (Localhost Model)

```
Skyrim.exe (Host Process):
│
├─ Skyrim Engine ← REAL GAME AUTHORITY
│   ├─ Spawns NPCs, runs AI, processes quests
│   └─ TRUE source of game state
│
├─ SkyrimTogetherClient.dll
│   ├─ Hooks Skyrim events via SKSE
│   ├─ Client World (entt) ← COPY #1 of game state
│   └─ TransportService → connects to localhost:10578
│
└─ GameServer (embedded)
    ├─ Server World (entt) ← COPY #2 of game state
    ├─ Does NOT run Skyrim engine
    ├─ Just stores positions, IDs, data
    └─ Sends updates back to client (redundant!)
```

**The Problem:**
1. **Three copies of state:**
   - Skyrim Engine (real game state)
   - Client World (mirror via hooks)
   - Server World (mirror via localhost messages)

2. **Redundant flow:**
   - Skyrim spawns "Lydia" → Client detects → sends to localhost → Server stores → sends back to client
   - **Why send to localhost if it doesn't run the game?**

3. **Server World is pointless:**
   - It's just an ECS registry (data storage)
   - It doesn't execute Skyrim's AI
   - It doesn't run Skyrim's physics
   - It doesn't process Skyrim's quests
   - **It's a glorified cache!**

4. **Localhost overhead:**
   - Serialize messages to localhost
   - Deserialize on server
   - Process and store
   - Serialize response
   - Deserialize on client
   - **All for data that already exists in Skyrim!**

### What You Need (Direct Authority Model)

```
Skyrim.exe (Host Process):
│
├─ Skyrim Engine ← THE ONLY AUTHORITY
│   └─ Runs the actual game
│
└─ SkyrimTogetherClient.dll
    ├─ SKSE hooks capture Skyrim events
    ├─ World (entt) ← SINGLE mirror of Skyrim state
    └─ NetworkBridge broadcasts to peers
        └─ NO localhost connection!

Peer (Skyrim.exe):
│
├─ Skyrim Engine ← REPLICA (controlled by network)
│
└─ SkyrimTogetherClient.dll
    ├─ World (entt) ← Stores remote state
    ├─ NetworkClient receives updates
    └─ Applies to local Skyrim (via SKSE injection)
```

**The Solution:**
1. **One copy of state** (per process):
   - Host: Skyrim Engine → World (mirror) → NetworkBridge
   - Peer: NetworkClient → World (mirror) → Skyrim Engine

2. **Direct flow:**
   - Skyrim spawns "Lydia" → SKSE hook → World.Add() → NetworkBridge broadcasts
   - **No localhost roundtrip!**

3. **World is a sync layer:**
   - Not a separate game simulation
   - Just mirrors Skyrim's state
   - Broadcasts deltas to peers

4. **Zero localhost overhead:**
   - Host modifies World directly
   - NetworkBridge broadcasts only to peers
   - **No self-networking!**

---

## Why StringCache Proves This

### Current StringCache Flow (Broken)

```
1. Host's Skyrim spawns NPC "Lydia"
2. SKSE hook detects it
3. Client code: StringCache::AddWanted("Lydia")
4. Message serialized → sent to localhost:10578
5. GameServer receives → StringCache::AddWanted("Lydia")
   └─ But GameServer ISN'T RUNNING SKYRIM!
   └─ It's just storing the string in a map
6. Server::ProcessDirty() → Adds "Lydia" with ID 42
7. Server broadcasts StringCacheUpdate to ALL clients
8. Host client receives → Deserializes
   └─ But we already have "Lydia" from step 3!
```

**Three problems:**
- ❌ Duplicate AddWanted() calls (client + server)
- ❌ Localhost message overhead (serialize → deserialize)
- ❌ Redundant update back to host (already has it)

**Why does this happen?** Because the server is pretending to be authoritative, but it's not - **Skyrim is**.

### Correct StringCache Flow (Direct Authority)

```
1. Host's Skyrim spawns NPC "Lydia"
2. SKSE hook detects it
3. Client code: StringCache::Add("Lydia") → Returns ID 42 immediately
   └─ Host IS the authority, can assign IDs directly
4. NetworkBridge detects dirty cache
5. NetworkBridge broadcasts StringCacheUpdate to PEERS ONLY
   └─ "ID 42 = Lydia"
6. Peers receive → StringCache::Deserialize(42, "Lydia")
7. Host never receives anything (no loopback)
```

**Benefits:**
- ✅ Single Add() call (host only)
- ✅ No localhost messages
- ✅ No redundant updates to host
- ✅ Instant ID assignment for host

**This is only possible with direct authority.**

---

## Comparison: Why Other Games Can Use Localhost

### Unity/Unreal Game (Developer Controls Engine)

**Standalone Server:**
```
DedicatedServer.exe:
├─ Full game engine code (YOUR CODE)
├─ Physics simulation (YOU CONTROL)
├─ AI systems (YOUR CODE)
├─ Game rules (YOUR CODE)
└─ Can run headless (YOU IMPLEMENTED IT)
```

**Listen Server (Localhost):**
```
Game.exe:
├─ Full game engine (YOUR CODE)
│   └─ Server logic runs in thread/process
└─ Client logic connects to localhost
    └─ Both have access to same codebase
```

**Why localhost works:**
- Server can execute game logic (it's your code)
- Server can spawn entities (you control engine)
- Server can run AI (you wrote it)
- **You own the engine, you decide what runs where**

### SkyrimCoop (Modder Does NOT Control Engine)

**Attempt Standalone Server:**
```
SkyrimTogetherServer.exe:
├─ ??? How to run Skyrim's engine? (CLOSED SOURCE)
├─ ??? How to simulate physics? (No Havok access)
├─ ??? How to execute Papyrus? (Proprietary VM)
└─ IMPOSSIBLE without Bethesda's source code
```

**Attempt Localhost:**
```
Skyrim.exe:
├─ Skyrim Engine (BETHESDA'S CODE - runs the real game)
├─ Client → localhost:10578
└─ GameServer (embedded)
    ├─ Just an ECS registry (YOUR CODE)
    ├─ CANNOT run Skyrim's engine
    ├─ CANNOT execute game logic
    └─ Pointless middleman!
```

**Why localhost fails:**
- Server cannot execute Skyrim's logic (closed source)
- Server cannot spawn Skyrim's entities (no engine access)
- Server cannot run Skyrim's AI (proprietary)
- **Server is just a data cache, not a game authority**

**Only Option:**
```
Skyrim.exe:
├─ Skyrim Engine ← THE AUTHORITY (Bethesda's code)
└─ SkyrimTogetherClient.dll ← YOUR MOD
    ├─ Hooks Skyrim (observe and inject)
    ├─ Mirrors state (entt ECS)
    └─ Broadcasts to peers (NetworkBridge)
```

**This works because:**
- Skyrim engine runs the game (you hook into it)
- Your mod mirrors state (via SKSE)
- NetworkBridge syncs to peers (your code)
- **No need for separate server!**

---

## The Architecture You MUST Build

### Host Architecture

```
┌─────────────────────────────────────────────────┐
│        Skyrim.exe (Host Process)                │
│                                                 │
│  ┌───────────────────────────────────────────┐ │
│  │     Skyrim Engine (AUTHORITY)             │ │
│  │     ├─ Physics (Havok)                    │ │
│  │     ├─ AI (Bethesda)                      │ │
│  │     ├─ Quests (Papyrus)                   │ │
│  │     ├─ Combat (engine)                    │ │
│  │     └─ World state                        │ │
│  └───────────────────────────────────────────┘ │
│               ↓ (SKSE hooks)                    │
│  ┌───────────────────────────────────────────┐ │
│  │  SkyrimTogetherClient.dll                 │ │
│  │                                           │ │
│  │  ┌─────────────────────────────────────┐ │ │
│  │  │ World (entt ECS)                    │ │ │
│  │  │ - Mirrors Skyrim state              │ │ │
│  │  │ - Actors, positions, health, etc.   │ │ │
│  │  └─────────────────────────────────────┘ │ │
│  │               ↓                           │ │
│  │  ┌─────────────────────────────────────┐ │ │
│  │  │ NetworkBridge                       │ │ │
│  │  │ - Listens for peer connections      │ │ │
│  │  │ - Receives peer inputs              │ │ │
│  │  │ - Broadcasts World deltas to peers  │ │ │
│  │  │ - NO localhost connection!          │ │ │
│  │  └─────────────────────────────────────┘ │ │
│  └───────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
                     ↓ (Network)
           ┌──────────────────────┐
           │   Peer Connections   │
           └──────────────────────┘
```

### Peer Architecture

```
┌─────────────────────────────────────────────────┐
│        Skyrim.exe (Peer Process)                │
│                                                 │
│  ┌───────────────────────────────────────────┐ │
│  │     Skyrim Engine (REPLICA)               │ │
│  │     - Controlled by network updates       │ │
│  │     - Renders remote actors               │ │
│  │     - Executes local player input only    │ │
│  └───────────────────────────────────────────┘ │
│               ↑ (SKSE injection)                │
│  ┌───────────────────────────────────────────┐ │
│  │  SkyrimTogetherClient.dll                 │ │
│  │                                           │ │
│  │  ┌─────────────────────────────────────┐ │ │
│  │  │ World (entt ECS)                    │ │ │
│  │  │ - Stores remote state               │ │ │
│  │  │ - Receives updates from host        │ │ │
│  │  └─────────────────────────────────────┘ │ │
│  │               ↑                           │ │
│  │  ┌─────────────────────────────────────┐ │ │
│  │  │ NetworkClient                       │ │ │
│  │  │ - Connects to host IP               │ │ │
│  │  │ - Sends player inputs to host       │ │ │
│  │  │ - Receives world updates from host  │ │ │
│  │  └─────────────────────────────────────┘ │ │
│  └───────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
```

### Data Flow

**Host:**
```
Skyrim spawns NPC
  → SKSE hook captures event
    → World.Add(entity)
      → NetworkBridge.BroadcastSpawn(entity)
        → Send to all peers
          → NO LOOPBACK TO HOST!
```

**Peer:**
```
NetworkClient receives SpawnNPC message
  → World.Add(entity) [creates local mirror]
    → SKSE injects into Skyrim
      → Skyrim spawns visual representation
        → Peer sees NPC in their game
```

---

## Why This is Non-Negotiable

### You Cannot Choose

Other games can choose between:
- **Localhost networking** (simpler, unified code)
- **Direct authority** (faster, optimized)

**SkyrimCoop has no choice** because:
1. Skyrim's engine is closed source
2. You cannot run server logic outside Skyrim.exe
3. You cannot recreate Skyrim's systems
4. The host IS playing Skyrim, which IS running the game
5. Your mod is a sync layer, not a game engine

### This is Actually Good

**Benefits of forced direct authority:**
- ✅ **Better performance** - No localhost overhead
- ✅ **Simpler state** - One World per process, not two
- ✅ **Natural architecture** - Mirrors reality (Skyrim is authority)
- ✅ **Clearer code** - Host owns, peers sync
- ✅ **No artificial separation** - Mod hooks directly into game

**The constraint is a feature, not a bug.**

---

## Conclusion

### Direct Authority is REQUIRED

**Not an optimization. Not a preference. REQUIRED.**

**Why:**
1. ✅ Closed-source engine (can't run server separately)
2. ✅ Modding environment (hooks only, no engine access)
3. ✅ Skyrim IS the authority (not your server code)
4. ✅ One instance per player (can't run two Skyrims)
5. ✅ Your mod is a sync layer (not a game engine)

### The Refactor is Fixing a Design Flaw

The current localhost model is trying to **pretend you have a separate server** when you don't. It's:
- Maintaining a fake "authoritative" World that doesn't run the game
- Sending messages to localhost that achieve nothing
- Duplicating state unnecessarily
- Adding overhead for no benefit

**The refactor removes the pretense and embraces reality:**
- Host's Skyrim IS the authority
- Your mod mirrors and broadcasts state
- Peers sync to host's Skyrim
- No middleman needed

### This is The Way

Not "a way" or "one approach" - **the ONLY way** for a Skyrim multiplayer mod.

**Remember:** You're not building a game engine. You're building a sync layer for a closed-source game. Act accordingly.

---

## References

- [Tilted Online Architecture](https://wiki.tiltedphoques.com/tilted-online/) - Why they needed limited server
- [SKSE Documentation](https://skse.silverlock.org/) - What hooks are available
- Original insight from this conversation (2025-11-03)

**Next time you doubt the architecture, read this document.**
