#include <core/HostRouter.h>

#include <spdlog/spdlog.h>

#include <core/HostNetwork.h>
#include <core/HostPlayers.h>

HostRouter::HostRouter(HostNetwork& aNetwork, const HostPlayers& acPlayers) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
{
}

bool HostRouter::Dispatch(const RequestMessage& acMessage, ConnectionId_t aSender) const noexcept
{
    const auto cOpcode = acMessage.GetOpcode();
    if (cOpcode >= m_rules.size() || !m_rules[cOpcode])
        return false;
    for (const Rule& cWatcher : m_watchers[cOpcode])
        cWatcher(acMessage, aSender);
    m_rules[cOpcode](acMessage, aSender);
    return true;
}

void HostRouter::Deliver(const NotifyMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept
{
    const ConnectionId_t cOwner = m_ownerOf ? m_ownerOf(aActor) : 0;
    switch (aDelivery)
    {
    case Delivery::FromOwner:
        if (cOwner == aSender)
            m_players.SendToOthers(acNotify, aSender, Net::kReliable);
        else
            spdlog::debug("HostRouter: {:x} sent opcode {} for {:x}, which it does not own", aSender, uint32_t(acNotify.GetOpcode()), aActor);
        break;
    case Delivery::FromAnyone: m_players.SendToOthers(acNotify, aSender, Net::kReliable); break;
    case Delivery::ToOwner:
        if (cOwner != 0 && cOwner != aSender)
            m_network.Send(cOwner, acNotify);
        break;
    }
}
