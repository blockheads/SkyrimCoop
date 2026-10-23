#include <Services/MagicService.h>

#include <World.h>

#include <Events/UpdateEvent.h>
#include <Events/SpellCastEvent.h>
#include <Events/InterruptCastEvent.h>
#include <Events/AddTargetEvent.h>
#include <Events/RemoveSpellEvent.h>

#include <Messages/Relayed.h>



#include <Bridges/IActorBridge.h>
#include <Bridges/IFormBridge.h>

#include <Games/Overrides.h>
#include <Components/LocalComponent.h>
#include <SessionIds.h>
#include <Components/FormIdComponent.h>
#include <FormIds.h>

MagicService::MagicService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                           IActorBridge& aActorBridge, IFormBridge& aFormBridge) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_formBridge(aFormBridge)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&MagicService::OnUpdate>(this);
    m_spellCastEventConnection = m_dispatcher.sink<SpellCastEvent>().connect<&MagicService::OnSpellCastEvent>(this);
    m_notifySpellCastConnection = m_dispatcher.sink<NotifySpellCast>().connect<&MagicService::OnNotifySpellCast>(this);
    m_interruptCastEventConnection = m_dispatcher.sink<InterruptCastEvent>().connect<&MagicService::OnInterruptCastEvent>(this);
    m_notifyInterruptCastConnection = m_dispatcher.sink<NotifyInterruptCast>().connect<&MagicService::OnNotifyInterruptCast>(this);
    m_addTargetEventConnection = m_dispatcher.sink<AddTargetEvent>().connect<&MagicService::OnAddTargetEvent>(this);
    m_notifyAddTargetConnection = m_dispatcher.sink<NotifyAddTarget>().connect<&MagicService::OnNotifyAddTarget>(this);
    m_removeSpellEventConnection = m_dispatcher.sink<RemoveSpellEvent>().connect<&MagicService::OnRemoveSpellEvent>(this);
    m_notifyRemoveSpell = m_dispatcher.sink<NotifyRemoveSpell>().connect<&MagicService::OnNotifyRemoveSpell>(this);
}

void MagicService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    ApplyQueuedEffects();
}

void MagicService::OnSpellCastEvent(const SpellCastEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;

    if (!acEvent.CasterFormId || !acEvent.HasNiNode)
    {
        spdlog::warn("Spell cast event has no actor or actor is not loaded");
        return;
    }

    // only sync concentration spells through spell cast sync, the rest through projectile sync for accuracy
    const uint32_t cSpellFormId = acEvent.SpellId;
    if (m_formBridge.FormExists(cSpellFormId))
    {
        const bool cIsConcentration = m_formBridge.IsConcentrationSpell(cSpellFormId);
        const bool cIsHealing = m_formBridge.IsHealingSpell(cSpellFormId);
        const bool cIsWard = m_formBridge.IsWardSpell(cSpellFormId);
        const bool cIsInvis = m_formBridge.IsInvisibilitySpell(cSpellFormId);

        if ((!cIsConcentration || cIsHealing) && !cIsWard && !cIsInvis)
        {
            spdlog::debug("Canceled magic spell");
            return;
        }
    }

    const uint32_t cFormId = acEvent.CasterFormId;

    auto view = m_world.view<FormIdComponent, LocalComponent>();
    const auto casterEntityIt = std::find_if(std::begin(view), std::end(view), [cFormId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == cFormId; });

    if (casterEntityIt == std::end(view))
        return;

    auto& localComponent = view.get<LocalComponent>(*casterEntityIt);

    SpellCastRequest request{};

    request.CasterId = localComponent.Id;
    request.CastingSource = acEvent.CastingSource;
    request.IsDualCasting = acEvent.IsDualCasting;

    const auto cSpell = ToSession(acEvent.SpellId);
    if (!cSpell)
        return;
    request.SpellFormId = *cSpell;

    if (acEvent.DesiredTargetID != 0)
    {
        auto targetView = m_world.view<FormIdComponent>();
        const auto targetEntityIt = std::find_if(std::begin(targetView), std::end(targetView), [id = acEvent.DesiredTargetID, targetView](entt::entity entity) { return targetView.get<FormIdComponent>(entity).Id == id; });

        if (targetEntityIt != std::end(targetView))
        {
            auto desiredTargetIdRes = Utils::GetSessionId(*targetEntityIt);
            if (desiredTargetIdRes.has_value())
                request.DesiredTarget = desiredTargetIdRes.value();
            else
                spdlog::error("{}: failed to find session id", __FUNCTION__);
        }
    }

    spdlog::debug("Spell cast event sent, ID: {:X}, Source: {}, IsDualCasting: {}, desired target: {:X}", request.CasterId, request.CastingSource, request.IsDualCasting, request.DesiredTarget);

    m_transport.Send(request);
}

void MagicService::OnNotifySpellCast(const NotifySpellCast& acMessage) const noexcept
{
    const uint32_t cFormId = Utils::GetFormIdBySessionId(acMessage.CasterId);
    if (!cFormId)
    {
        spdlog::warn("Caster with remote id {:X} not found.", acMessage.CasterId);
        return;
    }

    m_actorBridge.GenerateMagicCasters(cFormId);

    // Only left hand casters need dual casting (?)
    // CastingSource::LEFT_HAND = 1
    m_actorBridge.SetMagicCasterDualCasting(cFormId, 1u, acMessage.IsDualCasting);

    if (acMessage.CastingSource >= 4)
    {
        spdlog::warn("{}: could not find casting source {}", __FUNCTION__, acMessage.CastingSource);
        return;
    }

    // Try to get the currently equipped spell for this casting source slot
    uint32_t spellFormId = m_actorBridge.GetActiveMagicItemFormId(cFormId, acMessage.CastingSource);

    if (!spellFormId)
    {
        spellFormId = ToGame(acMessage.SpellFormId);
        if (spellFormId == 0)
        {
            spdlog::error("Could not find spell form id for GameId base {:X}, mod {:X}", acMessage.SpellFormId.BaseId, acMessage.SpellFormId.ModId);
            return;
        }

        if (!m_formBridge.FormExists(spellFormId))
        {
            spdlog::error("Cannot find spell form, id: {:X}.", spellFormId);
            return;
        }
    }

    // Find desired target form ID if applicable
    uint32_t desiredTargetFormId = 0;
    if (acMessage.DesiredTarget != 0)
    {
        desiredTargetFormId = Utils::GetFormIdBySessionId(acMessage.DesiredTarget);
    }

    ScopedSpellCastOverride _;

    m_actorBridge.CastSpell(cFormId, spellFormId, acMessage.CastingSource, desiredTargetFormId);

    spdlog::debug("Successfully casted remote spell");
}

void MagicService::OnInterruptCastEvent(const InterruptCastEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;

    uint32_t formId = acEvent.CasterFormID;

    auto view = m_world.view<FormIdComponent, LocalComponent>();
    const auto casterEntityIt = std::find_if(std::begin(view), std::end(view), [formId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (casterEntityIt == std::end(view))
    {
        spdlog::warn("{}: could not find caster, form id {:X}", __FUNCTION__, formId);
        return;
    }

    auto& localComponent = view.get<LocalComponent>(*casterEntityIt);

    InterruptCastRequest request;
    request.CasterId = localComponent.Id;
    request.CastingSource = acEvent.CastingSource;

    spdlog::debug("Sending out interrupt cast");

    m_transport.Send(request);
}

void MagicService::OnNotifyInterruptCast(const NotifyInterruptCast& acMessage) const noexcept
{
    if (acMessage.CastingSource >= 4)
    {
        spdlog::warn("{}: could not find casting source {}", __FUNCTION__, acMessage.CastingSource);
        return;
    }

    const uint32_t cFormId = Utils::GetFormIdBySessionId(acMessage.CasterId);
    if (!cFormId)
    {
        spdlog::warn("Caster with remote id {:X} not found.", acMessage.CasterId);
        return;
    }

    m_actorBridge.GenerateMagicCasters(cFormId);
    m_actorBridge.InterruptCast(cFormId, acMessage.CastingSource);

    spdlog::debug("Interrupt remote cast successful");
}

void MagicService::OnAddTargetEvent(const AddTargetEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    // Self-only effects follow the caster's own state. Everything else that hits another
    // game's actor, concentration spells included, is a request for that actor's owner.
    if (m_formBridge.FormExists(acEvent.SpellID))
    {
        if (m_formBridge.IsWardSpell(acEvent.SpellID) || m_formBridge.IsInvisibilitySpell(acEvent.SpellID) ||
            m_formBridge.IsBoundWeaponSpell(acEvent.SpellID))
            return;
    }

    AddTargetRequest request{};

    const auto cSpell = ToSession(acEvent.SpellID);
    const auto cEffect = ToSession(acEvent.EffectID);
    if (!cSpell || !cEffect)
        return;
    request.SpellId = *cSpell;
    request.EffectId = *cEffect;

    request.Magnitude = acEvent.Magnitude;

    // Because it takes time to create Actors, the Caster or Target may not
    // exist on the server yet, or server may not have told us yet. Have to queue to compensate.
    auto view = m_world.view<FormIdComponent>();
    const auto it = std::find_if(std::begin(view), std::end(view), [id = acEvent.TargetID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (it == std::end(view))
    {
        MagicQueue::Spdlog("{}: server entity for target formID not found, formID: {:X}, queueing", __FUNCTION__, acEvent.TargetID);
        m_queuedEffects.push(MagicAddTargetEventQueue(acEvent));
        return;
    }

    std::optional<uint32_t> sessionIdRes = Utils::GetSessionId(*it);
    if (!sessionIdRes.has_value())
    {
        MagicQueue::Spdlog("{}: server ID for target formID not found, formID: {:X}, queueing", __FUNCTION__, acEvent.TargetID);
        m_queuedEffects.push(MagicAddTargetEventQueue(acEvent));
        return;
    }

    request.TargetId = sessionIdRes.value();

    if (acEvent.CasterID)
    {
        const auto casterIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.CasterID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

        if (casterIt == std::end(view))
        {
            MagicQueue::Spdlog("{}: server entity for caster formID not found, formID: {:X}, queueing", __FUNCTION__, acEvent.CasterID);
            m_queuedEffects.push(MagicAddTargetEventQueue(acEvent));
            return;
        }

        sessionIdRes = Utils::GetSessionId(*casterIt);
        if (!sessionIdRes.has_value())
        {
            MagicQueue::Spdlog("{}: server ID for caster formID not found, formID: {:X}, queueing", __FUNCTION__, acEvent.CasterID);
            m_queuedEffects.push(MagicAddTargetEventQueue(acEvent));
            return;
        }

        request.CasterId = sessionIdRes.value();
    }

    request.IsDualCasting = acEvent.IsDualCasting;
    request.ApplyHealPerkBonus = acEvent.ApplyHealPerkBonus;
    request.ApplyStaminaPerkBonus = acEvent.ApplyStaminaPerkBonus;

    m_transport.Send(request);

    spdlog::debug("Sending effect sync request");
}

void MagicService::OnNotifyAddTarget(const NotifyAddTarget& acMessage) noexcept
{
    const uint32_t cSpellId = ToGame(acMessage.SpellId);
    if (cSpellId == 0)
    {
        spdlog::error("{}: failed to retrieve formID of server spell id, GameId base: {:X}, mod: {:X}, discarding", __FUNCTION__, acMessage.SpellId.BaseId, acMessage.SpellId.ModId);
        return;
    }

    if (!m_formBridge.IsMagicItem(cSpellId))
    {
        spdlog::error("{}: failed to retrieve spell by formID {:X}, discarding", __FUNCTION__, cSpellId);
        return;
    }

    const uint32_t cEffectId = ToGame(acMessage.EffectId);
    if (cEffectId == 0)
    {
        spdlog::error("{}: failed to retrieve formID of server effect id, GameId base: {:X}, mod: {:X}, discarding", __FUNCTION__, acMessage.EffectId.BaseId, acMessage.EffectId.ModId);
        return;
    }

    const uint32_t cTargetFormId = Utils::GetFormIdBySessionId(acMessage.TargetId);
    if (!cTargetFormId)
    {
        MagicQueue::Spdlog("{}: could not find targeted Actor for serverID {:X}, queueing", __FUNCTION__, acMessage.TargetId);
        m_queuedRemoteEffects.push(acMessage);
        return;
    }

    uint32_t cCasterFormId = 0;
    if (acMessage.CasterId)
    {
        cCasterFormId = Utils::GetFormIdBySessionId(acMessage.CasterId);
        if (!cCasterFormId)
        {
            MagicQueue::Spdlog("{}: could not find caster Actor for serverID {:X}, queueing", __FUNCTION__, acMessage.CasterId);
            m_queuedRemoteEffects.push(acMessage);
            return;
        }
    }

    // Only the owner applies effects; a copy shows the owner's result.
    if (m_actorBridge.IsActorCopy(cTargetFormId))
    {
        spdlog::warn("{}: effect for a copy ({:X}) ignored", __FUNCTION__, cTargetFormId);
        return;
    }

    m_formBridge.AddMagicTargetEffect(cTargetFormId, cCasterFormId, cSpellId, cEffectId,
                                       acMessage.Magnitude, acMessage.IsDualCasting,
                                       acMessage.ApplyHealPerkBonus, acMessage.ApplyStaminaPerkBonus);

    // A harmful spell from another player: our actor reacts as if it had physically been hit
    // (fights back, flees, raises the alarm), against that player's character here.
    if (cCasterFormId && !m_formBridge.IsHealingSpell(cSpellId) && !m_formBridge.IsBuffSpell(cSpellId))
        m_actorBridge.ReactToAttack(cTargetFormId, cCasterFormId);
    spdlog::debug("Applied remote magic effect");
}

void MagicService::OnRemoveSpellEvent(const RemoveSpellEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    RemoveSpellRequest request{};

    const auto cSpell = ToSession(acEvent.SpellId);
    if (!cSpell)
        return;
    request.SpellId = *cSpell;

    auto view = m_world.view<FormIdComponent>();
    const auto it = std::find_if(std::begin(view), std::end(view), [id = acEvent.TargetId, view](auto entity) {
        return view.get<FormIdComponent>(entity).Id == id;
    });

    if (it == std::end(view))
    {
        spdlog::warn("Form id not found for magic remove target, form id: {:X}", acEvent.TargetId);
        return;
    }

    std::optional<uint32_t> sessionIdRes = Utils::GetSessionId(*it);
    if (!sessionIdRes.has_value())
    {
        spdlog::warn("Session id not found for magic remove target, form id: {:X}", acEvent.TargetId);
        return;
    }

    request.TargetId = sessionIdRes.value();

    m_transport.Send(request);
}

void MagicService::OnNotifyRemoveSpell(const NotifyRemoveSpell& acMessage) noexcept
{
    const uint32_t cTargetFormId = Utils::GetFormIdBySessionId(acMessage.TargetId);
    if (!cTargetFormId)
    {
        spdlog::warn("{}: : could not find actor session id {:X}", __FUNCTION__, acMessage.TargetId);
        return;
    }

    const uint32_t cSpellId = ToGame(acMessage.SpellId);
    if (cSpellId == 0)
    {
        spdlog::error("{}: failed to retrieve spell id, GameId base: {:X}, mod: {:X}", __FUNCTION__,
                      acMessage.SpellId.BaseId, acMessage.SpellId.ModId);
        return;
    }

    m_actorBridge.RemoveSpell(cTargetFormId, cSpellId);
}

void MagicService::ApplyQueuedEffects() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 100ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    // Search queued events
    while (!m_queuedEffects.empty())
    {
        AddTargetEvent target = m_queuedEffects.front().Target();

        // Check for and skip expired (timed out) events, that Actor isn't likely to exist anymore.
        if (m_queuedEffects.front().Expired())
            MagicQueue::Spdlog("{}: removing expired AddTargetEvent from queue: caster {:X}, spell {:X}, effect {:X}, target {:X}",
                               __FUNCTION__, target.CasterID, target.SpellID, target.EffectID, target.TargetID);
        else
        {
            // Process queued target events. If caster and target now have server IDs, this should work.
            // If they don't, stop processing the list until next iteration.
            auto view = m_world.view<FormIdComponent>();
            const auto it = std::find_if(std::begin(view), std::end(view), [id = target.TargetID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

            if (it == std::end(view))
            {
                spdlog::debug("{}: server entity for AddTargetEvent target formID still not found: caster {:X}, spell {:X}, effect {:X}, target {:X}",
                              __FUNCTION__, target.CasterID, target.SpellID, target.EffectID, target.TargetID);
                break;
            }

            entt::entity entity = *it;
            std::optional<uint32_t> sessionIdRes = Utils::GetSessionId(entity);
            if (!sessionIdRes.has_value())
            {
                spdlog::debug("{}: serverID for AddTargetEvent target formID still not found: caster {:X}, spell {:X}, effect {:X}, target {:X}",
                              __FUNCTION__, target.CasterID, target.SpellID, target.EffectID, target.TargetID);
                break;
            }

            if (target.CasterID)
            {
                const auto casterIt = std::find_if(std::begin(view), std::end(view), [id = target.CasterID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

                if (casterIt == std::end(view))
                {
                    spdlog::debug("{}: serverID for AddTargetEvent caster formID still not found: caster {:X}, spell {:X}, effect {:X}, target {:X}",
                                  __FUNCTION__, target.CasterID, target.SpellID, target.EffectID, target.TargetID);
                    break;
                }

                sessionIdRes = Utils::GetSessionId(*casterIt);
                if (!sessionIdRes.has_value())
                {
                    spdlog::debug("{}: serverID for AddTargetEvent caster formID still not found: caster {:X}, spell {:X}, effect {:X}, target {:X}",
                                  __FUNCTION__, target.CasterID, target.SpellID, target.EffectID, target.TargetID);
                    break;
                }
            }

            // At this point, it will succeed or fail, but not queue another one ad infinitum
            MagicQueue::Spdlog("{}: retrying AddTargetEvent for caster {:X}, spell {:X}, effect {:X}, target {:X}",
                               __FUNCTION__, target.CasterID, target.SpellID, target.EffectID, target.TargetID);
            OnAddTargetEvent(target);
        }
        m_queuedEffects.pop();
    }

    // Same again for remote events
    while (!m_queuedRemoteEffects.empty())
    {
        NotifyAddTarget target = m_queuedRemoteEffects.front().Target();
        const uint32_t cTargetFormId = Utils::GetFormIdBySessionId(target.TargetId);
        const uint32_t cCasterFormId = Utils::GetFormIdBySessionId(target.CasterId);

        if (m_queuedRemoteEffects.front().Expired())
            MagicQueue::Spdlog("{}: removing expired NotifyAddTarget event from queue: caster {:X}, spell {:X}, effect {:X}, target {:X}",
                               __FUNCTION__, target.CasterId, target.SpellId.LogFormat(), target.EffectId.LogFormat(), target.TargetId);
        else
        {
            if (!cTargetFormId)
            {
                spdlog::debug("{}: Actor for target serverID still not found for NotifyAddTarget: caster {:X}, spell {:X}, effect {:X}, target {:X}",
                              __FUNCTION__, target.CasterId, target.SpellId.LogFormat(), target.EffectId.LogFormat(), target.TargetId);
                break;
            }

            if (target.CasterId && !cCasterFormId)
            {
                spdlog::debug("{}: Actor for caster serverID still not found for NotifyAddTarget: caster {:X}, spell {:X}, effect {:X}, target {:X}",
                              __FUNCTION__, target.CasterId, target.SpellId.LogFormat(), target.EffectId.LogFormat(), target.TargetId);
                break;
            }

            MagicQueue::Spdlog("{}: retrying NotifyAddTarget for caster {:X}, spell {:X}, effect {:X}, target {:X}",
                               __FUNCTION__, target.CasterId, target.SpellId.LogFormat(), target.EffectId.LogFormat(), target.TargetId);
            OnNotifyAddTarget(target);
        }
        m_queuedRemoteEffects.pop();
    }
}
