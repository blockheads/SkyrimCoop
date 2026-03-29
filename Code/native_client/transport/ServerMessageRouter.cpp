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
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/NotifyRemoveCharacter.h>

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
    // Handle ServerReferencesMoveRequest — route remote actor positions to CharacterService
    else if (pMessage->GetOpcode() == kServerReferencesMoveRequest)
    {
        auto* pMoveReq = static_cast<ServerReferencesMoveRequest*>(pMessage.get());
        if (m_onMove)
        {
            for (auto& [serverId, update] : pMoveReq->Updates)
            {
                auto& pos = update.UpdatedMovement.Position;
                auto& rot = update.UpdatedMovement.Rotation;
                m_onMove(serverId, pos.x, pos.y, pos.z, rot.x, rot.y);
            }
        }
    }
    // Handle AssignCharacterResponse — server assigns a character to this player
    else if (pMessage->GetOpcode() == kAssignCharacterResponse)
    {
        auto* pAssign = static_cast<AssignCharacterResponse*>(pMessage.get());
        spdlog::info("ServerMessageRouter: AssignCharacterResponse serverId={}, owner={}",
                     pAssign->ServerId, pAssign->Owner);
        if (m_onAssign)
        {
            m_onAssign(pAssign->ServerId, 0 /* formId not in response */,
                       pAssign->Position.x, pAssign->Position.y, pAssign->Position.z,
                       0.0f, 0.0f, pAssign->Owner);
        }
    }
    // Handle CharacterSpawnRequest — server spawns a remote character
    else if (pMessage->GetOpcode() == kCharacterSpawnRequest)
    {
        auto* pSpawn = static_cast<CharacterSpawnRequest*>(pMessage.get());
        spdlog::info("ServerMessageRouter: CharacterSpawnRequest serverId={}, formId={:#x}",
                     pSpawn->ServerId, pSpawn->FormId.BaseId);
        if (m_onSpawn)
        {
            m_onSpawn(pSpawn->ServerId, pSpawn->FormId.BaseId,
                      pSpawn->Position.x, pSpawn->Position.y, pSpawn->Position.z,
                      pSpawn->Rotation.x, pSpawn->Rotation.y);
        }
    }
    // Handle NotifyRemoveCharacter — server removes a remote character
    else if (pMessage->GetOpcode() == kNotifyRemoveCharacter)
    {
        auto* pRemove = static_cast<NotifyRemoveCharacter*>(pMessage.get());
        spdlog::info("ServerMessageRouter: NotifyRemoveCharacter serverId={}", pRemove->ServerId);
        if (m_onRemove)
        {
            m_onRemove(pRemove->ServerId);
        }
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

void ServerMessageRouter::SetAssignCallback(AssignCallback aCb)
{
    m_onAssign = std::move(aCb);
}

void ServerMessageRouter::SetSpawnCallback(SpawnCallback aCb)
{
    m_onSpawn = std::move(aCb);
}

void ServerMessageRouter::SetRemoveCallback(RemoveCallback aCb)
{
    m_onRemove = std::move(aCb);
}
