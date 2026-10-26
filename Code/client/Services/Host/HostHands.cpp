#include <Services/Host/HostHands.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/Relayed.h>
#include <Services/WorldStateLog.h>

HostHands::HostHands(HostAuthority& aAuthority, const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept
    : m_players(acPlayers)
    , m_characters(acCharacters)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostHands::OnPushes);
    aAuthority.On(*this, &HostHands::OnHeld);
    aAuthority.On(*this, &HostHands::OnReleased);
    aAuthority.On(*this, &HostHands::OnRests);
}

bool HostHands::OwnsRagdoll(const BodyKey& acKey, ConnectionId_t aSender) const noexcept
{
    return acKey.Actor && m_characters.OwnerOf(acKey.Actor) == aSender;
}

void HostHands::OnPushes(const BodyPushRequest& acMessage, ConnectionId_t aSender) noexcept
{
    if (!m_game.PushBody)
        return;
    for (const auto& cPush : acMessage.Pushes)
    {
        // What a player holds moves with its hands only.
        if (m_holds.contains(cPush.Key) || !m_game.PushBody(cPush))
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
        if (cFrame.Key.Actor)
        {
            if (!OwnsRagdoll(cFrame.Key, aSender))
                continue;
            if (++m_ragdolls[cFrame.Key] == 1)
                spdlog::info("HostHands: {:x}'s {} moves", aSender, WorldStateLog::DescribeBody(cFrame.Key));
        }
        else if (!TakeHeld(cFrame, acMessage.Tick, aSender))
            continue;
        taken.push_back(cFrame);
    }
    if (taken.empty())
        return;
    // Every other game sees it as its owner's game does, the host's too.
    NotifyBodyFrames frames;
    frames.Tick = acMessage.Tick;
    frames.Frames = std::move(taken);
    m_players.SendToOthers(frames, aSender, Net::kUnreliable);
}

bool HostHands::TakeHeld(const BodyFrame& acFrame, uint64_t aTick, ConnectionId_t aSender) noexcept
{
    if (const auto cReleased = m_released.find(acFrame.Key); cReleased != m_released.end())
    {
        if (aTick <= cReleased->second)
            return false;
        m_released.erase(cReleased);
    }
    const auto [it, cNew] = m_holds.try_emplace(acFrame.Key, Hold{.Holder = aSender});
    if (it->second.Holder != aSender)
        return false;
    if (cNew)
        spdlog::info("HostHands: {:x} holds {}", aSender, WorldStateLog::DescribeBody(acFrame.Key));
    it->second.Last = acFrame;
    it->second.At = m_tick;
    return true;
}

void HostHands::OnRests(const BodyRestsRequest& acMessage, ConnectionId_t aSender) noexcept
{
    NotifyBodyRests rests;
    rests.Tick = acMessage.Tick;
    for (const auto& cRest : acMessage.Rests)
        if (OwnsRagdoll(cRest.Key, aSender))
        {
            rests.Rests.push_back(cRest);
            const auto it = m_ragdolls.find(cRest.Key);
            spdlog::info("HostHands: {:x}'s {} rests at ({:.0f}, {:.0f}, {:.0f}) after {} frames", aSender, WorldStateLog::DescribeBody(cRest.Key),
                         cRest.Position.x, cRest.Position.y, cRest.Position.z, it != m_ragdolls.end() ? it->second : 0);
            if (it != m_ragdolls.end())
                m_ragdolls.erase(it);
        }
    if (!rests.Rests.empty())
        m_players.SendToOthers(rests, aSender, Net::kReliable);
}

void HostHands::OnReleased(const BodyReleaseRequest& acMessage, ConnectionId_t aSender) noexcept
{
    for (const auto& cFrame : acMessage.Frames)
    {
        const auto it = m_holds.find(cFrame.Key);
        if (it == m_holds.end() || it->second.Holder != aSender)
            continue;
        spdlog::info("HostHands: {:x} let go of {} at ({:.0f}, {:.0f}, {:.0f}), moving {:.0f}", aSender, WorldStateLog::DescribeBody(cFrame.Key),
                     cFrame.Position.x, cFrame.Position.y, cFrame.Position.z, glm::length(cFrame.LinearVelocity));
        m_released[cFrame.Key] = acMessage.Tick;
        Free(cFrame);
    }
}

void HostHands::Free(const BodyFrame& acFrame) noexcept
{
    m_holds.erase(acFrame.Key);
    if (m_game.FreeBody && !m_game.FreeBody(acFrame))
        spdlog::warn("HostHands: {} is not here to let go of", WorldStateLog::DescribeBody(acFrame.Key));
}

void HostHands::Update(uint64_t aTick) noexcept
{
    m_tick = aTick;
    std::erase_if(m_pushed, [aTick](const auto& acPushed) { return aTick - acPushed.second > 1000; });
    // A holder gone silent (its game stalled, or a lost let go): it falls where it was last held.
    std::vector<BodyFrame> dropped;
    for (const auto& [cKey, cHold] : m_holds)
        if (aTick - cHold.At > kSilentMs)
        {
            spdlog::warn("HostHands: no frame of {} from its holder for {} ms: let go", WorldStateLog::DescribeBody(cKey), kSilentMs);
            dropped.push_back(cHold.Last);
        }
    for (BodyFrame& frame : dropped)
    {
        frame.LinearVelocity = frame.AngularVelocity = {};
        Free(frame);
    }
}

void HostHands::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    // Its characters left with it (HostCharacters goes first).
    std::erase_if(m_ragdolls, [this](const auto& acRagdoll) { return !m_characters.OwnerOf(acRagdoll.first.Actor); });
    // What it held falls where it was.
    std::vector<BodyFrame> dropped;
    for (const auto& [cKey, cHold] : m_holds)
        if (cHold.Holder == aPlayer)
            dropped.push_back(cHold.Last);
    for (BodyFrame& frame : dropped)
    {
        frame.LinearVelocity = frame.AngularVelocity = {};
        Free(frame);
    }
}
