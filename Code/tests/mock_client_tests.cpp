// Network integration tests: headless MockClient connects to embedded GameServer
// Validates enet6 loopback + authentication handshake on Linux without Skyrim/Wine (TEST-02)

// Replicate server PCH includes needed for GameServer instantiation
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

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>
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


#include <catch2/catch.hpp>
#include "mock_client.h"
#include "test_server_fixture.h"
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>

TEST_CASE("MockClient_ConnectsToEmbeddedServer", "[NetworkIntegration]")
{
    auto& server = TestFixture::GetSharedServer();

    MockClient client;
    auto port = server.GetPort();
    REQUIRE(client.ConnectByIp("::1:" + std::to_string(port)));
    REQUIRE(client.WaitForConnection(server));
    REQUIRE(client.IsConnected());
}

TEST_CASE("MockClient_AuthHandshake_Accepted", "[NetworkIntegration]")
{
    auto& server = TestFixture::GetSharedServer();

    MockClient client;
    auto port = server.GetPort();
    REQUIRE(client.ConnectByIp("::1:" + std::to_string(port)));
    REQUIRE(client.WaitForConnection(server));

    // Send authentication request with matching version
    AuthenticationRequest authReq;
    authReq.Version = BUILD_COMMIT;
    authReq.Token = "";
    authReq.Username = "TestPlayer";
    authReq.SKSEActive = true;
    authReq.MO2Active = false;
    authReq.DiscordId = 12345;
    authReq.Level = 1;

    client.SendGameMessage(authReq);
    client.Pump(server, 100);

    REQUIRE(client.m_gotAuthResponse);
    REQUIRE(client.m_authAccepted);
    REQUIRE(client.m_authResponseType == AuthenticationResponse::ResponseType::kAccepted);
}

TEST_CASE("MockClient_AuthHandshake_WrongVersion", "[NetworkIntegration]")
{
    auto& server = TestFixture::GetSharedServer();

    MockClient client;
    auto port = server.GetPort();
    REQUIRE(client.ConnectByIp("::1:" + std::to_string(port)));
    REQUIRE(client.WaitForConnection(server));

    // Send authentication request with wrong version
    AuthenticationRequest authReq;
    authReq.Version = "wrong_version_hash";
    authReq.Token = "";
    authReq.Username = "WrongVersionPlayer";
    authReq.SKSEActive = true;
    authReq.MO2Active = false;
    authReq.Level = 1;

    client.SendGameMessage(authReq);
    client.Pump(server, 100);

    // Server sends kWrongVersion response and immediately kicks.
    // The auth response may or may not arrive before the disconnect event.
    // Either way, the client should be disconnected.
    if (client.m_gotAuthResponse)
    {
        REQUIRE(client.m_authResponseType == AuthenticationResponse::ResponseType::kWrongVersion);
    }
    // Client should be disconnected (kicked) regardless
    REQUIRE_FALSE(client.m_authAccepted);
    REQUIRE_FALSE(client.m_connected);
}

TEST_CASE("MockClient_ServerPlayerCount", "[NetworkIntegration]")
{
    auto& server = TestFixture::GetSharedServer();

    // Count before this test's player
    uint32_t countBefore = server.GetWorld().GetPlayerManager().Count();

    MockClient client;
    auto port = server.GetPort();
    REQUIRE(client.ConnectByIp("::1:" + std::to_string(port)));
    REQUIRE(client.WaitForConnection(server));

    // Auth
    AuthenticationRequest authReq;
    authReq.Version = BUILD_COMMIT;
    authReq.Token = "";
    authReq.Username = "CountTestPlayer";
    authReq.SKSEActive = true;
    authReq.MO2Active = false;
    authReq.Level = 1;

    client.SendGameMessage(authReq);
    client.Pump(server, 100);

    REQUIRE(client.m_authAccepted);
    REQUIRE(server.GetWorld().GetPlayerManager().Count() > countBefore);
}

TEST_CASE("MockClient_IPv4MappedToIPv6_Connects", "[NetworkIntegration]")
{
    auto& server = TestFixture::GetSharedServer();

    // Server binds IPv6 (dual-stack). ConnectByIp should auto-map 127.0.0.1
    // to the IPv4-mapped IPv6 address ::ffff:127.0.0.1 so the connection succeeds.
    MockClient client;
    auto port = server.GetPort();
    REQUIRE(client.ConnectByIp("127.0.0.1:" + std::to_string(port)));
    REQUIRE(client.WaitForConnection(server));
    REQUIRE(client.IsConnected());
}
