// -- Server PCH includes (must come first for GameServer.h transitive headers) --
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Platform.hpp>

#include <any>
#include <filesystem>
#include <mutex>
#include <optional>

#include <glm/glm.hpp>
#include <sol/sol.hpp>

#include <Server.hpp>

using TiltedPhoques::ConnectionId_t;
using TiltedPhoques::MakeShared;
using TiltedPhoques::MakeUnique;
using TiltedPhoques::Map;
using TiltedPhoques::ScopedAllocator;
using TiltedPhoques::String;
using TiltedPhoques::UniquePtr;
using TiltedPhoques::Vector;

#include <StringCache.h>

// -- Standard includes --
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <rpmalloc.h>
#include <entt/entt.hpp>

// -- GameServer and loopback transport --
#include <ConsoleRegistry.h>
#include <GameServer.h>
#include <BuildInfo.h>
#include <Messages/AuthenticationRequest.h>
#include "transport/LoopbackTransport.h"
#include "transport/ServerMessageRouter.h"

#include "game_bridge/proc_memory.h"
#include "game_bridge/pointer_table.h"
#include "game_bridge/tcp_client.h"
#include "game_bridge/game_reader.h"
#include "protocol.h"

// Services (all 8)
#include "Services/WeatherService.h"
#include "Services/CalendarService.h"
#include "Services/QuestService.h"
#include "Services/CombatService.h"
#include "Services/InventoryService.h"
#include "Services/ActorValueService.h"
#include "Services/MagicService.h"
#include "Services/CharacterService.h"

// Systems
#include "Systems/InterpolationSystem.h"
#include "Systems/AnimationSystem.h"

// rpmalloc must be initialized before static constructors that allocate
// (e.g., cxxopts regex in GameServer.cpp). Constructor priority 101 runs
// before default static init, matching the pattern in fake_player.cpp.
__attribute__((constructor(101)))
static void InitRpMallocEarly()
{
    rpmalloc_initialize();
}

static std::atomic<bool> s_running{true};

static void SignalHandler(int /*sig*/) {
    s_running.store(false, std::memory_order_relaxed);
}

static void PrintUsage(const char* apProgram) {
    fprintf(stderr, "Usage: %s --pid <pid> --port <port> [--connect <ip:port>] [--server-port <port>]\n", apProgram);
    fprintf(stderr, "       %s --host-only [--server-port <port>]\n", apProgram);
    fprintf(stderr, "  --pid          Target Skyrim process ID (required unless --host-only)\n");
    fprintf(stderr, "  --port         DLL TCP server port (required unless --host-only)\n");
    fprintf(stderr, "  --connect      Join mode: connect to remote host at ip:port (default: host mode)\n");
    fprintf(stderr, "  --server-port  GameServer listen port in host mode (default: 10578)\n");
    fprintf(stderr, "  --host-only    Run embedded GameServer without Skyrim process (standalone testing)\n");
    fprintf(stderr, "  --help         Show this help\n");
}

static bool ParseArgs(int argc, char* argv[], pid_t& aOutPid, uint16_t& aOutPort,
                      std::string& aOutConnect, uint16_t& aOutServerPort) {
    aOutPid = 0;
    aOutPort = 0;
    aOutConnect.clear();
    aOutServerPort = 10578;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            aOutPid = static_cast<pid_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            aOutPort = static_cast<uint16_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            aOutConnect = argv[++i];
        } else if (strcmp(argv[i], "--server-port") == 0 && i + 1 < argc) {
            aOutServerPort = static_cast<uint16_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]);
            exit(0);
        }
    }

    return aOutPid > 0 && aOutPort > 0;
}

// Dispatch a hook event to the pointer table and all services
static void DispatchHookEvent(const HookEventPacket& aHookPkt,
                               GameReader& aReader, PointerTable& aPtrs,
                               WeatherService& aWeather, CalendarService& aCalendar,
                               QuestService& aQuest, CombatService& aCombat,
                               InventoryService& aInventory, ActorValueService& aActorValue,
                               MagicService& aMagic, CharacterService& aCharacter)
{
    // Core pointer table tracking for actor add/remove
    switch (static_cast<HookOpcode>(aHookPkt.header.opcode)) {
    case HOOK_ACTOR_ADDED: {
        if (aHookPkt.argCount >= 1 && aHookPkt.args[0] != 0) {
            uint64_t actorPtr = aHookPkt.args[0];
            uint32_t formId = aReader.ReadFormId(actorPtr);
            if (formId != 0) {
                aPtrs.Insert(formId, actorPtr);
            }
        }
        break;
    }
    case HOOK_ACTOR_REMOVED: {
        if (aHookPkt.argCount >= 1 && aHookPkt.args[0] != 0) {
            uint64_t actorPtr = aHookPkt.args[0];
            uint32_t formId = aReader.ReadFormId(actorPtr);
            if (formId != 0) {
                aPtrs.Remove(formId);
            } else {
                aPtrs.RemoveByPointer(actorPtr);
            }
        }
        break;
    }
    default:
        break;
    }

    // Dispatch to all services
    aWeather.OnHookEvent(aHookPkt);
    aCalendar.OnHookEvent(aHookPkt);
    aQuest.OnHookEvent(aHookPkt);
    aCombat.OnHookEvent(aHookPkt);
    aInventory.OnHookEvent(aHookPkt);
    aActorValue.OnHookEvent(aHookPkt);
    aMagic.OnHookEvent(aHookPkt);
    aCharacter.OnHookEvent(aHookPkt);
}

static void HandleControlPacket(const PacketHeader& aHeader, TcpClient& aClient) {
    switch (static_cast<ControlOpcode>(aHeader.opcode)) {
    case CTRL_HEARTBEAT:
        // Respond with heartbeat
        aClient.SendControl(CTRL_HEARTBEAT);
        break;
    case CTRL_SHUTDOWN:
        spdlog::info("Received CTRL_SHUTDOWN from DLL");
        s_running.store(false, std::memory_order_relaxed);
        break;
    case CTRL_READY:
        spdlog::info("DLL confirmed CTRL_READY");
        break;
    default:
        spdlog::warn("Unknown control opcode: {:#x}", aHeader.opcode);
        break;
    }
}

// Note: non-blocking receive via poll() can be added when TcpClient exposes
// the raw socket fd. For now, blocking receive with frame pacing is used.

int main(int argc, char* argv[]) {
    // rpmalloc already initialized via constructor(101) above

    // Initialize spdlog
    spdlog::set_level(spdlog::level::info);

    // Check for --host-only before full arg parsing
    bool hostOnly = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host-only") == 0) hostOnly = true;
    }

    // Parse command line
    pid_t targetPid = 0;
    uint16_t port = 0;
    std::string connectAddr;
    uint16_t serverPort = 10578;
    if (!hostOnly && !ParseArgs(argc, argv, targetPid, port, connectAddr, serverPort)) {
        PrintUsage(argv[0]);
        rpmalloc_finalize();
        return 1;
    }
    if (hostOnly) {
        // Still parse --server-port if provided
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--server-port") == 0 && i + 1 < argc) {
                serverPort = static_cast<uint16_t>(atoi(argv[++i]));
            }
        }
        spdlog::info("Starting in HOST-ONLY mode (port {})", serverPort);
    } else {
        spdlog::info("skyrim-coop native client starting (pid={}, port={})", targetPid, port);
    }

    // Install signal handlers for clean shutdown
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // --- Host-only mode: run GameServer without Skyrim process ---
    if (hostOnly) {
        // Create ConOut logger (required by ConsoleRegistry)
        if (!spdlog::get("ConOut"))
            spdlog::stdout_color_mt("ConOut");

        auto pConsole = std::make_unique<ServerConsole::ConsoleRegistry>("ConOut");
        auto pGameServer = std::make_unique<Server::GameServer>(*pConsole);
        pGameServer->Initialize();

        spdlog::info("GameServer listening on port {}", pGameServer->GetPort());

        // Create loopback transport for host self-authentication
        auto pLoopback = std::make_unique<LoopbackTransport>(*pGameServer);
        auto pRouter = std::make_unique<ServerMessageRouter>();

        // Wire loopback: GameServer sends to router
        pGameServer->SetLoopbackRoute(
            LoopbackTransport::kHostConnectionId,
            [&pRouter](const void* apData, uint32_t aSize) { pRouter->OnServerMessage(apData, aSize); });
        pLoopback->SetServerMessageCallback(
            [&pRouter](const void* apData, uint32_t aSize) { pRouter->OnServerMessage(apData, aSize); });

        // Host self-authentication
        AuthenticationRequest authReq;
        authReq.Version = BUILD_COMMIT;
        authReq.Token = "";
        authReq.Username = "Host";
        authReq.SKSEActive = true;
        authReq.MO2Active = false;
        authReq.DiscordId = 0;
        authReq.Level = 1;
        pLoopback->SendToServer(authReq);

        pGameServer->Update();

        if (pRouter->IsAuthenticated()) {
            spdlog::info("Host player authenticated with embedded GameServer");
        } else {
            spdlog::error("Host player authentication FAILED -- this should not happen");
        }

        // Simplified main loop: only GameServer ticks
        spdlog::info("Entering host-only main loop (Ctrl+C to stop)");
        static constexpr auto kTargetFrameTime = std::chrono::milliseconds(16);

        while (s_running.load(std::memory_order_relaxed)) {
            auto frameStart = std::chrono::steady_clock::now();

            pGameServer->Update();

            auto frameEnd = std::chrono::steady_clock::now();
            auto frameElapsed = frameEnd - frameStart;
            if (frameElapsed < kTargetFrameTime) {
                std::this_thread::sleep_for(kTargetFrameTime - frameElapsed);
            }
        }

        // Shutdown
        pGameServer->Kill();
        pGameServer->Close();
        pGameServer.reset();
        pConsole.reset();
        spdlog::info("GameServer shut down");

        rpmalloc_finalize();
        return 0;
    }

    // --- Normal mode: connect to Skyrim process via relay DLL ---

    // Open /proc/pid/mem reader
    ProcMemReader memReader;
    if (!memReader.Open(targetPid)) {
        spdlog::error("Failed to open /proc/{}/mem -- is the process running?", targetPid);
        rpmalloc_finalize();
        return 1;
    }

    // Connect TCP client to DLL server
    TcpClient tcpClient;
    if (!tcpClient.Connect(port)) {
        spdlog::error("Failed to connect to DLL TCP server at 127.0.0.1:{}", port);
        memReader.Close();
        rpmalloc_finalize();
        return 1;
    }

    // Send CTRL_READY to signal native process is initialized
    if (!tcpClient.SendControl(CTRL_READY)) {
        spdlog::error("Failed to send CTRL_READY");
        tcpClient.Disconnect();
        memReader.Close();
        rpmalloc_finalize();
        return 1;
    }
    spdlog::info("Sent CTRL_READY to DLL");

    // Initialize game bridge
    PointerTable pointerTable;
    GameReader gameReader(memReader, pointerTable);

    // Initialize EnTT registry for ECS
    entt::registry registry;

    // Initialize all 8 services
    WeatherService weatherService(gameReader, tcpClient);
    CalendarService calendarService(gameReader, tcpClient);
    QuestService questService(gameReader, tcpClient);
    CombatService combatService(gameReader, tcpClient);
    InventoryService inventoryService(gameReader, tcpClient);
    ActorValueService actorValueService(gameReader, tcpClient);
    MagicService magicService(gameReader, tcpClient);
    CharacterService characterService(gameReader, tcpClient, registry);

    spdlog::info("All 8 services initialized (Weather, Calendar, Quest, Combat, Inventory, ActorValue, Magic, Character)");

    // --- GameServer setup (host mode by default, per D-01) ---
    std::unique_ptr<ServerConsole::ConsoleRegistry> pConsole;
    std::unique_ptr<Server::GameServer> pGameServer;
    std::unique_ptr<LoopbackTransport> pLoopback;
    std::unique_ptr<ServerMessageRouter> pRouter;
    bool isHostMode = connectAddr.empty();  // per D-02

    if (isHostMode) {
        spdlog::info("Starting in HOST mode (port {})", serverPort);

        // Create ConOut logger (required by ConsoleRegistry)
        if (!spdlog::get("ConOut"))
            spdlog::stdout_color_mt("ConOut");

        pConsole = std::make_unique<ServerConsole::ConsoleRegistry>("ConOut");
        pGameServer = std::make_unique<Server::GameServer>(*pConsole);
        pGameServer->Initialize();

        spdlog::info("GameServer listening on port {}", pGameServer->GetPort());

        // Create loopback transport
        pLoopback = std::make_unique<LoopbackTransport>(*pGameServer);
        pRouter = std::make_unique<ServerMessageRouter>();

        // Wire loopback: GameServer sends to router
        pGameServer->SetLoopbackRoute(
            LoopbackTransport::kHostConnectionId,
            [&pRouter](const void* apData, uint32_t aSize) { pRouter->OnServerMessage(apData, aSize); });
        pLoopback->SetServerMessageCallback(
            [&pRouter](const void* apData, uint32_t aSize) { pRouter->OnServerMessage(apData, aSize); });

        // Wire router callbacks to CharacterService
        pRouter->SetMoveCallback(
            [&characterService](uint32_t aServerId, float aX, float aY, float aZ, float aRx, float aRy) {
                characterService.OnReferencesMoveRequest(aServerId, {aX, aY, aZ}, {aRx, aRy});
            });

        // Set loopback pointer on CharacterService for outbound messages
        characterService.SetLoopback(pLoopback.get());

        // Host self-authentication (per D-04: host is a local player)
        AuthenticationRequest authReq;
        authReq.Version = BUILD_COMMIT;
        authReq.Token = "";
        authReq.Username = "Host";
        authReq.SKSEActive = true;
        authReq.MO2Active = false;
        authReq.DiscordId = 0;
        authReq.Level = 1;
        pLoopback->SendToServer(authReq);

        // Process the auth message
        pGameServer->Update();

        if (pRouter->IsAuthenticated()) {
            spdlog::info("Host player authenticated with embedded GameServer");
        } else {
            spdlog::error("Host player authentication FAILED -- this should not happen");
        }
    } else {
        spdlog::info("Starting in JOIN mode (connecting to {})", connectAddr);
        // Join mode: no GameServer, connect as enet6 client to remote host
        // TODO(Phase 8+): Implement enet6 client connection to remote host
        // For now, just log. The native process still operates as relay bridge.
    }

    // Main event loop
    spdlog::info("Entering main event loop");
    PacketHeader header{};
    uint8_t payload[sizeof(HookEventPacket)]; // max payload size

    // Delta time tracking
    auto lastTime = std::chrono::steady_clock::now();

    // Heartbeat tracking (send every 5 seconds)
    float heartbeatTimer = 0.0f;
    static constexpr float kHeartbeatInterval = 5.0f;

    // Target frame time (~60 FPS = 16ms per iteration)
    static constexpr auto kTargetFrameTime = std::chrono::milliseconds(16);

    while (s_running.load(std::memory_order_relaxed)) {
        auto frameStart = std::chrono::steady_clock::now();

        // Calculate delta time
        float deltaTime = std::chrono::duration<float>(frameStart - lastTime).count();
        lastTime = frameStart;

        // Clamp delta time to prevent spiral of death
        if (deltaTime > 0.25f)
            deltaTime = 0.25f;

        // --- Phase 1: Receive and dispatch all pending TCP packets ---
        int packetsProcessed = 0;
        static constexpr int kMaxPacketsPerFrame = 64;

        while (packetsProcessed < kMaxPacketsPerFrame) {
            if (!tcpClient.IsConnected()) {
                spdlog::warn("TCP connection lost");
                s_running.store(false, std::memory_order_relaxed);
                break;
            }

            if (!tcpClient.Receive(&header, payload, sizeof(payload))) {
                spdlog::warn("TCP receive failed or connection closed");
                s_running.store(false, std::memory_order_relaxed);
                break;
            }

            packetsProcessed++;

            if (header.opcode >= 0xF000) {
                HandleControlPacket(header, tcpClient);
            } else if (header.opcode < 0x8000) {
                HookEventPacket hookPkt{};
                hookPkt.header = header;
                if (header.length >= 1) {
                    hookPkt.argCount = payload[0];
                    if (hookPkt.argCount > 8) hookPkt.argCount = 8;
                    memcpy(hookPkt.args, payload + 8, hookPkt.argCount * sizeof(uint64_t));
                }

                DispatchHookEvent(hookPkt, gameReader, pointerTable,
                                  weatherService, calendarService, questService, combatService,
                                  inventoryService, actorValueService, magicService, characterService);
            } else {
                spdlog::warn("Unexpected command-range opcode from DLL: {:#x}", header.opcode);
            }

            break; // One packet per blocking call, then update
        }

        // --- Phase 2: Service updates ---
        weatherService.Update(deltaTime);
        calendarService.Update(deltaTime);
        questService.Update(deltaTime);
        combatService.Update(deltaTime);
        inventoryService.Update(deltaTime);
        actorValueService.Update(deltaTime);
        magicService.Update(deltaTime);
        characterService.Update(deltaTime);

        // --- Phase 3: Systems ---
        InterpolationSystem::Update(registry, deltaTime);
        AnimationSystem::Update(registry, tcpClient, deltaTime);

        // --- Phase 3.5: GameServer tick (host mode only) ---
        if (pGameServer) {
            pGameServer->Update();
        }

        // --- Phase 4: Heartbeat ---
        heartbeatTimer += deltaTime;
        if (heartbeatTimer >= kHeartbeatInterval) {
            heartbeatTimer = 0.0f;
            if (!tcpClient.SendControl(CTRL_HEARTBEAT)) {
                spdlog::warn("Failed to send heartbeat");
            }
        }

        // --- Phase 5: Frame pacing ---
        auto frameEnd = std::chrono::steady_clock::now();
        auto frameElapsed = frameEnd - frameStart;
        if (frameElapsed < kTargetFrameTime) {
            std::this_thread::sleep_for(kTargetFrameTime - frameElapsed);
        }
    }

    // Shutdown GameServer
    if (pGameServer) {
        pGameServer->Kill();
        pGameServer->Close();
        pGameServer.reset();
        pConsole.reset();
        spdlog::info("GameServer shut down");
    }

    // Cleanup
    spdlog::info("Shutting down native client");
    tcpClient.Disconnect();
    memReader.Close();
    rpmalloc_finalize();

    return 0;
}
