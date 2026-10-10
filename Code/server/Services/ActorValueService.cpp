#include <Components.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/RequestDeathStateChange.h>
#include <Services/ActorValueService.h>
#include <World.h>
#include <GameServer.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyDeathStateChange.h>

namespace Server
{

ActorValueService::ActorValueService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_updateHealthConnection = aDispatcher.sink<PacketEvent<RequestActorValueChanges>>().connect<&ActorValueService::OnActorValueChanges>(this);
    m_updateMaxValueConnection = aDispatcher.sink<PacketEvent<RequestActorMaxValueChanges>>().connect<&ActorValueService::OnActorMaxValueChanges>(this);
    m_updateDeltaHealthConnection = aDispatcher.sink<PacketEvent<RequestHealthChangeBroadcast>>().connect<&ActorValueService::OnHealthChangeBroadcast>(this);
    m_deathStateConnection = aDispatcher.sink<PacketEvent<RequestDeathStateChange>>().connect<&ActorValueService::OnDeathStateChange>(this);
}

void ActorValueService::OnActorValueChanges(const PacketEvent<RequestActorValueChanges>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    if (it != actorValuesView.end())
    {
        auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
        for (auto& [id, value] : message.Values)
        {
            actorValuesComponent.CurrentActorValues.ActorValuesList[id] = value;
        }
    }

    NotifyActorValueChanges notify;
    notify.Id = acMessage.Packet.Id;
    notify.Values = acMessage.Packet.Values;

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

void ActorValueService::OnActorMaxValueChanges(const PacketEvent<RequestActorMaxValueChanges>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    if (it != actorValuesView.end())
    {
        auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
        for (auto& [id, value] : message.Values)
        {
            actorValuesComponent.CurrentActorValues.ActorMaxValuesList[id] = value;
        }
    }

    NotifyActorMaxValueChanges notify;
    notify.Id = message.Id;
    notify.Values = message.Values;

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

void ActorValueService::OnHealthChangeBroadcast(const PacketEvent<RequestHealthChangeBroadcast>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    // Direct authority: a hit on someone else's actor is a request for that actor's owner (the
    // host for NPCs). Only the owner applies it; everyone else gets the owner's resulting health
    // through RequestActorValueChanges, which also keeps this server's copy current.
    const auto ownerView = m_world.view<OwnerComponent>();
    const auto it = ownerView.find(static_cast<entt::entity>(message.Id));
    if (it == ownerView.end())
        return;

    Player* pOwner = ownerView.get<OwnerComponent>(*it).GetOwner();
    if (!pOwner || pOwner == acMessage.pPlayer)
        return;

    NotifyHealthChangeBroadcast notify;
    notify.Id = message.Id;
    notify.DeltaHealth = message.DeltaHealth;
    pOwner->Send(notify);
}

void ActorValueService::OnDeathStateChange(const PacketEvent<RequestDeathStateChange>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto characterView = m_world.view<CharacterComponent, OwnerComponent>();

    const auto it = characterView.find(static_cast<entt::entity>(message.Id));

    if (it != characterView.end())
    {
        auto& characterComponent = characterView.get<CharacterComponent>(*it);
        characterComponent.SetDead(message.IsDead);
        spdlog::debug("Updating death state {:x}:{}", message.Id, message.IsDead);
    }

    NotifyDeathStateChange notify;
    notify.Id = message.Id;
    notify.IsDead = message.IsDead;

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

} // namespace Server
