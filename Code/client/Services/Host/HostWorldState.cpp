#include <Services/Host/HostWorldState.h>

#include <GameServer.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/WorldState.h>

#include <bit>

namespace
{
// About 1100 globals and 1500 quests: compared a few times a second, not every frame.
constexpr uint64_t kGlobalsIntervalMs = 250;
constexpr uint64_t kQuestsIntervalMs = 500;

uint64_t Key(const GameId& acId) noexcept
{
    return static_cast<uint64_t>(acId.ModId) << 32 | acId.BaseId;
}
} // namespace

HostWorldState::HostWorldState(Server::GameServer& aServer, const HostPlayers& acPlayers, const HostGame& acGame) noexcept
    : m_server(aServer)
    , m_players(acPlayers)
    , m_game(acGame)
{
}

void HostWorldState::Update(uint64_t aTick) noexcept
{
    UpdateObjectChanges();
    if (aTick - m_globalsTick >= kGlobalsIntervalMs)
    {
        m_globalsTick = aTick;
        UpdateGlobals();
    }
    if (aTick - m_questsTick >= kQuestsIntervalMs)
    {
        m_questsTick = aTick;
        UpdateQuests();
    }
}

void HostWorldState::UpdateGlobals() noexcept
{
    // Not before the host's own player is in: "everyone but the host" would include it.
    const ConnectionId_t cHost = m_players.Host();
    if (!cHost || !m_game.ReadGlobals)
        return;

    NotifyGlobals changed;
    for (const auto& cGlobal : m_game.ReadGlobals())
    {
        // The calendar is HostClock's: a guest runs its clock on, a global would set it back.
        if (cGlobal.Clock)
            continue;
        // By bits: a NaN equals itself, a sign change of zero is sent.
        const uint32_t cBits = std::bit_cast<uint32_t>(cGlobal.Value);
        auto [it, cAdded] = m_globals.try_emplace(Key(cGlobal.Id), cBits);
        if (!cAdded && it->second == cBits)
            continue;
        it.value() = cBits;
        changed.Values.push_back({cGlobal.Id, cGlobal.Value});
    }
    if (!changed.Values.empty())
        m_players.SendToOthers(changed, cHost, TiltedPhoques::kReliable);
}

void HostWorldState::UpdateQuests() noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    if (!cHost || !m_game.ReadQuests)
        return;

    // The first look sends every quest: the others take it as a load, not as the host's game playing them all now.
    NotifyQuests changed;
    changed.Join = m_quests.empty();
    for (auto& quest : m_game.ReadQuests())
    {
        auto [it, cAdded] = m_questIndex.try_emplace(Key(quest.Id), m_quests.size());
        if (cAdded)
            m_quests.push_back(quest);
        else if (m_quests[it->second] == quest)
            continue;
        else
            m_quests[it->second] = quest;
        changed.Quests.push_back(std::move(quest));
    }
    if (!changed.Quests.empty())
        m_players.SendToOthers(changed, cHost, TiltedPhoques::kReliable);
}

void HostWorldState::UpdateObjectChanges() noexcept
{
    if (!m_game.TakeObjectChanges)
        return;
    NotifyObjectChanges changes;
    changes.Changes = m_game.TakeObjectChanges();
    const ConnectionId_t cHost = m_players.Host();
    if (cHost && !changes.Changes.empty())
        m_players.SendToOthers(changes, cHost, TiltedPhoques::kReliable);
}

void HostWorldState::SendWorldState(ConnectionId_t aPlayer) const noexcept
{
    if (!m_quests.empty())
    {
        NotifyQuests all;
        all.Join = true;
        all.Quests = m_quests;
        m_server.Send(aPlayer, all);
    }
    if (m_globals.empty())
        return;

    NotifyGlobals all;
    all.Values.reserve(m_globals.size());
    for (const auto& [cKey, cBits] : m_globals)
        all.Values.push_back({GameId(static_cast<uint32_t>(cKey >> 32), static_cast<uint32_t>(cKey)), std::bit_cast<float>(cBits)});
    m_server.Send(aPlayer, all);
}
