# External Integrations

**Analysis Date:** 2026-03-27

## APIs & External Services

**Discord:**
- Service: Discord Rich Presence and overlay integration
  - SDK: Discord SDK v3.2.1 (Windows only via `Code/client/Services/DiscordService.h`)
  - Implementation: `Code/client/Services/Generic/DiscordService.cpp`
  - Features: Player presence display, custom activity state, overlay management
  - Integration points: OnUserUpdate callbacks, window message handling (WndProcHandler)
  - Data: Discord user ID tracked on server (`Code/server/Game/Player.h` stores `m_discordId`)

**Sentry (Error Tracking):**
- Service: Crash reporting and error telemetry
  - SDK: sentry-native v0.7.1 with Crashpad backend
  - Client DSN: `https://96c601d451c94b32adb826aa62c6d50f@o228105.ingest.sentry.io/6269770`
  - Server DSN: `https://6aff0a6955754bdebfffb064813b9042@o228105.ingest.sentry.io/6303666`
  - Organization: `together-team` (sentry CLI for symbol upload)
  - Implementation: `Code/components/crash_handler/CrashHandler.cpp`
  - Features: Minidump generation, automatic crash capture, symbol symbolization, debug logging integration
  - Configuration: Auto-session tracking disabled, stack trace symbolization enabled
  - Symbol upload: Via `xmake upload-symbols --key <api-token>` (sentry-cli v2.0.2)

**Skyrim Address Library (Game Integration):**
- Service: Runtime address resolution for Skyrim DLL function hooking
  - Integration: SKSE plugin dependency (`Code/client/main.cpp`)
  - Purpose: Enables function address lookup without manual offsets per Skyrim version
  - Error handling: Shows user dialog with links to Nexus Mods troubleshooting if load fails

## Data Storage

**Databases:**
- Not detected - Server operates stateless with in-memory ECS registry
- Player/world state stored in EnTT registry (ephemeral, lost on server restart)

**File Storage:**
- Local filesystem only
- Server data directories (via docker-compose):
  - `/home/server/config` - Server configuration (host-mounted)
  - `/home/server/Data` - Game data, scripts, resources (host-mounted)
  - `/home/server/logs` - Runtime logs (host-mounted)
- Client logs: `logs/` directory for spdlog output

**Caching:**
- String cache service: `Code/server/Services/StringCacheService.h` - Lightweight string interning
- In-memory navigation mesh cache via Recast Navigation (loaded once per server startup)

## Authentication & Identity

**Auth Provider:**
- Not explicitly implemented for public servers
- Discord ID stored on server for player identification
- P2P connection model: Direct peer-to-peer discovery (host model), no centralized auth service
- SKSE plugin runs in context of local Skyrim game (implicit Windows user authentication)

## Monitoring & Observability

**Error Tracking:**
- Sentry - Crash reports, minidumps, error aggregation, release tracking via BUILD_COMMIT
- Before-send handler capability (currently unused: `BeforeSendHandler()` in CrashHandler.cpp)

**Logs:**
- spdlog v1.13.0 - Structured logging with pluggable sinks
- Log files: `logs/` directory (configurable path in code)
- Log levels: debug, info, warning, error, critical
- Sentry logger integration: Sentry events logged via spdlog at matching levels
- CEF debug log: `logs/cef_debug.log` (optional)

**Metrics:**
- Discord presence tracking - Activity state updates
- Not detected: Prometheus, DataDog, or custom metrics collection

## CI/CD & Deployment

**Hosting:**
- Docker container deployment (multi-arch support: amd64, arm64)
- Base image: Ubuntu 22.04 slim
- Docker Compose orchestration: `docker-compose.yml` for production deployment
- Alternative compose: `docker-compose.msvc-wine.yml` for Windows build environment (MSVC cross-compile)

**CI Pipeline:**
- Not detected in codebase - Likely external (GitHub Actions, not checked in)
- Build artifacts: `.pdb` (Windows) and `.debug` (Linux) uploaded to Sentry
- Docker image: Built from Dockerfile with multi-stage build
  - Stage 1: `tiltedphoques/multiarch-builder:latest` - Compile all binaries
  - Stage 2: Ubuntu 22.04 - Runtime container with executables and Crashpad

**Build Outputs:**
- Windows: `SkyrimTogether.pdb`, `SkyrimTogetherServer.pdb`, `STServer.pdb`
- Linux: `SkyrimTogetherServer.debug`, `libSTServer.debug`
- Executables: `SkyrimTogetherServer`, `libSTServer.so` (shared library), `crashpad_handler`

## Environment Configuration

**Required env vars:**
- `XMAKE_ROOT=y` - XMake running as root (Docker builds)
- Git credentials - For cloning repositories in CI
- Sentry auth token - For `xmake upload-symbols` task
- Discord API credentials - Built into Discord SDK (v3.2.1)

**Optional configuration:**
- `_WIN32_WINNT=0x0A00`, `WINVER=0x0A00` - Windows target version (hard-coded in xmake.lua)
- `IS_MASTER`, `IS_BRANCH_BETA`, `IS_BRANCH_PREREL` - Branch flags (auto-generated in BranchInfo.h)
- Build mode: `debug`, `releasedbg`, `release` (via xmake config)
- Unity build: `--unitybuild=y` for faster compilation

**Secrets location:**
- None detected in codebase (environment-specific)
- Discord SDK secrets: Implicit in SDK initialization
- Sentry DSN: Hard-coded in CrashHandler.cpp (public project identifier, not a secret)

## Webhooks & Callbacks

**Incoming:**
- Discord overlay management: Overlay state callbacks (IDiscordOverlayManager)
- SKSE event handlers: Game state hooks (OnGameStateChange, etc.) via SKSE interfaces
- EnTT dispatcher events: Internal event system for inter-service communication

**Outgoing:**
- Discord presence updates: Discord SDK `IDiscordActivityManager` to update player activity
- Crash report: Sentry (automatic via sentry-native crash handler)
- CEF process communication: IPC bridge between C++ backend and Angular UI via tp_process worker

**Event System (Internal):**
- EnTT dispatcher: Service-to-service communication
- Event types: ActorAddedEvent, UpdateEvent, ConnectedEvent, CellChangeEvent (inferred from services)
- Scoped connections with RAII cleanup (entt::scoped_connection)

## Server Scripting

**Lua Integration:**
- Framework: Lua 5.1 + Sol2 v3.3.0 (C++ bindings)
- Location: `Code/server/Scripting/`
- Implementation: `Code/server/Services/ScriptService.h/cpp`
- Purpose: Custom game logic without recompiling server
- Bindings exposed: Player, World, GameServer, Components, GLM math library

---

*Integration audit: 2026-03-27*
