// Include order: server PCH prerequisites first, then GameServer headers.
// GameServer.h includes World.h which needs entt, Stl.hpp, etc.
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Platform.hpp>

#include <any>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>
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

#include <GameServer.h>

#include "LoopbackTransport.h"

LoopbackTransport::LoopbackTransport(Server::GameServer& aServer)
    : m_server(aServer)
{
}

void LoopbackTransport::SendToServer(const ClientMessage& acMessage)
{
    TiltedPhoques::Buffer buffer(1 << 16);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // First byte reserved for packet type (matches MockClient pattern)
    acMessage.Serialize(writer);

    // Skip first byte: OnConsume expects data AFTER the packet-type byte
    // (Server::HandleMessage strips pData[0] before calling OnConsume)
    const auto* pRawData = buffer.GetWriteData();
    const auto totalSize = static_cast<uint32_t>(writer.Size());

    if (totalSize <= 1)
    {
        spdlog::error("LoopbackTransport: message too small ({} bytes)", totalSize);
        return;
    }

    m_server.InjectMessage(pRawData + 1, totalSize - 1, kHostConnectionId);
}

void LoopbackTransport::SetServerMessageCallback(ServerMessageCallback aCallback)
{
    m_onServerMessage = std::move(aCallback);
}

void LoopbackTransport::DeliverToHost(const void* apData, uint32_t aSize)
{
    if (m_onServerMessage)
    {
        m_onServerMessage(apData, aSize);
    }
    else
    {
        spdlog::warn("LoopbackTransport: no server message callback set, dropping message");
    }
}
