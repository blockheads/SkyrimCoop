#include <Bridges/IActorBridge.h>
#include <Bridges/IPlayerBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/PlayerComponent.h>
#include <Events/ActorDamagedEvent.h>
#include <Events/UpdateEvent.h>
#include <Services/Host/HostAggro.h>
#include <Services/SessionService.h>
#include <World.h>
#include <algorithm>
#include <cmath>

HostAggro::HostAggro(World& aWorld, entt::dispatcher& aDispatcher, IActorBridge& aActors,
                     std::function<void(const PartyThreat::State&)> aPublish) noexcept
    : m_world(aWorld), m_actors(aActors), m_publish(std::move(aPublish))
{
    Listen<ActorDamagedEvent, &HostAggro::OnDamage>(m_connections, aDispatcher, this);
}
HostAggro::~HostAggro() noexcept
{
    Clear();
}
void HostAggro::Clear() noexcept
{
    m_state = {};
    m_publish(m_state);
}
void HostAggro::OnDamage(const ActorDamagedEvent& acEvent) noexcept
{
    if (m_state.Active && m_state.IsPlayer(acEvent.AttackerId) && !m_state.IsPlayer(acEvent.VictimId) &&
        m_actors.ActorExists(acEvent.VictimId) && !m_actors.IsActorCopy(acEvent.VictimId))
        m_state.Hit(acEvent.VictimId, acEvent.AttackerId, acEvent.Damage);
}
void HostAggro::Update(const UpdateEvent& acEvent) noexcept
{
    if (!m_world.GetTransport().IsConnected() || m_world.GetSessionService().IsEnding())
        return Clear();
    m_state.Active = true;
    if (std::isfinite(acEvent.Delta) && acEvent.Delta > 0)
        m_state.Now += acEvent.Delta;
    m_state.Players = {m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId()};
    for (auto [entity, player, form] : m_world.view<PlayerComponent, FormIdComponent>().each())
        if (m_world.GetSessionService().GetPlayers().contains(player.Id) && m_actors.ActorExists(form.Id))
            m_state.Players.push_back(form.Id);
    std::sort(m_state.Players.begin(), m_state.Players.end());
    m_state.Players.erase(std::unique(m_state.Players.begin(), m_state.Players.end()), m_state.Players.end());
    std::erase_if(m_state.DamageByPair, [this](const auto& acEntry) {
        const uint32_t cActor = acEntry.first >> 32, cPlayer = uint32_t(acEntry.first);
        return !m_state.IsPlayer(cPlayer) || !m_actors.ActorExists(cActor) || m_actors.IsActorDead(cActor) ||
               m_actors.IsActorCopy(cActor) ||
               (!m_actors.GetActorAiFields(cActor).CombatState &&
                m_state.Now - acEntry.second.At > PartyThreat::kRecentHit);
    });
    std::unordered_map<uint32_t, PartyThreat::Combat> combats;
    for (const auto& [cPair, cDamage] : m_state.DamageByPair)
    {
        const uint32_t cActor = cPair >> 32;
        const uint32_t cTarget = m_actors.GetActorAiFields(cActor).CombatTargetId;
        const auto cOld = m_state.Combats.find(cActor);
        combats[cActor] = cOld != m_state.Combats.end() && cOld->second.Target == cTarget
                              ? cOld->second
                              : PartyThreat::Combat{cTarget, m_state.Now};
    }
    m_state.Combats = std::move(combats);
    m_publish(m_state);
}
