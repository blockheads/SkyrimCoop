#include <Services/Host/HostItems.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostPlayers.h>

#include <algorithm>

HostItems::HostItems(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostItems::OnDrop);
    aAuthority.On(*this, &HostItems::OnPickUp);
}

void HostItems::Update() noexcept
{
    std::erase_if(m_pending, [this](const Pending& acPending) { return Make(acPending.Player, acPending.Request); });
}

void HostItems::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    std::erase_if(m_pending, [aPlayer](const Pending& acPending) { return acPending.Player == aPlayer; });
    std::erase_if(m_made, [aPlayer](const auto& acMade) { return acMade.first.first == aPlayer; });
}

void HostItems::OnDrop(const DropItemRequest& acRequest, ConnectionId_t aSender) noexcept
{
    // The host's own drops are its world already (HostWorldState sends them).
    if (aSender == m_players.Host() || !m_game.CreateItem || Make(aSender, acRequest))
        return;
    spdlog::info("HostItems: {:x}'s drop {:X} ({:X}) is in a cell not loaded here, made once it is", aSender, acRequest.Tag,
                 acRequest.Item.BaseId.LogFormat());
    if (m_pending.size() >= kMaxPending)
    {
        spdlog::warn("HostItems: {} drops waiting for the host's game, forgetting {:X}", m_pending.size(), m_pending.front().Request.Tag);
        m_pending.erase(m_pending.begin());
    }
    m_pending.push_back({aSender, acRequest});
}

bool HostItems::Make(ConnectionId_t aPlayer, const DropItemRequest& acRequest) noexcept
{
    const auto cObject = m_game.CreateItem(acRequest.Item, acRequest.Placement);
    if (!cObject)
        return false;
    if (*cObject == GameId{})
        spdlog::warn("HostItems: {:x}'s drop {:X} ({:X}) cannot be made in the host's game, left its own", aPlayer, acRequest.Tag,
                     acRequest.Item.BaseId.LogFormat());
    else
    {
        m_made[{aPlayer, acRequest.Tag}] = *cObject;
        spdlog::info("HostItems: made {:x}'s drop {:X} as {:X} ({} of {:X})", aPlayer, acRequest.Tag, cObject->LogFormat(),
                     acRequest.Item.Count, acRequest.Item.BaseId.LogFormat());
    }
    // Before the creation goes out (HostWorldState, next update): the dropper's own reference becomes its twin (an empty
    // id: stays its own).
    NotifyItemDropped made;
    made.Tag = acRequest.Tag;
    made.Object = *cObject;
    m_network.Send(aPlayer, made);
    return true;
}

void HostItems::OnPickUp(const PickUpRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (aSender == m_players.Host() || !m_game.TakeItem)
        return;
    // Its own drop still waiting for its cell: never made, it goes back.
    const auto cPending = std::ranges::find_if(m_pending, [&](const Pending& acPending) {
        return acRequest.Tag && acPending.Player == aSender && acPending.Request.Tag == acRequest.Tag;
    });
    if (cPending != m_pending.end())
    {
        const Inventory::Entry cItem = cPending->Request.Item;
        m_pending.erase(cPending);
        Grant(aSender, {}, acRequest.Tag, cItem);
        return;
    }
    // Its own drop made here, asked for by its tag before the answer reached it.
    GameId object = acRequest.Object;
    if (const auto cMade = m_made.find({aSender, acRequest.Tag}); acRequest.Tag && cMade != m_made.end())
        object = cMade->second;
    std::erase_if(m_made, [&](const auto& acMade) { return acMade.second == object; });
    if (const auto cItem = object != GameId{} ? m_game.TakeItem(object) : std::nullopt)
        Grant(aSender, object, acRequest.Tag, *cItem);
    else
        spdlog::info("HostItems: {:x} asked for {:X} (tag {:X}), already gone", aSender, object.LogFormat(), acRequest.Tag);
}

void HostItems::Grant(ConnectionId_t aPlayer, const GameId& acObject, uint32_t aTag, const Inventory::Entry& acItem) const noexcept
{
    spdlog::info("HostItems: {:x} picks up {:X} (tag {:X}): {} of {:X}", aPlayer, acObject.LogFormat(), aTag, acItem.Count, acItem.BaseId.LogFormat());
    NotifyPickUp granted;
    granted.Object = acObject;
    granted.Tag = aTag;
    granted.Item = acItem;
    m_network.Send(aPlayer, granted);
}
