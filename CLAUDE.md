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

<!-- GSD:project-start source:PROJECT.md -->
## Project

**SkyrimCoop**

A peer-to-peer cooperative multiplayer mod for Skyrim Special Edition, forked from Tilted Online (Skyrim Together Reborn). Converts the original MMO-style client-server architecture to a host-based co-op model (2-4 players, drop-in via Steam invite). The entire toolchain builds and debugs natively on Linux, cross-compiling the Windows client DLL via MinGW following the LethalInjection pattern.

**Core Value:** Two friends can drop into Skyrim together on Linux with zero server setup — host clicks "host," friend joins via Steam invite, and it just works.

### Constraints

- **Build system**: Must cross-compile Windows DLL from Linux via MinGW (LethalInjection pattern)
- **SKSE dependency**: Client DLL loads into Skyrim via SKSE — cannot modify SKSE itself
- **Wine/Proton**: Client always runs under Wine/Proton on Linux, debug tooling must work with this
- **License**: GPLv3 — all modifications must remain open source
- **Mod compatibility**: Host's mod list is authoritative; clients must match to connect
- **Platform**: Linux-first development, Windows compatibility maintained for end users
<!-- GSD:project-end -->

<!-- GSD:stack-start source:codebase/STACK.md -->
## Technology Stack

## Languages
- C++20 - Core multiplayer networking, client plugin, server executable, game integration
- TypeScript 5.1+ - UI framework (Angular), type-safe frontend development
- Lua 5.1 - Server-side scripting engine for game logic customization (Sol2 bindings)
- C99 - Legacy support code
## Runtime
- Windows (Client/Server development)
- Linux x86_64 + ARM64/v8 (Server deployment)
- Docker containerization for server distribution
- XMake 2.8.5+ - Primary build orchestration for C++, servers, and tests
- pnpm - Angular UI package manager
- Node 18.x (implied by toolchain)
## Frameworks
- ENet6 (UDP-based multiplayer protocol) - Replaces GameNetworkingSockets for P2P simplification
- TiltedConnect (custom networking wrapper) - Packet handling and reliability
- TiltedCore v0.2.7 - Core networking and utility library
- SKSE (Skyrim Script Extender) - Plugin API for hooking Skyrim systems
- Skyrim Address Library - Runtime address resolution for SKSE hooks
- EnTT v3.10.0 - ECS framework for actors, components, systems architecture
- Angular 16.1.2 - Web-based overlay framework
- Chromium Embedded Framework (CEF) 100.0.24 - Browser runtime for in-game UI rendering
- RxJS 7.8.1 - Reactive programming for UI state management
- NgELF (Elf Store) 2.3.2 - Angular reactive state management library
- Transloco 4.3.0 - Internationalization/localization
- Catch2 2.13.9 - C++ unit test framework (header-only)
- GTest v1.14.0 - Google Test framework for C++ tests
- Ngx-Playwright v0.4.2 - E2E testing for Angular (browser-based)
- CMake 3.30.2 (legacy support via xmake)
- Clang-format - C++ code formatting
- Prettier 2.8.8 - TypeScript/JSON formatting
- ESLint 8.43.0 - TypeScript linting with Angular-specific rules
- Visual Studio Project generation via XMake
## Key Dependencies
- enet6 - Replaces GameNetworkingSockets, UDP-based P2P networking
- libuv v1.48.0 - Event-driven I/O, async operations (underlying enet6 dependency)
- Sentry-Native v0.7.1 - Crash reporting with Crashpad backend (both client and server)
- Discord SDK 3.2.1 (Windows only) - Discord presence, overlay integration
- OpenSSL 1.1.1-w - TLS/cryptography (dependency via sentry-native)
- CryptoC++ 8.9.0 - Cryptographic algorithms for message integrity
- spdlog v1.13.0 - Structured logging (both client and server)
- GLM 0.9.9+8 - 3D mathematics (vectors, matrices, quaternions)
- cpp-httplib 0.14.0 (vendored) - Lightweight HTTP server for admin panel
- Zlib v1.3.1 - Compression for network payloads
- Mimalloc 2.2.4 - High-performance memory allocator
- Hopscotch-map v2.3.1 - Fast hash map implementation
- Snappy 1.1.10 - Fast compression library (network optimization)
- Recast Navigation v1.6.0 - Navigation mesh generation
- MinHook v1.3.3 - Function hooking (SKSE integration)
- XByak v7.06 - x86/x64 assembler code generator
- @angular/animations, @angular/cdk, @angular/common, @angular/compiler - Core Angular
- @angular/forms, @angular/router, @angular/platform-browser - Angular modules
- @fortawesome/fontawesome-svg-core v6.4.0, @fortawesome/angular-fontawesome v0.13.0 - Icon library
- @ngneat/elf-devtools v1.3.0 - Redux DevTools integration
- @ngneat/elf-entities v4.4.4 - Entity management store
- @ngneat/loadoff v2.1.0 - Angular loading state management
- @ngneat/reactive-forms v5.0.2 - Reactive form utilities
- tslib 2.5.3 - TypeScript runtime library
- zone.js 0.13.1 - Angular zone polyfill
- @angular-devkit/build-angular 16.1.1 - Angular build system
- @angular/cli 16.1.1 - Angular command-line interface
- @angular/compiler-cli 16.1.2 - Angular template compiler
- @typescript-eslint/eslint-plugin v5.60.0 - TypeScript ESLint rules
- @angular-eslint plugins (6 plugins) - Angular-specific linting
- ts-node 10.9.1 - TypeScript runtime for Node
- Mem 1.0.0 - Memory utilities
- ImGui v1.89.7 (Windows, vendored in Code/external) - Debug UI rendering
- DirectXTK (Windows, vendored in Code/external) - Direct3D utilities for overlay
## Configuration
- Platform detection: Windows vs. Linux via XMake `is_plat()` checks
- Build modes: `debug`, `releasedbg`, `release` via XMake mode system
- Static runtime linking on Windows (MT flag for zero dependency on MSVC runtime)
- Target: Windows 10+ (0x0A00 via `_WIN32_WINNT`)
- Unity builds supported via `--unitybuild=y` config flag
- SIMD optimization: SSE, SSE2, SSE3, SSSE3, NEON vector extensions enabled
- Large object files: `/bigobj` flag for Windows MSVC
- Full debug info: PDB paths embedded for debugger integration
- Warnings: All enabled (`set_warnings("all")`)
- Language: C++20 standard with modern features
- Compilation targets: x64 architecture exclusively
## Platform Requirements
- Windows 10+ (Visual Studio 2019+ recommended via XMake project generation)
- Linux development support (XMake on Linux)
- Git with recursive submodule support
- XMake 2.8.5 or later installed
- **Client:** Windows 10+ with SKSE 2.2+ installed, Skyrim Special Edition license
- **Server:** Ubuntu 22.04 (Docker deployable) or Windows server
## Symbol Management
- **Symbol Upload:** Sentry CLI for uploading .pdb (Windows) and .debug (Linux) files
- **Automatic Crash Reporting:** Sentry Integration with Crashpad backend, auto-session tracking disabled
- **Debug Symbols:** BuildInfo.h auto-generated with branch and commit hash for telemetry
<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->
## Conventions

## Naming Patterns
- Camel case starting with lowercase: `someVariableName`
- Function arguments prefixed with `a`: `aFunctionArgument`
- Const variables prefixed with `c`: `const int cSomeInt`
- Pointers prefixed with `p`: `int* pSomePointer`
- Combined rules for const pointers: `const int* acpSomeArgument`
- Static variables prefixed with `s_`: `static int s_someInt`
- Global variables prefixed with `g_`: `extern int g_someGlobalInt`
- PascalCase starting with uppercase: `class SomeClass`, `struct TransportService`
- Class attributes prefixed with `m_`: `int* m_pSomeMemberPointer`
- Example: `TransportService` has `m_world`, `m_dispatcher`, `m_connected`
- PascalCase starting with uppercase: `void SomeFunc()`, `bool IsOnline()`
- Handler methods prefixed with `Handle`: `HandleUpdate()`, `HandleConnected()`
- Getter methods use `Get` or direct property access: `GetWorld()`, `IsOnline()`
- camelCase for variables and functions: `autoScroll`, `messageTypeToClassName()`
- PascalCase for classes and interfaces: `ChatComponent`, `ChatMessage`
- Component selectors use kebab-case with `app` prefix: `app-chat`, `app-settings`
- Directive selectors use camelCase with `app` prefix: `appDirective`
## Code Style
- Tool: `clang-format`
- Config file: `.clang-format`
- Key settings:
- Tool: Prettier
- Config: `Code/skyrim_ui/.prettierrc`
- Key settings:
- Tool: clang-format only (no separate linter in use)
- Tool: ESLint
- Config: `Code/skyrim_ui/.eslintrc.js`
- Extends: `@typescript-eslint/recommended` and `@typescript-eslint/recommended-requiring-type-checking`
- Key enforced rules:
## Import Organization
- TypeScript uses `src/` for project root references
- Example: `import { ClientService } from 'src/app/services/client.service'`
## Error Handling
- No exceptions allowed (code style guideline)
- Use nothrow versions of functions
- Return success/failure via boolean or result codes
- Example from `TransportService`: functions return `bool`, use noexcept specifiers
- Validate preconditions before operations (e.g., checking if pointer is null before dereferencing)
- Log errors via spdlog: `spdlog::error("message")`, `spdlog::warn("message")`
- Use proper error handling in observables via `catchError` or error callbacks
- Validate component inputs in lifecycle hooks (`ngOnInit`)
- Handle missing data gracefully (e.g., `if (this.entryRefQuery && this.entryRefQuery.first)`)
## Logging
- Include header: `#include <spdlog/spdlog.h>`
- Log levels: `spdlog::info()`, `spdlog::warn()`, `spdlog::error()`, `spdlog::debug()`
- Create logger by name: `spdlog::stdout_color_mt("LoggerName")`
- Example from `HostService.cpp`:
- Custom log levels in service classes:
- Use `console.log()`, `console.warn()`, `console.error()` (allowed by ESLint config)
- Angular services can use dependency injection for custom loggers
- Example from chat component: component logs don't appear in examined code, but Angular follows standard patterns
## Comments
- Document public API contracts (what function does, preconditions, postconditions)
- Explain non-obvious algorithm choices or workarounds
- Mark TODO items with author attribution when significant: `// TODO(username): description`
- TSDoc format not strictly required (ESLint has `jsdoc/no-types` rule set to error)
- Keep comments as text without type annotations
- Example from services: minimal comments, mostly self-explanatory code
- Use forward slashes for inline comments: `// comment`
- Block comments for multi-line: `/* ... */`
- Doxygen comments for public APIs: `/** ... */`
- Example from `TransportService.h`:
## Function Design
- Use argument prefix `a` for all parameters
- Pass complex objects by const reference: `const AuthenticationResponse& acMessage`
- Use noexcept where possible: `void OnConnected() override noexcept;`
- Prefer returning `bool` for success/failure
- Use `[[nodiscard]]` attribute for important return values:
- Const references for returning internal data to prevent modification:
## Module Design
- C++ uses header files (.h) with struct/class definitions
- Services inherit from base classes or extend TiltedPhoques framework classes
- Example: `struct TransportService : Client` in `TransportService.h`
- Not used in C++
- TypeScript services are imported individually from their paths
- C++ uses `struct` for most definitions (POD-like structures with public members or services)
- Example: `struct TransportService`, `struct World`, `struct PlayerService`
## Special Naming Conventions
- Prefix with `Handle`: `HandleUpdate()`, `HandleConnected()`, `HandleAuthenticationResponse()`
- Placed in `protected` or `private` sections of services
- Connected via entt dispatcher subscriptions
- Suffix with `Component`: `LocalComponent`, `RemoteComponent`, `CharacterComponent`
- Located in `Code/client/Components/` or `Code/server/Components/`
- Suffix with `Service`: `TransportService`, `CharacterService`, `InventoryService`
- Located in `Code/client/Services/` or `Code/server/Services/`
- Suffix with `Request` or `Response`: `AuthenticationRequest`, `AssignCharacterRequest`
- Located in `Code/encoding/Messages/`
- Inherit from `ClientMessage` or `ServerMessage`
- Suffix with `Event`: `UpdateEvent`, `ConnectedEvent`, `DisconnectedEvent`
- Located in `Code/client/Events/` or `Code/server/Events/`
- Dispatched via entt::dispatcher
## Build and Compilation
- C++20 is fully supported and encouraged
- Use modern features: concepts, ranges, structured bindings
- Avoid templates when they cause compilation bloat (code guideline)
- Use `TP_NOCOPYMOVE` macro in service classes to prevent accidental copying:
- Avoid excessive macros
- Use `#pragma once` for header guards (no `#ifndef` guards)
- Example: all headers start with `#pragma once`
<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->
## Architecture

## Pattern Overview
- EnTT 3.10.0 registry-based ECS for all actor and object state
- Message-driven request/response pattern for client-server communication
- Event-dispatcher for internal service communication
- Dual-architecture: Client plugin (SKSE) and standalone server
- Host-based P2P model: Host player runs embedded server, others connect directly
## Layers
- Purpose: Handles UDP communication via GameNetworkingSockets + TiltedPhoques protocols
- Location: `Code/libraries/networking/`
- Contains: `Client.hpp`, `Server.hpp`, packet serialization, connection management
- Depends on: GameNetworkingSockets library, TiltedPhoques base classes
- Used by: Client `TransportService`, Server `GameServer`
- Purpose: Serialize/deserialize game state updates with bandwidth optimization
- Location: `Code/encoding/`
- Contains: `Messages/` (58 client opcodes, 61 server opcodes), `Structs/` (shared data types)
- Depends on: TiltedPhoques::Buffer, math libraries (GLM)
- Used by: Transport layer, Services for message creation/handling
- Purpose: Manages game system synchronization and local state
- Location: `Code/client/Services/`
- Contains: CharacterService, InventoryService, QuestService, MagicService, OverlayService, etc.
- Depends on: World registry, TransportService, event dispatcher
- Used by: World (context), Systems, message handlers
- Purpose: Authoritative game state management and player coordination
- Location: `Code/server/Services/`
- Contains: PlayerService, CharacterService, PartyService, ScriptService, CalendarService, etc.
- Depends on: World registry, event dispatcher
- Used by: GameServer, message handlers, admin interface
- Purpose: Data containers for ECS entities
- Location: `Code/client/Components/` and `Code/server/Components/`
- Contains: LocalComponent, RemoteComponent, InterpolationComponent, AnimationComponents, etc.
- Depends on: entt::registry
- Used by: Services, Systems via entt views
- Purpose: Game logic iteration over component combinations
- Location: `Code/client/Systems/`
- Contains: InterpolationSystem, AnimationSystem, FaceGenSystem, RenderSystemD3D11, etc.
- Depends on: Components, World registry
- Used by: World::Update(), Services
- Purpose: Skyrim-specific hooks, memory patching, RTTI introspection
- Location: `Code/client/Games/Skyrim/`
- Contains: SKSE plugin interface, form lookups, event handling, behavior modification
- Depends on: SKSE SDK, Skyrim executable structures
- Used by: Client services for actor manipulation
- Purpose: In-game web-based overlay and launcher
- Location: `Code/skyrim_ui/` (Angular), `Code/libraries/ui/` (CEF integration), `Code/tp_process/` (worker process)
- Contains: Angular components, CEF client, UI IPC bridge
- Depends on: Angular 16, CEF, D3D11 hooks
- Used by: OverlayService, player interactions
- Purpose: Server administration and Lua-based custom game logic
- Location: `Code/server/Services/ScriptService`, `Code/server/Scripting/`
- Contains: Lua 5.1 bindings via Sol2, admin commands
- Depends on: Sol2, spdlog
- Used by: Server for extending game rules without recompilation
## Data Flow
- Maintains local player's loaded cells and actors
- Mirrors remote players and their actors via network updates
- Separates `LocalComponent` (player-controlled) from `RemoteComponent` (synced)
- Authoritative registry of all player actors
- Maintains party/group state via `PartyService`
- Quest/skill progress owned by PlayerService
- NPC state synchronized on demand per cell
## Key Abstractions
- Purpose: Central registry combining ECS + services
- Examples: `Code/client/World.h`
- Pattern: Inherits `entt::registry`, stores dispatcher and all services in context
- Access: `World::Get()` singleton pattern
- Services accessed via `GetCharacterService()`, `GetTransport()`, etc.
- Purpose: Server-side authoritative registry
- Examples: `Code/server/World.h` in Server namespace
- Pattern: Inherits `entt::registry`, manages player manager and scripting
- Access: Passed to all services during initialization
- Purpose: Network communication gateway
- Examples: `Code/client/Services/TransportService.h`
- Pattern: Extends `TiltedPhoques::Client`, acts as transport abstraction
- Methods: `Send()` for outgoing messages, `OnConsume()` for incoming
- Message handlers: Array of function pointers indexed by opcode
- Purpose: Actor/player state synchronization
- Examples: `Code/client/Services/CharacterService.h`, `Code/server/Services/CharacterService.h`
- Pattern: Subscription-based event handling, message request/response pairs
- Client responsibilities: Local ownership, movement detection, animation interpolation
- Server responsibilities: Authoritative state, ownership assignment, broadcast to players
- Purpose: Opcode-driven deserialization of binary network data
- Examples: `Code/encoding/Messages/ClientMessageFactory.h`, `ServerMessageFactory.h`
- Pattern: Extract opcode from reader, instantiate correct message type, deserialize payload
- Used by: `TransportService::OnConsume()` to parse incoming packets
- Purpose: Pure data containers with no behavior
- Examples: `LocalComponent`, `RemoteComponent`, `InterpolationComponent`
- Pattern: Simple structs with public members, initialized in services
- Storage: In World registry via `registry.emplace<ComponentType>(entity, ...)`
- Purpose: Logic that operates on component combinations
- Examples: `InterpolationSystem`, `AnimationSystem`
- Pattern: Static methods taking World, component types, and tick/delta parameters
- Invocation: `World::Update()` calls systems in sequence
## Entry Points
- Location: `Code/client/main.cpp`
- Triggers: SKSE plugin load on Skyrim startup
- Responsibilities: Initialize address library, load game hooks, instantiate TiltedOnlineApp, begin main loop
- Creates: World singleton, initializes all services via World constructor
- Location: `Code/client/Services/HostService.h` (proposed/partial)
- Triggers: When client selects "Host" mode instead of connecting
- Responsibilities: Instantiate `GameServer`, listen for client connections
- Creates: Server World, services, player manager
- Location: `Code/server_runner/` executable
- Triggers: Launched as dedicated process
- Responsibilities: Initialize server, load mod database, bind to port
- Creates: Server World, services, scripting engine
- Location: `Code/skyrim_ui/src/main.ts` (Angular)
- Triggers: OverlayService initializes CEF browser
- Responsibilities: Bootstrap Angular app, initialize state management
- Communicates with: C++ via `tp_process` IPC bridge
## Error Handling
- Connection loss triggers `OnDisconnected(EDisconnectReason)` event
- Services unsubscribe from dispatcher
- Client respawns local player, clears remote entities
- Logs via spdlog at ERROR level
- Invalid opcode in `ClientMessageFactory` returns nullptr
- `OnConsume()` logs warning, discards packet
- No exception thrown (avoid crash in network thread)
- Server doesn't find entity for movement request: logs warning, updates on next valid sync
- Client receives actor deletion while still owning: cancels assignment, spawns as remote
- Factions/quest data mismatch: server is authoritative, client accepts sync
- Sol2 catches Lua runtime errors, logs stack trace
- Continues server execution, script function may return nil
- Admin notified via admin panel
## Cross-Cutting Concerns
- Framework: spdlog 1.13.0 with structured logging
- Pattern: `Log(Info|Warning|Error|Debug)(Category, Message)` in services
- Configuration: Runtime level adjustment via console/admin panel
- Pattern: Check form IDs, cell coordinates, and numeric ranges at service boundaries
- Server validates all client data before applying (don't trust clients)
- Client validates server responses to prevent crashes on corrupt data
- Mechanism: `AuthenticationRequest` with server password
- Pattern: `PlayerService::OnAuthenticate()` verifies before adding player
- Host-P2P mode: Uses Steam/EOS friend invite instead of password
- `SerializeRaw()`: Full baseline state for new connections or periodic sync
- `SerializeDifferential()`: Delta encoding for bandwidth optimization (e.g., only changed fields)
- Quantization: `Vector3_NetQuantize` compresses float positions into 64-bit integers
<!-- GSD:architecture-end -->

<!-- GSD:workflow-start source:GSD defaults -->
## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd:quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd:debug` for investigation and bug fixing
- `/gsd:execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->

<!-- GSD:profile-start -->
## Developer Profile

> Profile not yet configured. Run `/gsd:profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
