#include <TiltedOnlinePCH.h>

#include <Services/HostBridge.h>

#include <GameServer.h>
#include <World.h>
#include <Services/CharacterService.h>

#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/ServerReferencesMoveRequest.h>

HostBridge::HostBridge(Server::GameServer& aServer) noexcept
    : m_server(aServer)
{
    // Late join from the host's live state, not the server's records.
    m_server.SetLateJoinHandler([this](ConnectionId_t aPlayer, uint32_t aCharacterServerId) {
        const auto cSpawns = World::Get().GetCharacterService().BuildSpawnRequests(aCharacterServerId);
        spdlog::info("HostBridge: {} characters for the player who joined ({:x})", cSpawns.size(), aPlayer);
        for (const auto& cSpawn : cSpawns)
            m_server.Send(aPlayer, cSpawn);
    });
}

bool HostBridge::Handle(const ClientMessage& acMessage, ConnectionId_t aSender) noexcept
{
    if (!m_server.IsAuthenticated(aSender))
        return false; // the server's own handling until the player is in

    switch (acMessage.GetOpcode())
    {
    case kClientReferencesMoveRequest: OnMovement(static_cast<const ClientReferencesMoveRequest&>(acMessage), aSender); return true;
    default: return false;
    }
}

void HostBridge::OnMovement(const ClientReferencesMoveRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    ServerReferencesMoveRequest notify;
    notify.Tick = m_server.GetTick();
    for (const auto& [cServerId, cUpdate] : acRequest.Updates)
    {
        if (m_server.OwnerConnection(cServerId) == aSender)
            notify.Updates[cServerId] = cUpdate;
        else
            spdlog::debug("HostBridge: {:x} moved {:x}, which it does not own", aSender, cServerId);
    }
    if (!notify.Updates.empty())
        m_server.SendToOthers(notify, aSender, TiltedPhoques::kUnreliable);
}
