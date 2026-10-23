#include <Services/ActorValueService.h>
#include <World.h>
#include <Components.h>
#include <SessionIds.h>

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
    RunPoolUpdates();
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

        const bool isPlayer = m_actorBridge.IsOtherPlayer(formId);

        const int32_t cCount = m_actorBridge.GetActorValueCount();
        for (int i = 0; i < cCount; i++)
        {
            if (isPlayer && i == 133 /* ActorValueInfo::kDragonSouls */)
                continue;

            float newValue = m_actorBridge.GetActorValue(formId, i);
            float oldValue = actorValuesComponent.CurrentActorValues.ActorValuesList[i];
            // Health, magicka and stamina always go out, so copies converge even while the owner's
            // value is unchanged.
            if (newValue != oldValue || (i >= 24 && i <= 26) /* kHealth, kMagicka, kStamina */)
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

    // Our own actors' health reaches everyone through RunPoolUpdates. Only a hit on an actor
    // someone else owns is sent: as a request the server forwards to that owner.
    if (m_world.all_of<LocalComponent>(*hitteeIt))
        return;

    std::optional<uint32_t> sessionIdRes = Utils::GetSessionId(*hitteeIt);
    if (!sessionIdRes.has_value())
    {
        spdlog::error("{}: failed to find session id", __FUNCTION__);
        return;
    }

    uint32_t sessionId = sessionIdRes.value();

    if (acEvent.DeltaHealth > -1.0f && acEvent.DeltaHealth < 1.0f)
    {
        if (m_smallHealthChanges.find(sessionId) == m_smallHealthChanges.end())
            m_smallHealthChanges[sessionId] = acEvent.DeltaHealth;
        else
            m_smallHealthChanges[sessionId] += acEvent.DeltaHealth;
        return;
    }

    RequestHealthChangeBroadcast requestHealthChange;
    requestHealthChange.Id = sessionId;
    requestHealthChange.DeltaHealth = acEvent.DeltaHealth;
    if (acEvent.AttackerId)
    {
        const auto attackerIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.AttackerId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == id; });
        if (attackerIt != std::end(view))
            if (const auto cAttackerSessionId = Utils::GetSessionId(*attackerIt))
            {
                requestHealthChange.HasAttacker = true;
                requestHealthChange.AttackerId = *cAttackerSessionId;
            }
    }

    m_transport.Send(requestHealthChange);

    spdlog::debug("Sent out delta health through collection: {:X}:{:f}", sessionId, acEvent.DeltaHealth);
}

void ActorValueService::RunSmallHealthUpdates() noexcept
{
    constexpr auto cDelayBetweenUpdates = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastSmallHealthSend < cDelayBetweenUpdates)
        return;

    m_lastSmallHealthSend = now;

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

// Direct authority: the owner's health, magicka and stamina are the truth (copies neither spend nor
// regenerate). Send each within 100ms of a change (the 1s BroadcastActorValues resends them regardless).
void ActorValueService::RunPoolUpdates() noexcept
{
    if (!m_transport.IsConnected())
        return;

    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastPoolSend < 100ms)
        return;
    m_lastPoolSend = now;

    auto view = m_world.view<FormIdComponent, LocalComponent, ActorValuesComponent>();
    for (auto entity : view)
    {
        const uint32_t cFormId = view.get<FormIdComponent>(entity).Id;
        if (!m_actorBridge.ActorExists(cFormId))
            continue;

        auto& values = view.get<ActorValuesComponent>(entity).CurrentActorValues.ActorValuesList;
        RequestActorValueChanges request;
        for (const int32_t cIndex : {24, 25, 26} /* kHealth, kMagicka, kStamina */)
        {
            const float cValue = m_actorBridge.GetActorValue(cFormId, cIndex);
            if (values[cIndex] == cValue)
                continue;
            values[cIndex] = cValue;
            request.Values.insert({cIndex, cValue});
        }
        if (request.Values.empty())
            continue;

        request.Id = view.get<LocalComponent>(entity).Id;
        m_transport.Send(request);
    }
}

void ActorValueService::RunDeathStateUpdates() noexcept
{
    constexpr auto cDelayBetweenUpdates = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastDeathStateSend < cDelayBetweenUpdates)
        return;

    m_lastDeathStateSend = now;

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
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastValuesSend < cDelayBetweenUpdates)
        return;

    m_lastValuesSend = now;

    BroadcastActorValues();
}

void ActorValueService::OnHealthChangeBroadcast(const NotifyHealthChangeBroadcast& acMessage) const noexcept
{
    const uint32_t formId = Utils::GetFormIdBySessionId(acMessage.Id);
    if (formId == 0)
    {
        spdlog::error("{}: could not find actor session id {:X}", __FUNCTION__, acMessage.Id);
        return;
    }

    // The server only forwards hits to the actor's owner; never apply one to a copy.
    if (m_actorBridge.IsActorCopy(formId))
        return;

    const uint32_t cAttackerFormId = acMessage.HasAttacker ? Utils::GetFormIdBySessionId(acMessage.AttackerId) : 0;
    if (acMessage.DeltaHealth < 0.f)
    {
        // As the engine's own hit takes it: its death rules, death animation and kill credit.
        if (!m_actorBridge.DamageActorHealth(formId, cAttackerFormId, -acMessage.DeltaHealth))
        {
            spdlog::debug("Forwarded hit on {:X} by {:X} not applied (gone, or held by a kill move)", formId, cAttackerFormId);
            return;
        }
    }
    else
    {
        // kHealth = 24 (ActorValueInfo::kHealth)
        const float cNewHealth = m_actorBridge.GetActorValue(formId, 24) + acMessage.DeltaHealth;
        m_actorBridge.ForceActorValue(formId, 2 /* ForceMode::DAMAGE */, 24, cNewHealth);
    }
    spdlog::debug("Applied forwarded hit on {:X}: delta {:f}, health now {:f}, attacker {:X}", formId, acMessage.DeltaHealth,
                  m_actorBridge.GetActorValue(formId, 24), cAttackerFormId);

    if (cAttackerFormId && acMessage.DeltaHealth < 0.f && m_actorBridge.GetActorValue(formId, 24) > 0.f)
    {
        // The hit happened in the attacker's game: react here as if it had landed here.
        m_actorBridge.ReactToAttack(formId, cAttackerFormId);
    }
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
            // The owner's value as is: a copy is in deferred kill (HookUpdateAI), its death is its owner's.
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
    // Not at once: a copy dies with its owner's replayed death action (AnimationSystem), 300ms behind. The
    // state applies to a copy still alive after that, e.g. an owner ragdolled to death with no action.
    m_pendingDeathStates[acMessage.Id] = acMessage.IsDead;
    m_nextDeathStateRetry = std::chrono::steady_clock::now() + 500ms;
}

void ActorValueService::ApplyPendingDeathStates() noexcept
{
    std::vector<uint32_t> done;
    for (const auto& [cSessionId, cIsDead] : m_pendingDeathStates)
    {
        const uint32_t cFormId = Utils::GetFormIdBySessionId(cSessionId);
        // Gone, not a copy, or a player (players are never killed): nothing to apply.
        if (cFormId == 0 || !m_actorBridge.ActorExists(cFormId) || !m_actorBridge.IsActorCopy(cFormId) ||
            m_actorBridge.IsOtherPlayer(cFormId) || m_actorBridge.IsActorDead(cFormId) == cIsDead)
        {
            done.push_back(cSessionId);
            continue;
        }

        if (cIsDead)
            m_actorBridge.KillActor(cFormId);
        else
            m_actorBridge.ResurrectActor(cFormId);
    }
    for (const uint32_t cSessionId : done)
        m_pendingDeathStates.erase(cSessionId);
}
