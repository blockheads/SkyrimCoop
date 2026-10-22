#include <Services/Host/HostWorldState.h>

#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/WorldState.h>
#include <Services/WorldStateLog.h>

#include <bit>

namespace
{
// About 1100 globals and 1500 quests: compared a few times a second, not every frame.
constexpr uint64_t kGlobalsIntervalMs = 250;
constexpr uint64_t kQuestsIntervalMs = 500;
// A cell's state goes in messages of at most this many changes (NotifyObjectChanges takes 4096).
constexpr size_t kCellChangesPerMessage = 1024;
// Cells a guest loaded that the host's game has not: past this many, the oldest is forgotten.
constexpr size_t kMaxPendingCells = 64;

uint64_t Key(const GameId& acId) noexcept
{
    return static_cast<uint64_t>(acId.ModId) << 32 | acId.BaseId;
}
} // namespace

HostWorldState::HostWorldState(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostWorldState::OnCellState);
}

void HostWorldState::Update(uint64_t aTick) noexcept
{
    UpdateObjectChanges();
    UpdatePendingCells();
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
    if (changed.Values.empty())
        return;
    spdlog::info("HostWorldState: sent {} globals: {}", changed.Values.size(), WorldStateLog::List(changed.Values, [](const GlobalValue& acValue) {
                     return fmt::format("{:X}={}", acValue.Id.LogFormat(), acValue.Value);
                 }));
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
    if (changed.Quests.empty())
        return;
    spdlog::info("HostWorldState: sent {} quests{}: {}", changed.Quests.size(), changed.Join ? " (as a load)" : "",
                 WorldStateLog::List(changed.Quests, [](const QuestState& acQuest) {
                     return fmt::format("{:X} stage {} flags {:X}", acQuest.Id.LogFormat(), acQuest.CurrentStage, acQuest.Flags);
                 }));
    m_players.SendToOthers(changed, cHost, TiltedPhoques::kReliable);
}

void HostWorldState::UpdateObjectChanges() noexcept
{
    if (!m_game.TakeObjectChanges)
        return;
    NotifyObjectChanges changes;
    changes.Changes = m_game.TakeObjectChanges();
    const ConnectionId_t cHost = m_players.Host();
    if (!cHost || changes.Changes.empty())
        return;
    spdlog::info("HostWorldState: sent {} object changes: {}", changes.Changes.size(), WorldStateLog::List(changes.Changes, &WorldStateLog::Describe, 16));
    m_players.SendToOthers(changes, cHost, TiltedPhoques::kReliable);
}

void HostWorldState::OnCellState(const CellStateRequest& acRequest, ConnectionId_t aSender) noexcept
{
    // The host's own cells are its world already.
    if (aSender == m_players.Host() || !m_game.ReadCellState)
        return;
    // A newer load of the same cell replaces one still waiting.
    std::erase_if(m_pendingCells, [&](const PendingCell& acPending) { return acPending.Player == aSender && acPending.Request.Cell == acRequest.Cell; });
    if (SendCellState(aSender, acRequest))
        return;
    spdlog::info("HostWorldState: {:x}'s cell {:X} is not loaded here, its state goes once it is", aSender, acRequest.Cell.LogFormat());
    if (m_pendingCells.size() >= kMaxPendingCells)
    {
        spdlog::warn("HostWorldState: {} cells waiting for the host's game to load them, forgetting {:X}", m_pendingCells.size(),
                     m_pendingCells.front().Request.Cell.LogFormat());
        m_pendingCells.erase(m_pendingCells.begin());
    }
    m_pendingCells.push_back({aSender, acRequest});
}

bool HostWorldState::SendCellState(ConnectionId_t aPlayer, const CellStateRequest& acRequest) const noexcept
{
    auto changes = m_game.ReadCellState(acRequest.Cell, acRequest.Changed);
    if (!changes)
        return false;
    spdlog::info("HostWorldState: sent {:x} cell {:X}'s state, {} changes: {}", aPlayer, acRequest.Cell.LogFormat(), changes->size(),
                 WorldStateLog::List(*changes, &WorldStateLog::Describe, 16));
    // An empty answer too: the guest's changes there were the host's already.
    for (size_t first = 0; first == 0 || first < changes->size(); first += kCellChangesPerMessage)
    {
        NotifyObjectChanges notify;
        notify.Cell = acRequest.Cell;
        const size_t cLast = std::min(changes->size(), first + kCellChangesPerMessage);
        notify.Changes.assign(changes->begin() + first, changes->begin() + cLast);
        m_network.Send(aPlayer, notify);
    }
    return true;
}

void HostWorldState::UpdatePendingCells() noexcept
{
    std::erase_if(m_pendingCells, [this](const PendingCell& acPending) { return SendCellState(acPending.Player, acPending.Request); });
}

void HostWorldState::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    std::erase_if(m_pendingCells, [aPlayer](const PendingCell& acPending) { return acPending.Player == aPlayer; });
}

void HostWorldState::SendWorldState(ConnectionId_t aPlayer) const noexcept
{
    if (!m_quests.empty())
    {
        NotifyQuests all;
        all.Join = true;
        all.Quests = m_quests;
        m_network.Send(aPlayer, all);
    }
    if (m_globals.empty())
        return;

    NotifyGlobals all;
    all.Values.reserve(m_globals.size());
    for (const auto& [cKey, cBits] : m_globals)
        all.Values.push_back({GameId(static_cast<uint32_t>(cKey >> 32), static_cast<uint32_t>(cKey)), std::bit_cast<float>(cBits)});
    m_network.Send(aPlayer, all);
}
