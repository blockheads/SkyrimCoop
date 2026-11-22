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
    aAuthority.On(*this, &HostHands::OnHeld);
    aAuthority.On(*this, &HostHands::OnReleased);
    aAuthority.On(*this, &HostHands::OnRests);
}

bool HostHands::OwnsRagdoll(const BodyKey& acKey, ConnectionId_t aSender) const noexcept
{
    return acKey.Actor && m_characters.OwnerOf(acKey.Actor) == aSender;
}

bool HostHands::HostsBody(const BodyKey& acKey) const noexcept
{
    return !acKey.Actor || (m_characters.OwnerOf(acKey.Actor) == m_players.Host() && m_game.IsDead && m_game.IsDead(acKey.Actor));
}

void HostHands::OnHeld(const BodyFramesRequest& acMessage, ConnectionId_t aSender) noexcept
{
    NotifyBodyFrames ragdolls, held;
    ragdolls.Tick = held.Tick = acMessage.Tick;
    held.Held = acMessage.Held;
    for (const auto& cFrame : acMessage.Frames)
    {
        if (cFrame.Key.Actor && !HostsBody(cFrame.Key))
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
    // Taken back by the host's game: its mover's frames from before it heard are dropped.
    if (const auto cTaken = m_taken.find(acFrame.Key); cTaken != m_taken.end())
    {
        if (cTaken->second.Mover == aSender && aTick <= cTaken->second.Until)
            return false;
        m_taken.erase(cTaken);
    }
    const auto [it, cNew] = m_holds.try_emplace(acFrame.Key, Hold{.Holder = aSender});
    if (it->second.Holder != aSender)
    {
        m_defeated[acFrame.Key][aSender] = m_tick;
        return false;
    }
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
    std::vector<BodyKey> refused;
    for (const auto& cRest : acMessage.Rests)
        if (HostsBody(cRest.Key))
        {
            // What its player moved, at rest in its game: where it lies in every game.
            const auto it = m_holds.find(cRest.Key);
            if (it == m_holds.end() || it->second.Holder != aSender || it->second.Held || acMessage.Tick < it->second.Tick)
            {
                // A first mover whose (unreliable) frames never came, or came late: its rest is taken.
                if (it == m_holds.end() && !Defeated(cRest.Key, aSender))
                {
                    spdlog::info("HostHands: {:x}'s {} rests at ({:.0f}, {:.0f}, {:.0f}), no frame of it heard", aSender,
                                 WorldStateLog::DescribeBody(cRest.Key), cRest.Position.x, cRest.Position.y, cRest.Position.z);
                    if (!cRest.Key.Actor) // a corpse's rest becomes the host's part, not a guest's rest kept here
                        m_guestRests[cRest.Key] = {cRest, m_tick, aSender};
                    rests.Rests.push_back(cRest);
                    continue;
                }
                // Another mover's won and rested: the sender gets where the host has it. A winner still moving it
                // sends its rest later.
                if (it == m_holds.end())
                    refused.push_back(cRest.Key);
                continue;
            }
            spdlog::info("HostHands: {:x}'s {} rests at ({:.0f}, {:.0f}, {:.0f})", aSender, WorldStateLog::DescribeBody(cRest.Key), cRest.Position.x,
                         cRest.Position.y, cRest.Position.z);
            m_holds.erase(it);
            if (!cRest.Key.Actor)
                m_guestRests[cRest.Key] = {cRest, m_tick, aSender};
            rests.Rests.push_back(cRest);
        }
    if (!rests.Rests.empty())
        m_players.SendToOthers(rests, aSender, Net::kReliable);
    if (!refused.empty() && m_game.ReadBodies)
    {
        NotifyBodyRests answer;
        answer.Tick = m_tick;
        answer.Rests = m_game.ReadBodies(refused);
        if (!answer.Rests.empty())
        {
            spdlog::info("HostHands: {:x}'s rest of {} refused: it gets the host's", aSender, WorldStateLog::DescribeBody(refused[0]));
            m_players.SendTo(aSender, answer);
        }
    }
}

void HostHands::OnReleased(const BodyReleaseRequest& acMessage, ConnectionId_t aSender) noexcept
{
    NotifyBodyFrames freed;
    freed.Tick = acMessage.Tick;
    for (const auto& cFrame : acMessage.Frames)
    {
        const auto it = m_holds.find(cFrame.Key);
        if (!HostsBody(cFrame.Key) || (it != m_holds.end() && (it->second.Holder != aSender || acMessage.Tick < it->second.Tick)))
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
    // A corpse's bodies stay simulated while played (steered): nothing to free.
    if (acFrame.Key.Actor)
        return;
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

bool HostHands::Defeated(const BodyKey& acKey, ConnectionId_t aSender) noexcept
{
    if (m_taken.contains(acKey))
        return true;
    if (const auto it = m_guestRests.find(acKey); it != m_guestRests.end() && it->second.Mover != aSender)
        return true;
    const auto it = m_defeated.find(acKey);
    return it != m_defeated.end() && it->second.erase(aSender);
}

void HostHands::OnHostPlaced(const GameId& acObject, const glm::vec3& acWhere) noexcept
{
    const auto cTell = [&](const BodyKey& acKey, ConnectionId_t aMover, const glm::quat& acRotation) {
        NotifyBodyRests placed;
        placed.Tick = m_tick;
        const auto cPoses = m_game.ReadBodies ? m_game.ReadBodies({acKey}) : std::vector<BodyPose>{};
        placed.Rests = {cPoses.empty() ? BodyPose{.Key = acKey, .Position = acWhere, .Rotation = acRotation} : cPoses[0]};
        placed.Placed = true;
        m_players.SendTo(aMover, placed);
    };
    // Placed again (the console's setpos goes an axis at a time): its last mover gets each.
    for (const auto& [cKey, cTaken] : m_taken)
        if (cKey.Object == acObject)
            cTell(cKey, cTaken.Mover, glm::quat(1.f, 0.f, 0.f, 0.f));
    for (auto it = m_holds.begin(); it != m_holds.end();)
    {
        const BodyKey cKey = it->first;
        if (it->second.Held || cKey.Actor || cKey.Object != acObject || glm::distance(acWhere, it->second.Last.Position) <= kPlacedApart)
        {
            ++it;
            continue;
        }
        const ConnectionId_t cMover = it->second.Holder;
        spdlog::info("HostHands: the host's game placed {} at ({:.0f}, {:.0f}, {:.0f}): taken back from {:x}", WorldStateLog::DescribeBody(cKey),
                     acWhere.x, acWhere.y, acWhere.z, cMover);
        // On the mover's clock, as its frames are stamped.
        m_taken[cKey] = {cMover, it->second.Tick + kTakenMs, m_tick};
        const BodyFrame cPlaced{.Key = cKey, .Position = acWhere, .Rotation = it->second.Last.Rotation};
        it = m_holds.erase(it);
        m_guestRests.erase(cKey);
        // The mover's frames the host's game was following end here.
        if (m_game.FreeBody)
            (void)m_game.FreeBody(cPlaced, m_tick);
        cTell(cKey, cMover, cPlaced.Rotation);
    }
    // A mover's rest the host's game may still be putting: placed elsewhere by it, that rest is not put any more.
    for (auto it = m_guestRests.begin(); it != m_guestRests.end();)
    {
        const BodyKey cKey = it->first;
        if (cKey.Actor || cKey.Object != acObject || glm::distance(acWhere, it->second.Pose.Position) <= kPlacedApart)
        {
            ++it;
            continue;
        }
        spdlog::info("HostHands: the host's game placed {} at ({:.0f}, {:.0f}, {:.0f}): a mover's rest no longer put", WorldStateLog::DescribeBody(cKey),
                     acWhere.x, acWhere.y, acWhere.z);
        const BodyFrame cPlaced{.Key = cKey, .Position = acWhere, .Rotation = it->second.Pose.Rotation};
        const ConnectionId_t cMover = it->second.Mover;
        it = m_guestRests.erase(it);
        if (m_game.FreeBody)
            (void)m_game.FreeBody(cPlaced, m_tick);
        // Its mover's game gives it back: its own physics moving it after this is not its own rest settling.
        cTell(cKey, cMover, cPlaced.Rotation);
    }
}

void HostHands::Update(uint64_t aTick) noexcept
{
    m_tick = aTick;
    std::erase_if(m_guestRests, [aTick](const auto& acRest) { return aTick - acRest.second.At > kSilentMs; });
    std::erase_if(m_taken, [aTick](const auto& acTaken) { return aTick - acTaken.second.At > kSilentMs; });
    for (auto& [cKey, defeated] : m_defeated)
        std::erase_if(defeated, [aTick](const auto& acMover) { return aTick - acMover.second > kDefeatedMs; });
    std::erase_if(m_defeated, [](const auto& acDefeated) { return acDefeated.second.empty(); });
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
