#include <TiltedOnlinePCH.h>

#include <Services/ActorValueService.h>
#include <World.h>
#include <Components.h>
#include <Utils.h>

#include <Events/UpdateEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/HealthChangeEvent.h>

#include <Messages/NotifyActorValueChanges.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/RequestDeathStateChange.h>

#include <Bridges/IActorBridge.h>

ActorValueService::ActorValueService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_actorBridge(aWorld.ctx().at<IActorBridge&>())
{
    m_world.on_construct<LocalComponent>().connect<&ActorValueService::OnLocalComponentAdded>(this);
    m_dispatcher.sink<DisconnectedEvent>().connect<&ActorValueService::OnDisconnected>(this);
    m_dispatcher.sink<ActorRemovedEvent>().connect<&ActorValueService::OnActorRemoved>(this);
    m_dispatcher.sink<UpdateEvent>().connect<&ActorValueService::OnUpdate>(this);
    m_dispatcher.sink<NotifyActorValueChanges>().connect<&ActorValueService::OnActorValueChanges>(this);
    m_dispatcher.sink<NotifyActorMaxValueChanges>().connect<&ActorValueService::OnActorMaxValueChanges>(this);
    m_dispatcher.sink<HealthChangeEvent>().connect<&ActorValueService::OnHealthChange>(this);
    m_dispatcher.sink<NotifyHealthChangeBroadcast>().connect<&ActorValueService::OnHealthChangeBroadcast>(this);
    m_dispatcher.sink<NotifyDeathStateChange>().connect<&ActorValueService::OnDeathStateChange>(this);
}

void ActorValueService::CreateActorValuesComponent(const entt::entity aEntity, uint32_t aFormId) noexcept
{
    auto& actorValuesComponent = m_world.emplace_or_replace<ActorValuesComponent>(aEntity);

    const int32_t cCount = m_actorBridge.GetActorValueCount();
    for (int i = 0; i < cCount; i++)
    {
        float value = m_actorBridge.GetActorValue(aFormId, i);
        actorValuesComponent.CurrentActorValues.ActorValuesList.insert({i, value});
        float maxValue = m_actorBridge.GetActorPermanentValue(aFormId, i);
        actorValuesComponent.CurrentActorValues.ActorMaxValuesList.insert({i, maxValue});
    }
}

void ActorValueService::OnLocalComponentAdded(entt::registry& aRegistry, const entt::entity aEntity) noexcept
{
    const auto& formIdComponent = aRegistry.get<FormIdComponent>(aEntity);
    const uint32_t formId = formIdComponent.Id;

    if (m_actorBridge.ActorExists(formId))
    {
        CreateActorValuesComponent(aEntity, formId);
        aRegistry.get<ActorValuesComponent>(aEntity).IsDead = m_actorBridge.IsActorDead(formId);
    }
}

void ActorValueService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    // TODO: this crashes sometimes, no clue why
    m_world.clear<ActorValuesComponent>();
}

void ActorValueService::OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();
    const uint32_t formId = acEvent.FormId;

    const auto it = std::find_if(
        std::begin(view), std::end(view),
        [view, formId](auto entity)
        {
            const auto& formIdComponent = view.get<FormIdComponent>(entity);

            return formIdComponent.Id == formId;
        });

    if (it != std::end(view))
        m_world.remove<ActorValuesComponent>(*it);
}

void ActorValueService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    RunSmallHealthUpdates();
    RunHealthUpdates();
    RunDeathStateUpdates();
    RunActorValuesUpdates();

    const auto cNow = std::chrono::steady_clock::now();
    if (!m_pendingDeathStates.empty() && cNow >= m_nextDeathStateRetry)
    {
        m_nextDeathStateRetry = cNow + 500ms;
        ApplyPendingDeathStates();
    }
}

void ActorValueService::BroadcastActorValues() noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent, LocalComponent, ActorValuesComponent>();

    for (auto entity : view)
    {
        auto& formIdComponent = view.get<FormIdComponent>(entity);
        const uint32_t formId = formIdComponent.Id;

        if (!m_actorBridge.ActorExists(formId))
            continue;

        auto& localComponent = view.get<LocalComponent>(entity);
        auto& actorValuesComponent = view.get<ActorValuesComponent>(entity);

        RequestActorValueChanges requestValueChanges;
        requestValueChanges.Id = localComponent.Id;
        RequestActorMaxValueChanges requestMaxValueChanges;
        requestMaxValueChanges.Id = localComponent.Id;

        const bool isPlayer = m_actorBridge.IsRemotePlayer(formId);

        const int32_t cCount = m_actorBridge.GetActorValueCount();
        for (int i = 0; i < cCount; i++)
        {
            if (isPlayer && i == 133 /* ActorValueInfo::kDragonSouls */)
                continue;

            float newValue = m_actorBridge.GetActorValue(formId, i);
            float oldValue = actorValuesComponent.CurrentActorValues.ActorValuesList[i];
            // Health always goes out: copies mirror the owner's value, so a copy that drifted
            // is corrected even while the owner's health is unchanged.
            if (newValue != oldValue || i == 24 /* kHealth */)
            {
                requestValueChanges.Values.insert({i, newValue});
                actorValuesComponent.CurrentActorValues.ActorValuesList[i] = newValue;
            }

            float newMaxValue = m_actorBridge.GetActorPermanentValue(formId, i);
            float oldMaxValue = actorValuesComponent.CurrentActorValues.ActorMaxValuesList[i];
            // Health, magicka and stamina maxima always go out too, so copies converge.
            if (newMaxValue != oldMaxValue || (i >= 24 && i <= 26))
            {
                requestMaxValueChanges.Values.insert({i, newMaxValue});
                actorValuesComponent.CurrentActorValues.ActorMaxValuesList[i] = newMaxValue;
            }
        }

        if (requestValueChanges.Values.size() > 0)
        {
            m_transport.Send(requestValueChanges);
        }

        if (requestMaxValueChanges.Values.size() > 0)
        {
            m_transport.Send(requestMaxValueChanges);
        }
    }
}

void ActorValueService::OnHealthChange(const HealthChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto hitteeIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.HitteeId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (hitteeIt == std::end(view))
    {
        spdlog::warn("Health change event form id component not found, form id: {:X}", acEvent.HitteeId);
        return;
    }

    // Our own actors' health reaches everyone through RunHealthUpdates. Only a hit on an actor
    // someone else owns is sent: as a request the server forwards to that owner.
    if (m_world.all_of<LocalComponent>(*hitteeIt))
        return;

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*hitteeIt);
    if (!serverIdRes.has_value())
    {
        spdlog::error("{}: failed to find server id", __FUNCTION__);
        return;
    }

    uint32_t serverId = serverIdRes.value();

    if (acEvent.DeltaHealth > -1.0f && acEvent.DeltaHealth < 1.0f)
    {
        if (m_smallHealthChanges.find(serverId) == m_smallHealthChanges.end())
            m_smallHealthChanges[serverId] = acEvent.DeltaHealth;
        else
            m_smallHealthChanges[serverId] += acEvent.DeltaHealth;
        return;
    }

    RequestHealthChangeBroadcast requestHealthChange;
    requestHealthChange.Id = serverId;
    requestHealthChange.DeltaHealth = acEvent.DeltaHealth;
    if (acEvent.AttackerId)
    {
        const auto attackerIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.AttackerId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });
        if (attackerIt != std::end(view))
            if (const auto cAttackerServerId = Utils::GetServerId(*attackerIt))
            {
                requestHealthChange.HasAttacker = true;
                requestHealthChange.AttackerId = *cAttackerServerId;
            }
    }

    m_transport.Send(requestHealthChange);

    spdlog::debug("Sent out delta health through collection: {:X}:{:f}", serverId, acEvent.DeltaHealth);
}

void ActorValueService::RunSmallHealthUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    if (!m_smallHealthChanges.empty())
    {
        for (auto& value : m_smallHealthChanges)
        {
            RequestHealthChangeBroadcast requestHealthChange;
            requestHealthChange.Id = value.first;
            requestHealthChange.DeltaHealth = value.second;

            m_transport.Send(requestHealthChange);

            spdlog::debug("Sent out delta health through timer, {:X}:{:f}", value.first, value.second);
        }

        m_smallHealthChanges.clear();
    }
}

// Direct authority: the owner's health is the truth. Send it within 100ms of a change (the
// 1s BroadcastActorValues resends it regardless).
void ActorValueService::RunHealthUpdates() noexcept
{
    if (!m_transport.IsConnected())
        return;

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < 100ms)
        return;
    lastSendTimePoint = now;

    auto view = m_world.view<FormIdComponent, LocalComponent, ActorValuesComponent>();
    for (auto entity : view)
    {
        const uint32_t cFormId = view.get<FormIdComponent>(entity).Id;
        if (!m_actorBridge.ActorExists(cFormId))
            continue;

        auto& values = view.get<ActorValuesComponent>(entity).CurrentActorValues.ActorValuesList;
        const float cHealth = m_actorBridge.GetActorValue(cFormId, 24 /* kHealth */);
        if (values[24] == cHealth)
            continue;
        values[24] = cHealth;

        RequestActorValueChanges request;
        request.Id = view.get<LocalComponent>(entity).Id;
        request.Values.insert({24, cHealth});
        m_transport.Send(request);
    }
}

void ActorValueService::RunDeathStateUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    auto localView = m_world.view<FormIdComponent, LocalComponent, ActorValuesComponent>();

    for (auto entity : localView)
    {
        const auto& formIdComponent = localView.get<FormIdComponent>(entity);
        const uint32_t formId = formIdComponent.Id;

        if (!m_actorBridge.ActorExists(formId))
            continue;

        auto& localComponent = localView.get<LocalComponent>(entity);
        auto& actorValuesComponent = localView.get<ActorValuesComponent>(entity);

        bool isDead = m_actorBridge.IsActorDead(formId);
        if (isDead != actorValuesComponent.IsDead)
        {
            actorValuesComponent.IsDead = isDead;

            RequestDeathStateChange requestChange;
            requestChange.Id = localComponent.Id;
            requestChange.IsDead = isDead;

            m_transport.Send(requestChange);
        }
    }
}

void ActorValueService::RunActorValuesUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    BroadcastActorValues();
}

void ActorValueService::OnHealthChangeBroadcast(const NotifyHealthChangeBroadcast& acMessage) const noexcept
{
    const uint32_t formId = Utils::GetFormIdByServerId(acMessage.Id);
    if (formId == 0)
    {
        spdlog::error("{}: could not find actor server id {:X}", __FUNCTION__, acMessage.Id);
        return;
    }

    // The server only forwards hits to the actor's owner; never apply one to a copy.
    if (m_actorBridge.IsActorRemote(formId))
        return;

    // kHealth = 24 (ActorValueInfo::kHealth)
    const float newHealth = m_actorBridge.GetActorValue(formId, 24) + acMessage.DeltaHealth;
    m_actorBridge.ForceActorValue(formId, 2 /* ForceMode::DAMAGE */, 24, newHealth);

    const float health = m_actorBridge.GetActorValue(formId, 24);
    const uint32_t cAttackerFormId = acMessage.HasAttacker ? Utils::GetFormIdByServerId(acMessage.AttackerId) : 0;
    spdlog::debug("Applied forwarded hit on {:X}: delta {:f}, health now {:f}, attacker {:X}", formId, acMessage.DeltaHealth, health, cAttackerFormId);
    if (!m_actorBridge.IsActorDead(formId) && health <= 0.f)
    {
        // Players should never be killed
        if (!m_actorBridge.IsRemotePlayer(formId))
            m_actorBridge.KillActorBy(formId, cAttackerFormId);
    }
    else if (cAttackerFormId && acMessage.DeltaHealth < 0.f)
    {
        // The hit happened in the attacker's game: react here as if it had landed here.
        m_actorBridge.ReactToAttack(formId, cAttackerFormId);
    }

    // TODO(cosideci): find fix for player health sync so this can be used again
    /*
    if (m_actorBridge.IsRemotePlayer(formId))
        World::Get().GetOverlayService().SetPlayerHealthPercentage(formId);
    */
}

void ActorValueService::OnActorValueChanges(const NotifyActorValueChanges& acMessage) const noexcept
{
    auto view = m_world.view<FormIdComponent, RemoteComponent>();

    const auto itor = std::find_if(std::begin(view), std::end(view), [id = acMessage.Id, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

    if (itor == std::end(view))
        return;

    auto& formIdComponent = view.get<FormIdComponent>(*itor);
    const uint32_t formId = formIdComponent.Id;

    if (!m_actorBridge.ActorExists(formId))
        return;

    for (const auto& [key, value] : acMessage.Values)
    {
        // Syncing dragon souls triggers "Dragon soul collected" event
        if (key == 133 /* kDragonSouls */)
            continue;

        spdlog::debug("Actor value update, server ID: {:X}, key: {}, value: {}", acMessage.Id, key, value);

        if (key == 26 /* kStamina */ || key == 25 /* kMagicka */ || key == 24 /* kHealth */)
        {
            m_actorBridge.ForceActorValue(formId, 2 /* ForceMode::DAMAGE */, key, value);
            continue;
        }
        m_actorBridge.SetActorValue(formId, key, value);
    }
}

void ActorValueService::OnActorMaxValueChanges(const NotifyActorMaxValueChanges& acMessage) const noexcept
{
    auto view = m_world.view<FormIdComponent, RemoteComponent>();

    const auto it = std::find_if(std::begin(view), std::end(view), [id = acMessage.Id, view](entt::entity entity) { return view.get<RemoteComponent>(entity).Id == id; });

    if (it == std::end(view))
        return;

    auto& formIdComponent = view.get<FormIdComponent>(*it);
    const uint32_t formId = formIdComponent.Id;

    if (!m_actorBridge.ActorExists(formId))
        return;

    for (const auto& [key, value] : acMessage.Values)
    {
        if (key == 133 /* kDragonSouls */)
            continue;

        spdlog::debug("Actor max value update, server ID: {:X}, key: {}, value: {}", acMessage.Id, key, value);

        m_actorBridge.ForceActorValue(formId, 0 /* ForceMode::PERMANENT */, key, value);
    }
}

void ActorValueService::OnDeathStateChange(const NotifyDeathStateChange& acMessage) noexcept
{
    m_pendingDeathStates[acMessage.Id] = acMessage.IsDead;
    ApplyPendingDeathStates();
}

void ActorValueService::ApplyPendingDeathStates() noexcept
{
    Vector<uint32_t> done;
    for (const auto& [cServerId, cIsDead] : m_pendingDeathStates)
    {
        const uint32_t cFormId = Utils::GetFormIdByServerId(cServerId);
        // Gone, not a copy, or a player (players are never killed): nothing to apply.
        if (cFormId == 0 || !m_actorBridge.ActorExists(cFormId) || !m_actorBridge.IsActorRemote(cFormId) ||
            m_actorBridge.IsRemotePlayer(cFormId) || m_actorBridge.IsActorDead(cFormId) == cIsDead)
        {
            done.push_back(cServerId);
            continue;
        }

        if (cIsDead)
            m_actorBridge.KillActor(cFormId);
        else
            m_actorBridge.ResurrectActor(cFormId);
    }
    for (const uint32_t cServerId : done)
        m_pendingDeathStates.erase(cServerId);
}
