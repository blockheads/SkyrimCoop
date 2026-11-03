#include <Services/PartyMarkerService.h>

#include <Bridges/IActorBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/PlayerComponent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/PartyMarkers.h>
#include <Services/SessionService.h>
#include <World.h>
#include <algorithm>
#include <unordered_set>

PartyMarkerService::PartyMarkerService(World& aWorld, entt::dispatcher& aDispatcher, IActorBridge& aActors,
                                       IPlayerBridge& aPlayer) noexcept
    : m_world(aWorld), m_actors(aActors), m_player(aPlayer)
{
    Listen<UpdateEvent, &PartyMarkerService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &PartyMarkerService::OnDisconnected>(m_connections, aDispatcher, this);
}

PartyMarkerService::~PartyMarkerService() noexcept
{
    m_player.SetPartyMarkers({});
}

void PartyMarkerService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_player.SetPartyMarkers({});
}

void PartyMarkerService::OnUpdate(const UpdateEvent&) noexcept
{
    PartyMarkerSet next;
    auto& transport = m_world.GetTransport();
    const auto& session = m_world.GetSessionService();
    if (transport.IsConnected() && !session.IsEnding() && m_player.IsPlayerLoaded())
    {
        next.PlayerCellId = m_player.GetPlayerParentCellId();
        next.PlayerWorldSpaceId = m_player.GetPlayerWorldSpaceId();
        m_player.GetPlayerPosition(next.PlayerPosition.x, next.PlayerPosition.y, next.PlayerPosition.z);
        std::unordered_set<uint32_t> seen;
        for (auto [entity, player, form] : m_world.view<PlayerComponent, FormIdComponent>().each())
        {
            const auto member = session.GetPlayers().find(player.Id);
            if (member == session.GetPlayers().end() || player.Id == transport.GetLocalPlayerId() ||
                form.Id == m_player.GetPlayerFormId() || !m_actors.ActorExists(form.Id) || !seen.insert(player.Id).second)
                continue;
            PartyMarker marker;
            marker.PlayerId = player.Id;
            marker.FormId = form.Id;
            marker.Name = member->second.Name;
            marker.CellId = m_actors.GetActorParentCellFormId(form.Id);
            marker.WorldSpaceId = m_actors.GetActorWorldSpaceFormId(form.Id);
            m_actors.GetActorPosition(form.Id, marker.Position.x, marker.Position.y, marker.Position.z);
            float x{}, y{};
            m_actors.GetActorRotation(form.Id, x, y, marker.Heading);
            if (std::isfinite(marker.Position.x) && std::isfinite(marker.Position.y) &&
                std::isfinite(marker.Position.z) && std::isfinite(marker.Heading))
                next.Members.push_back(std::move(marker));
        }
        std::sort(next.Members.begin(), next.Members.end(), [](const auto& acLeft, const auto& acRight) {
            return acLeft.PlayerId < acRight.PlayerId;
        });
    }
    m_player.SetPartyMarkers(next);
}
