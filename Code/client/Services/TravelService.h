#pragma once

#include <Structs/GameId.h>

#include <optional>

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

    TP_NOCOPYMOVE(TravelService);

    // A place a peer reports is around our player: its interior cell, or outdoors its worldspace within the
    // loaded grid.
    [[nodiscard]] bool IsAroundPlayer(const GameId& acWorldSpaceId, const GameId& acCellId,
                                      const glm::vec3& acPosition) const noexcept;
    // The cell at a place a peer reports (outdoors the worldspace's cell at the position), 0 if none.
    [[nodiscard]] uint32_t CellAt(const GameId& acWorldSpaceId, const GameId& acCellId,
                                  const glm::vec3& acPosition) const noexcept;

  private:
    void OnUpdate(const UpdateEvent&) noexcept;
    // Another player travelled by itself: ours goes where it arrived, unless already there.
    void OnPartyTravel(const NotifyPartyTravel& acMessage) noexcept;

    // 0 for the host; the others by player id, the same on every game.
    uint32_t ArrivalSlot() const noexcept;

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
    uint32_t m_arrivalCellId = 0; // where the player last arrived (stepping aside is for new cells)
    // Where the party tether sent the player and it has not arrived yet, oldest first. Arriving at one of
    // them is following, not leading.
    struct TetherTarget
    {
        GameId WorldSpaceId{};
        GameId CellId{};
        glm::vec3 Position{};
    };
    Vector<TetherTarget> m_tetherTargets;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_partyTravelConnection;
};
