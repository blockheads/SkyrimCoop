#pragma once

#include <Events/Listen.h>
#include <Services/ArrivalStack.h>
#include <Structs/GameId.h>

#include <optional>
#include <NoCopyMove.h>
#include <MainLoopTasks.h>

struct World;
struct UpdateEvent;
struct FastTravelEvent;
struct PlayerTravelQueuedEvent;
struct NotifyPartyTravel;
struct NotifyPartySummon;
struct PartySummonCommandEvent;
struct TransportService;
class IActorBridge;
class IFormBridge;
class IPlayerBridge;

/**
 * @brief The player's travel in the session: it notices an arrival (PositionPlayerEvent), steps aside from
 * the others there, and keeps the party together (the party tether): a player who travels by itself leads,
 * everyone else follows it (docs/design/host-authority-migration.md, "Party tether").
 */
struct TravelService
{
    TravelService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                  IActorBridge& aActorBridge, IFormBridge& aFormBridge, IPlayerBridge& aPlayerBridge) noexcept;

    NO_COPY_MOVE(TravelService);

    // A place a peer reports is around our player: its interior cell, or outdoors its worldspace within the
    // loaded grid.
    [[nodiscard]] bool IsAroundPlayer(const GameId& acWorldSpaceId, const GameId& acCellId,
                                      const glm::vec3& acPosition) const noexcept;
    // A follower move; aForce also moves a player already near the destination (respawn beside the party).
    void FollowTo(const GameId& aWorld, const GameId& aCell, const glm::vec3& aPosition, float aHeading, bool aForce = false) noexcept;
    // The cell at a place a peer reports (outdoors the worldspace's cell at the position), 0 if none.
    [[nodiscard]] uint32_t CellAt(const GameId& acWorldSpaceId, const GameId& acCellId,
                                  const glm::vec3& acPosition) const noexcept;

  private:
    // Our player stands on that spot (within a step of it, ArrivalStack::kOnSpot).
    [[nodiscard]] bool OnSpot(const glm::vec3& acSpot) const noexcept;
    void FollowBetweenFrames(const GameId& aWorld, const GameId& aCell, const glm::vec3& aPosition, float aHeading, bool aForce) noexcept;
    // The party arrived on one spot: the players and actors on it pass through each other until each walks off it.
    void StackOn(const glm::vec3& acSpot) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnTravelQueued(const PlayerTravelQueuedEvent& acEvent) noexcept;
    void OnFastTravel(const FastTravelEvent& acEvent) noexcept;
    // Another player travelled by itself: ours goes where it arrived, unless already on that spot.
    void OnPartyTravel(const NotifyPartyTravel& acMessage) noexcept;
    // /tpa: everyone else comes onto our player's spot.
    void OnSummonCommand(const PartySummonCommandEvent&) noexcept;
    // Another player summoned the party: ours goes there even from the same cell, as the tether takes a down player too.
    void OnPartySummon(const NotifyPartySummon& acMessage) noexcept;
    // Who is still on the stack (ArrivalStack), and so who passes through whom.
    void UpdateStack() noexcept;
    // Our player and the copies of the others whose owners are around it: where each stands, by form id.
    [[nodiscard]] std::unordered_map<uint32_t, glm::vec3> PlayersHere() const noexcept;
    // Every other actor in our player's cell (outdoors: loaded), our own and copies: where each stands.
    [[nodiscard]] std::unordered_map<uint32_t, glm::vec3> OthersHere() const noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    IFormBridge& m_formBridge;
    IPlayerBridge& m_playerBridge;

    // Teleported since the last arrival: the next frame in a cell is one.
    bool m_arriving = false;
    // The arrival is the player's own travel (it leads the party), not one from before joining.
    bool m_leading = false;
    bool m_teleportedOffline = false;
    uint32_t m_arrivalCellId = 0; // where the player last arrived (the party stacks in new cells)
    bool m_stackNextArrival = false; // a summon's arrival stacks even in the same cell
    // Where the party tether sent the player and it has not arrived yet, oldest first. Arriving at one of
    // them is following, not leading.
    struct TetherTarget
    {
        GameId WorldSpaceId{};
        GameId CellId{};
        glm::vec3 Position{};
        float Heading{}; // the leader's on arrival
    };
    std::vector<TetherTarget> m_tetherTargets;
    std::optional<ArrivalStack> m_stack;

    MainLoopTasks::Scope m_mainTasks;
    std::vector<entt::scoped_connection> m_connections;
};
