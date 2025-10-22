# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**SkyrimCoop** is a peer-to-peer cooperative multiplayer mod for Skyrim Special Edition, forked from Tilted Online (Skyrim Together Reborn). Unlike the original MMO-style client-server architecture, this project implements a **host-based P2P model** where one player hosts the game locally and others connect directly to them.

**Key Architectural Change:**
- Original: Dedicated server + multiple clients (MMO-style)
- SkyrimCoop: Host player runs embedded server + clients connect (co-op style)

This simplifies:
- Actor ownership (host always owns NPCs/world state)
- Cell management (host's loaded cells are authoritative)
- Quest progression (syncs to host's state)
- Combat resolution (host adjudicates)

**License:** GNU GPLv3

## Build System

**Build Tool:** XMake 2.8.5+ (with CMake legacy support)
**Languages:** C++20 (core), TypeScript/Angular (UI), Lua (server scripting)
**Platforms:** Windows (client), Windows/Linux (server)

### Build Commands

**Windows (Full Build):**
```bash
# Quick build
xmake -y

# Full build with UI
xmake config -m releasedbg
xmake -y
xmake install -o distrib

# Build UI separately
cd Code/skyrim_ui
pnpm install
pnpm deploy:production
```

**Linux (Server Only):**
```bash
xmake config -m release -y
xmake -y
xmake install -o package
```

**Docker Build:**
```bash
docker build . -t skyrimcoop-server:latest
```

**Generate Visual Studio Projects:**
```cmd
xmake project -k vsxmake
```

**Unity Build (Faster Compilation):**
```bash
xmake config --unitybuild=y
xmake -y
```

### Build Targets

- `SkyrimTogetherClient` - SKSE plugin (Windows only)
- `SkyrimTogetherServer` - Game server executable + library
- `SkyrimEncoding` - Network message serialization
- `TPTests` - Unit tests (Catch2-based)
- `tp_process` - CEF worker process for UI overlay
- `immersive_launcher` - Game launcher/updater

### Testing

```bash
# Build and run tests
xmake build TPTests
./build/release/tests/TPTests

# Run specific test
./build/release/tests/TPTests "Vector3_NetQuantize"
```

**What's tested:**
- Message encoding/decoding
- Network quantization accuracy
- Differential serialization

### UI Development

```bash
cd Code/skyrim_ui

# Development server (hot reload)
pnpm start
# Opens on http://0.0.0.0:4200

# Build for production
pnpm deploy:production

# Format code
pnpm format

# Lint
pnpm lint
```

## Architecture

### Directory Structure

```
Code/
├── client/              # Skyrim client plugin (6,203 LOC)
│   ├── Services/        # Game systems (Character, Inventory, Combat, etc.)
│   ├── Components/      # ECS components (LocalComponent, RemoteComponent, etc.)
│   ├── Games/Skyrim/    # Skyrim-specific hooks and RTTI
│   └── main.cpp         # SKSE plugin entry point
├── server/              # Game server (5,310 LOC)
│   ├── Services/        # Server authority (PlayerService, CharacterService, etc.)
│   ├── Components/      # Server-side components
│   ├── Scripting/       # Lua bindings (Sol2)
│   └── GameServer.h/cpp # Core server extending TiltedPhoques::Server
├── encoding/            # Network message definitions (shared)
│   ├── Messages/        # Request/Response messages (58 client, 61 server opcodes)
│   └── Structs/         # Serializable data structures
├── common/              # Shared utilities (GameId, Map, Cell, DateTime)
├── components/          # Reusable components (Console, Resources, CrashHandler)
├── skyrim_ui/           # Angular TypeScript UI
│   └── src/app/         # Components, services, store
└── tests/               # Unit tests
```

### Core Technologies

| Component | Technology | Purpose |
|-----------|-----------|---------|
| ECS | EnTT 3.10.0 | Entity-component system |
| Networking | GameNetworkingSockets + TiltedConnect | UDP transport |
| UI | Angular 16 + CEF | Web-based overlay |
| Server Scripting | Lua 5.1 + Sol2 | Custom game logic |
| Game Integration | SKSE | Skyrim plugin API |
| Logging | spdlog 1.13.0 | Structured logging |
| Crash Reporting | Sentry + Crashpad | Telemetry |
| Math | GLM 0.9.9 | 3D math library |
| HTTP | cpp-httplib | Admin panel |
| Testing | Catch2 | Unit tests |

### Key Architectural Patterns

**1. Entity-Component System (EnTT)**
- `World` class extends `entt::registry`
- All actors/objects are `entt::entity` handles
- Systems iterate components via `entt::view`

**2. Message-Based Networking**
- Opcode-driven request/response pattern
- Differential serialization for bandwidth optimization
- `ClientMessage` (58 opcodes) ↔ `ServerMessage` (61 opcodes)

**3. Event-Driven Communication**
- `entt::dispatcher` for internal events
- Services connect to event sinks
- Example: `ActorAddedEvent`, `UpdateEvent`, `ConnectedEvent`

**4. Service-Oriented Architecture**
- Client services: `CharacterService`, `InventoryService`, `QuestService`, etc.
- Server services: `PlayerService`, `CharacterService`, `AdminService`, etc.
- Services manage specific game systems and message handling

### Client-Server Communication Flow

**Example: Actor Movement Sync**
```
Client (CharacterService)
  → Detects position change via entt::view
  → Creates ClientReferencesMoveRequest
  → TransportService::Send() serializes + sends via UDP
  → Server (CharacterService) receives OnReferencesMoveRequest
  → Updates World registry
  → Broadcasts ServerReferencesMoveRequest to nearby players
  → Remote clients update via OnReferencesMoveRequest
  → InterpolationSystem smooths movement
```

### Network Serialization

**Optimized Data Types:**
- `Vector3_NetQuantize` - 64-bit packed 3D positions
- `Rotator2_NetQuantize` - Quantized rotations
- `GameId` - (ModId << 32) | BaseId form encoding

**Serialization Modes:**
- `SerializeRaw()` - Full state baseline
- `SerializeDifferential()` - Delta encoding (bandwidth optimization)

### Critical Services

**Client (`Code/client/Services/`):**
- `TransportService` - Extends `TiltedPhoques::Client`, handles server communication
- `CharacterService` - Actor spawning, movement, animation sync
- `InventoryService` - Equipment and item synchronization
- `QuestService` - Quest objective tracking (optional)
- `MagicService` - Spell casting and effects
- `PartyService` - Group management
- `OverlayService` - CEF UI integration

**Server (`Code/server/Services/`):**
- `GameServer` - Extends `TiltedPhoques::Server`, main server class
- `PlayerService` - Authentication, connection management
- `CharacterService` - Authoritative actor state
- `ScriptService` - Lua scripting engine
- `AdminService` - Administrative commands
- `MapService` - Cell and worldspace management

## P2P Conversion Strategy

**Key Files to Modify for Host-Based Architecture:**

1. **Server Embedding:**
   - `Code/client/main.cpp` - Embed GameServer instance
   - `Code/client/Services/TransportService.h` - Add host mode flag
   - Create `HostService` to manage embedded server lifecycle

2. **Connection Logic:**
   - `Code/server/GameServer.cpp` - Simplify to single-host model
   - `Code/client/Services/TransportService.cpp` - Detect host vs. client mode
   - Remove dedicated server authentication (use Steam/EOS friend invite)

3. **Ownership Simplification:**
   - `Code/server/Services/CharacterService.cpp` - Host owns all NPCs/world actors
   - `Code/client/Services/CharacterService.cpp` - Remote clients never own world state
   - Remove cell ownership handoff logic

4. **Quest/World State:**
   - Enable quest sync by default (host is authoritative)
   - `Code/server/Services/QuestService.cpp` - Force sync to host's progress
   - `Code/server/Services/CalendarService.cpp` - Use host's game time

5. **Network Discovery:**
   - Replace server list with Steam/EOS lobby system
   - `Code/server/Services/ServerListService.h` - Remove or repurpose for LAN discovery

## Important Implementation Notes

### SKSE Plugin Structure
- Entry point: `SKSEPluginLoad()` in `Code/client/main.cpp`
- Plugin name: `SkyrimTogetherClient.dll`
- Hooks Skyrim's event system via SKSE interfaces

### UI Integration
- CEF renders Angular app as overlay
- `tp_process/` bridges C++ ↔ JavaScript via IPC
- `OverlayService` manages CEF lifecycle
- UI state managed via NgELF reactive store

### Animation Synchronization
- `AnimationSystem` interpolates remote animations
- `LocalAnimationComponent` / `RemoteAnimationComponent` track state
- Animation variables sent via differential encoding
- Custom behavior support via Nemesis/Pandora patches (see README-ANIMATION-MODS.md)

### Lua Scripting (Server-Side Only)
- Located in `Code/server/Scripting/`
- Bindings: Player, World, GameServer, Components, GLM math
- Use for custom game rules without recompiling

### Message Handler Registration
```cpp
// Server
m_messageHandlers[kClientMessageType] = [this](auto& msg) {
    OnClientMessage(*reinterpret_cast<ClientMessageType*>(msg.get()));
};

// Client
m_messageHandlers[kServerMessageType] = [this](auto& msg) {
    OnServerMessage(*reinterpret_cast<ServerMessageType*>(msg.get()));
};
```

### Adding New Messages
1. Define struct in `Code/encoding/Messages/`
2. Inherit from `ClientMessage` or `ServerMessage`
3. Implement `Serialize/Deserialize` methods
4. Add opcode to `Code/encoding/Opcodes.h`
5. Register handler in service's constructor
6. Update `kClientOpcodeMax` / `kServerOpcodeMax`

## Common Development Workflows

### Adding a New Game Feature
1. Define message in `Code/encoding/Messages/`
2. Add client handler in appropriate `Code/client/Services/` service
3. Add server handler in corresponding `Code/server/Services/` service
4. Add ECS component if tracking state (in both client/server Components/)
5. Update UI if needed in `Code/skyrim_ui/`
6. Add tests in `Code/tests/`

### Debugging Network Issues
1. Enable spdlog verbose logging in config
2. Check `logs/` directory for client/server logs
3. Use Wireshark with GameNetworkingSockets filter
4. Inspect message serialization in `Code/encoding/`

### Crash Debugging
- Debug symbols uploaded to Sentry via `xmake upload-symbols`
- Crashpad generates minidumps
- Check Sentry dashboard for stack traces

## Code Quality

**Formatting:**
- C++: `clang-format` (run before commit)
- TypeScript: `prettier` via `pnpm format`

**Guidelines:**
- Follow CODE_GUIDELINES.md in repository
- Pull requests to `dev` branch, not `master`
- Keep code clean and documented

## Dependencies (Pinned Versions)

Direct dependencies managed via XMake:
- entt v3.10.0
- recastnavigation v1.6.0
- tiltedcore v0.2.7
- cryptopp 8.9.0
- spdlog v1.13.0
- cpp-httplib 0.14.0
- gtest v1.14.0
- glm 0.9.9+8
- sentry-native 0.7.1
- zlib v1.3.1
- Windows-only: discord SDK 3.2.1, imgui v1.89.7

## References

- [Original Tilted Online Wiki](https://wiki.tiltedphoques.com/tilted-online/)
- [Build Guide](https://wiki.tiltedphoques.com/tilted-online/technical-documentation/build-guide)
- [Nexus Mods Page](https://www.nexusmods.com/skyrimspecialedition/mods/69993)
- Animation Mods Support: See README-ANIMATION-MODS.md
