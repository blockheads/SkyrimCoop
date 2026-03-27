# Codebase Structure

**Analysis Date:** 2026-03-27

## Directory Layout

```
Code/
├── client/                      # SKSE plugin - Skyrim client (6,203 LOC)
│   ├── main.cpp                 # SKSE plugin entry point
│   ├── World.h/cpp              # Client ECS registry + services
│   ├── Services/                # Game system managers
│   ├── Components/              # ECS data containers
│   ├── Systems/                 # ECS iteration logic
│   ├── Events/                  # Internal event types
│   ├── Games/Skyrim/            # Skyrim-specific hooks and RTTI
│   └── ModCompat/               # Mod compatibility shims
├── server/                      # Game server (5,310 LOC)
│   ├── main.cpp                 # Standalone server entry
│   ├── World.h/cpp              # Server ECS registry + services
│   ├── Services/                # Authoritative game systems
│   ├── Components/              # Server-side data containers
│   ├── Events/                  # Server event types
│   ├── Game/PlayerManager.h     # Player state coordination
│   └── Scripting/               # Lua bindings and script engine
├── encoding/                    # Network message definitions (shared)
│   ├── Messages/                # Request/response message types (119 total)
│   │   ├── ClientMessageFactory.h      # Opcode → message type mapper
│   │   ├── ServerMessageFactory.h      # Server-side mapper
│   │   ├── *Request.h/cpp              # Client-initiated messages
│   │   └── *Response.h/cpp             # Server responses
│   └── Structs/                 # Serializable data structures
│       ├── ActorData.h          # Complete actor state snapshot
│       ├── AnimationVariables.h # Animation synchronization
│       ├── GameId.h             # (ModId << 32) | BaseId encoding
│       ├── ReferenceUpdate.h    # Movement delta encoding
│       └── Skyrim/              # Game-specific data types
├── common/                      # Shared utilities
│   ├── GameServerInstance.h     # Server singleton access
│   ├── Cell.h/cpp               # Cell coordinate types
│   ├── Map.h/cpp                # Worldspace mapping
│   └── DateTime.h/cpp           # Game time management
├── base/                        # Low-level utilities (allocators, containers, threading)
│   ├── allocator/               # Custom memory allocation
│   ├── containers/              # Hash tables, vectors
│   ├── threading/               # Thread utilities
│   └── dialogues/               # Windows task dialog wrappers
├── components/                  # Cross-cutting shared modules
│   ├── console/                 # In-game console UI
│   ├── crash_handler/           # Sentry + Crashpad integration
│   ├── resources/               # Asset management
│   ├── imgui/                   # Debug GUI rendering
│   └── es_loader/               # Elder Scrolls file parsing
├── libraries/                   # Network and rendering abstractions
│   ├── networking/              # GameNetworkingSockets wrappers
│   │   ├── Client.hpp           # Client network class
│   │   ├── Server.hpp           # Server network class
│   │   └── Packet.cpp           # UDP packet handling
│   ├── hooks/                   # Game function hooking
│   │   ├── D3D11Hook.cpp        # DirectX 11 rendering intercept
│   │   ├── DInputHook.cpp       # Input device hooking
│   │   └── WindowsHook.cpp      # Windows API patching
│   ├── ui/                      # CEF browser integration
│   │   ├── OverlayClient.h      # CEF process communication
│   │   ├── OverlayContextHandler.cpp
│   │   ├── OverlayRenderHandlerD3D11.cpp
│   │   └── OverlayApp.cpp       # CEF application lifecycle
│   ├── ui_process/              # Subprocess for UI rendering
│   │   ├── OverlayProc.cpp      # V8 engine integration
│   │   └── EventsV8Handler.cpp  # JavaScript bridge
│   ├── reverse/                 # Memory manipulation utilities
│   │   ├── FunctionHook.cpp     # Function hooking implementation
│   │   ├── AutoPtr.cpp          # Auto-pointer for hooked functions
│   │   ├── Pattern.cpp          # Signature scanning
│   │   └── App.cpp              # Application initialization
│   └── plugins/                 # Native plugin support (if present)
├── skyrim_ui/                   # Web UI - Angular 16 application
│   ├── src/
│   │   ├── app/
│   │   │   ├── components/      # Reusable UI components
│   │   │   │   ├── server-list/
│   │   │   │   ├── party-menu/
│   │   │   │   ├── chat/
│   │   │   │   ├── player-manager/
│   │   │   │   ├── settings/
│   │   │   │   └── notification-popup/
│   │   │   ├── services/        # Angular services (HTTP, WebSocket)
│   │   │   └── store/           # NgELF state management
│   │   ├── assets/
│   │   │   ├── i18n/            # Localization files
│   │   │   ├── images/          # UI graphics
│   │   │   ├── sounds/          # UI sound effects
│   │   │   └── fonts/           # Embedded font files
│   │   └── styles/              # SCSS styles and mixins
│   ├── angular.json             # Angular build configuration
│   ├── tsconfig.json            # TypeScript configuration
│   └── package.json             # NPM dependencies
├── tp_process/                  # CEF worker process executable
│   ├── main.cpp                 # Process entry point
│   ├── ProcessHandler.h/cpp     # CEF process message handling
│   └── OverlayProc.cpp          # V8 JavaScript bridge
├── server_runner/               # Standalone server launcher
│   └── main.cpp                 # Server bootstrap executable
├── immersive_launcher/          # Game launcher/updater application
│   ├── Launcher.h/cpp           # Installer and updater UI
│   └── Resources/               # Launcher assets
├── admin/                       # Server administration API
│   ├── AdminService.h           # Admin command processor
│   └── web/                     # Admin panel (HTTP endpoints)
├── admin_protocol/              # Admin RPC protocol definition
│   └── Messages/
├── external/                    # Vendored/external dependencies
│   └── [Third-party libraries]
├── tests/                       # Unit test suite (Catch2)
│   └── encoding.cpp             # Message encoding tests
├── TiltedCore/                  # Custom networking library subset
│   └── [TiltedPhoques core code]
└── xmake.lua                    # Build system configuration
```

## Directory Purposes

**client/**
- Purpose: SKSE plugin that runs in Skyrim process
- Contains: Game integration, local state management, rendering, input handling
- Key files: `main.cpp` (SKSE entry), `World.h` (ECS registry)
- Built as: `SkyrimTogetherClient.dll` (Windows only)

**server/**
- Purpose: Authoritative game state server
- Contains: Player management, actor ownership, quest synchronization, scripting
- Key files: `main.cpp`, `World.h`, `GameServer.h/cpp`
- Built as: `SkyrimTogetherServer` executable or library

**encoding/**
- Purpose: Shared network protocol definition
- Contains: 58 client message types + 61 server message types, serialization logic
- Key files: `ClientMessageFactory.h`, `ServerMessageFactory.h`
- Pattern: Each message has .h (declaration) and .cpp (serialization implementation)
- Used by: Both client and server for send/receive

**common/**
- Purpose: Shared utilities between client and server
- Contains: Game ID encoding, cell coordinate systems, game time
- Key files: `GameId.h`, `Cell.h`, `DateTime.h`
- Rationale: Dual-build (client + server) need same data type definitions

**base/**
- Purpose: Low-level building blocks
- Contains: Memory allocators, container types, threading primitives
- Key files: Reusable abstractions for memory and concurrency
- Used by: All higher layers

**components/**
- Purpose: Platform-independent cross-cutting concerns
- Contains: Console UI, crash reporting, asset loading, debug visualization
- Key files: Modular components that both client and server can use
- Pattern: Each component is independently buildable

**libraries/networking/**
- Purpose: Thin wrapper over GameNetworkingSockets
- Contains: Client/Server base classes, packet serialization
- Key files: `Client.hpp`, `Server.hpp`, `ENetInterface.cpp`
- Pattern: Extends with TiltedOnline-specific message handling

**libraries/hooks/**
- Purpose: Function hooking and memory patching infrastructure
- Contains: D3D11 rendering intercept, DirectInput hook, Windows API patching
- Key files: `D3D11Hook.cpp`, `DInputHook.cpp`, `FunctionHook.cpp`
- Pattern: Detours-style hook installation and uninstallation

**libraries/ui/**
- Purpose: CEF (Chromium Embedded Framework) integration
- Contains: Browser lifecycle, rendering to D3D11, IPC communication
- Key files: `OverlayApp.cpp`, `OverlayClient.h`, `OverlayRenderHandlerD3D11.cpp`
- Pattern: CEF process host communicates with subprocess via IPC

**libraries/ui_process/**
- Purpose: Subprocess for sandboxed UI rendering
- Contains: V8 JavaScript engine, event bridge, render process handler
- Key files: `OverlayProc.cpp`, `EventsV8Handler.cpp`
- Pattern: Separate process runs UI, communicates back via IPC

**libraries/reverse/**
- Purpose: Memory manipulation and reverse engineering utilities
- Contains: Function hooking, signature scanning, auto-pointer management
- Key files: `FunctionHook.cpp`, `Pattern.cpp`, `AutoPtr.cpp`
- Pattern: Safe RAII wrappers around unsafe operations

**skyrim_ui/**
- Purpose: In-game user interface
- Contains: Angular components, services, state management
- Key files: `src/app/components/`, `src/app/services/`
- Build: `pnpm deploy:production` → compiled assets served via CEF
- Tech: Angular 16, TypeScript, SCSS, NgELF (reactive store)

**server_runner/**
- Purpose: Standalone server executable
- Contains: Server initialization, CLI argument parsing
- Key files: `main.cpp`
- Usage: Runs on Linux/Windows without Skyrim

**immersive_launcher/**
- Purpose: Game launcher and mod updater
- Contains: UI for installation, update checking, configuration
- Key files: `Launcher.h/cpp`
- Built as: `ImmersiveLauncher.exe`

**tests/**
- Purpose: Unit test suite
- Contains: Message encoding/decoding tests, vector quantization tests
- Key files: `encoding.cpp` (Catch2-based tests)
- Run via: `xmake build TPTests && ./build/release/tests/TPTests`

## Key File Locations

**Entry Points:**
- `Code/client/main.cpp`: SKSE plugin load function, address library init
- `Code/server/main.cpp` or `Code/server_runner/main.cpp`: Standalone server
- `Code/tp_process/main.cpp`: CEF worker process
- `Code/skyrim_ui/src/main.ts`: Angular bootstrap

**Core ECS:**
- `Code/client/World.h`: Client registry, service container, singleton access
- `Code/server/World.h`: Server registry, player manager, scripting
- `Code/*/Components/*.h`: ECS data containers (LocalComponent, RemoteComponent, etc.)

**Services:**
- `Code/client/Services/TransportService.h`: Network communication
- `Code/client/Services/CharacterService.h`: Actor synchronization
- `Code/server/Services/PlayerService.h`: Player authentication and management
- `Code/server/Services/CharacterService.h`: Authoritative actor state

**Network Protocol:**
- `Code/encoding/Messages/ClientMessageFactory.h`: Parse client→server
- `Code/encoding/Messages/ServerMessageFactory.h`: Parse server→client
- `Code/encoding/Opcodes.h`: Opcode enum definitions
- `Code/encoding/Structs/*.h`: Shared data structures

**Game Integration:**
- `Code/client/Games/Skyrim/Hooks.cpp`: SKSE event hooks
- `Code/client/Games/Skyrim/Events/*.h`: Game event types
- `Code/client/Games/Skyrim/BSCore/`: Skyrim runtime type definitions

**UI:**
- `Code/libraries/ui/OverlayApp.cpp`: CEF browser host
- `Code/libraries/ui_process/OverlayProc.cpp`: CEF render process
- `Code/skyrim_ui/src/app/components/`: Angular UI components
- `Code/skyrim_ui/src/app/services/`: Angular HTTP/WebSocket services

## Naming Conventions

**Files:**
- Singular names for class files: `CharacterService.h` (one class per file)
- PascalCase for class names matching file names
- `*Request.h/cpp` for client→server messages
- `*Response.h/cpp` for server→client messages
- `*Event.h` for internal event types (no .cpp needed for simple events)
- `.cpp` file implements corresponding `.h` header

**Directories:**
- PascalCase for component directories: `Services/`, `Components/`, `Systems/`
- Plural when containing multiple similar items: `Messages/`, `Events/`, `Structs/`
- Game-specific under `Games/Skyrim/` namespace
- Lowercase for leaf modules: `console/`, `crash_handler/`, `resources/`

**Functions & Methods:**
- PascalCase: `OnActorAdded()`, `TakeOwnership()`, `SendMessage()`
- Getters: `GetCharacterService()`, `GetLocalPlayerId()`
- Handlers: `Handle<EventName>()` or `On<EventName>()`
- Internal: `m_` prefix for member variables, `s_` for static

**Types & Components:**
- PascalCase: `LocalComponent`, `RemoteComponent`, `InterpolationComponent`
- Message types: `CharacterSpawnRequest`, `AssignCharacterResponse`
- Event types: `ActorAddedEvent`, `UpdateEvent`, `ConnectedEvent`
- Struct types: `ActorData`, `ReferenceUpdate`, `GameId`

## Where to Add New Code

**New Feature (e.g., new game system):**
- Primary code: `Code/server/Services/NewService.h/cpp` (authoritative logic)
- Client code: `Code/client/Services/NewService.h/cpp` (local handling)
- Messages: `Code/encoding/Messages/NewRequest.h/cpp`, `NewResponse.h/cpp`
- Events: `Code/client/Events/NewEvent.h`, `Code/server/Events/NewEvent.h`
- Tests: `Code/tests/new_feature.cpp` (if testable in isolation)

**New Component/Module:**
- ECS component: `Code/client/Components/NewComponent.h` (data only, no .cpp)
- System: `Code/client/Systems/NewSystem.h/cpp` (iteration logic)
- Service method: Add to appropriate `Code/client/Services/ExistingService.h`
- Register in: `Code/client/World.h` context if service-level, or in existing service

**Utilities:**
- Shared helpers: `Code/common/` if used by both client and server
- Client-only utilities: `Code/base/` or `Code/components/`
- Game-specific helpers: `Code/client/Games/Skyrim/`
- Math/algorithm: `Code/base/` (prefer GLM for 3D math)

**UI Components:**
- New Angular component: `Code/skyrim_ui/src/app/components/<feature-name>/`
- Service for HTTP/state: `Code/skyrim_ui/src/app/services/`
- Store actions: `Code/skyrim_ui/src/app/store/` (NgELF)
- Styling: `Code/skyrim_ui/src/styles/` for shared, component directory for scoped

**Tests:**
- Encoding tests: `Code/tests/encoding.cpp` (append to existing)
- Feature tests: `Code/tests/<feature>.cpp` (if new test file needed)
- Run with: `xmake build TPTests && ./build/release/tests/TPTests`

## Special Directories

**Code/external/**
- Purpose: Vendored third-party libraries
- Generated: No (checked into repo)
- Committed: Yes (but via git submodule in some cases)
- Usage: Build system pulls from here, not package managers

**Code/TiltedCore/**
- Purpose: Subset of TiltedPhoques networking library
- Generated: No
- Committed: Yes (proprietary fork)
- Usage: Base classes for Client, Server, message handling

**Code/skyrim_ui/node_modules/**
- Purpose: NPM dependencies
- Generated: Yes (via pnpm install)
- Committed: No (.gitignore entry)
- Regenerate: `cd Code/skyrim_ui && pnpm install`

**build/**
- Purpose: Compiled binaries and intermediate files
- Generated: Yes (by xmake)
- Committed: No
- Location: `build/release/` or `build/debug/` depending on config

**distrib/**
- Purpose: Final packaged output (plugins, executables, assets)
- Generated: Yes (via xmake install)
- Committed: No
- Contains: SkyrimTogetherClient.dll, SkyrimTogetherServer, UI assets

**logs/**
- Purpose: Runtime log files
- Generated: Yes (at runtime)
- Committed: No
- Pattern: `logs/client.log`, `logs/server.log` with spdlog rotation

---

*Structure analysis: 2026-03-27*
