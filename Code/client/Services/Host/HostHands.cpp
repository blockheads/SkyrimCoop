#include <Services/Host/HostHands.h>

#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>

#include <Messages/Relayed.h>
#include <Physics/BodyPrediction.h>
#include <Physics/BodySends.h>
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
    NotifyBodyFrames ragdolls, held;
    ragdolls.Tick = held.Tick = acMessage.Tick;
    held.Held = acMessage.Held;
    for (const auto& cFrame : acMessage.Frames)
    {
        if (cFrame.Key.Actor)
        {
            if (!OwnsRagdoll(cFrame.Key, aSender))
                continue;
            if (++m_ragdolls[cFrame.Key] == 1)
                spdlog::info("HostHands: {:x}'s {} moves", aSender, WorldStateLog::DescribeBody(cFrame.Key));
            ragdolls.Frames.push_back(cFrame);
        }
        else if (TakeHeld(cFrame, acMessage.Tick, aSender, acMessage.Held))
            held.Frames.push_back(cFrame);
    }
    // A ragdoll: every other game sees it as its owner's game does, the host's too.
    if (!ragdolls.Frames.empty())
        m_players.SendToOthers(ragdolls, aSender, Net::kUnreliable);
    // A hold: the holder's alone (2026-11-02), so every other game, the host's too, plays its frames keyframed; a body
    // it moved, steered (its mass kept).
    if (!held.Frames.empty())
        m_players.SendToOthers(held, aSender, Net::kUnreliable);
}

bool HostHands::TakeHeld(const BodyFrame& acFrame, uint64_t aTick, ConnectionId_t aSender, bool aHeld) noexcept
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
    if (!cNew && aTick < it->second.Tick)
        return false;
    if (cNew || it->second.Held != aHeld)
        spdlog::info("HostHands: {:x} {} {}", aSender, aHeld ? "holds" : "moves", WorldStateLog::DescribeBody(acFrame.Key));
    m_guestRests.erase(acFrame.Key);
    it->second.Held = aHeld;
    it->second.Last = acFrame;
    it->second.Tick = aTick;
    it->second.At = m_tick;
    return true;
}

void HostHands::OnRests(const BodyRestsRequest& acMessage, ConnectionId_t aSender) noexcept
{
    NotifyBodyRests rests;
    rests.Tick = acMessage.Tick;
    for (const auto& cRest : acMessage.Rests)
        if (!cRest.Key.Actor)
        {
            // What its player moved, at rest in its game: where it lies in every game.
            const auto it = m_holds.find(cRest.Key);
            if (it == m_holds.end() || it->second.Holder != aSender || it->second.Held || acMessage.Tick < it->second.Tick)
                continue;
            spdlog::info("HostHands: {:x}'s {} rests at ({:.0f}, {:.0f}, {:.0f})", aSender, WorldStateLog::DescribeBody(cRest.Key), cRest.Position.x,
                         cRest.Position.y, cRest.Position.z);
            m_holds.erase(it);
            m_guestRests[cRest.Key] = {cRest, m_tick};
            rests.Rests.push_back(cRest);
        }
        else if (OwnsRagdoll(cRest.Key, aSender))
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
    NotifyBodyFrames freed;
    freed.Tick = acMessage.Tick;
    for (const auto& cFrame : acMessage.Frames)
    {
        const auto it = m_holds.find(cFrame.Key);
        if (cFrame.Key.Actor || (it != m_holds.end() && (it->second.Holder != aSender || acMessage.Tick < it->second.Tick)))
            continue;
        if (const auto cLast = m_released.find(cFrame.Key); cLast != m_released.end() && acMessage.Tick <= cLast->second)
            continue;
        spdlog::info("HostHands: {:x} let go of {} at ({:.0f}, {:.0f}, {:.0f}), moving {:.0f}", aSender, WorldStateLog::DescribeBody(cFrame.Key),
                     cFrame.Position.x, cFrame.Position.y, cFrame.Position.z, glm::length(cFrame.LinearVelocity));
        m_released[cFrame.Key] = acMessage.Tick;
        Free(cFrame, acMessage.Tick);
        // Still the mover's: its own fall decides until its rest; every other game's hold of it ends here.
        m_holds[cFrame.Key] = Hold{.Holder = aSender, .Last = cFrame, .Tick = acMessage.Tick, .At = m_tick};
        freed.Frames.push_back(cFrame);
    }
    if (!freed.Frames.empty())
        m_players.SendToOthers(freed, aSender, Net::kReliable);
}

void HostHands::Free(const BodyFrame& acFrame, uint64_t aReleasedAt) noexcept
{
    m_holds.erase(acFrame.Key);
    if (m_game.FreeBody && !m_game.FreeBody(acFrame, aReleasedAt ? aReleasedAt : m_tick))
        spdlog::warn("HostHands: {} is not here to let go of", WorldStateLog::DescribeBody(acFrame.Key));
}

bool HostHands::HostRests(const BodyPose& acRest) noexcept
{
    const auto it = m_guestRests.find(acRest.Key);
    if (it == m_guestRests.end())
        return !m_holds.contains(acRest.Key);
    const BodySendLimits cLimits;
    const auto cApart = BodyPrediction::Between({.Position = acRest.Position, .Rotation = acRest.Rotation},
                                                {.Position = it->second.Pose.Position, .Rotation = it->second.Pose.Rotation});
    m_guestRests.erase(it);
    return cApart.Distance > cLimits.Distance || cApart.Angle > cLimits.Angle;
}

void HostHands::Update(uint64_t aTick) noexcept
{
    m_tick = aTick;
    std::erase_if(m_pushed, [aTick](const auto& acPushed) { return aTick - acPushed.second > 1000; });
    std::erase_if(m_guestRests, [aTick](const auto& acRest) { return aTick - acRest.second.At > kSilentMs; });
    // A holder gone silent (its game stalled, or a lost let go): it falls where it was last held. A mover's free body
    // goes back to the host's physics where it is.
    std::vector<BodyFrame> dropped;
    for (auto it = m_holds.begin(); it != m_holds.end();)
    {
        if (aTick - it->second.At <= kSilentMs)
        {
            ++it;
            continue;
        }
        spdlog::warn("HostHands: no frame of {} from its {} for {} ms: the host's again", WorldStateLog::DescribeBody(it->first),
                     it->second.Held ? "holder" : "mover", kSilentMs);
        if (it->second.Held)
        {
            dropped.push_back(it->second.Last);
            ++it;
        }
        else
            it = m_holds.erase(it);
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
    // What it held falls where it was; what it moved is the host's where it is.
    std::vector<BodyFrame> dropped;
    for (const auto& [cKey, cHold] : m_holds)
        if (cHold.Holder == aPlayer && cHold.Held)
            dropped.push_back(cHold.Last);
    std::erase_if(m_holds, [aPlayer](const auto& acHold) { return acHold.second.Holder == aPlayer && !acHold.second.Held; });
    for (BodyFrame& frame : dropped)
    {
        frame.LinearVelocity = frame.AngularVelocity = {};
        Free(frame);
    }
}
