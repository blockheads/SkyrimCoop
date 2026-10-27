#include <Services/BodyService.h>
#include <Services/CopyClock.h>

#include <Bridges/IBodyBridge.h>
#include <Components.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Messages/Relayed.h>
#include <Services/TransportService.h>
#include <Services/WorldStateLog.h>
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

void BodyService::Forget(const BodyKey& acKey) noexcept
{
    PlaybackOf(acKey).Forget(acKey);
    m_following.erase(acKey);
}

void BodyService::OnFrames(const NotifyBodyFrames& acMessage) noexcept
{
    std::vector<BodyFrame> frames, ragdolls;
    for (const auto& cFrame : acMessage.Frames)
    {
        if (m_pKeeper && !m_pKeeper->TakesFrame(cFrame.Key))
            continue;
        // This game's own physics may have moved it first (its copy of the blast): from there into the owner's.
        if (!m_playback.Has(cFrame.Key))
            if (const auto cShown = m_bridge.ReadPose(cFrame.Key))
                m_playback.StartFrom(*cShown);
        (cFrame.Key.Actor ? ragdolls : frames).push_back(cFrame);
    }
    m_playback.Add(acMessage.Tick, frames);
    m_ragdolls.Add(acMessage.Tick, ragdolls);
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
    m_missing.clear();
}

uint32_t BodyService::CopyOf(uint32_t aSessionId) const noexcept
{
    for (const auto cEntity : m_world.view<RemoteComponent, FormIdComponent>())
        if (m_world.get<RemoteComponent>(cEntity).Id == aSessionId)
            return m_world.get<FormIdComponent>(cEntity).Id;
    return 0;
}

void BodyService::OnUpdate(const UpdateEvent&) noexcept
{
    const uint64_t cTick = m_world.GetTransport().GetTick();
    // What this game's player took is its own now: the owner's playback of it stops.
    if (m_pKeeper)
        for (const BodyKey& cKey : m_pKeeper->Update(cTick, m_following))
            Forget(cKey);
    Show(m_playback, cTick > kDelayMs ? cTick - kDelayMs : 0);
    Show(m_ragdolls, cTick > kCopyDelayMs ? cTick - kCopyDelayMs : 0);
}

void BodyService::Show(BodyPlayback& aPlayback, uint64_t aTick) noexcept
{
    if (!aPlayback.Count() || !aTick)
        return;
    for (const auto& cPose : aPlayback.At(aTick))
    {
        const BodyKey& cKey = cPose.Body.Key;
        const uint32_t cCopy = cKey.Actor ? CopyOf(cKey.Actor) : 0;
        const BodyPose cAt{.Key = cKey, .Position = cPose.Body.Position, .Rotation = cPose.Body.Rotation};
        const bool cDone = cPose.Rest ? m_bridge.Rest(cAt, cCopy) : m_bridge.Drive(cPose.Body, cCopy);
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
