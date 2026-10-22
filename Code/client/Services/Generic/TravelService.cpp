#include <Services/TravelService.h>

#include <Events/PlayerArrivedEvent.h>
#include <Events/UpdateEvent.h>

#include <Messages/NotifyPartyTravel.h>
#include <Structs/GridCellCoords.h>

#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Services/TransportService.h>
#include <Components.h>
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
    UpdateStack();
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

    // The party arrives on one spot (a door's, a coc's, the leader's): its players stack there, passing through
    // each other until each walks off (docs/design/host-authority-migration.md, "Party tether").
    if (cCellId != m_arrivalCellId)
    {
        m_arrivalCellId = cCellId;
        glm::vec3 spot;
        m_playerBridge.GetPlayerPosition(spot.x, spot.y, spot.z);
        StackOn(spot);
    }

    // Following: it arrived where the tether sent it, the latest place or an older one (a stop on the way; the
    // engine runs only the latest queued move, so some are never reached). Arriving anywhere else is its own
    // travel: it leads, and what the tether had queued is superseded.
    const auto cReached =
        std::find_if(m_tetherTargets.rbegin(), m_tetherTargets.rend(), [this](const TetherTarget& acTarget) {
            return IsAroundPlayer(acTarget.WorldSpaceId, acTarget.CellId, acTarget.Position);
        });
    const bool cFollowing = cReached != m_tetherTargets.rend();
    // A follower's view is the leader's: the same spot, facing the same way.
    if (cFollowing)
        m_playerBridge.SetPlayerHeading(cReached->Heading);
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
    m_tetherTargets.push_back({acMessage.WorldSpaceId, acMessage.CellId, acMessage.Position, acMessage.Heading});
    spdlog::info("Party travel: following to cell {:X} (worldspace {:X})", acMessage.CellId.BaseId,
                 acMessage.WorldSpaceId.BaseId);
    m_actorBridge.MoveActorTo(m_playerBridge.GetPlayerFormId(), cCellId, acMessage.Position.x, acMessage.Position.y,
                              acMessage.Position.z);
}

void TravelService::StackOn(const glm::vec3& acSpot) noexcept
{
    // Every player: a follower is still on its way.
    m_stack = Stack{acSpot, std::chrono::steady_clock::now(), {{m_playerBridge.GetPlayerFormId(), false}}};
    for (const entt::entity cEntity : m_world.view<FormIdComponent, RemoteComponent, PlayerComponent>())
        m_stack->Players.push_back({m_world.get<FormIdComponent>(cEntity).Id, false});
    UpdateStack();
}

void TravelService::UpdateStack() noexcept
{
    std::vector<uint32_t> stacked;
    if (m_stack)
    {
        // Within a step of the spot is on it; a copy coming from afar has a few seconds to get there, and one
        // off it in this cell for a moment has walked off (a stale follow takes a player out and back).
        constexpr float cOnSpot = 60.f;
        constexpr auto cArrival = std::chrono::seconds(5);
        constexpr auto cWalkOff = std::chrono::milliseconds(500);
        const auto cNow = std::chrono::steady_clock::now();
        const bool cLate = cNow - m_stack->Since > cArrival;
        const auto cHere = PlayersHere();
        std::erase_if(m_stack->Players, [&](Stack::Player& aPlayer) {
            const auto cAt = cHere.find(aPlayer.FormId);
            const float cDistance =
                cAt == cHere.end() ? 0.f : glm::distance(glm::vec2(cAt->second), glm::vec2(m_stack->Spot));
            const bool cOff = cAt != cHere.end() && cDistance >= cOnSpot;
            aPlayer.Arrived = aPlayer.Arrived || (cAt != cHere.end() && !cOff);
            if (!aPlayer.Arrived)
            {
                if (cLate)
                    spdlog::info("Stack: {:X} never came", aPlayer.FormId);
                return cLate;
            }
            if (!cOff)
            {
                aPlayer.OffSince = {};
                return false;
            }
            if (aPlayer.OffSince == std::chrono::steady_clock::time_point{})
                aPlayer.OffSince = cNow;
            if (cNow - aPlayer.OffSince < cWalkOff)
                return false;
            spdlog::info("Stack: {:X} walked off ({:.0f} from the spot)", aPlayer.FormId, cDistance);
            return true;
        });
        // One off the spot collides at once; it leaves the stack only if it stays off.
        for (const Stack::Player& cPlayer : m_stack->Players)
            if (cPlayer.OffSince == std::chrono::steady_clock::time_point{})
                stacked.push_back(cPlayer.FormId);
        if (m_stack->Players.empty())
            m_stack.reset();
    }

    // The stacked players pass through each other (every frame: a collision group is its current controller's).
    m_playerBridge.SetPassThrough(stacked);
}

std::unordered_map<uint32_t, glm::vec3> TravelService::PlayersHere() const noexcept
{
    std::unordered_map<uint32_t, glm::vec3> players;
    glm::vec3 position;
    m_playerBridge.GetPlayerPosition(position.x, position.y, position.z);
    players[m_playerBridge.GetPlayerFormId()] = position;
    auto view = m_world.view<FormIdComponent, RemoteComponent, PlayerComponent, OwnerPlaceComponent,
                             InterpolationComponent>();
    for (const entt::entity cEntity : view)
    {
        // A copy is where its owner is: one left behind in a cell its owner has left is not here.
        const auto& cPlace = view.get<OwnerPlaceComponent>(cEntity);
        if (!IsAroundPlayer(cPlace.WorldSpaceId, cPlace.CellId, view.get<InterpolationComponent>(cEntity).Position))
            continue;
        const uint32_t cFormId = view.get<FormIdComponent>(cEntity).Id;
        m_actorBridge.GetActorPosition(cFormId, position.x, position.y, position.z);
        players[cFormId] = position;
    }
    return players;
}
