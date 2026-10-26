#include <Services/Guest/GuestPhysics.h>

#include <Bridges/GuestEngine.h>
#include <Components.h>
#include <Events/UpdateEvent.h>
#include <Messages/Relayed.h>
#include <Services/TransportService.h>
#include <Services/WorldStateLog.h>
#include <World.h>

GuestPhysics::GuestPhysics(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept
    : m_world(aWorld)
    , m_engine(aEngine)
    , m_hands(aWorld, aEngine)
{
    Listen<NotifyBodyFrames, &GuestPhysics::OnFrames>(m_connections, aDispatcher, this);
    Listen<NotifyBodyRests, &GuestPhysics::OnRests>(m_connections, aDispatcher, this);
}

void GuestPhysics::OnFrames(const NotifyBodyFrames& acMessage) noexcept
{
    std::vector<BodyFrame> frames;
    for (const auto& cFrame : acMessage.Frames)
    {
        if (!m_hands.TakesHostFrame(cFrame.Key))
            continue;
        // This game's own physics may have moved it first (its copy of the blast): from there into the host's.
        if (!m_playback.Has(cFrame.Key))
            if (const auto cShown = m_engine.ReadPose(cFrame.Key))
                m_playback.StartFrom(*cShown);
        frames.push_back(cFrame);
    }
    m_playback.Add(acMessage.Tick, frames);
}

void GuestPhysics::OnRests(const NotifyBodyRests& acMessage) noexcept
{
    std::vector<BodyPose> rests;
    for (const auto& cRest : acMessage.Rests)
        if (!m_hands.IsHeld(cRest.Key))
            rests.push_back(cRest);
    m_playback.Rest(acMessage.Tick, rests);
}

uint32_t GuestPhysics::CopyOf(uint32_t aSessionId) const noexcept
{
    for (const auto cEntity : m_world.view<RemoteComponent, FormIdComponent>())
        if (m_world.get<RemoteComponent>(cEntity).Id == aSessionId)
            return m_world.get<FormIdComponent>(cEntity).Id;
    return 0;
}

void GuestPhysics::OnUpdate(const UpdateEvent&) noexcept
{
    const uint64_t cTick = m_world.GetTransport().GetTick();
    m_engine.FreePlaced();
    // What this game's player took is its own now: the host's playback of it stops.
    for (const BodyKey& cKey : m_hands.Update(cTick, m_following))
    {
        m_playback.Forget(cKey);
        m_following.erase(cKey);
    }
    ShowHost(cTick);
}

void GuestPhysics::ShowHost(uint64_t aTick) noexcept
{
    if (!m_playback.Count() || aTick <= kDelayMs)
        return;
    for (const auto& cPose : m_playback.At(aTick - kDelayMs))
    {
        const BodyKey& cKey = cPose.Body.Key;
        const uint32_t cCopy = cKey.Actor ? CopyOf(cKey.Actor) : 0;
        const BodyPose cAt{.Key = cKey, .Position = cPose.Body.Position, .Rotation = cPose.Body.Rotation};
        const bool cDone = cPose.Rest ? m_engine.RestBody(cAt, cCopy) : m_engine.DriveBody(cPose.Body, cCopy);
        // Not loaded here (its cell, or its 3D): nothing to show; its cell's load gets the host's rest.
        if (!cDone)
        {
            m_playback.Forget(cKey);
            if (m_missing.insert(cKey).second)
                spdlog::warn("GuestPhysics: the host's {} is not in this game", WorldStateLog::DescribeBody(cKey));
            continue;
        }
        m_missing.erase(cKey);
        if (cPose.Rest)
        {
            m_following.erase(cKey);
            m_hands.OnHostRest(cAt);
            spdlog::info("GuestPhysics: placed the host's rest of {} at ({:.0f}, {:.0f}, {:.0f}) after {} frames", WorldStateLog::DescribeBody(cKey),
                         cPose.Body.Position.x, cPose.Body.Position.y, cPose.Body.Position.z, cPose.Frames);
        }
        else if (m_following.insert(cKey).second)
            spdlog::info("GuestPhysics: follows the host's {}", WorldStateLog::DescribeBody(cKey));
    }
    for (const auto& cLost : m_playback.TakeLost())
    {
        m_following.erase(cLost.Key);
        spdlog::warn("GuestPhysics: lost the host's {}: no frame for {} ms and no rest, after {} frames", WorldStateLog::DescribeBody(cLost.Key),
                     BodyPlayback::kLostMs, cLost.Frames);
    }
}
