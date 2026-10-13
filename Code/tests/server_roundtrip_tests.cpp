// Message round-trip tests through real enet6 network stack (TEST-03)
// Validates: client serialize -> enet6 UDP -> server OnConsume -> factory Extract -> handler dispatch
// -> response serialize -> enet6 UDP -> client OnConsume -> factory Extract

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

TEST_CASE("RoundTrip_AuthenticationRequest", "[NetworkRoundTrip]")
{
    auto& server = TestFixture::GetSharedServer();

    MockClient client;
    auto port = server.GetPort();
    REQUIRE(client.ConnectByIp("::1:" + std::to_string(port)));
    REQUIRE(client.WaitForConnection(server));

    // Send AuthenticationRequest through actual enet6 network
    AuthenticationRequest authReq;
    authReq.Version = BUILD_COMMIT;
    authReq.Token = "";
    authReq.Username = "RoundTripPlayer";
    authReq.SKSEActive = true;
    authReq.MO2Active = false;
    authReq.DiscordId = 99999;
    authReq.Level = 5;

    client.SendGameMessage(authReq);
    client.Pump(server, 100);

    // Verify response was received and correctly deserialized through the full path.
    // Note: m_lastOpcode may not be kAuthenticationResponse because the server sends
    // additional messages after auth (StringCacheUpdate, NotifyPlayerJoined, etc.)
    REQUIRE(client.m_gotAuthResponse);
    REQUIRE(client.m_authAccepted);
}

TEST_CASE("RoundTrip_MessageSurvivesNetwork", "[NetworkRoundTrip]")
{
    auto& server = TestFixture::GetSharedServer();

    MockClient client;
    auto port = server.GetPort();
    REQUIRE(client.ConnectByIp("::1:" + std::to_string(port)));
    REQUIRE(client.WaitForConnection(server));

    // First authenticate
    AuthenticationRequest authReq;
    authReq.Version = BUILD_COMMIT;
    authReq.Token = "";
    authReq.Username = "SurvivalPlayer";
    authReq.SKSEActive = true;
    authReq.MO2Active = false;
    authReq.Level = 10;

    client.SendGameMessage(authReq);
    client.Pump(server, 100);
    REQUIRE(client.m_authAccepted);

    // After auth, server should have created the player. Verify the connection survived
    // the full round-trip: client serialize -> enet6 -> server OnConsume -> factory
    // -> handler dispatch -> response serialize -> enet6 -> client OnConsume -> factory
    REQUIRE(server.GetWorld().GetPlayerManager().Count() >= 1);
    REQUIRE(client.IsConnected());
}
