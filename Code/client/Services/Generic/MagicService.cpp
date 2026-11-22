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
#include <Components/RemoteComponent.h>
#include <Services/CopyRecords.h>
#include <Structs/RecordPart.h>
#include <algorithm>
#include <SessionIds.h>
#include <Components/FormIdComponent.h>
#include <FormIds.h>
#include <Diagnostics/Profile.h>

namespace
{
// This game's actor's session id (none: not this game's).
std::optional<uint32_t> LocalIdOf(World& aWorld, uint32_t aFormId) noexcept
{
    for (const entt::entity cEntity : aWorld.view<FormIdComponent, LocalComponent>())
        if (aWorld.get<FormIdComponent>(cEntity).Id == aFormId)
            return aWorld.get<LocalComponent>(cEntity).Id;
    return std::nullopt;
}
} // namespace

MagicService::MagicService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                           IActorBridge& aActorBridge, IFormBridge& aFormBridge) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_formBridge(aFormBridge)
{
    Listen<UpdateEvent, &MagicService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<SpellCastEvent, &MagicService::OnSpellCastEvent>(m_connections, m_dispatcher, this);
    m_world.ctx().at<CopyRecords>().Register(
        RecordKind::kCast, [this](uint32_t aId, const std::string& acBytes) { return ApplyCast(aId, acBytes); },
        [this](uint32_t aFormId) -> std::optional<std::string> {
            const auto it = m_held.find(aFormId);
            if (it == m_held.end() || std::ranges::none_of(it->second.Sources, [](const auto& acCast) { return acCast.has_value(); }))
                return std::nullopt;
            return ToPartBytes(it->second);
        });
    Listen<&MagicService::OnCopyRemoved>(m_connections, m_world.on_destroy<RemoteComponent>(), this);
    Listen<&MagicService::OnLocalRemoved>(m_connections, m_world.on_destroy<LocalComponent>(), this);
    Listen<InterruptCastEvent, &MagicService::OnInterruptCastEvent>(m_connections, m_dispatcher, this);
    Listen<NotifyInterruptCast, &MagicService::OnNotifyInterruptCast>(m_connections, m_dispatcher, this);
    Listen<AddTargetEvent, &MagicService::OnAddTargetEvent>(m_connections, m_dispatcher, this);
    Listen<NotifyAddTarget, &MagicService::OnNotifyAddTarget>(m_connections, m_dispatcher, this);
    Listen<RemoveSpellEvent, &MagicService::OnRemoveSpellEvent>(m_connections, m_dispatcher, this);
    Listen<NotifyRemoveSpell, &MagicService::OnNotifyRemoveSpell>(m_connections, m_dispatcher, this);
}

void MagicService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PROFILE_ZONE("MagicService::OnUpdate");
    if (!m_transport.IsConnected())
        return;

    ApplyQueuedEffects();
}

void MagicService::OnSpellCastEvent(const SpellCastEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    if (!acEvent.CasterFormId || !acEvent.HasNiNode)
    {
        spdlog::warn("Spell cast event has no actor or actor is not loaded");
        return;
    }

    // Only held casts are the part; the rest travel as projectiles for accuracy.
    const uint32_t cSpellFormId = acEvent.SpellId;
    if (m_formBridge.FormExists(cSpellFormId))
    {
        const bool cIsConcentration = m_formBridge.IsConcentrationSpell(cSpellFormId);
        const bool cIsHealing = m_formBridge.IsHealingSpell(cSpellFormId);
        const bool cIsWard = m_formBridge.IsWardSpell(cSpellFormId);
        const bool cIsInvis = m_formBridge.IsInvisibilitySpell(cSpellFormId);

        if ((!cIsConcentration || cIsHealing) && !cIsWard && !cIsInvis)
            return;
    }
    if (acEvent.CastingSource >= CastPart{}.Sources.size())
        return;

    const auto cId = LocalIdOf(m_world, acEvent.CasterFormId);
    const auto cSpell = ToSession(acEvent.SpellId);
    if (!cId || !cSpell)
        return;

    HeldCast cast{*cSpell, acEvent.IsDualCasting, 0};
    if (acEvent.DesiredTargetID)
        cast.Target = Utils::GetSessionIdByFormId(acEvent.DesiredTargetID).value_or(0);
    CastPart& part = m_held[acEvent.CasterFormId];
    part.Sources[acEvent.CastingSource] = cast;
    spdlog::debug("MagicService: {:X} casts {:X} in source {}", *cId, cSpell->BaseId, acEvent.CastingSource);
    auto& records = m_world.ctx().at<CopyRecords>();
    records.Declare(*cId, RecordKind::kCast, ToPartBytes(part));
    // A one-shot cast (invisibility) holds nothing: its copies cast it once, a joiner gets none.
    if (!m_formBridge.IsConcentrationSpell(cSpellFormId))
    {
        part.Sources[acEvent.CastingSource].reset();
        records.Declare(*cId, RecordKind::kCast, ToPartBytes(part));
    }
}

std::vector<uint32_t> MagicService::ApplyCast(uint32_t aId, const std::string& acBytes) noexcept
{
    CastPart part;
    const uint32_t cFormId = Utils::GetFormIdBySessionId(aId);
    if (!cFormId || !FromPartBytes(acBytes, part))
        return {};
    Applied& applied = m_applied[aId];
    if (applied.FormId != cFormId)
        applied = {cFormId, {}};
    std::vector<uint32_t> waiting;
    for (uint32_t source = 0; source < part.Sources.size(); ++source)
    {
        const auto& cHeld = part.Sources[source];
        auto& was = applied.Part.Sources[source];
        if (cHeld == was)
            continue;
        // A named target not here yet: this slot waits for it.
        if (cHeld && cHeld->Target && !Utils::GetFormIdBySessionId(cHeld->Target))
        {
            waiting.push_back(cHeld->Target);
            continue;
        }
        if (cHeld)
            CastOn(cFormId, source, *cHeld);
        else if (m_formBridge.IsConcentrationSpell(ToGame(was->Spell))) // a one-shot cast has nothing to stop
        {
            m_actorBridge.GenerateMagicCasters(cFormId);
            m_actorBridge.InterruptCast(cFormId, source);
        }
        was = cHeld;
    }
    return waiting;
}

void MagicService::CastOn(uint32_t aFormId, uint32_t aSource, const HeldCast& acCast) const noexcept
{
    m_actorBridge.GenerateMagicCasters(aFormId);
    // The dual flag on caster 1 (RIGHT_HAND, MagicSystem.h), as the old cast message did; which caster reads it is unread.
    m_actorBridge.SetMagicCasterDualCasting(aFormId, 1u, acCast.Dual);

    // The owner's spell, else the one equipped in that source.
    uint32_t spellFormId = ToGame(acCast.Spell);
    if (!spellFormId || !m_formBridge.FormExists(spellFormId))
        spellFormId = m_actorBridge.GetActiveMagicItemFormId(aFormId, aSource);
    if (!spellFormId)
    {
        spdlog::error("MagicService: no spell {:X}:{:X} here to cast", acCast.Spell.ModId, acCast.Spell.BaseId);
        return;
    }
    const uint32_t cTarget = acCast.Target ? Utils::GetFormIdBySessionId(acCast.Target) : 0;

    ScopedSpellCastOverride _;
    m_actorBridge.CastSpell(aFormId, spellFormId, aSource, cTarget);
}

void MagicService::OnInterruptCastEvent(const InterruptCastEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    const uint32_t cFormId = acEvent.CasterFormID;
    const auto cId = LocalIdOf(m_world, cFormId);
    if (!cId)
    {
        spdlog::warn("{}: could not find caster, form id {:X}", __FUNCTION__, cFormId);
        return;
    }

    // A held cast's end: its slot cleared in the part.
    if (const auto it = m_held.find(cFormId); it != m_held.end() && acEvent.CastingSource < it->second.Sources.size() &&
                                                 it->second.Sources[acEvent.CastingSource])
    {
        it->second.Sources[acEvent.CastingSource].reset();
        m_world.ctx().at<CopyRecords>().Declare(*cId, RecordKind::kCast, ToPartBytes(it->second));
        return;
    }

    InterruptCastRequest request;
    request.CasterId = *cId;
    request.CastingSource = acEvent.CastingSource;
    m_transport.Send(request);
}

void MagicService::OnCopyRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    if (const auto* cpRemote = aRegistry.try_get<RemoteComponent>(aEntity))
        m_applied.erase(cpRemote->Id);
}

void MagicService::OnLocalRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    if (const auto* cpForm = aRegistry.try_get<FormIdComponent>(aEntity))
        m_held.erase(cpForm->Id);
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
    // (fights back, flees, raises the alarm), against that player's character here. Our player does not.
    if (cCasterFormId && cTargetFormId != 0x14 && !m_formBridge.IsHealingSpell(cSpellId) && !m_formBridge.IsBuffSpell(cSpellId))
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
