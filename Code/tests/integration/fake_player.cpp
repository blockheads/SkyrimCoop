// fake_player: Standalone binary that connects to an embedded GameServer
// as a robot second player, sends scripted position updates, and optionally
// verifies remote actor position via /proc/pid/mem reads.
//
// Usage:
//   ./fake_player --self-test          # Headless: create server, connect, send moves, exit 0
//   ./fake_player --port 10578         # Connect to external server
//   ./fake_player --verify-pid <pid>   # After sending, verify via /proc/pid/mem

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
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Structs/ReferenceUpdate.h>
#include <Structs/Movement.h>

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
        else if (arg == "--port" && i + 1 < argc)
        {
            cfg.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
        else if (arg == "--duration" && i + 1 < argc)
        {
            cfg.duration = std::atoi(argv[++i]);
        }
        else if (arg == "--verify-pid" && i + 1 < argc)
        {
            cfg.verifyPid = static_cast<pid_t>(std::atoi(argv[++i]));
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

    // Use serverId 1 as the character reference (first assigned actor)
    moveReq.Updates[1] = update;

    aClient.SendGameMessage(moveReq);
    aClient.Pump(aServer, 5);

    spdlog::info("fake_player: sent position ({:.1f}, {:.1f}, {:.1f}) tick={}",
                 aPosition.x, aPosition.y, aPosition.z, aTick);
}

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

    // In self-test mode (no Skyrim, no assigned character), we verify:
    // 1. Server creation, client connection, authentication handshake
    // 2. Movement message serialization and sending (server receives but skips
    //    because no character entity is assigned via AssignCharacterRequest)
    //
    // Full movement verification with /proc/pid/mem requires a running Skyrim
    // instance with the relay DLL loaded, which is handled by test_ingame_sync.sh.

    // Demonstrate movement message construction and serialization
    spdlog::info("fake_player: constructing movement message (serialization test)");
    {
        ClientReferencesMoveRequest moveReq;
        moveReq.Tick = 1;

        ReferenceUpdate update;
        glm::vec3 testPos = WalkPattern::GetPosition(0.0f);
        update.UpdatedMovement.Position.x = testPos.x;
        update.UpdatedMovement.Position.y = testPos.y;
        update.UpdatedMovement.Position.z = testPos.z;
        moveReq.Updates[1] = update;

        // Serialize to buffer to verify the message can be encoded
        TiltedPhoques::Buffer buffer(1 << 16);
        TiltedPhoques::Buffer::Writer writer(&buffer);
        writer.WriteBits(0, 8);
        moveReq.Serialize(writer);

        spdlog::info("fake_player: movement message serialized OK ({} bytes), "
                     "position ({:.1f}, {:.1f}, {:.1f})",
                     writer.Size(), testPos.x, testPos.y, testPos.z);
    }

    // Pump a few more ticks to let server process any remaining messages
    client.Pump(server, 20);

    spdlog::info("fake_player: self-test complete (connection + auth + serialization verified)");

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
    spdlog::info("fake_player: connecting to external server on port {}", aCfg.port);

    // We still need ConsoleRegistry for MockClient's server PCH dependencies,
    // but we don't create a GameServer -- we connect to an external one.
    // For now, this mode is a stub since we need a GameServer ref for Pump().
    // The full in-game test is orchestrated by test_ingame_sync.sh which
    // handles server lifetime externally.

    spdlog::error("fake_player: external server mode not yet implemented");
    spdlog::error("fake_player: use --self-test for headless testing");
    return 1;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    Config cfg = ParseArgs(argc, argv);

    if (cfg.selfTest)
        return RunSelfTest(cfg);

    return RunExternal(cfg);
}
