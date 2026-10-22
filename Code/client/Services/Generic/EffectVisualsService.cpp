#include <Services/EffectVisualsService.h>
#include <Services/TransportService.h>

#include <Bridges/IActorBridge.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyActiveEffects.h>
#include <Messages/RequestActiveEffects.h>
#include <World.h>

namespace
{
constexpr uint32_t cInactive = 1u << 15; // ActiveEffect flag: conditions not met, nothing shows
constexpr auto cCheckEvery = std::chrono::milliseconds(250);
constexpr auto cSendAllEvery = std::chrono::seconds(2);
constexpr auto cOwnerQuietAfter = std::chrono::seconds(5);
} // namespace

EffectVisualsService::EffectVisualsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                           IActorBridge& aActorBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&EffectVisualsService::OnUpdate>(this);
    m_notifyConnection = aDispatcher.sink<NotifyActiveEffects>().connect<&EffectVisualsService::OnNotifyActiveEffects>(this);
    m_remoteRemovedConnection = m_world.on_destroy<RemoteComponent>().connect<&EffectVisualsService::OnRemoteComponentRemoved>(this);
}

void EffectVisualsService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_transport.IsConnected())
        return;
    const auto cNow = Clock::now();
    if (cNow - m_lastCheck < cCheckEvery)
        return;
    m_lastCheck = cNow;
    SendOwnedEffects();
    RunCopies();
}

void EffectVisualsService::SendOwnedEffects() noexcept
{
    const auto cNow = Clock::now();
    const bool cSendAll = cNow - m_lastFullSend >= cSendAllEvery;
    if (cSendAll)
        m_lastFullSend = cNow;

    auto& modSystem = m_world.GetModSystem();
    auto view = m_world.view<FormIdComponent, LocalComponent>();
    for (auto entity : view)
    {
        const uint32_t cFormId = view.get<FormIdComponent>(entity).Id;
        const uint32_t cSessionId = view.get<LocalComponent>(entity).Id;
        if (!m_actorBridge.ActorExists(cFormId))
            continue;

        std::vector<uint32_t> ids;
        for (const auto& cEffect : m_actorBridge.GetActorActiveEffects(cFormId))
            if (!(cEffect.Flags & cInactive) && m_actorBridge.EffectHasHitVisuals(cEffect.EffectFormId))
                ids.push_back(cEffect.EffectFormId);
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

        const auto cSent = m_sent.find(cSessionId);
        const bool cChanged = cSent == m_sent.end() ? !ids.empty() : cSent->second != ids;
        if (!cChanged && (!cSendAll || ids.empty()))
            continue;

        RequestActiveEffects request;
        request.SessionId = cSessionId;
        for (const uint32_t cId : ids)
        {
            GameId effectId;
            if (modSystem.GetServerModId(cId, effectId))
                request.EffectIds.push_back(effectId);
        }
        spdlog::debug("Active effects of {:X} (session id {:X}): {} with visuals", cFormId, cSessionId, request.EffectIds.size());
        m_transport.Send(request);
        if (ids.empty())
            m_sent.erase(cSessionId);
        else
            m_sent[cSessionId] = std::move(ids);
    }
}

void EffectVisualsService::OnNotifyActiveEffects(const NotifyActiveEffects& acMessage) noexcept
{
    const uint32_t cFormId = Utils::GetFormIdBySessionId(acMessage.SessionId);
    if (cFormId == 0 || !m_actorBridge.ActorExists(cFormId) || !m_actorBridge.IsActorCopy(cFormId))
        return;

    auto& modSystem = m_world.GetModSystem();
    std::vector<uint32_t> wanted;
    for (const auto& cEffectId : acMessage.EffectIds)
        if (const uint32_t cId = modSystem.GetGameId(cEffectId))
            wanted.push_back(cId);

    auto& shown = m_shown[cFormId];
    shown.LastHeard = Clock::now();
    for (const uint32_t cId : shown.EffectIds)
        if (std::find(wanted.begin(), wanted.end(), cId) == wanted.end())
            m_actorBridge.StopEffectHitVisuals(cFormId, cId);
    shown.EffectIds = std::move(wanted);
    if (shown.EffectIds.empty())
    {
        m_shown.erase(cFormId);
        return;
    }
    PlayMissing(cFormId, shown);
}

void EffectVisualsService::PlayMissing(uint32_t aFormId, const Shown& acShown) noexcept
{
    // The engine drops visuals with the 3D, and the first use of a shader in this game plays
    // nothing while it loads: those play again on the next check.
    for (const uint32_t cId : acShown.EffectIds)
        if (!m_actorBridge.IsPlayingEffectHitVisuals(aFormId, cId))
            m_actorBridge.PlayEffectHitVisuals(aFormId, cId);
}

void EffectVisualsService::RunCopies() noexcept
{
    const auto cNow = Clock::now();
    for (auto it = m_shown.begin(); it != m_shown.end();)
    {
        if (cNow - it->second.LastHeard < cOwnerQuietAfter)
        {
            if (m_actorBridge.ActorExists(it->first))
                PlayMissing(it->first, it->second);
            ++it;
            continue;
        }
        for (const uint32_t cId : it->second.EffectIds)
            m_actorBridge.StopEffectHitVisuals(it->first, cId);
        it = m_shown.erase(it);
    }
}

void EffectVisualsService::OnRemoteComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    if (const auto* pFormId = aRegistry.try_get<FormIdComponent>(aEntity))
        m_shown.erase(pFormId->Id);
}
