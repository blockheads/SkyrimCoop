#include <Services/Host/HostFollowers.h>

#include <FormIds.h>
#include <Messages/FollowerMessages.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostDeath.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostPlayers.h>
#include <algorithm>

HostFollowers::HostFollowers(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers,
                             const HostCharacters& acCharacters, const HostDeath& acDeath,
                             const HostGame& acGame) noexcept
    : m_network(aNetwork), m_players(acPlayers), m_characters(acCharacters), m_death(acDeath), m_game(acGame)
{
    aAuthority.On(*this, &HostFollowers::OnTradeClosed);
}

void HostFollowers::Update(uint64_t aTick) noexcept
{
    if (!m_game.ReadFollowers || !m_game.FollowerAnchors || !m_game.SetFollowerTargets)
        return;
    if (m_game.TakeFollowerOrders)
        for (const auto& cOrder : m_game.TakeFollowerOrders())
        {
            m_pending.push_back({cOrder, aTick + 10000});
        }
    const auto cFollowers = m_game.ReadFollowers();
    if (!cFollowers)
        return; // alias data is not ready during a load
    auto& anchors = m_game.FollowerAnchors();
    std::vector<uint32_t> active;
    for (const auto& cFollower : *cFollowers)
        if (!cFollower.Dead)
            active.push_back(cFollower.Reference);
    anchors.Retain(active);
    std::erase_if(m_trading, [&](const auto& acTrade) {
        return !m_players.IsPlayer(acTrade.second) || std::ranges::none_of(*cFollowers, [&](const auto& acFollower) {
            return !acFollower.Dead && acFollower.Session == acTrade.first;
        });
    });

    // Teammate recruitment precedes ForceRefTo; keep the cause until the alias actually fills.
    std::erase_if(m_pending, [&](const Pending& acPending) {
        const auto& cOrder = acPending.Order;
        if (std::ranges::find(active, cOrder.Reference) == active.end())
            return aTick >= acPending.Until;
        const auto* pPlayer = m_players.Find(m_characters.OwnerOf(cOrder.Character));
        if (!pPlayer || pPlayer->Key.empty())
            return true;
        if (cOrder.What == Followers::Command::Trade)
        {
            const auto it = std::ranges::find(*cFollowers, cOrder.Reference, &Followers::State::Reference);
            if (!it->Session)
                return aTick >= acPending.Until;
            GrantTrade(*it, m_characters.OwnerOf(cOrder.Character));
        }
        if (cOrder.What == Followers::Command::Follow || cOrder.What == Followers::Command::Recruit)
            anchors.Follow(cOrder.Reference, pPlayer->Key);
        return true;
    });

    std::vector<Followers::Member> members;
    m_players.ForEach([&](auto aConnection, const HostPlayers::Player& acPlayer) {
        const uint32_t cCharacter = m_characters.PlayerCharacterOf(aConnection);
        const auto cAt = m_characters.CharacterPlace(cCharacter);
        Followers::Member member{
            acPlayer.Key, cCharacter, acPlayer.IsHost, m_death.Standing(aConnection) && cAt.has_value(), {}};
        if (cAt)
            member.At = {
                ToGame(cAt->WorldSpaceId), ToGame(cAt->CellId), {cAt->Position.x, cAt->Position.y, cAt->Position.z}};
        members.push_back(std::move(member));
    });
    const auto* pHost = m_players.Find(m_players.Host());
    std::vector<Followers::Target> targets;
    for (const auto& cFollower : *cFollowers)
    {
        if (cFollower.Dead)
            continue;
        if (anchors.Desired(cFollower.Reference).empty() && pHost)
            anchors.Follow(cFollower.Reference, pHost->Key);
        const uint32_t cTarget = cFollower.Waiting ? 0 : anchors.Choose(cFollower.Reference, cFollower.At, members);
        targets.push_back({cFollower.Reference, cTarget});
    }
    m_game.SetFollowerTargets(targets);
}

void HostFollowers::GrantTrade(const Followers::State& acFollower, Net::ConnectionId_t aPicker) noexcept
{
    if (m_characters.OwnerOf(acFollower.Session) != m_players.Host() ||
        m_characters.IsPlayerCharacter(acFollower.Session))
        return;
    if (m_trading.contains(acFollower.Session))
    {
        NotifyChatMessageBroadcast busy;
        busy.ChatMessage = "Someone is already trading with this follower.";
        m_network.Send(aPicker, busy);
        return;
    }
    m_trading[acFollower.Session] = aPicker;
    NotifyFollowerTrade open;
    open.NpcId = acFollower.Session;
    m_network.Send(aPicker, open);
}

void HostFollowers::OnTradeClosed(const FollowerTradeClosedRequest& acRequest, Net::ConnectionId_t aSender) noexcept
{
    const auto it = m_trading.find(acRequest.NpcId);
    if (it != m_trading.end() && it->second == aSender)
        m_trading.erase(it);
}
