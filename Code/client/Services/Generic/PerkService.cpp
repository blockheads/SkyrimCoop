#include <Services/PerkService.h>

#include <Bridges/IActorBridge.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/Relayed.h>
#include <Services/TransportService.h>
#include <SessionIds.h>

PerkService::PerkService(entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aBridge) noexcept
    : m_transport(aTransport), m_bridge(aBridge)
{
    Listen<UpdateEvent, &PerkService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &PerkService::OnDisconnected>(m_connections, aDispatcher, this);
    Listen<NotifyActorPerks, &PerkService::OnPerks>(m_connections, aDispatcher, this);
    Listen<NotifyCharacterSpawn, &PerkService::OnSpawn>(m_connections, aDispatcher, this);
    Listen<NotifyRemoveCharacter, &PerkService::OnRemove>(m_connections, aDispatcher, this);
}

void PerkService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_sent.reset();
    m_copies.clear();
}

void PerkService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_transport.IsOnline())
        return;
    // RemovePerk queues its mutation (40682); observe the completed list, without assuming when its task runs.
    if (const auto cSession = Utils::GetSessionIdByFormId(0x14))
    {
        auto perks = m_bridge.GetPlayerPerks();
        if (!m_sent || *m_sent != perks)
        {
            ActorPerksRequest request;
            request.SessionId = *cSession;
            request.Content = perks;
            if (!m_transport.Send(request))
                return;
            m_sent = std::move(perks);
        }
    }
    for (auto& [cSession, copy] : m_copies)
    {
        const uint32_t cForm = Utils::GetFormIdBySessionId(cSession);
        if (cForm && cForm != copy.AppliedTo && m_bridge.ActorExists(cForm) && m_bridge.IsActorCopy(cForm))
        {
            m_bridge.SetCopyPerks(cForm, copy.Content);
            copy.AppliedTo = cForm;
        }
    }
}

void PerkService::OnPerks(const NotifyActorPerks& acMessage) noexcept
{
    m_copies[acMessage.SessionId] = {acMessage.Content};
}

void PerkService::OnSpawn(const NotifyCharacterSpawn& acMessage) noexcept
{
    if (acMessage.IsPlayer)
        m_copies[acMessage.SessionId] = {acMessage.PerksContent};
}

void PerkService::OnRemove(const NotifyRemoveCharacter& acMessage) noexcept
{
    m_copies.erase(acMessage.SessionId);
}
