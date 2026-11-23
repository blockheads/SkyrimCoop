#pragma once

#include <network/ENetInterface.h>

#include <Messages/RelayedMessages.h>
#include <Physics/BodyAuthority.h>
#include <Physics/BodyMotions.h>
#include <Physics/BodyStream.h>
#include <Physics/PairedPoses.h>

#include <map>
#include <NoCopyMove.h>

struct HostAuthority;
struct HostCharacters;
struct HostGame;
struct HostNetwork;
struct HostPlayers;

/**
 * @brief The host arbitrates every body (docs/design/physics-authority-plan.md, piece 2): one row a body, its claim
 * (BodyAuthority) and last state, streamed to the games that do not simulate it (BodyStream).
 */
struct HostBodies
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostBodies(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostCharacters& acCharacters,
               const HostGame& acGame) noexcept;

    NO_COPY_MOVE(HostBodies);

    void Update(uint64_t aTick) noexcept;
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;
    // The host's game placed that reference itself: nobody's again, where it was put.
    void OnHostPlaced(const GameId& acObject, const glm::vec3& acWhere) noexcept;
    // Closer than this to its last state, the host's body was put there by that state, not by the host's game.
    static constexpr float kPlacedApart = 16.f;
    // An authority is silent after BodyTolerances::kSilentMs and this many updates without a state.
    static constexpr uint32_t kQuietUpdates = 30;

private:
    struct Row
    {
        BodyFrame Frame{};
        uint64_t Tick{};      // its state's time (the frame clock)
        uint64_t Heard{};     // when its authority last spoke
        uint32_t Quiet{};     // updates since then: one long frame (a stall) is not silence
        bool FromGuest{};     // a guest's rest the host's game is still putting
        bool Given{};         // FromGuest: the host's game was sent it
        bool Placed{};        // where the host's game put it, not where its physics settled it
    };

    void OnStates(const BodyStatesRequest& acMessage, ConnectionId_t aSender) noexcept;
    // The host's player's touches and grabs, and what its bodies touch.
    void TakeHands(uint64_t aTick, const std::vector<BodyKey>& acHeld) noexcept;
    // The host's game's reads of the bodies it simulates.
    void Read(uint64_t aTick) noexcept;
    void Stream(uint64_t aTick) noexcept;
    // Nobody's again: the host's game simulates it from where it is.
    void Unclaim(const BodyKey& acKey, const char* acpWhy) noexcept;
    // A ragdoll body the sender's character owns, or a corpse the host's game owns (a guest's player may grab it).
    [[nodiscard]] bool MayClaim(const BodyKey& acKey, ConnectionId_t aSender) const noexcept;
    [[nodiscard]] bool IsGuest(uint32_t aPlayer) const noexcept;
    // A connection's number in BodyAuthority (its claims), and back; 0 is nobody.
    [[nodiscard]] uint32_t NumberOf(ConnectionId_t aPlayer) noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
    BodyAuthority m_authority;
    std::map<BodyKey, Row> m_rows;
    std::map<ConnectionId_t, BodyStream> m_streams;
    BodyMotions m_motions;
    PairedPoses m_paired; // what Havok pairs with its player's bodies, and where each lay
    std::vector<BodyKey> m_held; // the host's player holds them
    std::map<ConnectionId_t, uint32_t> m_numbers;
    uint32_t m_lastNumber{};
    uint64_t m_tick{};
};
