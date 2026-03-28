// Loopback transport tests for P2P host mode (08-01)
// Validates: LoopbackTransport serializes ClientMessages -> GameServer::OnConsume -> handler dispatch
//            GameServer::Send routes host ConnectionId to loopback callback (not enet6)
//            ServerMessageRouter deserializes server responses from loopback bytes

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

#include <StringCache.h>

#include <catch2/catch.hpp>
#include "test_server_fixture.h"
#include <transport/LoopbackTransport.h>
#include <transport/ServerMessageRouter.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/NotifySettingsChange.h>

TEST_CASE("Loopback_AuthRoundTrip", "[Loopback]")
{
    auto& server = TestFixture::GetSharedServer();

    LoopbackTransport loopback(server);
    ServerMessageRouter router;

    // Wire: server sends to loopback -> router processes
    server.SetLoopbackRoute(
        LoopbackTransport::kHostConnectionId,
        [&router](const void* apData, uint32_t aSize) {
            router.OnServerMessage(apData, aSize);
        });

    loopback.SetServerMessageCallback(
        [&router](const void* apData, uint32_t aSize) {
            router.OnServerMessage(apData, aSize);
        });

    // Create AuthenticationRequest matching server's expected version
    AuthenticationRequest authReq;
    authReq.Version = BUILD_COMMIT;
    authReq.Token = "";
    authReq.Username = "HostTest";
    authReq.SKSEActive = true;
    authReq.MO2Active = false;
    authReq.DiscordId = 0;
    authReq.Level = 1;

    // Send through loopback (synchronous: InjectMessage -> OnConsume -> handler -> Send -> callback)
    loopback.SendToServer(authReq);

    // Auth response is sent synchronously during HandleAuthenticationRequest.
    // Note: the server sends additional messages after auth (StringCacheUpdate, etc.)
    // so GetLastOpcode() may not be kAuthenticationResponse. Check IsAuthenticated()
    // which is set specifically when the auth response is processed.
    REQUIRE(router.IsAuthenticated() == true);

    // Clean up loopback route
    server.SetLoopbackRoute(0, nullptr);
}

TEST_CASE("Loopback_SendRouting", "[Loopback]")
{
    auto& server = TestFixture::GetSharedServer();

    bool callbackFired = false;

    server.SetLoopbackRoute(
        LoopbackTransport::kHostConnectionId,
        [&callbackFired](const void*, uint32_t) {
            callbackFired = true;
        });

    // Send a simple server message to the loopback ConnectionId
    NotifySettingsChange settingsMsg;
    server.Send(LoopbackTransport::kHostConnectionId, settingsMsg);

    REQUIRE(callbackFired == true);

    // Clean up
    server.SetLoopbackRoute(0, nullptr);
}

TEST_CASE("Loopback_NonHostSkipsLoopback", "[Loopback]")
{
    auto& server = TestFixture::GetSharedServer();

    bool callbackFired = false;

    server.SetLoopbackRoute(
        LoopbackTransport::kHostConnectionId,
        [&callbackFired](const void*, uint32_t) {
            callbackFired = true;
        });

    // Send to a non-loopback ConnectionId -- should go to enet6 path (silently fail, no peer)
    NotifySettingsChange settingsMsg;
    server.Send(0x12345ULL, settingsMsg);

    REQUIRE(callbackFired == false);

    // Clean up
    server.SetLoopbackRoute(0, nullptr);
}

TEST_CASE("Loopback_ServerMessageRouter", "[Loopback]")
{
    ServerMessageRouter router;

    // Manually serialize an AuthenticationResponse with type kAccepted
    using TiltedPhoques::Buffer;

    AuthenticationResponse authResponse;
    authResponse.Type = AuthenticationResponse::ResponseType::kAccepted;
    authResponse.Version = BUILD_COMMIT;
    authResponse.PlayerId = 42;

    Buffer buffer(1 << 16);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Packet type byte (matches GameServer::Send format)
    authResponse.Serialize(writer);

    // Feed to router
    router.OnServerMessage(buffer.GetWriteData(), static_cast<uint32_t>(writer.Size()));

    REQUIRE(router.GetLastOpcode() == kAuthenticationResponse);
    REQUIRE(router.IsAuthenticated() == true);
}
