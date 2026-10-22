#pragma once

#include <Structs/GameId.h>

#include <optional>
#include <NoCopyMove.h>

struct World;
struct UpdateEvent;
struct NotifyPartyTravel;
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
    // The cell at a place a peer reports (outdoors the worldspace's cell at the position), 0 if none.
    [[nodiscard]] uint32_t CellAt(const GameId& acWorldSpaceId, const GameId& acCellId,
                                  const glm::vec3& acPosition) const noexcept;

  private:
    // The party arrived on one spot: the players on it pass through each other until each walks off it.
    void StackOn(const glm::vec3& acSpot) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    // Another player travelled by itself: ours goes where it arrived, unless already there.
    void OnPartyTravel(const NotifyPartyTravel& acMessage) noexcept;
    // Who is still on the stack (a player leaves it once it walks off it in this cell, or if it never came
    // within a few seconds), and so who passes through whom.
    void UpdateStack() noexcept;
    // Our player and the copies of the others whose owners are around it: where each stands, by form id.
    [[nodiscard]] std::unordered_map<uint32_t, glm::vec3> PlayersHere() const noexcept;

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
    struct Stack
    {
        struct Player
        {
            uint32_t FormId{};
            bool Arrived{}; // has stood on the spot
            std::chrono::steady_clock::time_point OffSince{}; // in this cell off the spot since (none: on it, or away)
        };
        glm::vec3 Spot{};
        std::chrono::steady_clock::time_point Since;
        std::vector<Player> Players;
    };
    std::optional<Stack> m_stack;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_partyTravelConnection;
};
