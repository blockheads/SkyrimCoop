#include <Services/Guest/GuestHands.h>

#include <Bridges/PhysicsBodies.h>
#include <Physics/BodyPrediction.h>

#include <Components.h>
#include <Messages/Relayed.h>
#include <Services/TransportService.h>
#include <Services/WorldStateLog.h>
#include <World.h>

GuestHands::GuestHands(World& aWorld, GuestEngine& aEngine) noexcept
    : m_world(aWorld)
    , m_engine(aEngine)
{
    m_world.ctx().at<BodyService>().SetKeeper(this);
}

GuestHands::~GuestHands() noexcept
{
    m_world.ctx().at<BodyService>().SetKeeper(nullptr);
}

std::vector<BodyKey> GuestHands::Update(uint64_t aTick, const std::set<BodyKey>& acPlayed) noexcept
{
    std::vector<BodyKey> owned;
    std::vector<BodyFrame> frames;
    auto own = m_engine.TakeOwnBodies(aTick);
    // Its player's drop falls as this game's physics drops it until the host's rest.
    for (const BodyKey& cKey : m_engine.TakeAdoptedDrops())
        Own(cKey, aTick, "dropped", owned);
    SendHands(aTick, owned, frames);
    SendOwn(aTick, own, frames);
    SendRagdoll(aTick, own, frames);
    // Its frames, where the others' guess of them goes wrong (the host's send rule): holds apart, played keyframed.
    if (!frames.empty())
    {
        BodyFramesRequest held, free;
        held.Tick = free.Tick = aTick;
        held.Held = true;
        for (BodyFrame& frame : m_sends.Due(frames, aTick))
            (m_holding.contains(frame.Key) ? held : free).Frames.push_back(std::move(frame));
        for (const auto* pSent : {&held, &free})
            if (!pSent->Frames.empty())
                m_world.GetTransport().Send(*pSent);
    }
    TrackMoved(aTick, acPlayed, own.Moved);
    AskHost(aTick);
    return owned;
}

bool GuestHands::TakesFrame(const BodyKey& acKey) noexcept
{
    // This game's player's: its own flight decides until it rests here.
    if (m_own.contains(acKey))
        return false;
    // Moved alone, and now the host's frames follow it: nothing to ask.
    m_moved.erase(acKey);
    return true;
}

bool GuestHands::KeepsRest(const BodyPose& acRest) const noexcept
{
    if (m_own.contains(acRest.Key))
        return true;
    const auto it = m_ownRests.find(acRest.Key);
    if (it == m_ownRests.end())
        return false;
    const auto cApart = BodyPrediction::Between({.Position = acRest.Position, .Rotation = acRest.Rotation},
                                                {.Position = it->second.Position, .Rotation = it->second.Rotation});
    return cApart.Distance <= PhysicsBodies::kSameRest && cApart.Angle <= PhysicsBodies::kSameRestAngle;
}

void GuestHands::OnRest(const BodyPose& acRest) noexcept
{
    m_ownRests.erase(acRest.Key);
    m_own.erase(acRest.Key);
    m_moved.erase(acRest.Key);
    if (!acRest.Key.Actor)
        m_hostRests[acRest.Key] = acRest;
}

void GuestHands::Own(const BodyKey& acKey, uint64_t aTick, const char* acpHow, std::vector<BodyKey>& aOwned) noexcept
{
    m_moved.erase(acKey);
    m_ownRests.erase(acKey);
    const auto [it, cNew] = m_own.try_emplace(acKey);
    it->second.Touched = aTick;
    if (!cNew)
        return;
    aOwned.push_back(acKey);
    spdlog::info("GuestHands: this game's player {} the host's {}", acpHow, WorldStateLog::DescribeBody(acKey));
}

void GuestHands::SendHands(uint64_t aTick, std::vector<BodyKey>& aOwned, std::vector<BodyFrame>& aFrames) noexcept
{
    auto& transport = m_world.GetTransport();
    // Pushes: the impulse its character gave, for the host to give alike (what it holds moves with its hands only).
    BodyPushRequest pushes;
    pushes.Pushes = m_engine.TakePushes();
    std::erase_if(pushes.Pushes, [this](const BodyPush& acPush) { return m_holding.contains(acPush.Key); });
    for (const auto& cPush : pushes.Pushes)
        Own(cPush.Key, aTick, "pushes", aOwned);
    // Its weapon's hits: pushes too (the host's copy of its swing hits nothing).
    for (const auto& cHit : m_engine.TakeHits())
    {
        if (m_holding.contains(cHit.Key))
            continue;
        Own(cHit.Key, aTick, "hits", aOwned);
        pushes.Pushes.push_back(cHit);
    }
    if (!pushes.Pushes.empty())
        transport.Send(pushes);

    // Let go: what it held and holds no more, from its last frame.
    const auto cHeld = m_engine.ReadHeld();
    BodyReleaseRequest released;
    released.Tick = aTick;
    for (auto it = m_holding.begin(); it != m_holding.end();)
    {
        if (std::ranges::find(cHeld, it->first, &BodyFrame::Key) != cHeld.end())
        {
            ++it;
            continue;
        }
        spdlog::info("GuestHands: this game's player let go of the host's {}", WorldStateLog::DescribeBody(it->first));
        released.Frames.push_back(it->second);
        m_sends.Forget(it->first);
        it = m_holding.erase(it);
    }
    if (!released.Frames.empty())
        transport.Send(released);

    for (const auto& cFrame : cHeld)
    {
        Own(cFrame.Key, aTick, "holds", aOwned);
        auto& own = m_own.at(cFrame.Key);
        own.Moving = true;
        own.Last = BodyPose{.Key = cFrame.Key, .Position = cFrame.Position, .Rotation = cFrame.Rotation};
        m_holding[cFrame.Key] = cFrame;
        aFrames.push_back(cFrame);
    }
}

void GuestHands::SendOwn(uint64_t aTick, GuestEngine::OwnBodies& aOwn, std::vector<BodyFrame>& aFrames) noexcept
{
    for (const BodyFrame& cFrame : aOwn.Objects)
        if (const auto it = m_own.find(cFrame.Key); it != m_own.end())
        {
            it->second.Moving = true;
            it->second.Seen = aTick;
            it->second.Last = BodyPose{.Key = cFrame.Key, .Position = cFrame.Position, .Rotation = cFrame.Rotation};
            if (!m_holding.contains(cFrame.Key))
                aFrames.push_back(cFrame);
        }
    BodyRestsRequest rests;
    rests.Tick = aTick;
    for (const BodyPose& cRest : aOwn.ObjectsRested)
    {
        const auto it = m_own.find(cRest.Key);
        if (it == m_own.end() || m_holding.contains(cRest.Key) || !it->second.Moving)
            continue;
        spdlog::info("GuestHands: the host's {} rests where this game's player put it, at ({:.0f}, {:.0f}, {:.0f})",
                     WorldStateLog::DescribeBody(cRest.Key), cRest.Position.x, cRest.Position.y, cRest.Position.z);
        m_own.erase(it);
        Rested(cRest, rests);
    }
    if (!rests.Rests.empty())
        m_world.GetTransport().Send(rests);
}

void GuestHands::Rested(const BodyPose& acRest, BodyRestsRequest& aRests) noexcept
{
    m_sends.Forget(acRest.Key);
    m_hostRests[acRest.Key] = acRest;
    m_ownRests[acRest.Key] = acRest;
    aRests.Rests.push_back(acRest);
}

uint32_t GuestHands::PlayerId() const noexcept
{
    for (const auto cEntity : m_world.view<LocalComponent, FormIdComponent>())
        if (m_world.get<FormIdComponent>(cEntity).Id == 0x14)
            return m_world.get<LocalComponent>(cEntity).Id;
    return 0;
}

void GuestHands::SendRagdoll(uint64_t aTick, GuestEngine::OwnBodies& aOwn, std::vector<BodyFrame>& aFrames) noexcept
{
    if (aOwn.Ragdoll.empty() && aOwn.RagdollRested.empty())
        return;
    const uint32_t cPlayer = PlayerId();
    if (!cPlayer)
        return;
    for (BodyFrame& frame : aOwn.Ragdoll)
        frame.Key.Actor = cPlayer;
    m_motions.Mark(aOwn.Ragdoll, aTick);
    if (!aOwn.Ragdoll.empty() && m_ragdollFrames++ == 0)
        spdlog::info("GuestHands: this game's player's ragdoll moves ({} bodies awake)", aOwn.Ragdoll.size());
    aFrames.insert(aFrames.end(), aOwn.Ragdoll.begin(), aOwn.Ragdoll.end());
    if (aOwn.RagdollRested.empty())
        return;
    BodyRestsRequest rests;
    rests.Tick = aTick;
    for (BodyPose& rest : aOwn.RagdollRested)
    {
        rest.Key.Actor = cPlayer;
        m_sends.Forget(rest.Key);
        m_motions.Forget(rest.Key);
        rests.Rests.push_back(rest);
    }
    spdlog::info("GuestHands: this game's player's ragdoll rests ({} bodies, {} updates moving)", rests.Rests.size(), std::exchange(m_ragdollFrames, 0));
    m_world.GetTransport().Send(rests);
}

void GuestHands::TrackMoved(uint64_t aTick, const std::set<BodyKey>& acPlayed, const std::vector<BodyPose>& acMoved) noexcept
{
    for (auto& [cKey, moved] : m_moved)
        moved.Seen = false;
    for (const auto& cMove : acMoved)
    {
        if (m_own.contains(cMove.Key) || acPlayed.contains(cMove.Key))
            continue;
        const auto cRest = m_hostRests.find(cMove.Key);
        auto& moved = m_moved.try_emplace(cMove.Key, Moved{aTick, cRest != m_hostRests.end() ? cRest->second : cMove}).first->second;
        moved.Seen = true;
        // As far as the host's send rule would call wrong.
        const BodySendLimits cLimits;
        const auto cApart = BodyPrediction::Between({.Position = cMove.Position, .Rotation = cMove.Rotation},
                                                    {.Position = moved.From.Position, .Rotation = moved.From.Rotation});
        moved.Far |= cApart.Distance > cLimits.Distance || cApart.Angle > cLimits.Angle;
    }
    // Settled where it was: a nudge, nothing to ask.
    std::erase_if(m_moved, [](const auto& acMoved) { return !acMoved.second.Seen && !acMoved.second.Far; });
}

void GuestHands::AskHost(uint64_t aTick) noexcept
{
    BodyPoseRequest asked;
    BodyRestsRequest rests;
    rests.Tick = aTick;
    for (auto it = m_own.begin(); it != m_own.end();)
    {
        if (m_holding.contains(it->first) || aTick - std::max(it->second.Touched, it->second.Seen) < kOwnMs)
        {
            ++it;
            continue;
        }
        // Not awake here since: where it was last is its rest (a let go that never fell); never moved, the host's.
        if (it->second.Moving && it->second.Last)
            Rested(*it->second.Last, rests);
        else
            spdlog::info("GuestHands: this game's player touched the host's {}, which never moved here", WorldStateLog::DescribeBody(it->first));
        it = m_own.erase(it);
    }
    if (!rests.Rests.empty())
        m_world.GetTransport().Send(rests);
    for (auto it = m_moved.begin(); it != m_moved.end();)
    {
        if (!it->second.Far || aTick - it->second.Since < kOwnMs)
        {
            ++it;
            continue;
        }
        spdlog::info("GuestHands: this game moved the host's {} by itself", WorldStateLog::DescribeBody(it->first));
        asked.Rests.push_back({.Key = it->first});
        it = m_moved.erase(it);
    }
    if (!asked.Rests.empty())
        m_world.GetTransport().Send(asked);
}
