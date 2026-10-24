#pragma once

#include <network/ENetInterface.h>

#include <Messages/CellStateRequest.h>
#include <Messages/WorldState.h>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <NoCopyMove.h>

struct HostAuthority;
struct HostGame;
struct HostPlayers;

struct HostNetwork;

/**
 * @brief The world is the host's (docs/design/world-state-sync.md, step 4): what changes in its
 * game reaches the others, who apply it with their engine's own call and decide nothing themselves.
 * Quests and globals go out on engine writes. Object changes go out every frame.
 * A guest's cell loading gets the host's state once that cell is loaded in the host's game too.
 */
struct HostWorldState
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostWorldState(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostWorldState);

    // Every frame: tells the others what changed in the host's world.
    void Update(uint64_t aTick) noexcept;
    // A joining player: the host's world as the others hold it.
    void SendWorldState(ConnectionId_t aPlayer) const noexcept;
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;

private:
    void UpdateGlobals() noexcept;
    void UpdateQuests() noexcept;
    void UpdateObjectChanges() noexcept;
    void UpdatePendingCells() noexcept;
    // A guest's cell loaded: the host's state there, or kept until the host's game loads it.
    void OnCellState(const CellStateRequest& acRequest, ConnectionId_t aSender) noexcept;
    // False while the cell is not loaded in the host's game.
    bool SendCellState(ConnectionId_t aPlayer, const CellStateRequest& acRequest) const noexcept;

    struct PendingCell
    {
        ConnectionId_t Player;
        CellStateRequest Request;
    };

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostGame& m_game;
    // The globals' values the others hold, by session id ((mod id << 32) | base id), as float bits.
    std::unordered_map<uint64_t, uint32_t> m_globals;
    bool m_globalsRead = false;
    // The quests as the others hold them, by session id, in the order the host's game lists them.
    std::unordered_map<uint64_t, size_t> m_questIndex;
    std::vector<QuestState> m_quests;
    bool m_questsRead = false;
    // Guests' cells the host's game has not loaded yet, oldest first.
    std::vector<PendingCell> m_pendingCells;
};
