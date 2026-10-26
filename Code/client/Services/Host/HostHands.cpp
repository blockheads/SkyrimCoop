#include <Services/Host/HostHands.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/Relayed.h>
#include <Services/WorldStateLog.h>

HostHands::HostHands(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostHands::OnPushes);
    aAuthority.On(*this, &HostHands::OnHeld);
    aAuthority.On(*this, &HostHands::OnReleased);
}

void HostHands::OnPushes(const BodyPushRequest& acMessage, ConnectionId_t aSender) noexcept
{
    if (!m_game.PushBody)
        return;
    for (const auto& cPush : acMessage.Pushes)
    {
        // What a player holds moves with its hands only.
        if (m_holders.contains(cPush.Key) || !m_game.PushBody(cPush))
            continue;
        // Logged once a run of pushes (a second without one ends it).
        const auto [it, cNew] = m_pushed.insert_or_assign(cPush.Key, m_tick);
        if (cNew)
            spdlog::info("HostHands: {:x} pushes {} ({:.1f} at ({:.0f}, {:.0f}, {:.0f}))", aSender, WorldStateLog::DescribeBody(cPush.Key),
                         glm::length(cPush.Impulse), cPush.Point.x, cPush.Point.y, cPush.Point.z);
    }
}

void HostHands::OnHeld(const BodyFramesRequest& acMessage, ConnectionId_t aSender) noexcept
{
    std::vector<BodyFrame> taken;
    for (const auto& cFrame : acMessage.Frames)
    {
        if (const auto cReleased = m_released.find(cFrame.Key); cReleased != m_released.end())
        {
            if (acMessage.Tick <= cReleased->second)
                continue;
            m_released.erase(cReleased);
        }
        const auto [it, cNew] = m_holders.try_emplace(cFrame.Key, aSender);
        if (it->second != aSender)
            continue;
        if (cNew)
            spdlog::info("HostHands: {:x} holds {}", aSender, WorldStateLog::DescribeBody(cFrame.Key));
        m_lastHeld[cFrame.Key] = cFrame;
        taken.push_back(cFrame);
    }
    if (taken.empty())
        return;
    m_heldPlayback.Add(acMessage.Tick, taken);
    // The other guests see it in the holder's hands as the host does.
    NotifyBodyFrames frames;
    frames.Tick = acMessage.Tick;
    frames.Frames = std::move(taken);
    for (const ConnectionId_t cGuest : m_players.Connections())
        if (cGuest != aSender && cGuest != m_players.Host())
            m_network.Send(cGuest, frames, Net::kUnreliable);
}

void HostHands::OnReleased(const BodyReleaseRequest& acMessage, ConnectionId_t aSender) noexcept
{
    for (const auto& cFrame : acMessage.Frames)
    {
        const auto it = m_holders.find(cFrame.Key);
        if (it == m_holders.end() || it->second != aSender)
            continue;
        spdlog::info("HostHands: {:x} let go of {} at ({:.0f}, {:.0f}, {:.0f}), moving {:.0f}", aSender, WorldStateLog::DescribeBody(cFrame.Key),
                     cFrame.Position.x, cFrame.Position.y, cFrame.Position.z, glm::length(cFrame.LinearVelocity));
        m_released[cFrame.Key] = acMessage.Tick;
        Free(cFrame);
    }
}

void HostHands::Free(const BodyFrame& acFrame) noexcept
{
    m_holders.erase(acFrame.Key);
    m_lastHeld.erase(acFrame.Key);
    m_heldPlayback.Forget(acFrame.Key);
    if (m_game.FreeBody && !m_game.FreeBody(acFrame))
        spdlog::warn("HostHands: {} is not here to let go of", WorldStateLog::DescribeBody(acFrame.Key));
}

void HostHands::Update(uint64_t aTick) noexcept
{
    m_tick = aTick;
    std::erase_if(m_pushed, [aTick](const auto& acPushed) { return aTick - acPushed.second > 1000; });
    if (!m_heldPlayback.Count() || !m_game.DriveBody || aTick <= kHeldDelayMs)
        return;
    for (const auto& cPose : m_heldPlayback.At(aTick - kHeldDelayMs, 0.f))
        if (!cPose.Rest)
            m_game.DriveBody(cPose.Body);
    // A holder gone silent (its game stalled, or a lost release): it falls where it was last held.
    for (const auto& cLost : m_heldPlayback.TakeLost())
        if (const auto it = m_lastHeld.find(cLost.Key); it != m_lastHeld.end())
        {
            spdlog::warn("HostHands: no frame of {} from its holder for {} ms: let go", WorldStateLog::DescribeBody(cLost.Key), BodyPlayback::kLostMs);
            BodyFrame frame = it->second;
            frame.LinearVelocity = frame.AngularVelocity = {};
            Free(frame);
        }
}

void HostHands::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    // What it held falls where it was.
    std::vector<BodyFrame> dropped;
    for (const auto& [cKey, cHolder] : m_holders)
        if (cHolder == aPlayer)
            if (const auto it = m_lastHeld.find(cKey); it != m_lastHeld.end())
                dropped.push_back(it->second);
    for (BodyFrame& frame : dropped)
    {
        frame.LinearVelocity = frame.AngularVelocity = {};
        Free(frame);
    }
}
