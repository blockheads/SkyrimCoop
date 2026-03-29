// Server PCH prerequisites (same as other test files that use encoding)
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Platform.hpp>

#include <optional>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <catch2/catch.hpp>

#include <Messages/AssignCharacterResponse.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/NotifyRemoveCharacter.h>

#include "ServerMessageRouter.h"

// Helper: serialize a server message with the leading prefix byte
// that ServerMessageRouter::OnServerMessage expects.
template <typename T>
static std::vector<uint8_t> SerializeWithPrefix(const T& aMsg)
{
    TiltedPhoques::Buffer buf(1 << 16);
    TiltedPhoques::Buffer::Writer writer(&buf);
    // Prefix byte (packet type 0)
    writer.WriteBits(0, 8);
    aMsg.Serialize(writer);

    const uint32_t totalSize = writer.Size();
    std::vector<uint8_t> out(totalSize);
    std::memcpy(out.data(), buf.GetWriteData(), totalSize);
    return out;
}

TEST_CASE("ServerMessageRouter routes AssignCharacterResponse", "[router][assign]")
{
    ServerMessageRouter router;

    bool called = false;
    uint32_t capturedServerId = 0;
    bool capturedOwner = false;

    router.SetAssignCallback(
        [&](uint32_t aServerId, uint32_t /*aFormId*/, float /*aX*/, float /*aY*/, float /*aZ*/,
            float /*aRx*/, float /*aRy*/, bool aOwner) {
            called = true;
            capturedServerId = aServerId;
            capturedOwner = aOwner;
        });

    AssignCharacterResponse msg;
    msg.Cookie = 42;
    msg.ServerId = 777;
    msg.PlayerId = 1;
    msg.Position.x = 100.0f;
    msg.Position.y = 200.0f;
    msg.Position.z = 300.0f;
    msg.Owner = true;

    auto data = SerializeWithPrefix(msg);
    router.OnServerMessage(data.data(), static_cast<uint32_t>(data.size()));

    REQUIRE(called);
    REQUIRE(capturedServerId == 777);
    REQUIRE(capturedOwner == true);
}

TEST_CASE("ServerMessageRouter routes CharacterSpawnRequest", "[router][spawn]")
{
    ServerMessageRouter router;

    bool called = false;
    uint32_t capturedServerId = 0;
    uint32_t capturedFormId = 0;

    router.SetSpawnCallback(
        [&](uint32_t aServerId, uint32_t aFormId, float /*aX*/, float /*aY*/, float /*aZ*/,
            float /*aRx*/, float /*aRy*/) {
            called = true;
            capturedServerId = aServerId;
            capturedFormId = aFormId;
        });

    CharacterSpawnRequest msg;
    msg.ServerId = 555;
    msg.FormId = GameId{0, 0x14};
    msg.BaseId = GameId{0, 0x7};
    msg.CellId = GameId{0, 0x3C};
    msg.Position.x = 10.0f;
    msg.Position.y = 20.0f;
    msg.Position.z = 30.0f;
    msg.Rotation.x = 0.5f;
    msg.Rotation.y = 1.0f;
    msg.IsPlayer = true;

    auto data = SerializeWithPrefix(msg);
    router.OnServerMessage(data.data(), static_cast<uint32_t>(data.size()));

    REQUIRE(called);
    REQUIRE(capturedServerId == 555);
    REQUIRE(capturedFormId == 0x14);
}

TEST_CASE("ServerMessageRouter routes NotifyRemoveCharacter", "[router][remove]")
{
    ServerMessageRouter router;

    bool called = false;
    uint32_t capturedServerId = 0;

    router.SetRemoveCallback(
        [&](uint32_t aServerId) {
            called = true;
            capturedServerId = aServerId;
        });

    NotifyRemoveCharacter msg;
    msg.ServerId = 999;

    auto data = SerializeWithPrefix(msg);
    router.OnServerMessage(data.data(), static_cast<uint32_t>(data.size()));

    REQUIRE(called);
    REQUIRE(capturedServerId == 999);
}
