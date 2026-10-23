#include <Services/EffectVisualsService.h>
#include <Services/TransportService.h>

#include <Bridges/IActorBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/LocalComponent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/EffectVisualsChangeEvent.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Messages/Relayed.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <SessionIds.h>
#include <World.h>

namespace
{
constexpr uint32_t cInactive = 1u << 15; // ActiveEffect flag: conditions not met, nothing shows
constexpr uint32_t cRemoved = 1u << 17;  // ActiveEffect::Update finished it (its visuals stopped), still listed
} // namespace

EffectVisualsService::EffectVisualsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                                           IActorBridge& aActorBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
{
    m_changeConnection = aDispatcher.sink<EffectVisualsChangeEvent>().connect<&EffectVisualsService::OnEffectVisualsChange>(this);
    m_loadedConnection = aDispatcher.sink<ReferenceLoadedEvent>().connect<&EffectVisualsService::OnReferenceLoaded>(this);
    m_notifyConnection = aDispatcher.sink<NotifyActiveEffects>().connect<&EffectVisualsService::OnNotifyActiveEffects>(this);
    m_flashConnection = aDispatcher.sink<NotifyEffectFlash>().connect<&EffectVisualsService::OnNotifyEffectFlash>(this);
    m_spawnConnection = aDispatcher.sink<NotifyCharacterSpawn>().connect<&EffectVisualsService::OnCharacterSpawn>(this);
    m_removeConnection = aDispatcher.sink<NotifyRemoveCharacter>().connect<&EffectVisualsService::OnRemoveCharacter>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&EffectVisualsService::OnDisconnected>(this);
}

std::vector<uint32_t> EffectVisualsService::ReadEffectForms(uint32_t aFormId) const noexcept
{
    std::vector<uint32_t> ids;
    for (const auto& cEffect : m_actorBridge.GetActorActiveEffects(aFormId))
        if (!(cEffect.Flags & (cInactive | cRemoved)) && m_actorBridge.EffectHasHitVisuals(cEffect.EffectFormId))
            ids.push_back(cEffect.EffectFormId);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::vector<GameId> EffectVisualsService::ReadEffects(uint32_t aFormId) const noexcept
{
    std::vector<GameId> effects;
    for (const uint32_t cId : ReadEffectForms(aFormId))
        if (GameId effectId; m_world.GetModSystem().GetServerModId(cId, effectId))
            effects.push_back(effectId);
    return effects;
}

void EffectVisualsService::OnEffectVisualsChange(const EffectVisualsChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;
    auto view = m_world.view<FormIdComponent, LocalComponent>();
    for (const auto cEntity : view)
        if (view.get<FormIdComponent>(cEntity).Id == acEvent.ActorId)
        {
            const uint32_t cSessionId = view.get<LocalComponent>(cEntity).Id;
            if (acEvent.FlashEffectId)
                SendFlash(cSessionId, acEvent.FlashEffectId);
            return SendOwned(acEvent.ActorId, cSessionId);
        }
}

void EffectVisualsService::SendFlash(uint32_t aSessionId, uint32_t aEffectFormId) noexcept
{
    EffectFlashRequest request;
    request.SessionId = aSessionId;
    if (m_world.GetModSystem().GetServerModId(aEffectFormId, request.EffectId))
        m_transport.Send(request);
}

void EffectVisualsService::OnNotifyEffectFlash(const NotifyEffectFlash& acMessage) noexcept
{
    const uint32_t cFormId = Utils::GetFormIdBySessionId(acMessage.SessionId);
    const uint32_t cEffectId = m_world.GetModSystem().GetGameId(acMessage.EffectId);
    if (cFormId && cEffectId && m_actorBridge.ActorExists(cFormId) && m_actorBridge.IsActorCopy(cFormId))
        m_actorBridge.FlashEffectHitVisuals(cFormId, cEffectId);
}

void EffectVisualsService::OnReferenceLoaded(const ReferenceLoadedEvent& acEvent) noexcept
{
    if (const auto cSessionId = Utils::GetSessionIdByFormId(acEvent.FormId); cSessionId && m_shown.contains(*cSessionId))
        PlayMissing(*cSessionId);
}

void EffectVisualsService::SendOwned(uint32_t aFormId, uint32_t aSessionId) noexcept
{
    if (!m_actorBridge.ActorExists(aFormId))
        return;
    auto ids = ReadEffectForms(aFormId);
    const auto cSent = m_sent.find(aSessionId);
    if (cSent != m_sent.end() && cSent->second == ids)
        return;

    RequestActiveEffects request;
    request.SessionId = aSessionId;
    for (const uint32_t cId : ids)
        if (GameId effectId; m_world.GetModSystem().GetServerModId(cId, effectId))
            request.EffectIds.push_back(effectId);
    spdlog::debug("Active effects of {:X} (session id {:X}): {} with visuals", aFormId, aSessionId, request.EffectIds.size());
    m_transport.Send(request);
    m_sent[aSessionId] = std::move(ids);
}

void EffectVisualsService::OnNotifyActiveEffects(const NotifyActiveEffects& acMessage) noexcept
{
    std::vector<uint32_t> wanted;
    for (const auto& cEffectId : acMessage.EffectIds)
        if (const uint32_t cId = m_world.GetModSystem().GetGameId(cEffectId))
            wanted.push_back(cId);
    Show(acMessage.SessionId, std::move(wanted));
}

void EffectVisualsService::OnCharacterSpawn(const NotifyCharacterSpawn& acMessage) noexcept
{
    // A list heard since is newer than the spawn's (a respawn's spawn is the character's record).
    if (m_shown.contains(acMessage.SessionId))
    {
        m_world.GetRunner().Queue([this, cSessionId = acMessage.SessionId]() { PlayMissing(cSessionId); });
        return;
    }
    std::vector<uint32_t> wanted;
    for (const auto& cEffectId : acMessage.EffectIds)
        if (const uint32_t cId = m_world.GetModSystem().GetGameId(cEffectId))
            wanted.push_back(cId);
    if (wanted.empty())
        return;
    m_shown[acMessage.SessionId].EffectIds = std::move(wanted);
    // After every handler of the spawn: CharacterService makes the copy in its own.
    m_world.GetRunner().Queue([this, cSessionId = acMessage.SessionId]() { PlayMissing(cSessionId); });
}

void EffectVisualsService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept
{
    StopShown(acMessage.SessionId);
    m_shown.erase(acMessage.SessionId);
}

void EffectVisualsService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    for (const auto& [cSessionId, cShown] : m_shown)
        StopShown(cSessionId);
    m_shown.clear();
    m_sent.clear();
}

void EffectVisualsService::Show(uint32_t aSessionId, std::vector<uint32_t> aEffectIds) noexcept
{
    auto& shown = m_shown[aSessionId];
    const uint32_t cFormId = Utils::GetFormIdBySessionId(aSessionId);
    if (cFormId && m_actorBridge.ActorExists(cFormId) && m_actorBridge.IsActorCopy(cFormId))
        for (const uint32_t cId : shown.EffectIds)
            if (std::find(aEffectIds.begin(), aEffectIds.end(), cId) == aEffectIds.end())
                m_actorBridge.StopEffectHitVisuals(cFormId, cId);
    shown.EffectIds = std::move(aEffectIds);
    PlayMissing(aSessionId);
}

void EffectVisualsService::PlayMissing(uint32_t aSessionId) noexcept
{
    const auto cShown = m_shown.find(aSessionId);
    const uint32_t cFormId = Utils::GetFormIdBySessionId(aSessionId);
    if (cShown == m_shown.end() || !cFormId || !m_actorBridge.ActorExists(cFormId) || !m_actorBridge.IsActorCopy(cFormId))
        return;
    cShown->second.FormId = cFormId;
    for (const uint32_t cId : cShown->second.EffectIds)
        if (!m_actorBridge.IsPlayingEffectHitVisuals(cFormId, cId))
            m_actorBridge.PlayEffectHitVisuals(cFormId, cId);
}

void EffectVisualsService::StopShown(uint32_t aSessionId) noexcept
{
    const auto cShown = m_shown.find(aSessionId);
    if (cShown == m_shown.end() || !cShown->second.FormId || !m_actorBridge.ActorExists(cShown->second.FormId))
        return;
    for (const uint32_t cId : cShown->second.EffectIds)
        m_actorBridge.StopEffectHitVisuals(cShown->second.FormId, cId);
}
