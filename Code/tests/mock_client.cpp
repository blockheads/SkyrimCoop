#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Platform.hpp>

#include <chrono>
#include <optional>
#include <mutex>
#include <filesystem>
#include <thread>
#include <cstring>

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>
#include <sol/sol.hpp>
#include <BuildInfo.h>

#include <Client.hpp>
#include <Packet.hpp>

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

#include <Messages/ServerMessageFactory.h>
#include <Messages/AuthenticationResponse.h>
#include <Messages/AssignCharacterResponse.h>
#include <GameServer.h>

MockClient::MockClient() noexcept = default;

MockClient::~MockClient()
{
    // Close() must be called while MockClient's vtable is still intact,
    // otherwise Client::~Client() calls Close() which triggers OnDisconnected()
    // through the base class vtable (pure virtual in Client).
    Close();
}

void MockClient::SendGameMessage(const ClientMessage& acMessage)
{
    TiltedPhoques::Buffer buffer(1 << 16);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // First byte reserved for packet type
    acMessage.Serialize(writer);
    TiltedPhoques::PacketView packet(
        reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());
    Send(&packet);
}

bool MockClient::WaitForConnection(Server::GameServer& aServer, int aMaxIterations)
{
    for (int i = 0; i < aMaxIterations; ++i)
    {
        aServer.Update();
        Update();
        if (IsConnected())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void MockClient::Pump(Server::GameServer& aServer, int aIterations)
{
    for (int i = 0; i < aIterations; ++i)
    {
        aServer.Update();
        Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void MockClient::OnConsume(const void* apData, uint32_t aSize)
{
    // Parse incoming server messages using ServerMessageFactory
    TiltedPhoques::Buffer buffer(aSize);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBytes(reinterpret_cast<const uint8_t*>(apData), aSize);

    TiltedPhoques::Buffer::Reader reader(&buffer);

    ServerMessageFactory factory;
    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::warn("MockClient: failed to extract server message");
        return;
    }

    m_lastOpcode = pMessage->GetOpcode();

    if (pMessage->GetOpcode() == kAssignCharacterResponse)
    {
        auto* response = static_cast<AssignCharacterResponse*>(pMessage.get());
        if (response->Cookie == 1)
        {
            m_gotAssignment = true;
            m_characterServerId = response->ServerId;
            spdlog::info("MockClient: character registered as server ID {}", m_characterServerId);
        }
    }

    // Handle AuthenticationResponse specifically
    if (pMessage->GetOpcode() == kAuthenticationResponse)
    {
        auto* pAuthResponse = static_cast<AuthenticationResponse*>(pMessage.get());
        m_authResponseType = pAuthResponse->Type;
        m_authAccepted = (pAuthResponse->Type == AuthenticationResponse::ResponseType::kAccepted);
        m_gotAuthResponse = true;
        spdlog::info("MockClient: received auth response, type={}", static_cast<int>(pAuthResponse->Type));
    }
}

void MockClient::OnConnected()
{
    m_connected = true;
    spdlog::info("MockClient: connected to server");
}

void MockClient::OnDisconnected(EDisconnectReason aReason)
{
    m_connected = false;
    spdlog::info("MockClient: disconnected, reason={}", static_cast<int>(aReason));
}

void MockClient::OnUpdate()
{
    // Empty - no per-tick logic needed for tests
}
