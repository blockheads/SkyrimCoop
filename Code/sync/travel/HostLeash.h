#pragma once

#include <network/ENetInterface.h>
#include <Messages/NotifyPartyTravel.h>

#include <NoCopyMove.h>

#include <cstdint>
#include <unordered_map>

struct HostCharacters;
struct HostDeath;
struct HostNetwork;
struct HostPlayers;

/**
 * @brief The soft leash (docs/design/party-travel.md, "Soft leash"): a guest outdoors out of the host's loaded grid
 * sees none of the host's NPCs, so it hears a countdown and then goes to the host's player; back in time, it stays.
 */
struct HostLeash
{
    using ConnectionId_t = Net::ConnectionId_t;

    static constexpr uint8_t kCountdownSeconds = 10;
    // Out this long before the countdown starts: walking along the grid's edge warns nobody.
    static constexpr double kGraceSeconds = 2.0;
    // A trip to the host that has not landed after this long is sent again.
    static constexpr double kRetrySeconds = 15.0;

    HostLeash(HostNetwork& aNetwork, const HostPlayers& acPlayers, HostCharacters& aCharacters, const HostDeath& acDeath) noexcept;

    NO_COPY_MOVE(HostLeash);

    // Every frame: who left or came back into the host's grid, and whose countdown ran out (aDelta: seconds since
    // the last update).
    void Update(double aDelta) noexcept;
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;
    // /leash off (dev-commands.md): no countdown for anyone, a running one stops.
    void SetOn(bool aOn) noexcept;
    [[nodiscard]] bool IsOn() const noexcept { return m_on; }

private:
    void Send(ConnectionId_t aPlayer, NotifyLeash::Kind aState) const noexcept;

    struct Leash
    {
        double Left{}; // seconds: of the grace, then (Told) of its countdown, then (Sent) until its trip is sent again
        bool Sent{};   // sent to the host: no countdown until it is back in the grid
        bool Told{};   // its countdown started: its game shows it
    };

    HostNetwork& m_network;
    const HostPlayers& m_players;
    HostCharacters& m_characters;
    const HostDeath& m_death;
    std::unordered_map<ConnectionId_t, Leash> m_out;
    bool m_on{true};
};
