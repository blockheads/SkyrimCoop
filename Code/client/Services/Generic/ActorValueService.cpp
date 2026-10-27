#include <Services/ActorValueService.h>
#include <World.h>
#include <Components.h>
#include <SessionIds.h>

#include <Events/UpdateEvent.h>
#include <Events/ActorRemovedEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/HealthChangeEvent.h>
#include <Events/ActorValueChangeEvent.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Games/DeathActions.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyRemoveCharacter.h>

#include <Messages/Relayed.h>

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
    m_dispatcher.sink<ActorValueChangeEvent>().connect<&ActorValueService::OnValueChanged>(this);
    m_dispatcher.sink<ActorDeathChangeEvent>().connect<&ActorValueService::OnDeathChanged>(this);
    m_dispatcher.sink<DeathActionReplayedEvent>().connect<&ActorValueService::OnDeathActionReplayed>(this);
    m_dispatcher.sink<ReferenceLoadedEvent>().connect<&ActorValueService::OnReferenceLoaded>(this);
    m_dispatcher.sink<NotifyCharacterSpawn>().connect<&ActorValueService::OnSpawn>(this);
    m_dispatcher.sink<NotifyRemoveCharacter>().connect<&ActorValueService::OnRemove>(this);
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
    m_world.clear<ActorValuesComponent>();
    m_smallHealthChanges.clear();
    m_changedValues.clear();
    m_changedDeaths.clear();
    m_copyDeathStates.clear();
    m_resurrections.clear();
    m_replayedDeaths.clear();
    m_copyValues.clear();
    m_copyPoolsPending.clear();
}

void ActorValueService::OnActorRemoved(const ActorRemovedEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();
    const uint32_t formId = acEvent.FormId;
    m_changedValues.erase(formId);
    m_changedDeaths.erase(formId);
    m_replayedDeaths.erase(formId);
    m_resurrections.erase(formId);

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

void ActorValueService::OnUpdate(const UpdateEvent&) noexcept
{
    RunSmallHealthUpdates();
    BroadcastActorValues();
    RunPoolUpdates();
    RunDeathStateUpdates();
    ApplyCopyPools();
}

void ActorValueService::OnValueChanged(const ActorValueChangeEvent& acEvent) noexcept
{
    m_changedValues[acEvent.FormId].insert(acEvent.Value);
    for (auto&& [cEntity, form, copy] : m_world.view<FormIdComponent, RemoteComponent>().each())
        if (form.Id == acEvent.FormId && m_copyValues.contains(copy.Id))
            m_copyPoolsPending.insert(copy.Id);
}

void ActorValueService::OnDeathChanged(const ActorDeathChangeEvent& acEvent) noexcept
{
    m_changedDeaths[acEvent.FormId] |= acEvent.Final;
    if (acEvent.Resurrected)
        m_resurrections[acEvent.FormId] = acEvent.ActionTick;
}

void ActorValueService::OnDeathActionReplayed(const DeathActionReplayedEvent& acEvent) noexcept
{
    m_replayedDeaths[acEvent.FormId] = acEvent.Tick;
    for (auto&& [cEntity, form, copy] : m_world.view<FormIdComponent, RemoteComponent>().each())
        if (form.Id == acEvent.FormId)
        {
            const auto it = m_copyDeathStates.find(copy.Id);
            if (it != m_copyDeathStates.end() && !it->second.IsDead &&
                (!it->second.ActionTick || *it->second.ActionTick < acEvent.Tick))
                m_copyDeathStates.erase(it);
            ApplyDeathState(copy.Id);
        }
}

void ActorValueService::OnReferenceLoaded(const ReferenceLoadedEvent& acEvent) noexcept
{
    for (auto&& [cEntity, form, copy] : m_world.view<FormIdComponent, RemoteComponent>().each())
        if (form.Id == acEvent.FormId)
        {
            if (m_copyValues.contains(copy.Id))
                m_copyPoolsPending.insert(copy.Id);
            ApplyDeathState(copy.Id);
        }
}

void ActorValueService::OnSpawn(const NotifyCharacterSpawn& acMessage) noexcept
{
    m_copyValues[acMessage.SessionId] = acMessage.InitialActorValues;
    m_copyPoolsPending.insert(acMessage.SessionId);
}

void ActorValueService::OnRemove(const NotifyRemoveCharacter& acMessage) noexcept
{
    m_replayedDeaths.erase(Utils::GetFormIdBySessionId(acMessage.SessionId));
    m_copyDeathStates.erase(acMessage.SessionId);
    m_copyValues.erase(acMessage.SessionId);
    m_copyPoolsPending.erase(acMessage.SessionId);
}

void ActorValueService::BroadcastActorValues() noexcept
{
    auto changes = std::exchange(m_changedValues, {});
    if (!m_transport.IsConnected())
        return;
    for (auto&& [cEntity, form, local, cached] : m_world.view<FormIdComponent, LocalComponent, ActorValuesComponent>().each())
    {
        const auto it = changes.find(form.Id);
        if (it == changes.end() || !m_actorBridge.ActorExists(form.Id))
            continue;
        RequestActorValueChanges current;
        RequestActorMaxValueChanges maximum;
        current.Id = maximum.Id = local.Id;
        for (const uint32_t cIndex : it->second)
        {
            if (cIndex >= static_cast<uint32_t>(m_actorBridge.GetActorValueCount()) ||
                (m_actorBridge.IsOtherPlayer(form.Id) && cIndex == 133))
                continue;
            // Continuous pools go with their tick; their maxima still follow writes.
            if (cIndex < 24 || cIndex > 26)
            {
                const float cValue = m_actorBridge.GetActorValue(form.Id, cIndex);
                auto& last = cached.CurrentActorValues.ActorValuesList[cIndex];
                if (cValue != last)
                {
                    last = cValue;
                    current.Values[cIndex] = cValue;
                }
            }
            const float cMax = m_actorBridge.GetActorPermanentValue(form.Id, cIndex);
            auto& lastMax = cached.CurrentActorValues.ActorMaxValuesList[cIndex];
            if (cMax != lastMax)
            {
                lastMax = cMax;
                maximum.Values[cIndex] = cMax;
            }
        }
        if (!current.Values.empty())
            m_transport.Send(current);
        if (!maximum.Values.empty())
            m_transport.Send(maximum);
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
    if (!m_smallHealthChanges.empty())
    {
        for (auto& value : m_smallHealthChanges)
        {
            RequestHealthChangeBroadcast requestHealthChange;
            requestHealthChange.Id = value.first;
            requestHealthChange.DeltaHealth = value.second;

            m_transport.Send(requestHealthChange);

            spdlog::debug("Sent out delta health for the frame, {:X}:{:f}", value.first, value.second);
        }

        m_smallHealthChanges.clear();
    }
}

// Direct authority: the owner's health, magicka and stamina are the truth (copies neither spend nor
// regenerate). Send each within 100ms of a change.
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
    const auto changes = std::exchange(m_changedDeaths, {});
    const auto resurrections = std::exchange(m_resurrections, {});
    if (!m_transport.IsConnected())
        return;
    for (auto&& [cEntity, form, local, cached] : m_world.view<FormIdComponent, LocalComponent, ActorValuesComponent>().each())
    {
        const auto it = changes.find(form.Id);
        if (it == changes.end() || !m_actorBridge.ActorExists(form.Id))
            continue;
        const bool cDead = m_actorBridge.IsActorDead(form.Id);
        if (cached.IsDead == cDead && !resurrections.contains(form.Id))
            continue;
        const auto cAction = m_actorBridge.GetActorLatestAnimation(form.Id);
        const bool cHasAction = cDead && cAction.ActionId && DeathActions::IsDying(cAction.State1);
        // A dying event can precede the action: without one, wait for the engine's final death event.
        if (cDead && !cHasAction && !it->second)
            continue;
        cached.IsDead = cDead;
        if (cDead)
            cached.DeathActionTick = cHasAction ? std::optional<uint64_t>{cAction.Tick} : std::nullopt;
        else if (const auto resurrected = resurrections.find(form.Id); resurrected != resurrections.end() && resurrected->second)
            cached.DeathActionTick = resurrected->second;
        RequestDeathStateChange request;
        request.Id = local.Id;
        request.IsDead = cDead;
        request.DeathActionTick = cached.DeathActionTick;
        m_transport.Send(request);
    }
    for (auto&& [cEntity, form, copy] : m_world.view<FormIdComponent, RemoteComponent>().each())
        if (changes.contains(form.Id))
            ApplyDeathState(copy.Id);
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

    // A player is a person: a teammate's hit turns no one on the attacker (friendly-fire.md).
    if (cAttackerFormId && formId != 0x14 && acMessage.DeltaHealth < 0.f && m_actorBridge.GetActorValue(formId, 24) > 0.f)
    {
        // The hit happened in the attacker's game: react here as if it had landed here.
        m_actorBridge.ReactToAttack(formId, cAttackerFormId);
    }
}

void ActorValueService::OnActorValueChanges(const NotifyActorValueChanges& acMessage) noexcept
{
    for (const auto& [key, value] : acMessage.Values)
        if (key >= 24 && key <= 26)
        {
            m_copyValues[acMessage.Id].ActorValuesList[key] = value;
            m_copyPoolsPending.insert(acMessage.Id);
        }
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
            continue;
        }
        m_actorBridge.SetActorValue(formId, key, value);
    }
}

void ActorValueService::OnActorMaxValueChanges(const NotifyActorMaxValueChanges& acMessage) noexcept
{
    for (const auto& [key, value] : acMessage.Values)
        if (key >= 24 && key <= 26)
        {
            m_copyValues[acMessage.Id].ActorMaxValuesList[key] = value;
            m_copyPoolsPending.insert(acMessage.Id);
        }
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

        if (key < 24 || key > 26)
            m_actorBridge.ForceActorValue(formId, 0 /* ForceMode::PERMANENT */, key, value);
    }
}

void ActorValueService::ApplyCopyPools() noexcept
{
    const auto pending = std::exchange(m_copyPoolsPending, {});
    for (const uint32_t cId : pending)
    {
        const uint32_t cFormId = Utils::GetFormIdBySessionId(cId);
        const auto it = m_copyValues.find(cId);
        if (it == m_copyValues.end() || !cFormId || !m_actorBridge.ActorExists(cFormId) || !m_actorBridge.IsActorCopy(cFormId))
            continue;
        for (const int32_t cIndex : {24, 25, 26})
        {
            const auto& values = it->second;
            if (const auto max = values.ActorMaxValuesList.find(cIndex); max != values.ActorMaxValuesList.end())
                m_actorBridge.ForceActorValue(cFormId, 0, cIndex, max->second);
            if (const auto value = values.ActorValuesList.find(cIndex); value != values.ActorValuesList.end())
                m_actorBridge.ForceActorValue(cFormId, 2, cIndex, value->second);
        }
    }
}

void ActorValueService::OnDeathStateChange(const NotifyDeathStateChange& acMessage) noexcept
{
    if (!acMessage.IsDead)
        if (const auto it = m_copyValues.find(acMessage.Id); it != m_copyValues.end())
            if (const auto health = it->second.ActorValuesList.find(24); health != it->second.ActorValuesList.end() && health->second <= 0.f)
                it->second.ActorValuesList.erase(health);
    m_copyDeathStates[acMessage.Id] = {acMessage.IsDead, acMessage.DeathActionTick};
    ApplyDeathState(acMessage.Id);
}

void ActorValueService::ApplyDeathState(uint32_t aSessionId) noexcept
{
    const auto it = m_copyDeathStates.find(aSessionId);
    if (it == m_copyDeathStates.end())
        return;
    const uint32_t cFormId = Utils::GetFormIdBySessionId(aSessionId);
    const auto& cDesired = it->second;
    if (!cFormId || !m_actorBridge.ActorExists(cFormId))
        return;
    if (!m_actorBridge.IsActorCopy(cFormId) || (!cDesired.IsDead && m_actorBridge.IsOtherPlayer(cFormId)))
    {
        m_copyDeathStates.erase(it);
        return;
    }
    if (m_actorBridge.IsActorDead(cFormId) == cDesired.IsDead)
        return;
    if (cDesired.IsDead && cDesired.ActionTick)
    {
        const auto replay = m_replayedDeaths.find(cFormId);
        if (replay == m_replayedDeaths.end() || replay->second < *cDesired.ActionTick)
            return;
    }
    if (cDesired.IsDead)
        m_actorBridge.KillActor(cFormId);
    else
    {
        m_actorBridge.ResurrectActor(cFormId);
        if (!m_actorBridge.IsActorDead(cFormId))
            m_actorBridge.ResumeActorAnimation(cFormId);
    }
}
