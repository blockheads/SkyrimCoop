#include "ServerMessageRouter.h"

#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>

#include <optional>

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>

using TiltedPhoques::UniquePtr;

#include <Messages/ServerMessageFactory.h>
#include <Messages/AuthenticationResponse.h>

using TiltedPhoques::Buffer;

void ServerMessageRouter::OnServerMessage(const void* apData, uint32_t aSize)
{
    if (aSize <= 1)
    {
        spdlog::warn("ServerMessageRouter: message too small ({} bytes)", aSize);
        return;
    }

    // Skip first byte (packet type), same as Server::HandleMessage strips pData[0]
    const auto* pPayload = static_cast<const uint8_t*>(apData) + 1;
    const uint32_t payloadSize = aSize - 1;

    // Copy into a Buffer for the reader (same pattern as MockClient::OnConsume)
    Buffer buffer(payloadSize);
    Buffer::Writer writer(&buffer);
    writer.WriteBytes(pPayload, payloadSize);

    Buffer::Reader reader(&buffer);

    ServerMessageFactory factory;
    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::warn("ServerMessageRouter: failed to extract server message");
        return;
    }

    m_lastOpcode = pMessage->GetOpcode();

    // Handle AuthenticationResponse
    if (pMessage->GetOpcode() == kAuthenticationResponse)
    {
        auto* pAuthResponse = static_cast<AuthenticationResponse*>(pMessage.get());
        m_authenticated = (pAuthResponse->Type == AuthenticationResponse::ResponseType::kAccepted);

        if (m_onAuth)
        {
            m_onAuth(m_authenticated, pAuthResponse->PlayerId);
        }

        spdlog::info("ServerMessageRouter: auth response, type={}, accepted={}",
                     static_cast<int>(pAuthResponse->Type), m_authenticated);
    }
}

void ServerMessageRouter::SetAuthCallback(AuthCallback aCb)
{
    m_onAuth = std::move(aCb);
}

void ServerMessageRouter::SetMoveCallback(MoveCallback aCb)
{
    m_onMove = std::move(aCb);
}
