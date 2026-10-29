#include <Services/BodyService.h>

#include <Bridges/IBodyBridge.h>
#include <Components.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/Relayed.h>
#include <Services/TransportService.h>
#include <Services/WorldStateLog.h>
#include <Systems/InterpolationSystem.h>
#include <World.h>

BodyService::BodyService(World& aWorld, entt::dispatcher& aDispatcher, IBodyBridge& aBridge) noexcept
    : m_world(aWorld)
    , m_bridge(aBridge)
{
    Listen<NotifyBodyFrames, &BodyService::OnFrames>(m_connections, aDispatcher, this);
    Listen<NotifyBodyRests, &BodyService::OnRests>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &BodyService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &BodyService::OnDisconnected>(m_connections, aDispatcher, this);
}

void BodyService::Forget(const BodyKey& acKey, uint64_t aReleasedAt) noexcept
{
    if (aReleasedAt)
        PlaybackOf(acKey).ForgetUntil(acKey, aReleasedAt);
    else
        PlaybackOf(acKey).Forget(acKey);
    m_ragdollRests.erase(acKey);
    m_pendingRests.erase(acKey);
    m_following.erase(acKey);
}

void BodyService::EndRagdoll(uint32_t aSessionId, uint64_t aTick) noexcept
{
    std::erase_if(m_ragdollRests, [aSessionId, aTick](const auto& acRest) {
        return acRest.first.Actor == aSessionId && acRest.second.first <= aTick;
    });
}

void BodyService::OnFrames(const NotifyBodyFrames& acMessage) noexcept
{
    std::vector<BodyFrame> frames, ragdolls;
    for (const auto& cFrame : acMessage.Frames)
    {
        if (m_pKeeper && !m_pKeeper->TakesFrame(cFrame.Key))
            continue;
        if (const auto it = m_pendingRests.find(cFrame.Key); it != m_pendingRests.end() && acMessage.Tick > it->second.first)
            m_pendingRests.erase(it);
        // This game's own physics may have moved it first (its copy of the blast): from there into the owner's.
        if (!cFrame.Key.Actor && !m_playback.Has(cFrame.Key))
            if (const auto cShown = m_bridge.ReadPose(cFrame.Key))
                m_playback.StartFrom(*cShown);
        (cFrame.Key.Actor ? ragdolls : frames).push_back(cFrame);
    }
    m_playback.Add(acMessage.Tick, frames);
    m_ragdolls.Add(acMessage.Tick, ragdolls);
    if (!ragdolls.empty())
        m_world.GetCopyClock().OnSnapshot(acMessage.Tick, m_world.GetTransport().GetTick());
}

void BodyService::OnRests(const NotifyBodyRests& acMessage) noexcept
{
    std::vector<BodyPose> rests, ragdolls;
    for (const auto& cRest : acMessage.Rests)
        if (!m_pKeeper || !m_pKeeper->KeepsRest(cRest.Key))
            (cRest.Key.Actor ? ragdolls : rests).push_back(cRest);
    m_playback.Rest(acMessage.Tick, rests);
    m_ragdolls.Rest(acMessage.Tick, ragdolls);
}

void BodyService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_playback = {};
    m_ragdolls = {};
    m_following.clear();
    m_ragdollRests.clear();
    m_pendingRests.clear();
    m_missing.clear();
}

entt::entity BodyService::CopyEntity(uint32_t aSessionId) const noexcept
{
    for (const auto cEntity : m_world.view<RemoteComponent>())
        if (m_world.get<RemoteComponent>(cEntity).Id == aSessionId)
            return cEntity;
    return entt::null;
}

uint32_t BodyService::CopyOf(uint32_t aSessionId) const noexcept
{
    const auto cEntity = CopyEntity(aSessionId);
    const auto* pFormId = cEntity != entt::null ? m_world.try_get<FormIdComponent>(cEntity) : nullptr;
    return pFormId ? pFormId->Id : 0;
}

void BodyService::OnUpdate(const UpdateEvent&) noexcept
{
    const uint64_t cTick = m_world.GetTransport().GetTick();
    // What this game's player took is its own now: the owner's playback of it stops.
    if (m_pKeeper)
        for (const BodyKey& cKey : m_pKeeper->Update(cTick, m_following))
            Forget(cKey);
    Show(m_playback, [cAt = cTick > kDelayMs ? cTick - kDelayMs : 0](const BodyKey&) { return cAt; });
    Show(m_ragdolls, [this](const BodyKey& acKey) { return CopyTickOf(acKey.Actor); });
    for (auto it = m_pendingRests.begin(); it != m_pendingRests.end();)
    {
        if ((!m_pKeeper || !m_pKeeper->KeepsRest(it->first)) && m_bridge.Rest(it->second.second, 0))
        {
            if (m_pKeeper)
                m_pKeeper->OnRest(it->second.second);
            it = m_pendingRests.erase(it);
        }
        else
            ++it;
    }
    for (auto it = m_ragdollRests.begin(); it != m_ragdollRests.end();)
    {
        const auto& cRest = it->second.second;
        const uint32_t cCopy = CopyOf(cRest.Key.Actor);
        const BodyFrame cFrame{.Key = cRest.Key, .Position = cRest.Position, .Rotation = cRest.Rotation, .Knocked = cRest.Knocked};
        if (!cCopy || !m_bridge.Rest(cRest, cCopy) || !m_bridge.Drive(cFrame, cCopy))
            it = m_ragdollRests.erase(it);
        else
            ++it;
    }
}

uint64_t BodyService::CopyTickOf(uint32_t aSessionId) noexcept
{
    const auto cEntity = CopyEntity(aSessionId);
    return cEntity != entt::null ? InterpolationSystem::PlaybackTick(m_world, cEntity) : 0;
}

void BodyService::Show(BodyPlayback& aPlayback, const BodyPlayback::TickOf& acTickOf) noexcept
{
    if (!aPlayback.Count())
        return;
    for (const auto& cPose : aPlayback.At(acTickOf))
    {
        const BodyKey& cKey = cPose.Body.Key;
        const uint32_t cCopy = cKey.Actor ? CopyOf(cKey.Actor) : 0;
        const BodyPose cAt{.Key = cKey, .Position = cPose.Body.Position, .Rotation = cPose.Body.Rotation, .Knocked = cPose.Body.Knocked};
        if (cPose.Rest && cKey.Actor)
            m_ragdollRests[cKey] = {cPose.Tick, cAt};
        else
            m_ragdollRests.erase(cKey);
        const bool cDone = cPose.Rest ? m_bridge.Rest(cAt, cCopy) : m_bridge.Drive(cPose.Body, cCopy);
        if (cPose.Rest && !cKey.Actor && !cDone)
        {
            m_pendingRests[cKey] = {cPose.Tick, cAt};
            m_following.erase(cKey);
            continue;
        }
        m_pendingRests.erase(cKey);
        // Not loaded here (its cell, or its 3D): nothing to show; its cell's load gets the owner's rest.
        if (!cDone)
        {
            aPlayback.Forget(cKey);
            if (m_missing.insert(cKey).second)
                spdlog::warn("BodyService: {} is not in this game", WorldStateLog::DescribeBody(cKey));
            continue;
        }
        m_missing.erase(cKey);
        if (cPose.Rest)
        {
            m_following.erase(cKey);
            if (m_pKeeper)
                m_pKeeper->OnRest(cAt);
            spdlog::info("BodyService: placed the owner's rest of {} at ({:.0f}, {:.0f}, {:.0f}) after {} frames", WorldStateLog::DescribeBody(cKey),
                         cPose.Body.Position.x, cPose.Body.Position.y, cPose.Body.Position.z, cPose.Frames);
        }
        else if (m_following.insert(cKey).second)
            spdlog::info("BodyService: follows the owner's {}", WorldStateLog::DescribeBody(cKey));
    }
    for (const auto& cLost : aPlayback.TakeLost())
    {
        m_following.erase(cLost.Key);
        spdlog::warn("BodyService: lost {}: no frame for {} ms and no rest, after {} frames", WorldStateLog::DescribeBody(cLost.Key),
                     BodyPlayback::kLostMs, cLost.Frames);
    }
}
