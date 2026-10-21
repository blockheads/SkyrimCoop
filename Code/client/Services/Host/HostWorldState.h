#pragma once

#include <ENetInterface.hpp>

#include <Messages/CellStateRequest.h>
#include <Messages/WorldState.h>
#include <TiltedCore/Stl.hpp>

struct HostAuthority;
struct HostGame;
struct HostPlayers;

namespace Server
{
struct GameServer;
}

/**
 * @brief The world is the host's (docs/design/world-state-sync.md, step 4): what changes in its
 * game reaches the others, who apply it with their engine's own call and decide nothing themselves.
 * Global variables and quests are compared with what the others hold a few times a second: globals never
 * reach the engine's change funnel, nor does a script's alias fill. What the host's world scripts did to objects
 * (doors, animations) goes out each frame (step 5). A guest's cell loading gets the host's state of the references
 * there, once the host's game has that cell loaded too (step 2 of finishing it, docs/progress.md).
 */
struct HostWorldState
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    HostWorldState(HostAuthority& aAuthority, Server::GameServer& aServer, const HostPlayers& acPlayers, const HostGame& acGame) noexcept;

    TP_NOCOPYMOVE(HostWorldState);

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

    Server::GameServer& m_server;
    const HostPlayers& m_players;
    const HostGame& m_game;
    // The globals' values the others hold, by session id ((mod id << 32) | base id), as float bits.
    TiltedPhoques::Map<uint64_t, uint32_t> m_globals;
    uint64_t m_globalsTick{};
    // The quests as the others hold them, by session id, in the order the host's game lists them.
    TiltedPhoques::Map<uint64_t, size_t> m_questIndex;
    TiltedPhoques::Vector<QuestState> m_quests;
    uint64_t m_questsTick{};
    // Guests' cells the host's game has not loaded yet, oldest first.
    TiltedPhoques::Vector<PendingCell> m_pendingCells;
};
