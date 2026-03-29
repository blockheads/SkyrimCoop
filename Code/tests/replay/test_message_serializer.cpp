#include <glm/glm.hpp>
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/ViewBuffer.hpp>

#include <catch2/catch.hpp>
#include <Recording/MessageSerializer.h>
#include <Recording/Base64.h>
#include <Messages/AuthenticationRequest.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>
#include <Opcodes.h>

TEST_CASE("Base64 round-trip encodes and decodes correctly", "[Enet6][Serializer]")
{
    const uint8_t data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64};
    auto encoded = Recording::Base64Encode(data, sizeof(data));
    REQUIRE(!encoded.empty());

    auto decoded = Recording::Base64Decode(encoded);
    REQUIRE(decoded.size() == sizeof(data));
    for (size_t i = 0; i < sizeof(data); ++i)
    {
        REQUIRE(decoded[i] == data[i]);
    }
}

TEST_CASE("Base64 empty input produces empty output", "[Enet6][Serializer]")
{
    auto encoded = Recording::Base64Encode(nullptr, 0);
    REQUIRE(encoded.empty());

    auto decoded = Recording::Base64Decode("");
    REQUIRE(decoded.empty());
}

TEST_CASE("ClientToJson for AuthenticationRequest produces correct JSON", "[Enet6][Serializer]")
{
    AuthenticationRequest msg;
    msg.Token = "test-token";

    auto j = MessageSerializer::ClientToJson(msg);
    REQUIRE(j["type"] == "AuthenticationRequest");
    REQUIRE(j["opcode"] == 0);
    REQUIRE(j.contains("payload"));
    REQUIRE(!j["payload"].get<std::string>().empty());
}

TEST_CASE("ServerToJson for AuthenticationResponse produces correct JSON", "[Enet6][Serializer]")
{
    AuthenticationResponse msg;
    msg.Type = AuthenticationResponse::ResponseType::kAccepted;

    auto j = MessageSerializer::ServerToJson(msg);
    REQUIRE(j["type"] == "AuthenticationResponse");
    REQUIRE(j["opcode"] == 0);
    REQUIRE(j.contains("payload"));
}

TEST_CASE("ClientFromJson round-trip preserves opcode", "[Enet6][Serializer]")
{
    AuthenticationRequest msg;
    msg.Token = "round-trip-test";

    auto j = MessageSerializer::ClientToJson(msg);
    auto binary = MessageSerializer::ClientFromJson(j);

    // binary should start with opcode byte 0 (kAuthenticationRequest)
    REQUIRE(!binary.empty());
    REQUIRE(binary[0] == static_cast<uint8_t>(kAuthenticationRequest));

    // Feed into factory Extract to verify it can parse
    TiltedPhoques::ViewBuffer buf(binary.data(), binary.size());
    TiltedPhoques::Buffer::Reader reader(&buf);
    ClientMessageFactory factory;
    auto pMessage = factory.Extract(reader);
    REQUIRE(pMessage != nullptr);
    REQUIRE(pMessage->GetOpcode() == kAuthenticationRequest);
}

TEST_CASE("GetClientMessageName returns AuthenticationRequest for opcode 0", "[Enet6][Serializer]")
{
    auto name = MessageSerializer::GetClientMessageName(kAuthenticationRequest);
    REQUIRE(name == "AuthenticationRequest");
}

TEST_CASE("GetServerMessageName returns AuthenticationResponse for opcode 0", "[Enet6][Serializer]")
{
    auto name = MessageSerializer::GetServerMessageName(kAuthenticationResponse);
    REQUIRE(name == "AuthenticationResponse");
}

TEST_CASE("GetClientOpcodeByName returns correct opcode for AssignCharacterRequest", "[Enet6][Serializer]")
{
    auto opcode = MessageSerializer::GetClientOpcodeByName("AssignCharacterRequest");
    REQUIRE(opcode == kAssignCharacterRequest);
}

TEST_CASE("All client opcodes have non-empty names", "[Enet6][Serializer]")
{
    for (int i = 0; i < static_cast<int>(kClientOpcodeMax); ++i)
    {
        auto name = MessageSerializer::GetClientMessageName(static_cast<ClientOpcode>(i));
        INFO("Client opcode " << i << " has no registered name");
        REQUIRE(!name.empty());
    }
}

TEST_CASE("All server opcodes have non-empty names", "[Enet6][Serializer]")
{
    for (int i = 0; i < static_cast<int>(kServerOpcodeMax); ++i)
    {
        auto name = MessageSerializer::GetServerMessageName(static_cast<ServerOpcode>(i));
        INFO("Server opcode " << i << " has no registered name");
        REQUIRE(!name.empty());
    }
}
