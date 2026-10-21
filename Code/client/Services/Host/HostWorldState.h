#pragma once

#include <ENetInterface.hpp>

#include <Messages/WorldState.h>
#include <TiltedCore/Stl.hpp>

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
 * (doors, animations) goes out each frame (step 5).
 */
struct HostWorldState
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    HostWorldState(Server::GameServer& aServer, const HostPlayers& acPlayers, const HostGame& acGame) noexcept;

    TP_NOCOPYMOVE(HostWorldState);

    // Every frame: tells the others what changed in the host's world.
    void Update(uint64_t aTick) noexcept;
    // A joining player: the host's world as the others hold it.
    void SendWorldState(ConnectionId_t aPlayer) const noexcept;

private:
    void UpdateGlobals() noexcept;
    void UpdateQuests() noexcept;
    void UpdateScriptResults() noexcept;

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
};
