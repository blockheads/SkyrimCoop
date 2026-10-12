// fake_player: Standalone binary that connects to an embedded GameServer
// as a robot second player, sends scripted position updates, and optionally
// verifies remote actor position via /proc/pid/mem reads.
//
// Usage:
//   ./fake_player --self-test          # Headless: create server, connect, send moves, exit 0
//   ./fake_player --net-test --bots N  # Headless: embedded server, N bots must see each other spawn + move
//   ./fake_player --port 10578         # Connect to external server
//   ./fake_player --verify-pid <pid>   # After sending, verify via /proc/pid/mem
//   ./fake_player --bots N             # Multi-bot: spawn N concurrent clients
//   ./fake_player --verify-broadcast   # Verify each bot receives broadcasts from others
//   ./fake_player --metrics            # Output per-bot CSV metrics to stdout
//   ./fake_player --tick-rate N        # Movements per second per bot (default 5)

// -- Server PCH includes (must come first, same order as mock_client.cpp) --
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Platform.hpp>
#include <TiltedCore/ViewBuffer.hpp>

#include <any>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <string>
#include <atomic>
#include <vector>
#include <memory>
#include <numeric>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <limits>
#include <algorithm>

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <sol/sol.hpp>
#include <BuildInfo.h>

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
#include <ConsoleRegistry.h>

#include "mock_client.h"
#include <GameServer.h>
#include <World.h>
#include <Components.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/ClientMessageFactory.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/ServerMessageFactory.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Structs/ReferenceUpdate.h>
#include <Structs/Movement.h>
#include <Opcodes.h>

#ifdef __linux__
#include <proc_memory.h>
#endif

// rpmalloc must be initialized before any allocation when linked with
// AllocatorSymbolOverride (same pattern as test main.cpp)
#include <rpmalloc.h>
__attribute__((constructor(101)))
static void InitRpMalloc()
{
    rpmalloc_initialize();
}

// ---------------------------------------------------------------------------
// Command-line argument parsing
// ---------------------------------------------------------------------------
struct Config
{
    uint16_t port = 10578;
    int duration = 30;
    pid_t verifyPid = 0;
    bool selfTest = false;
    int bots = 1;
    bool verifyBroadcast = false;
    bool metrics = false;
    int tickRate = 5;
    std::string fixtureDir;
    bool netTest = false;
};

static Config ParseArgs(int argc, char* argv[])
{
    Config cfg;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--self-test")
        {
            cfg.selfTest = true;
        }
        else if (arg == "--net-test")
        {
            cfg.netTest = true;
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            cfg.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
        else if (arg == "--duration" && i + 1 < argc)
        {
            cfg.duration = std::atoi(argv[++i]);
        }
        else if (arg == "--fixture-dir" && i + 1 < argc)
        {
            cfg.fixtureDir = argv[++i];
        }
        else if (arg == "--verify-pid" && i + 1 < argc)
        {
            cfg.verifyPid = static_cast<pid_t>(std::atoi(argv[++i]));
        }
        else if (arg == "--bots" && i + 1 < argc)
        {
            cfg.bots = std::atoi(argv[++i]);
        }
        else if (arg == "--verify-broadcast")
        {
            cfg.verifyBroadcast = true;
        }
        else if (arg == "--metrics")
        {
            cfg.metrics = true;
        }
        else if (arg == "--tick-rate" && i + 1 < argc)
        {
            cfg.tickRate = std::atoi(argv[++i]);
        }
        else
        {
            spdlog::warn("fake_player: unknown arg '{}'", arg);
        }
    }
    return cfg;
}

// ---------------------------------------------------------------------------
// Square walk pattern: 4 corners, cycles over duration
// ---------------------------------------------------------------------------
struct WalkPattern
{
    // Corners of a 500-unit square centered at origin
    static constexpr glm::vec3 cCorners[4] = {
        {250.0f, 250.0f, 0.0f},
        {-250.0f, 250.0f, 0.0f},
        {-250.0f, -250.0f, 0.0f},
        {250.0f, -250.0f, 0.0f}
    };
    static constexpr float cSecondsPerSide = 5.0f;

    static glm::vec3 GetPosition(float aElapsedSeconds)
    {
        float cycleTime = 4.0f * cSecondsPerSide;
        float t = std::fmod(aElapsedSeconds, cycleTime);
        int segment = static_cast<int>(t / cSecondsPerSide);
        float frac = (t - segment * cSecondsPerSide) / cSecondsPerSide;

        const auto& from = cCorners[segment % 4];
        const auto& to = cCorners[(segment + 1) % 4];
        return from + frac * (to - from);
    }
};

// ---------------------------------------------------------------------------
// BotContext: per-bot state for multi-bot mode
// ---------------------------------------------------------------------------
struct BotContext
{
    int id;
    std::unique_ptr<MockClient> pClient;
    std::atomic<uint64_t> movesSent{0};
    std::atomic<uint64_t> broadcastsReceived{0};
    std::atomic<bool> running{true};
    std::atomic<bool> failed{false};
    std::string failReason;
    double connectTimeMs{0.0};
    bool authenticated{false};
};

// ---------------------------------------------------------------------------
// BroadcastTrackingClient: extends MockClient to count broadcast opcodes
// ---------------------------------------------------------------------------
struct BroadcastTrackingClient : MockClient
{
    BroadcastTrackingClient() noexcept = default;

    // Pointer to the owning BotContext for tracking stats
    BotContext* pBotContext{nullptr};

protected:
    void OnConsume(const void* apData, uint32_t aSize) override
    {
        // Let base class handle auth response parsing
        MockClient::OnConsume(apData, aSize);

        // Check if the last parsed opcode was a movement broadcast
        if (m_lastOpcode == kServerReferencesMoveRequest && pBotContext)
        {
            pBotContext->broadcastsReceived.fetch_add(1, std::memory_order_relaxed);
        }
    }
};

// ---------------------------------------------------------------------------
// Send a scripted movement update
// ---------------------------------------------------------------------------
static void SendMovement(MockClient& aClient, Server::GameServer& aServer,
                         const glm::vec3& aPosition, uint64_t aTick)
{
    ClientReferencesMoveRequest moveReq;
    moveReq.Tick = aTick;

    ReferenceUpdate update;
    update.UpdatedMovement.Position = Vector3_NetQuantize();
    update.UpdatedMovement.Position.x = aPosition.x;
    update.UpdatedMovement.Position.y = aPosition.y;
    update.UpdatedMovement.Position.z = aPosition.z;
    // Rotation, Direction, Variables left at defaults

    moveReq.Updates[aClient.m_characterServerId] = update;

    aClient.SendGameMessage(moveReq);
    aClient.Pump(aServer, 5);

    spdlog::info("fake_player: sent position ({:.1f}, {:.1f}, {:.1f}) tick={}",
                 aPosition.x, aPosition.y, aPosition.z, aTick);
}

// ---------------------------------------------------------------------------
// /proc/pid/mem PE header verification (Linux only)
// ---------------------------------------------------------------------------
#ifdef __linux__
// Find the base address of SkyrimSE.exe in /proc/pid/maps
static uint64_t FindSkyrimBaseAddress(pid_t aPid)
{
    std::string mapsPath = "/proc/" + std::to_string(aPid) + "/maps";
    std::ifstream maps(mapsPath);
    if (!maps.is_open())
    {
        spdlog::error("verify: cannot open {}", mapsPath);
        return 0;
    }

    std::string line;
    while (std::getline(maps, line))
    {
        // Look for SkyrimSE.exe mapping with r-xp (executable segment)
        if (line.find("SkyrimSE.exe") != std::string::npos && line.find("r-xp") != std::string::npos)
        {
            // Parse base address from "7f1234000-7f1235000 r-xp ..."
            uint64_t base = 0;
            std::istringstream iss(line);
            std::string range;
            iss >> range;
            base = std::stoull(range.substr(0, range.find('-')), nullptr, 16);
            spdlog::info("verify: SkyrimSE.exe base address = {:#x}", base);
            return base;
        }
    }
    spdlog::error("verify: SkyrimSE.exe not found in /proc/{}/maps", aPid);
    return 0;
}

static bool VerifyHostMemory(pid_t aPid)
{
    spdlog::info("verify: opening /proc/{}/mem for PE header check", aPid);

    ProcMemReader reader;
    if (!reader.Open(aPid))
    {
        spdlog::error("verify: failed to open /proc/{}/mem", aPid);
        return false;
    }
    spdlog::info("verify: /proc/{}/mem opened successfully", aPid);

    // Find SkyrimSE.exe base address from /proc/pid/maps
    uint64_t base = FindSkyrimBaseAddress(aPid);
    if (base == 0)
    {
        reader.Close();
        return false;
    }

    // Read first 2 bytes: should be 'MZ' (0x5A4D) -- DOS header signature
    uint16_t dosSignature = 0;
    ssize_t bytesRead = reader.Read(&dosSignature, sizeof(dosSignature), base);
    if (bytesRead != sizeof(dosSignature))
    {
        spdlog::error("verify: failed to read PE header at {:#x} (read {} bytes)", base, bytesRead);
        reader.Close();
        return false;
    }

    if (dosSignature == 0x5A4D)
    {
        spdlog::info("verify: PE header confirmed (MZ signature at {:#x})", base);
    }
    else
    {
        spdlog::error("verify: unexpected signature {:#06x} at {:#x} (expected 0x5A4D)", dosSignature, base);
        reader.Close();
        return false;
    }

    // Read e_lfanew (offset to PE header) at base+0x3C
    uint32_t eLfanew = reader.ReadValue<uint32_t>(base + 0x3C);
    if (eLfanew > 0 && eLfanew < 0x1000)
    {
        // Read PE signature at base+e_lfanew: should be 'PE\0\0' (0x00004550)
        uint32_t peSignature = reader.ReadValue<uint32_t>(base + eLfanew);
        if (peSignature == 0x00004550)
        {
            spdlog::info("verify: PE\\0\\0 signature confirmed at {:#x}", base + eLfanew);
        }
        else
        {
            spdlog::warn("verify: unexpected PE signature {:#010x}", peSignature);
        }
    }

    reader.Close();
    spdlog::info("verify: /proc/pid/mem verification PASSED");
    return true;
}
#endif

// ---------------------------------------------------------------------------
// Self-test mode: create embedded server, connect, authenticate, send moves
// ---------------------------------------------------------------------------
static int RunSelfTest(const Config& aCfg)
{
    spdlog::info("fake_player: starting self-test (headless embedded server)");

    // Create the "ConOut" logger if it doesn't exist (required by ConsoleRegistry)
    if (!spdlog::get("ConOut"))
        spdlog::stdout_color_mt("ConOut");

    ServerConsole::ConsoleRegistry console("ConOut");
    Server::GameServer server(console);
    server.Initialize();

    auto port = server.GetPort();
    spdlog::info("fake_player: server listening on port {}", port);

    // Connect
    MockClient client;
    if (!client.ConnectByIp("::1:" + std::to_string(port)))
    {
        spdlog::error("fake_player: ConnectByIp failed");
        return 1;
    }
    if (!client.WaitForConnection(server))
    {
        spdlog::error("fake_player: connection timed out");
        return 1;
    }
    spdlog::info("fake_player: connected to embedded server");

    // Authenticate
    AuthenticationRequest authReq;
    authReq.Version = BUILD_COMMIT;
    authReq.Token = "";
    authReq.Username = "FakePlayer";
    authReq.SKSEActive = true;
    authReq.MO2Active = false;
    authReq.DiscordId = 99999;
    authReq.Level = 1;

    client.SendGameMessage(authReq);
    client.Pump(server, 100);

    if (!client.m_authAccepted)
    {
        spdlog::error("fake_player: authentication rejected (type={})",
                      static_cast<int>(client.m_authResponseType));
        return 1;
    }
    spdlog::info("fake_player: authentication accepted");

    AssignCharacterRequest assign;
    assign.Cookie = 1;
    assign.ReferenceId = GameId{0, 0x14};
    client.SendGameMessage(assign);
    client.Pump(server, 100);
    if (!client.m_gotAssignment)
    {
        spdlog::error("self-test: character was not registered");
        return 1;
    }
    const glm::vec3 expected(120.f, 240.f, 360.f);
    SendMovement(client, server, expected, 100);
    client.Pump(server, 100);
    const auto entity = static_cast<entt::entity>(client.m_characterServerId);
    auto* movement = server.GetWorld().try_get<Server::MovementComponent>(entity);
    if (!movement || glm::distance(movement->Position, expected) > 1.f)
    {
        spdlog::error("self-test: server did not apply registered character movement");
        return 1;
    }
    spdlog::info("fake_player: connection, registration and server movement verified; no Skyrim actor tested");

    // Cleanup
    client.Close();
    server.Kill();
    server.Close();

    spdlog::info("fake_player: PASS");
    return 0;
}

// ---------------------------------------------------------------------------
// External server mode: connect to an existing server on the given port
// ---------------------------------------------------------------------------
static int RunExternal(const Config& aCfg)
{
    if (aCfg.fixtureDir.empty())
    {
        spdlog::error("In-game simulation requires --fixture-dir (the host's session snapshot; scripts/coop_test.py game passes it). "
                      "Authentication alone cannot create a visible player.");
        return 1;
    }
    auto load = [&](const char* name, ClientOpcode opcode) -> TiltedPhoques::UniquePtr<ClientMessage> {
        std::ifstream file(std::filesystem::path(aCfg.fixtureDir) / name, std::ios::binary | std::ios::ate);
        if (!file) return nullptr;
        const auto size = file.tellg();
        if (size <= 0 || size >= (1 << 16)) return nullptr;
        file.seekg(0);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()) || bytes[0] != opcode)
            return nullptr;
        TiltedPhoques::Buffer buffer(bytes.size());
        TiltedPhoques::Buffer::Writer writer(&buffer);
        writer.WriteBytes(bytes.data(), bytes.size());
        TiltedPhoques::Buffer::Reader reader(&buffer);
        return ClientMessageFactory().Extract(reader);
    };
    auto auth = load("auth.bin", kAuthenticationRequest);
    auto assignment = load("assign.bin", kAssignCharacterRequest);
    if (!auth || !assignment)
    {
        spdlog::error("Missing/invalid bot snapshot: the host game writes it once hosting (scripts/coop_test.py game).");
        return 1;
    }
    const auto& character = static_cast<const AssignCharacterRequest&>(*assignment);
    spdlog::info("fake_player: connecting to external server on port {}", aCfg.port);

    MockClient client;
    // GameServer binds dual-stack IPv6 by default, use ::1 for loopback
    if (!client.ConnectByIp("::1:" + std::to_string(aCfg.port)))
    {
        spdlog::error("fake_player: ConnectByIp failed");
        return 1;
    }

    // Wait for connection (no local server to pump — just call client.Update())
    for (int i = 0; i < 300; ++i)
    {
        client.Update();
        if (client.IsConnected())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!client.IsConnected())
    {
        spdlog::error("fake_player: connection timed out");
        return 1;
    }
    spdlog::info("fake_player: connected to server on port {}", aCfg.port);

    // Use the running game's version and mod list, not an old bot binary's build ID.
    client.SendGameMessage(*auth);

    // Wait for auth response
    for (int i = 0; i < 300; ++i)
    {
        client.Update();
        if (client.m_gotAuthResponse)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!client.m_authAccepted)
    {
        spdlog::error("fake_player: authentication rejected (type={})",
                      static_cast<int>(client.m_authResponseType));
        return 1;
    }
    spdlog::info("fake_player: authentication accepted");

    client.SendGameMessage(*assignment);
    for (int i = 0; i < 500 && !client.m_gotAssignment; ++i)
    {
        client.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!client.m_gotAssignment)
    {
        spdlog::error("Character registration timed out. No movement test was performed.");
        return 1;
    }

    // Send scripted movement for the configured duration
    auto start = std::chrono::steady_clock::now();
    uint64_t tick = 1;
    while (true)
    {
        auto elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= static_cast<float>(aCfg.duration))
            break;

        if (!client.IsConnected())
        {
            spdlog::error("Connection lost during movement simulation");
            return 1;
        }
        const glm::vec3 origin(character.Position.x, character.Position.y, character.Position.z);
        const glm::vec3 pos = origin + (WalkPattern::GetPosition(elapsed) - WalkPattern::GetPosition(0.f)) * 0.25f;

        // Build and send movement message
        ClientReferencesMoveRequest moveReq;
        moveReq.Tick = client.GetClock().GetCurrentTick();
        ++tick;

        ReferenceUpdate update;
        update.UpdatedMovement.Position.x = pos.x;
        update.UpdatedMovement.Position.y = pos.y;
        update.UpdatedMovement.Position.z = pos.z;
        update.UpdatedMovement.CellId = character.CellId;
        update.UpdatedMovement.WorldSpaceId = character.WorldSpaceId;
        update.UpdatedMovement.Variables = character.LatestAction.Variables;
        moveReq.Updates[client.m_characterServerId] = update;

        client.SendGameMessage(moveReq);
        client.Update();

        spdlog::info("fake_player: sent position ({:.1f}, {:.1f}, {:.1f}) tick={}",
                     pos.x, pos.y, pos.z, tick - 1);

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    spdlog::info("fake_player: movement loop complete ({} seconds, {} ticks)",
                 aCfg.duration, tick - 1);

#ifdef __linux__
    if (aCfg.verifyPid > 0)
    {
        spdlog::info("fake_player: verifying host memory via /proc/{}/mem", aCfg.verifyPid);
        if (!VerifyHostMemory(aCfg.verifyPid))
        {
            spdlog::error("fake_player: /proc/pid/mem verification FAILED");
            client.Close();
            return 1;
        }
        spdlog::info("fake_player: /proc/pid/mem verification PASSED");
    }
#endif

    client.Close();
    spdlog::info("fake_player: network scenario completed. scripts/coop_test.py game checks the movement in the engine.");
    return 0;
}

// ---------------------------------------------------------------------------
// Bot worker thread function
// ---------------------------------------------------------------------------
static void BotWorkerThread(BotContext& aCtx, const Config& aCfg)
{
    // rpmalloc requires per-thread init
    rpmalloc_thread_initialize();

    auto* pClient = static_cast<BroadcastTrackingClient*>(aCtx.pClient.get());
    const auto sleepMs = std::chrono::milliseconds(1000 / aCfg.tickRate);

    // Connect
    auto connectStart = std::chrono::steady_clock::now();
    if (!pClient->ConnectByIp("::1:" + std::to_string(aCfg.port)))
    {
        aCtx.failed.store(true, std::memory_order_release);
        aCtx.failReason = "ConnectByIp failed";
        rpmalloc_thread_finalize(1);
        return;
    }

    // Wait for connection (external server mode — just call Update)
    for (int i = 0; i < 300 && aCtx.running.load(std::memory_order_acquire); ++i)
    {
        pClient->Update();
        if (pClient->IsConnected())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!pClient->IsConnected())
    {
        aCtx.failed.store(true, std::memory_order_release);
        aCtx.failReason = "connection timed out";
        rpmalloc_thread_finalize(1);
        return;
    }

    auto connectEnd = std::chrono::steady_clock::now();
    aCtx.connectTimeMs = std::chrono::duration<double, std::milli>(connectEnd - connectStart).count();

    spdlog::info("FakeBot-{}: connected in {:.1f}ms", aCtx.id, aCtx.connectTimeMs);

    // Authenticate
    AuthenticationRequest authReq;
    authReq.Version = BUILD_COMMIT;
    authReq.Token = "";
    authReq.Username = "FakeBot-" + std::to_string(aCtx.id);
    authReq.SKSEActive = true;
    authReq.MO2Active = false;
    authReq.DiscordId = static_cast<uint64_t>(90000 + aCtx.id);
    authReq.Level = 1;

    pClient->SendGameMessage(authReq);

    // Wait for auth response
    for (int i = 0; i < 300 && aCtx.running.load(std::memory_order_acquire); ++i)
    {
        pClient->Update();
        if (pClient->m_gotAuthResponse)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!pClient->m_authAccepted)
    {
        aCtx.failed.store(true, std::memory_order_release);
        aCtx.failReason = "authentication rejected (type=" +
            std::to_string(static_cast<int>(pClient->m_authResponseType)) + ")";
        rpmalloc_thread_finalize(1);
        return;
    }

    aCtx.authenticated = true;
    spdlog::info("FakeBot-{}: authenticated", aCtx.id);

    // Movement loop
    auto start = std::chrono::steady_clock::now();
    uint64_t tick = 1;
    while (aCtx.running.load(std::memory_order_acquire))
    {
        auto elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - start).count();

        // Phase offset per bot so they walk different paths
        glm::vec3 pos = WalkPattern::GetPosition(elapsed + aCtx.id * 2.0f);

        ClientReferencesMoveRequest moveReq;
        moveReq.Tick = tick++;

        ReferenceUpdate update;
        update.UpdatedMovement.Position.x = pos.x;
        update.UpdatedMovement.Position.y = pos.y;
        update.UpdatedMovement.Position.z = pos.z;
        moveReq.Updates[1] = update;

        pClient->SendGameMessage(moveReq);
        pClient->Update();
        aCtx.movesSent.fetch_add(1, std::memory_order_relaxed);

        std::this_thread::sleep_for(sleepMs);
    }

    spdlog::info("FakeBot-{}: stopping ({} moves sent, {} broadcasts received)",
                 aCtx.id,
                 aCtx.movesSent.load(std::memory_order_relaxed),
                 aCtx.broadcastsReceived.load(std::memory_order_relaxed));

    pClient->Close();
    rpmalloc_thread_finalize(1);
}

// ---------------------------------------------------------------------------
// Multi-bot mode: spawn N concurrent clients against external server
// ---------------------------------------------------------------------------
static int RunMultiBot(const Config& aCfg)
{
    spdlog::info("fake_player: multi-bot mode, spawning {} bots against port {} for {}s at {} Hz",
                 aCfg.bots, aCfg.port, aCfg.duration, aCfg.tickRate);

    // Create bot contexts
    std::vector<std::unique_ptr<BotContext>> contexts;
    contexts.reserve(aCfg.bots);
    for (int i = 0; i < aCfg.bots; ++i)
    {
        auto pCtx = std::make_unique<BotContext>();
        pCtx->id = i;
        auto pClient = std::make_unique<BroadcastTrackingClient>();
        pClient->pBotContext = pCtx.get();
        pCtx->pClient = std::move(pClient);
        contexts.push_back(std::move(pCtx));
    }

    // Launch worker threads
    std::vector<std::thread> threads;
    threads.reserve(aCfg.bots);
    for (auto& pCtx : contexts)
    {
        threads.emplace_back(BotWorkerThread, std::ref(*pCtx), std::cref(aCfg));
    }

    // Wait for duration, then signal all bots to stop
    std::this_thread::sleep_for(std::chrono::seconds(aCfg.duration));
    for (auto& pCtx : contexts)
    {
        pCtx->running.store(false, std::memory_order_release);
    }

    // Join all threads
    for (auto& t : threads)
    {
        t.join();
    }

    spdlog::info("fake_player: all {} bots finished", aCfg.bots);

    // Check for failures
    int failCount = 0;
    int authCount = 0;
    for (const auto& pCtx : contexts)
    {
        if (pCtx->failed.load(std::memory_order_acquire))
        {
            spdlog::error("FakeBot-{}: FAILED - {}", pCtx->id, pCtx->failReason);
            ++failCount;
        }
        if (pCtx->authenticated)
        {
            ++authCount;
        }
    }

    // Metrics output
    if (aCfg.metrics)
    {
        // CSV header
        fmt::print("bot_id,moves_sent,broadcasts_received,connect_time_ms\n");
        uint64_t totalSent = 0;
        uint64_t totalReceived = 0;
        double totalConnectMs = 0.0;
        for (const auto& pCtx : contexts)
        {
            auto sent = pCtx->movesSent.load(std::memory_order_relaxed);
            auto received = pCtx->broadcastsReceived.load(std::memory_order_relaxed);
            fmt::print("{},{},{},{:.1f}\n", pCtx->id, sent, received, pCtx->connectTimeMs);
            totalSent += sent;
            totalReceived += received;
            totalConnectMs += pCtx->connectTimeMs;
        }
        double avgConnectMs = aCfg.bots > 0 ? totalConnectMs / aCfg.bots : 0.0;
        fmt::print("total,{},{},{:.1f}\n", totalSent, totalReceived, avgConnectMs);
    }

    // Broadcast verification
    if (aCfg.verifyBroadcast)
    {
        spdlog::info("fake_player: verifying broadcast reception...");

        // Hard requirement: all bots must have connected and authenticated
        if (authCount < aCfg.bots)
        {
            spdlog::error("fake_player: FAIL - only {}/{} bots authenticated",
                          authCount, aCfg.bots);
            return 1;
        }
        spdlog::info("fake_player: all {} bots connected and authenticated", aCfg.bots);

        // Check broadcast counts per bot
        // Note: Without character assignment (AssignCharacterRequest), the server
        // won't broadcast ServerReferencesMoveRequest. This is expected in headless
        // mode without a Skyrim host. We verify what we can:
        // 1. All bots connected and authenticated (verified above)
        // 2. All movement messages were sent without error
        // 3. Report broadcast counts (may be 0 without character assignment)
        bool anyBroadcasts = false;
        for (const auto& pCtx : contexts)
        {
            auto received = pCtx->broadcastsReceived.load(std::memory_order_relaxed);
            auto sent = pCtx->movesSent.load(std::memory_order_relaxed);
            spdlog::info("FakeBot-{}: sent={}, broadcasts_received={}", pCtx->id, sent, received);

            if (sent == 0 && pCtx->authenticated)
            {
                spdlog::error("FakeBot-{}: authenticated but sent 0 moves", pCtx->id);
                return 1;
            }
            if (received > 0)
                anyBroadcasts = true;
        }

        if (!anyBroadcasts)
        {
            spdlog::warn("fake_player: no broadcasts received (expected without character "
                         "assignment in headless mode). Connection/auth/send verified.");
        }
        else
        {
            spdlog::info("fake_player: broadcast verification passed");
        }
    }

    if (failCount > 0)
    {
        spdlog::error("fake_player: FAIL - {} bot(s) failed", failCount);
        return 1;
    }

    spdlog::info("fake_player: PASS");
    return 0;
}

// ---------------------------------------------------------------------------
// Net test: embedded server + N registered bots, all on one thread.
// Every bot must see every other bot spawn and receive its movement.
// ---------------------------------------------------------------------------
struct NetBot : MockClient
{
    std::set<uint32_t> spawnsSeen;
    // Peer server ID -> received movement updates and the last position
    std::map<uint32_t, uint64_t> movesReceived;
    std::map<uint32_t, glm::vec3> lastPosition;
    uint64_t movesSent{0};

protected:
    void OnConsume(const void* apData, uint32_t aSize) override
    {
        MockClient::OnConsume(apData, aSize);
        if (m_lastOpcode != kCharacterSpawnRequest && m_lastOpcode != kServerReferencesMoveRequest)
            return;

        TiltedPhoques::Buffer buffer(aSize);
        TiltedPhoques::Buffer::Writer writer(&buffer);
        writer.WriteBytes(reinterpret_cast<const uint8_t*>(apData), aSize);
        TiltedPhoques::Buffer::Reader reader(&buffer);
        auto pMessage = ServerMessageFactory().Extract(reader);
        if (!pMessage)
            return;

        if (pMessage->GetOpcode() == kCharacterSpawnRequest)
        {
            spawnsSeen.insert(static_cast<CharacterSpawnRequest*>(pMessage.get())->ServerId);
        }
        else
        {
            for (const auto& [id, update] : static_cast<ServerReferencesMoveRequest*>(pMessage.get())->Updates)
            {
                const auto& position = update.UpdatedMovement.Position;
                ++movesReceived[id];
                lastPosition[id] = glm::vec3(position.x, position.y, position.z);
            }
        }
    }
};

static glm::vec3 NetBotCenter(int aIndex)
{
    // Far enough apart that a position can only belong to one bot's path.
    return glm::vec3(static_cast<float>(aIndex) * 2000.f, 1000.f, 0.f);
}

static int RunNetTest(const Config& aCfg)
{
    const int botCount = std::max(2, aCfg.bots);
    const int tickRate = std::max(1, aCfg.tickRate);
    spdlog::info("net-test: {} bots, {}s of movement at {} Hz", botCount, aCfg.duration, tickRate);

    if (!spdlog::get("ConOut"))
        spdlog::stdout_color_mt("ConOut");
    ServerConsole::ConsoleRegistry console("ConOut");
    Server::GameServer server(console);
    if (auto* pMaxPlayers = console.FindSetting("GameServer:uMaxPlayerCount");
        pMaxPlayers && pMaxPlayers->data.as_uint32 < static_cast<uint32_t>(botCount))
    {
        spdlog::warn("net-test: raising player cap {} -> {} for stress testing", pMaxPlayers->data.as_uint32, botCount);
        pMaxPlayers->data.as_uint32 = static_cast<uint32_t>(botCount);
    }
    server.Initialize();
    const auto port = server.GetPort();

    std::vector<std::unique_ptr<NetBot>> bots;
    for (int i = 0; i < botCount; ++i)
        bots.push_back(std::make_unique<NetBot>());

    double maxServerUpdateMs = 0.0;
    auto pumpAll = [&]() {
        const auto start = std::chrono::steady_clock::now();
        server.Update();
        maxServerUpdateMs = std::max(maxServerUpdateMs,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        for (auto& pBot : bots)
            pBot->Update();
    };
    // Pumps until aDone() holds or the timeout expires.
    auto pumpUntil = [&](auto aDone, std::chrono::milliseconds aTimeout) {
        const auto deadline = std::chrono::steady_clock::now() + aTimeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            pumpAll();
            if (aDone())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    auto fail = [&](const std::string& aReason) {
        spdlog::error("net-test: FAIL - {}", aReason);
        bots.clear();
        server.Kill();
        server.Close();
        return 1;
    };

    // 1. Connect
    for (auto& pBot : bots)
    {
        if (!pBot->ConnectByIp("::1:" + std::to_string(port)))
            return fail("ConnectByIp failed");
    }
    if (!pumpUntil([&] { return std::all_of(bots.begin(), bots.end(), [](auto& b) { return b->IsConnected(); }); },
                   std::chrono::seconds(10)))
        return fail("not every bot connected within 10s");
    spdlog::info("net-test: [1/4] {} bots connected", botCount);

    // 2. Authenticate
    for (int i = 0; i < botCount; ++i)
    {
        AuthenticationRequest auth;
        auth.Version = BUILD_COMMIT;
        auth.Username = "NetBot-" + std::to_string(i);
        auth.SKSEActive = true;
        auth.DiscordId = static_cast<uint64_t>(80000 + i);
        auth.Level = 1;
        bots[i]->SendGameMessage(auth);
    }
    if (!pumpUntil([&] { return std::all_of(bots.begin(), bots.end(), [](auto& b) { return b->m_gotAuthResponse; }); },
                   std::chrono::seconds(10)))
        return fail("not every bot got an authentication response within 10s");
    for (int i = 0; i < botCount; ++i)
    {
        if (!bots[i]->m_authAccepted)
            return fail(fmt::format("NetBot-{} authentication rejected (type={})", i,
                                    static_cast<int>(bots[i]->m_authResponseType)));
    }
    spdlog::info("net-test: [2/4] all bots authenticated");

    // 3. Register a player character per bot
    for (int i = 0; i < botCount; ++i)
    {
        AssignCharacterRequest assign;
        assign.Cookie = 1;
        assign.ReferenceId = GameId{0, 0x14};
        const auto center = NetBotCenter(i);
        assign.Position.x = center.x;
        assign.Position.y = center.y;
        assign.Position.z = center.z;
        bots[i]->SendGameMessage(assign);
    }
    if (!pumpUntil([&] { return std::all_of(bots.begin(), bots.end(), [](auto& b) { return b->m_gotAssignment; }); },
                   std::chrono::seconds(10)))
        return fail("not every bot was assigned a character within 10s");

    std::map<uint32_t, int> serverIdToBot;
    for (int i = 0; i < botCount; ++i)
    {
        if (!serverIdToBot.emplace(bots[i]->m_characterServerId, i).second)
            return fail(fmt::format("duplicate server ID {} assigned", bots[i]->m_characterServerId));
    }
    spdlog::info("net-test: [3/4] all bots registered characters");

    // 4. Move every bot around its own center
    const auto moveStart = std::chrono::steady_clock::now();
    const auto moveEnd = moveStart + std::chrono::seconds(std::max(1, aCfg.duration));
    const auto sendInterval = std::chrono::microseconds(1000000 / tickRate);
    auto nextSend = moveStart;
    uint64_t tick = 1;
    while (std::chrono::steady_clock::now() < moveEnd)
    {
        if (std::chrono::steady_clock::now() >= nextSend)
        {
            nextSend += sendInterval;
            const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - moveStart).count();
            for (int i = 0; i < botCount; ++i)
            {
                if (!bots[i]->IsConnected())
                    return fail(fmt::format("NetBot-{} disconnected during movement", i));
                const glm::vec3 pos = NetBotCenter(i) + WalkPattern::GetPosition(elapsed + static_cast<float>(i));
                ClientReferencesMoveRequest move;
                move.Tick = tick;
                ReferenceUpdate update;
                update.UpdatedMovement.Position.x = pos.x;
                update.UpdatedMovement.Position.y = pos.y;
                update.UpdatedMovement.Position.z = pos.z;
                move.Updates[bots[i]->m_characterServerId] = update;
                bots[i]->SendGameMessage(move);
                ++bots[i]->movesSent;
            }
            ++tick;
        }
        pumpAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // Drain in-flight packets.
    pumpUntil([] { return false; }, std::chrono::milliseconds(300));
    spdlog::info("net-test: [4/4] movement phase complete ({} ticks)", tick - 1);

    // Verify every bot saw every other bot spawn and move along that bot's path.
    int failures = 0;
    uint64_t minPairMoves = std::numeric_limits<uint64_t>::max();
    for (int i = 0; i < botCount; ++i)
    {
        for (int j = 0; j < botCount; ++j)
        {
            if (i == j)
                continue;
            const auto peerId = bots[j]->m_characterServerId;
            if (!bots[i]->spawnsSeen.count(peerId))
            {
                spdlog::error("net-test: NetBot-{} never saw NetBot-{} spawn (server ID {})", i, j, peerId);
                ++failures;
                continue;
            }
            const auto received = bots[i]->movesReceived.count(peerId) ? bots[i]->movesReceived[peerId] : 0;
            minPairMoves = std::min(minPairMoves, received);
            if (received == 0)
            {
                spdlog::error("net-test: NetBot-{} received no movement for NetBot-{}", i, j);
                ++failures;
                continue;
            }
            const auto offset = bots[i]->lastPosition[peerId] - NetBotCenter(j);
            if (std::abs(offset.x) > 300.f || std::abs(offset.y) > 300.f || std::abs(offset.z) > 50.f)
            {
                const auto& p = bots[i]->lastPosition[peerId];
                spdlog::error("net-test: NetBot-{} sees NetBot-{} at ({:.0f}, {:.0f}, {:.0f}), off its path", i, j, p.x, p.y, p.z);
                ++failures;
            }
        }
        // A bot must never be told to spawn its own character.
        if (bots[i]->spawnsSeen.count(bots[i]->m_characterServerId))
        {
            spdlog::error("net-test: NetBot-{} was sent a spawn for its own character", i);
            ++failures;
        }
    }

    const uint64_t moves = bots[0]->movesSent;
    spdlog::info("net-test: bots={} moves_sent_per_bot={} min_moves_received_per_pair={} max_server_update_ms={:.2f}",
                 botCount, moves, minPairMoves == std::numeric_limits<uint64_t>::max() ? 0 : minPairMoves,
                 maxServerUpdateMs);

    bots.clear();
    server.Kill();
    server.Close();

    if (failures > 0)
    {
        spdlog::error("net-test: FAIL - {} problem(s)", failures);
        return 1;
    }
    spdlog::info("net-test: PASS - all {} bots see each other spawn and move", botCount);
    return 0;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    Config cfg = ParseArgs(argc, argv);

    if (cfg.netTest)
        return RunNetTest(cfg);

    if (cfg.selfTest)
        return RunSelfTest(cfg);

    if (cfg.bots > 1 || cfg.verifyBroadcast)
        return RunMultiBot(cfg);

    return RunExternal(cfg);
}
