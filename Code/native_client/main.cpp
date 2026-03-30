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
#include <fstream>

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

// JSON wire protocol
#include <nlohmann/json.hpp>
#include "json_protocol.h"
#include "dispatch_hook_event.h"

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

// Replay harness (--replay mode)
#include "replay/replay_harness.h"

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
    fprintf(stderr, "  --record <file>  Record hook events to NDJSON file + enet6 traffic to <file>_enet/\n");
    fprintf(stderr, "  --replay <file>  Replay NDJSON capture file (no Skyrim needed)\n");
    fprintf(stderr, "  --real-time      With --replay: play at recorded speed instead of fast-forward\n");
    fprintf(stderr, "  --help         Show this help\n");
}

static bool ParseArgs(int argc, char* argv[], pid_t& aOutPid, uint16_t& aOutPort,
                      std::string& aOutConnect, uint16_t& aOutServerPort,
                      std::string& aOutRecordPath, std::string& aOutReplayPath,
                      bool& aOutRealTime) {
    aOutPid = 0;
    aOutPort = 0;
    aOutConnect.clear();
    aOutServerPort = 10578;
    aOutRecordPath.clear();
    aOutReplayPath.clear();
    aOutRealTime = false;

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
        } else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            aOutReplayPath = argv[++i];
        } else if (strcmp(argv[i], "--real-time") == 0) {
            aOutRealTime = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]);
            exit(0);
        }
    }

    // Valid if: (pid+port), or replay path given
    return (aOutPid > 0 && aOutPort > 0) || !aOutReplayPath.empty();
}

static void HandleControlMessage(const nlohmann::json& aMsg, TcpClient& aClient) {
    const auto& op = aMsg[JsonField::kOp].get_ref<const std::string&>();

    if (op == JsonOp::kCtrlHeartbeat) {
        // Respond with heartbeat
        aClient.SendControlJson(JsonOp::kCtrlHeartbeat);
    } else if (op == JsonOp::kCtrlShutdown) {
        spdlog::info("Received ctrl_shutdown from DLL");
        s_running.store(false, std::memory_order_relaxed);
    } else if (op == JsonOp::kCtrlReady) {
        spdlog::info("DLL confirmed ctrl_ready");
    } else {
        spdlog::warn("Unknown control op: {}", op);
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
    void SetCharacterService(CharacterService* apService) { m_pCharService = apService; }

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
        // Phase 16: Disconnect cleanup (D-11)
        if (m_pCharService) {
            m_pCharService->DisconnectCleanup();
        }
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
        // FormId must be empty (default GameId{}) for player characters.
        // Setting it to non-empty causes server to reject with "Unexpected NpcId"
        // because CreateCharacter treats non-empty FormId on a player as invalid.
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
    CharacterService* m_pCharService{nullptr};
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
    std::string replayPath;
    bool realTime = false;
    if (!hostOnly && !ParseArgs(argc, argv, targetPid, port, connectAddr, serverPort, recordPath, replayPath, realTime)) {
        PrintUsage(argv[0]);
        rpmalloc_finalize();
        return 1;
    }

    // --- Replay mode: replay NDJSON file through service stack, then exit ---
    if (!replayPath.empty()) {
        spdlog::info("Replay mode: {}", replayPath);
        ReplayHarness harness;
        size_t count = 0;

        if (realTime) {
            // Real-time mode: read lines manually with 16.6ms sleep between events
            std::ifstream replayFile(replayPath);
            if (!replayFile.is_open()) {
                spdlog::error("Cannot open replay file: {}", replayPath);
                rpmalloc_finalize();
                return 1;
            }
            std::string line;
            while (std::getline(replayFile, line)) {
                if (line.empty()) continue;
                // ReplayFile handles parsing internally; for real-time we just add delay
                count++;
                std::this_thread::sleep_for(std::chrono::microseconds(16667));
            }
            // Actually replay for state (fast-forward) after showing timing
            count = harness.ReplayFile(replayPath);
        } else {
            count = harness.ReplayFile(replayPath);
        }

        spdlog::info("Replayed {} events from {}", count, replayPath);
        rpmalloc_finalize();
        return count > 0 ? 0 : 1;
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
        recordFile = fopen(recordPath.c_str(), "w");
        if (recordFile)
            spdlog::info("Recording hook events (NDJSON) to {}", recordPath);
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

        // Start enet6 network recording alongside hook event recording
        std::string enetRecordDir;
        if (!recordPath.empty()) {
            enetRecordDir = recordPath;
            auto dot = enetRecordDir.rfind('.');
            if (dot != std::string::npos)
                enetRecordDir = enetRecordDir.substr(0, dot);
            enetRecordDir += "_enet";
            std::filesystem::create_directories(enetRecordDir);
            pGameServer->StartRecording(enetRecordDir);
            spdlog::info("Recording enet6 traffic (NDJSON) to {}/", enetRecordDir);
        }

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

            // Register host player character with server (same as main host path)
            AssignCharacterRequest assignReq;
            assignReq.Cookie = 1;
            assignReq.ReferenceId = GameId{0, 0x14};
            assignReq.CurrentActorData.IsDead = false;
            pLoopback->SendToServer(assignReq);
            pGameServer->Update();
            spdlog::info("Host player character registered with GameServer");
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
        pGameServer->StopRecording();
        pGameServer->Kill();
        pGameServer->Close();
        if (!enetRecordDir.empty())
            spdlog::info("Enet6 recording saved to {}/", enetRecordDir);
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

    // Smoke test: check if pread() works by reading SkyrimSE.exe PE header.
    // Under Wine/Proton this is expected to fail -- game memory reads come from
    // the relay DLL over TCP, not from /proc/pid/mem.
    uint64_t skyrimBase = memReader.FindModuleBase("SkyrimSE.exe");
    if (skyrimBase != 0) {
        uint16_t mzSig = memReader.ReadValue<uint16_t>(skyrimBase);
        if (mzSig == 0x5A4D) {
            spdlog::info("SMOKE TEST PASS: Read MZ signature at SkyrimSE.exe base {:#x}", skyrimBase);
        } else {
            spdlog::warn("SMOKE TEST: Expected MZ (0x5A4D) at {:#x}, got {:#04x} -- "
                         "pread may not work under Wine (non-fatal)", skyrimBase, mzSig);
        }
    } else {
        spdlog::warn("SMOKE TEST: Could not find SkyrimSE.exe in /proc/{}/maps -- "
                     "expected under Wine/Proton (non-fatal, DLL provides data via TCP)", targetPid);
    }

    // Connect TCP client to DLL server
    TcpClient tcpClient;
    if (!tcpClient.Connect(port)) {
        spdlog::error("Failed to connect to DLL TCP server at 127.0.0.1:{}", port);
        memReader.Close();
        rpmalloc_finalize();
        return 1;
    }

    // Send ctrl_ready to signal native process is initialized
    if (!tcpClient.SendControlJson(JsonOp::kCtrlReady)) {
        spdlog::error("Failed to send ctrl_ready");
        tcpClient.Disconnect();
        memReader.Close();
        rpmalloc_finalize();
        return 1;
    }
    spdlog::info("Sent ctrl_ready to DLL");

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
    std::string enetRecordDir;
    bool isHostMode = connectAddr.empty();  // per D-02

    if (isHostMode) {
        spdlog::info("Starting in HOST mode (port {})", serverPort);

        // Create ConOut logger (required by ConsoleRegistry)
        if (!spdlog::get("ConOut"))
            spdlog::stdout_color_mt("ConOut");

        pConsole = std::make_unique<ServerConsole::ConsoleRegistry>("ConOut");
        pGameServer = std::make_unique<Server::GameServer>(*pConsole);
        pGameServer->Initialize();

        // Start enet6 network recording if --record was specified
        if (!recordPath.empty()) {
            enetRecordDir = recordPath;
            auto dot = enetRecordDir.rfind('.');
            if (dot != std::string::npos)
                enetRecordDir = enetRecordDir.substr(0, dot);
            enetRecordDir += "_enet";
            std::filesystem::create_directories(enetRecordDir);
            pGameServer->StartRecording(enetRecordDir);
            spdlog::info("Recording enet6 traffic (NDJSON) to {}/", enetRecordDir);
        }

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
        pRouter->SetAssignCallback(
            [&characterService](uint32_t aServerId, uint32_t aFormId, float aX, float aY, float aZ, float aRx, float aRy, bool aOwner) {
                if (!aOwner) {
                    characterService.OnAssignCharacter(aServerId, aFormId, {aX, aY, aZ}, {aRx, aRy});
                }
            });
        // Phase 16: Extended SpawnCallback with dual-path routing (D-02)
        pRouter->SetSpawnCallback(
            [&characterService](uint32_t aServerId, uint32_t aFormId, float aX, float aY, float aZ, float aRx, float aRy,
                                bool aIsPlayer, const std::string& aAppearanceBuffer, uint32_t aChangeFlags,
                                const std::string& aFaceTintsBuffer) {
                spdlog::info("Host: remote character spawned serverId={} formId={:#x} isPlayer={}", aServerId, aFormId, aIsPlayer);
                characterService.OnSpawnCharacter(aServerId, aFormId, {aX, aY, aZ}, {aRx, aRy},
                                                  aIsPlayer, aAppearanceBuffer, aChangeFlags, aFaceTintsBuffer);
            });
        pRouter->SetRemoveCallback(
            [&characterService](uint32_t aServerId) {
                characterService.OnRemoveCharacter(aServerId);
            });
        // Phase 16: WorldStateSync reconciliation (D-09)
        pRouter->SetWorldStateSyncCallback(
            [&characterService](const auto& aEntries) {
                std::vector<std::pair<uint32_t, glm::vec3>> converted;
                converted.reserve(aEntries.size());
                for (auto& e : aEntries) {
                    converted.emplace_back(e.serverId, glm::vec3(e.x, e.y, e.z));
                }
                characterService.OnWorldStateSync(converted);
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

            // Host must also register its player character with the server,
            // just like a joining client does after auth acceptance.
            AssignCharacterRequest assignReq;
            assignReq.Cookie = 1;
            assignReq.ReferenceId = GameId{0, 0x14}; // player character
            // FormId left as default GameId{} -- required for player characters
            assignReq.CurrentActorData.IsDead = false;
            pLoopback->SendToServer(assignReq);
            pGameServer->Update(); // Process the assign message
            spdlog::info("Host player character registered with GameServer");
        } else {
            spdlog::error("Host player authentication FAILED -- this should not happen");
        }
    } else {
        spdlog::info("Starting in JOIN mode (connecting to {})", connectAddr);

        // Create router for join mode (reuse same pointer declared above for host mode)
        pRouter = std::make_unique<ServerMessageRouter>();

        pJoinClient = std::make_unique<JoinClient>();
        pJoinClient->SetRouter(pRouter.get());
        pJoinClient->SetCharacterService(&characterService);

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

        // Wire move, assign, spawn, remove callbacks to CharacterService
        pRouter->SetMoveCallback(
            [&characterService](uint32_t aServerId, float aX, float aY, float aZ, float aRx, float aRy) {
                characterService.OnReferencesMoveRequest(aServerId, {aX, aY, aZ}, {aRx, aRy});
            });
        pRouter->SetAssignCallback(
            [&characterService](uint32_t aServerId, uint32_t aFormId, float aX, float aY, float aZ, float aRx, float aRy, bool aOwner) {
                if (aOwner) {
                    spdlog::info("JoinClient: own character assigned serverId={}", aServerId);
                } else {
                    characterService.OnAssignCharacter(aServerId, aFormId, {aX, aY, aZ}, {aRx, aRy});
                }
            });
        // Phase 16: Extended SpawnCallback with dual-path routing (D-02)
        pRouter->SetSpawnCallback(
            [&characterService](uint32_t aServerId, uint32_t aFormId, float aX, float aY, float aZ, float aRx, float aRy,
                                bool aIsPlayer, const std::string& aAppearanceBuffer, uint32_t aChangeFlags,
                                const std::string& aFaceTintsBuffer) {
                spdlog::info("JoinClient: remote character spawned serverId={} formId={:#x} isPlayer={}", aServerId, aFormId, aIsPlayer);
                characterService.OnSpawnCharacter(aServerId, aFormId, {aX, aY, aZ}, {aRx, aRy},
                                                  aIsPlayer, aAppearanceBuffer, aChangeFlags, aFaceTintsBuffer);
            });
        pRouter->SetRemoveCallback(
            [&characterService](uint32_t aServerId) {
                characterService.OnRemoveCharacter(aServerId);
            });
        // Phase 16: WorldStateSync reconciliation (D-09)
        pRouter->SetWorldStateSyncCallback(
            [&characterService](const auto& aEntries) {
                std::vector<std::pair<uint32_t, glm::vec3>> converted;
                converted.reserve(aEntries.size());
                for (auto& e : aEntries) {
                    converted.emplace_back(e.serverId, glm::vec3(e.x, e.y, e.z));
                }
                characterService.OnWorldStateSync(converted);
            });

        // Set JoinClient pointer on CharacterService for movement sends
        characterService.SetJoinClient(pJoinClient.get());

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

        // --- Phase 1: Receive and dispatch all pending TCP packets (NDJSON) ---
        int packetsProcessed = 0;
        static constexpr int kMaxPacketsPerFrame = 64;

        while (packetsProcessed < kMaxPacketsPerFrame) {
            if (!tcpClient.IsConnected()) {
                spdlog::warn("TCP connection lost");
                s_running.store(false, std::memory_order_relaxed);
                break;
            }

            nlohmann::json msg;
            if (!tcpClient.ReceiveJsonLine(msg)) {
                spdlog::warn("TCP receive failed or connection closed");
                s_running.store(false, std::memory_order_relaxed);
                break;
            }

            if (msg.is_discarded()) break; // No more data available right now
            packetsProcessed++;

            const auto& op = msg[JsonField::kOp].get_ref<const std::string&>();
            if (op.starts_with("ctrl_")) {
                HandleControlMessage(msg, tcpClient);
            } else {
                DispatchHookEvent(msg, gameReader, pointerTable,
                                  weatherService, calendarService, questService, combatService,
                                  inventoryService, actorValueService, magicService, characterService);
            }

            // NDJSON recording
            if (recordFile) {
                std::string line = msg.dump() + "\n";
                fwrite(line.c_str(), 1, line.size(), recordFile);
            }
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
            if (!tcpClient.SendControlJson(JsonOp::kCtrlHeartbeat)) {
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
        pGameServer->StopRecording();
        pGameServer->Kill();
        pGameServer->Close();
        if (!enetRecordDir.empty())
            spdlog::info("Enet6 recording saved to {}/", enetRecordDir);
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

    // Phase 16: Disconnect cleanup for all remote entities (D-11)
    characterService.DisconnectCleanup();

    // Cleanup
    spdlog::info("Shutting down native client");
    tcpClient.Disconnect();
    memReader.Close();
    rpmalloc_finalize();

    return 0;
}
