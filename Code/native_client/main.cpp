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
#include <Client.hpp>
#include <Packet.hpp>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/ServerMessageFactory.h>
#include <Structs/GameId.h>
#include <Structs/ActorData.h>
#include "transport/LoopbackTransport.h"
#include "transport/ServerMessageRouter.h"

#include <memory>

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
    fprintf(stderr, "  --record <file>  Record all hook events to binary file\n");
    fprintf(stderr, "  --help         Show this help\n");
}

static bool ParseArgs(int argc, char* argv[], pid_t& aOutPid, uint16_t& aOutPort,
                      std::string& aOutConnect, uint16_t& aOutServerPort,
                      std::string& aOutRecordPath) {
    aOutPid = 0;
    aOutPort = 0;
    aOutConnect.clear();
    aOutServerPort = 10578;
    aOutRecordPath.clear();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            aOutPid = static_cast<pid_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            aOutPort = static_cast<uint16_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            aOutConnect = argv[++i];
        } else if (strcmp(argv[i], "--server-port") == 0 && i + 1 < argc) {
            aOutServerPort = static_cast<uint16_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
            aOutRecordPath = argv[++i];
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
    // FormIds are now resolved DLL-side and packed in args[1] (Phase 9.1)
    switch (static_cast<HookOpcode>(aHookPkt.header.opcode)) {
    case HOOK_ACTOR_ADDED:
    case HOOK_SPAWN_ACTOR:
    case HOOK_CHARACTER_CTOR:
    case HOOK_ACTOR_PROCESS: {
        // HOOK_ACTOR_PROCESS fires every frame for loaded actors — formID is
        // guaranteed populated by then (unlike ctor hooks where it's still 0).
        // Insert is idempotent so repeated calls for the same actor are safe.
        if (aHookPkt.argCount >= 2) {
            uint64_t rawPtr = aHookPkt.args[0];
            uint32_t formId = static_cast<uint32_t>(aHookPkt.args[1]);
            if (formId != 0 && rawPtr != 0) {
                aPtrs.Insert(formId, rawPtr);
            }
        }
        break;
    }
    case HOOK_ACTOR_REMOVED: {
        if (aHookPkt.argCount >= 2) {
            uint32_t formId = static_cast<uint32_t>(aHookPkt.args[1]);
            if (formId != 0) {
                aPtrs.Remove(formId);
            } else {
                aPtrs.RemoveByPointer(aHookPkt.args[0]);
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

// ---------------------------------------------------------------------------
// JoinClient: enet6 client for join mode (connects to remote host's GameServer)
// ---------------------------------------------------------------------------
class JoinClient : public TiltedPhoques::Client {
public:
    JoinClient() noexcept = default;
    ~JoinClient() override { Close(); }

    void SetRouter(ServerMessageRouter* apRouter) { m_pRouter = apRouter; }

    void OnConsume(const void* apData, uint32_t aSize) override {
        m_messagesReceived++;
        if (m_pRouter) {
            // enet6 Client::OnConsume receives raw payload without the prefix byte
            // that Server::Send prepends. ServerMessageRouter::OnServerMessage expects
            // the prefix byte, so prepend a dummy zero byte.
            std::vector<uint8_t> padded(aSize + 1);
            padded[0] = 0;
            std::memcpy(padded.data() + 1, apData, aSize);
            m_pRouter->OnServerMessage(padded.data(), static_cast<uint32_t>(padded.size()));
        }
    }

    void OnConnected() override {
        spdlog::info("JoinClient: connected to remote host");
        m_connected = true;
    }

    void OnDisconnected(EDisconnectReason aReason) override {
        spdlog::info("JoinClient: disconnected from remote host (reason: {})", (int)aReason);
        m_connected = false;
    }

    void OnUpdate() override {
        // Called each tick -- nothing needed for basic connect
    }

    [[nodiscard]] bool IsJoinConnected() const { return m_connected; }
    [[nodiscard]] uint64_t GetMessagesReceived() const { return m_messagesReceived; }

    void SendAuthRequest(const std::string& aUsername = "JoinPlayer") {
        AuthenticationRequest authReq;
        authReq.Version = BUILD_COMMIT;
        authReq.Token = "";
        authReq.Username = aUsername;
        authReq.SKSEActive = true;
        authReq.MO2Active = false;
        authReq.DiscordId = 0;
        authReq.Level = 1;

        TiltedPhoques::Buffer buffer(1 << 16);
        TiltedPhoques::Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8); // First byte reserved for packet type
        authReq.Serialize(writer);
        TiltedPhoques::PacketView packet(
            reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());
        Send(&packet);
    }

    void SendAssignCharacterRequest() {
        AssignCharacterRequest req;
        req.Cookie = 1;
        req.ReferenceId = GameId{0, 0x14}; // player character
        req.FormId = GameId{0, 0x14};
        req.CurrentActorData.IsDead = false;

        TiltedPhoques::Buffer buffer(1 << 16);
        TiltedPhoques::Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8); // packet type prefix byte
        req.Serialize(writer);
        TiltedPhoques::PacketView packet(
            reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());
        Send(&packet);
        spdlog::info("JoinClient: sent AssignCharacterRequest (cookie=1, formId=0x14)");
    }

private:
    ServerMessageRouter* m_pRouter{nullptr};
    bool m_connected{false};
    uint64_t m_messagesReceived{0};
};

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
    std::string recordPath;
    if (!hostOnly && !ParseArgs(argc, argv, targetPid, port, connectAddr, serverPort, recordPath)) {
        PrintUsage(argv[0]);
        rpmalloc_finalize();
        return 1;
    }
    if (hostOnly) {
        // Still parse --server-port and --record if provided
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--server-port") == 0 && i + 1 < argc) {
                serverPort = static_cast<uint16_t>(atoi(argv[++i]));
            } else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) {
                recordPath = argv[++i];
            }
        }
        spdlog::info("Starting in HOST-ONLY mode (port {})", serverPort);
    } else {
        spdlog::info("skyrim-coop native client starting (pid={}, port={})", targetPid, port);
    }

    // Install signal handlers for clean shutdown
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // Open record file if --record was specified
    FILE* recordFile = nullptr;
    if (!recordPath.empty()) {
        recordFile = fopen(recordPath.c_str(), "wb");
        if (recordFile)
            spdlog::info("Recording hook events to {}", recordPath);
        else
            spdlog::warn("Failed to open record file: {}", recordPath);
    }

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

    // Load memory maps for address validation and module lookup
    memReader.LoadMemoryMaps();

    // Smoke test: prove pread() works by reading SkyrimSE.exe PE header
    uint64_t skyrimBase = memReader.FindModuleBase("SkyrimSE.exe");
    if (skyrimBase != 0) {
        uint16_t mzSig = memReader.ReadValue<uint16_t>(skyrimBase);
        if (mzSig == 0x5A4D) {
            spdlog::info("SMOKE TEST PASS: Read MZ signature at SkyrimSE.exe base {:#x}", skyrimBase);
        } else {
            spdlog::error("SMOKE TEST FAIL: Expected MZ (0x5A4D) at {:#x}, got {:#04x}", skyrimBase, mzSig);
        }
        // Also hex dump the first 32 bytes of the PE header
        memReader.HexDump(skyrimBase, 32, "SkyrimSE.exe PE header");
    } else {
        spdlog::error("SMOKE TEST FAIL: Could not find SkyrimSE.exe in /proc/{}/maps", targetPid);
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
    std::unique_ptr<JoinClient> pJoinClient;
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

        // Create router for join mode (reuse same pointer declared above for host mode)
        pRouter = std::make_unique<ServerMessageRouter>();

        pJoinClient = std::make_unique<JoinClient>();
        pJoinClient->SetRouter(pRouter.get());

        // Wire auth callback: trigger AssignCharacterRequest after auth acceptance
        pRouter->SetAuthCallback(
            [&pJoinClient](bool aAccepted, uint32_t aPlayerId) {
                if (aAccepted) {
                    spdlog::info("JoinClient: authenticated (playerId={}), sending AssignCharacterRequest", aPlayerId);
                    pJoinClient->SendAssignCharacterRequest();
                } else {
                    spdlog::error("JoinClient: authentication rejected");
                }
            });

        pJoinClient->ConnectByIp(connectAddr);

        // Wait for connection (up to 10 seconds)
        for (int i = 0; i < 200 && !pJoinClient->IsJoinConnected(); i++) {
            pJoinClient->Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (!pJoinClient->IsJoinConnected()) {
            spdlog::error("JoinClient: failed to connect to {} after 10s", connectAddr);
        } else {
            spdlog::info("JoinClient: connected, sending auth request");
            pJoinClient->SendAuthRequest();
        }
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
                    // args start at payload offset 4: argCount(1) + _pad[3](3) = 4
                    memcpy(hookPkt.args, payload + 4, hookPkt.argCount * sizeof(uint64_t));
                }

                DispatchHookEvent(hookPkt, gameReader, pointerTable,
                                  weatherService, calendarService, questService, combatService,
                                  inventoryService, actorValueService, magicService, characterService);

                // Record hook event to binary file if --record is active
                if (recordFile) {
                    auto now = std::chrono::steady_clock::now().time_since_epoch();
                    uint64_t timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
                    fwrite(&timestampUs, sizeof(timestampUs), 1, recordFile);
                    fwrite(&hookPkt, sizeof(hookPkt), 1, recordFile);
                }
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

        // --- Phase 3.5: GameServer tick (host mode) / JoinClient tick (join mode) ---
        if (pGameServer) {
            pGameServer->Update();
        }
        if (pJoinClient) {
            pJoinClient->Update();
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

    // Shutdown JoinClient
    if (pJoinClient) {
        pJoinClient->Close();
        spdlog::info("JoinClient: received {} messages total", pJoinClient->GetMessagesReceived());
        pJoinClient.reset();
        spdlog::info("JoinClient shut down");
    }

    // Close record file
    if (recordFile) {
        fclose(recordFile);
        spdlog::info("Closed record file: {}", recordPath);
    }

    // Cleanup
    spdlog::info("Shutting down native client");
    tcpClient.Disconnect();
    memReader.Close();
    rpmalloc_finalize();

    return 0;
}
