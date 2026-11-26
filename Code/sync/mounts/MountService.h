#pragma once

#include <core/Listen.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

#include <glm/vec3.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class IActorBridge;
class IPlayerBridge;

struct World;
struct TransportService;
struct CharacterService;
struct MountEvent;
struct DismountEvent;
struct BoardEvent;

/**
 * @brief Riding: a player on a mount the host gives its game, a second player riding along, and getting off
 * (docs/design/player-mount.md).
 */
struct MountService
{
    MountService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge,
                 IPlayerBridge& aPlayerBridge, CharacterService& aCharacters) noexcept;
    ~MountService() noexcept = default;

    NO_COPY_MOVE(MountService);

    // The mount our player rides, given to this game by the host (its form id), or 0.
    [[nodiscard]] uint32_t RiddenMount() const noexcept { return m_riddenMount; }
    // Our player rides or rides along.
    [[nodiscard]] bool PlayerOnMount() const noexcept { return m_riddenMount || m_boardedMount; }
    // Ridden by a player (a session id): kept through cell loads, travels with the party.
    [[nodiscard]] bool IsPartyMount(uint32_t aSessionId) const noexcept;
    // A late joiner hears who rides what right after the spawns: a rider's or mount's copy was just made.
    void LinkPendingRides(uint32_t aSessionId) noexcept;
    // The frame's end: the Ride parts waiting for their copies' streams.
    void ApplyDueRides() noexcept;
    // The player's owned horse accompanies a tether move.
    void CarryMountTo(uint32_t aCellId, const glm::vec3& acPosition) noexcept;
    // The session ended: every mount this game kept is its own again, then nothing is remembered.
    void ReleaseAll() noexcept;
    void Clear() noexcept;

    void OnMountEvent(const MountEvent& acEvent) noexcept;
    void OnNotifyMount(const NotifyMount& acMessage) noexcept;
    // Our player got off the mount the host gave this game: the host takes it back.
    void OnDismountEvent(const DismountEvent& acEvent) noexcept;
    void OnNotifyMountRefused(const NotifyMountRefused& acMessage) noexcept;
    // Another player got on first (a ride's claim refused): ours gets off, as a refused boarding.
    void OnRideRefused(const NotifyClaimRefused& acMessage) noexcept;
    // Our player activated a horse a party player drives: on as a passenger, the host told.
    void OnBoardEvent(const BoardEvent& acEvent) noexcept;
    // Another player rides along: its copy seated at once (its replayed get-on never ends).
    void OnNotifyBoard(const NotifyBoard& acMessage) noexcept;
    // The mount is the host's again: a copy in its rider's game, the host's game's to simulate.
    void OnNotifyDismount(const NotifyDismount& acMessage) noexcept;
    void OnNotifyUnboard(const NotifyUnboard& acMessage) noexcept;

private:
    void KeepMount(uint32_t aFormId) noexcept;
    void ReleaseMount(uint32_t aFormId) noexcept;
    void LeaveBoard() noexcept;
    std::vector<uint32_t> ApplyRide(uint32_t aId, const std::string& acBytes) noexcept;
    // Any rider's Detach empties its mount's one slot: the driver still on holds it again.
    void KeepDriverOn(const DismountEvent& acEvent) noexcept;

    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    IPlayerBridge& m_playerBridge;
    CharacterService& m_characters;
    uint32_t m_riddenMount{};
    std::unordered_map<uint32_t, bool> m_mountPersistence; // form id -> persistence before this ride
    std::unordered_set<uint32_t> m_partyMounts; // session ids: ridden by a player, kept through cell loads
    std::unordered_map<uint32_t, uint32_t> m_pendingRides; // mount -> rider session ids, heard before both copies were made
    uint32_t m_boardedMount{};  // session id: our player rides along on it
    uint32_t m_boardingMount{}; // session id: asked the host for a seat on it
    std::unordered_map<uint32_t, uint32_t> m_drivers; // mount -> its driver, session ids
    std::unordered_set<uint32_t> m_dueRides; // session ids: a Ride part waiting for its copy's stream

    std::vector<entt::scoped_connection> m_connections;
};
