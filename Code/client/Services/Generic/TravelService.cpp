#include <Services/TravelService.h>

#include <Events/PlayerArrivedEvent.h>
#include <Events/UpdateEvent.h>

#include <Messages/NotifyPartyTravel.h>
#include <Structs/GridCellCoords.h>

#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Services/HostService.h>
#include <Services/SessionService.h>
#include <Services/TransportService.h>
#include <World.h>

namespace
{
// One place a peer reports, as the tether compares them: the same interior cell, or outdoors the same
// worldspace within the loaded grid.
bool SamePlace(const GameId& acWorldSpaceA, const GameId& acCellA, const glm::vec3& acPositionA,
               const GameId& acWorldSpaceB, const GameId& acCellB, const glm::vec3& acPositionB) noexcept
{
    if (acWorldSpaceA != acWorldSpaceB)
        return false;
    if (!acWorldSpaceA)
        return acCellA == acCellB;
    return GridCellCoords::IsCellInGridCell(GridCellCoords::CalculateGridCellCoords(acPositionA.x, acPositionA.y),
                                            GridCellCoords::CalculateGridCellCoords(acPositionB.x, acPositionB.y),
                                            false);
}
} // namespace

TravelService::TravelService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                             IActorBridge& aActorBridge, IFormBridge& aFormBridge,
                             IPlayerBridge& aPlayerBridge) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher), m_transport(aTransport), m_actorBridge(aActorBridge),
      m_formBridge(aFormBridge), m_playerBridge(aPlayerBridge)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&TravelService::OnUpdate>(this);
    m_partyTravelConnection = aDispatcher.sink<NotifyPartyTravel>().connect<&TravelService::OnPartyTravel>(this);
}

bool TravelService::IsAroundPlayer(const GameId& acWorldSpaceId, const GameId& acCellId,
                                   const glm::vec3& acPosition) const noexcept
{
    auto& modSystem = m_world.GetModSystem();
    if (!acWorldSpaceId)
        return modSystem.GetGameId(acCellId) == m_playerBridge.GetPlayerParentCellFormId();
    const uint32_t cPlayerWorldSpaceId = m_playerBridge.GetPlayerWorldSpaceId();
    if (!cPlayerWorldSpaceId || modSystem.GetGameId(acWorldSpaceId) != cPlayerWorldSpaceId)
        return false;
    float x, y, z;
    m_playerBridge.GetPlayerPosition(x, y, z);
    return GridCellCoords::IsCellInGridCell(GridCellCoords::CalculateGridCellCoords(acPosition.x, acPosition.y),
                                            GridCellCoords::CalculateGridCellCoords(x, y), false);
}

uint32_t TravelService::CellAt(const GameId& acWorldSpaceId, const GameId& acCellId,
                               const glm::vec3& acPosition) const noexcept
{
    auto& modSystem = m_world.GetModSystem();
    if (!acWorldSpaceId)
        return modSystem.GetGameId(acCellId);
    const GridCellCoords cCoordinates = GridCellCoords::CalculateGridCellCoords(acPosition.x, acPosition.y);
    return m_formBridge.LookupCellByCoords(modSystem.GetGameId(acWorldSpaceId), cCoordinates.X, cCoordinates.Y);
}

// The player travelled (PlayerCharacter::ConsumeTeleported). Runs here, not in the engine's event, which
// holds cell locks (docs/re/systems/player-travel.md).
void TravelService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_transport.IsOnline())
    {
        // A travel before joining leads nobody: the game joins where the host is.
        if (m_playerBridge.ConsumeTeleported())
            m_teleportedOffline = true;
        m_arriving = false;
        return;
    }
    if (m_playerBridge.ConsumeTeleported())
        m_arriving = m_leading = true;
    else if (std::exchange(m_teleportedOffline, false))
    {
        m_arriving = true;
        m_leading = false;
    }
    const uint32_t cCellId = m_playerBridge.GetPlayerParentCellFormId();
    if (!m_arriving || !cCellId)
        return;
    m_arriving = false;

    // The party arrives on one spot (a door's, a coc's, the leader's): each player but the host steps aside,
    // by its place in the session, so nobody stands in anybody (docs/design/host-authority-migration.md).
    if (cCellId != m_arrivalCellId)
    {
        m_arrivalCellId = cCellId;
        if (const uint32_t cSlot = ArrivalSlot())
            m_playerBridge.StepPlayerAside(cSlot);
    }

    // Following: it arrived where the tether sent it, the latest place or an older one (a stop on the way; the
    // engine runs only the latest queued move, so some are never reached). Arriving anywhere else is its own
    // travel: it leads, and what the tether had queued is superseded.
    const auto cReached =
        std::find_if(m_tetherTargets.rbegin(), m_tetherTargets.rend(), [this](const TetherTarget& acTarget) {
            return IsAroundPlayer(acTarget.WorldSpaceId, acTarget.CellId, acTarget.Position);
        });
    const bool cFollowing = cReached != m_tetherTargets.rend();
    m_tetherTargets.erase(m_tetherTargets.begin(), cFollowing ? cReached.base() : m_tetherTargets.end());
    using Travel = EnterCellRequest::Travel;
    const Travel cHow = cFollowing ? Travel::kTether : m_leading ? Travel::kOwn : Travel::kBeforeJoining;
    m_dispatcher.trigger(PlayerArrivedEvent{cCellId, m_playerBridge.GetPlayerWorldSpaceId(), cHow});
}

void TravelService::OnPartyTravel(const NotifyPartyTravel& acMessage) noexcept
{
    // Already there, unless a move of the tether's is still to come: that one would take it away again.
    if (m_tetherTargets.empty() && IsAroundPlayer(acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position))
    {
        spdlog::info("Party travel to cell {:X} (worldspace {:X}): already there", acMessage.CellId.BaseId,
                     acMessage.WorldSpaceId.BaseId);
        return;
    }
    // On its way there already (the host sends a follower on when it stops elsewhere first).
    if (!m_tetherTargets.empty() &&
        SamePlace(m_tetherTargets.back().WorldSpaceId, m_tetherTargets.back().CellId, m_tetherTargets.back().Position,
                  acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position))
        return;
    const uint32_t cCellId = CellAt(acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position);
    if (!cCellId)
    {
        spdlog::error("Party travel to cell {:X} (worldspace {:X}): no such cell here", acMessage.CellId.BaseId,
                      acMessage.WorldSpaceId.BaseId);
        return;
    }
    // A MoveTo of the player is a travel like a door's (TESObjectREFR::MoveTo_Impl 56626 queues it, 40442).
    m_tetherTargets.push_back({acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position});
    spdlog::info("Party travel: following to cell {:X} (worldspace {:X})", acMessage.CellId.BaseId,
                 acMessage.WorldSpaceId.BaseId);
    m_actorBridge.MoveActorTo(m_playerBridge.GetPlayerFormId(), cCellId, acMessage.Position.x, acMessage.Position.y,
                              acMessage.Position.z);
}

uint32_t TravelService::ArrivalSlot() const noexcept
{
    if (m_world.GetHostService().IsHosting())
        return 0;
    const uint32_t cMe = m_transport.GetLocalPlayerId();
    uint32_t slot = 1;
    for (const auto& [cId, cPlayer] : m_world.GetSessionService().GetPlayers())
        if (!cPlayer.IsHost && cId < cMe)
            ++slot;
    return slot;
}
