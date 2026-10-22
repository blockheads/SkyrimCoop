#pragma once

#include <ENetInterface.hpp>

struct NotifyMessage;

struct HostNetwork;

/**
 * @brief The session's players, by their game's connection: who is in, who hosts, and sending to
 * them. HostSession adds and removes them by its join and leave rules; the other parts read the list
 * and send through it.
 */
struct HostPlayers
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    // A player as the session keeps it (later also the account its stored character is under).
    struct Player
    {
        uint32_t Id{};
        TiltedPhoques::String Name;
        uint16_t Level{};
        bool IsHost{};
    };

    explicit HostPlayers(HostNetwork& aNetwork) noexcept;

    TP_NOCOPYMOVE(HostPlayers);

    // A game that joined; returns its id in the player list (never reused in a session).
    uint32_t Add(ConnectionId_t aConnection, const TiltedPhoques::String& acName, uint16_t aLevel, bool aIsHost) noexcept;
    void Remove(ConnectionId_t aConnection) noexcept;

    // The player on that connection (null: not a player).
    [[nodiscard]] const Player* Find(ConnectionId_t aConnection) const noexcept;
    [[nodiscard]] bool IsPlayer(ConnectionId_t aConnection) const noexcept { return Find(aConnection) != nullptr; }
    // The player's id and name (0 and empty: not a player).
    [[nodiscard]] uint32_t PlayerIdOf(ConnectionId_t aConnection) const noexcept;
    [[nodiscard]] TiltedPhoques::String PlayerNameOf(ConnectionId_t aConnection) const noexcept;
    // The host's connection, once its own player is in (0 before).
    [[nodiscard]] ConnectionId_t Host() const noexcept;
    [[nodiscard]] size_t Count() const noexcept { return m_players.size(); }
    // Every player's connection, the host's included.
    [[nodiscard]] TiltedPhoques::Vector<ConnectionId_t> Connections() const noexcept;
    // Each player, with its connection.
    template <class TFunction> void ForEach(const TFunction& acFunction) const
    {
        for (const auto& [cConnection, cPlayer] : m_players)
            acFunction(cConnection, cPlayer);
    }

    // The player's character level, as the player list shows it.
    void SetLevel(ConnectionId_t aConnection, uint16_t aLevel) noexcept;
    void SendToOthers(const NotifyMessage& acMessage, ConnectionId_t aExcluded, TiltedPhoques::EPacketFlags aFlags = TiltedPhoques::kReliable) const noexcept;
    void SendToAll(const NotifyMessage& acMessage) const noexcept { SendToOthers(acMessage, 0); }
    // The host removes a player (never itself: the host is the session).
    void Kick(uint32_t aPlayerId) noexcept;

private:
    HostNetwork& m_network;
    TiltedPhoques::Map<ConnectionId_t, Player> m_players;
    uint32_t m_lastId = 0;
};
