#include <Services/Guest/GuestHands.h>

#include <Bridges/IBodyBridge.h>
#include <Bridges/PhysicsBodies.h>
#include <Physics/BodyPrediction.h>
#include <Physics/BodyRead.h>

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
    auto own = m_engine.TakeOwnBodies(aTick, CopySessions());
    // Its player's drop falls as this game's physics drops it until the host's rest.
    for (const BodyKey& cKey : m_engine.TakeAdoptedDrops())
        Own(cKey, aTick, "dropped", owned);
    SendHands(aTick, owned, frames);
    OwnKnocked(aTick, acPlayed, own.Objects, owned);
    OwnNudged();
    OwnIsland(aTick, acPlayed, owned);
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
    m_asked.erase(acKey);
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

bool GuestHands::RestedHere(const GameId& acObject, const glm::vec3& acPosition) const noexcept
{
    // The host put this rest and its step awake there settled it a little (BodyPrediction::PlaceRest): within the
    // send limits it is the same rest.
    return std::ranges::any_of(m_ownRests, [&](const auto& acRest) {
        return acRest.first.Object == acObject && glm::distance(acRest.second.Position, acPosition) <= BodySendLimits{}.Distance;
    });
}

void GuestHands::GiveBack(const BodyKey& acKey) noexcept
{
    m_world.ctx().at<IBodyBridge&>().SetMoving(acKey, false);
    if (m_own.erase(acKey) + m_ownRests.erase(acKey))
        spdlog::info("GuestHands: the host's game placed {}: no longer this game's", WorldStateLog::DescribeBody(acKey));
    m_sends.Forget(acKey);
}

void GuestHands::OnRest(const BodyPose& acRest) noexcept
{
    m_ownRests.erase(acRest.Key);
    m_own.erase(acRest.Key);
    m_moved.erase(acRest.Key);
    if (acRest.Key.Actor)
        return;
    // A rest somewhere new starts the count over.
    const BodySendLimits cLimits;
    if (const auto it = m_hostRests.find(acRest.Key); it != m_hostRests.end())
        if (const auto cApart = BodyPrediction::Between({.Position = acRest.Position, .Rotation = acRest.Rotation},
                                                        {.Position = it->second.Position, .Rotation = it->second.Rotation});
            cApart.Distance > cLimits.Distance || cApart.Angle > cLimits.Angle)
            m_asked.erase(acRest.Key);
    m_hostRests[acRest.Key] = acRest;
}

void GuestHands::Own(const BodyKey& acKey, uint64_t aTick, const char* acpHow, std::vector<BodyKey>& aOwned) noexcept
{
    m_moved.erase(acKey);
    m_asked.erase(acKey);
    m_ownRests.erase(acKey);
    const auto [it, cNew] = m_own.try_emplace(acKey);
    it->second.Touched = aTick;
    if (!cNew)
        return;
    if (const auto cNow = m_world.ctx().at<IBodyBridge&>().ReadPose(acKey))
        it->second.From = LayAt(*cNow);
    aOwned.push_back(acKey);
    spdlog::info("GuestHands: this game's player {} the host's {}", acpHow, WorldStateLog::DescribeBody(acKey));
}

void GuestHands::SendHands(uint64_t aTick, std::vector<BodyKey>& aOwned, std::vector<BodyFrame>& aFrames) noexcept
{
    auto& transport = m_world.GetTransport();
    // What its character pushed or its weapon hit is this game's to move: its frames carry it to every other game
    // (no impulse goes to the host: applied there too, the body was launched twice).
    for (const auto& cPush : m_engine.TakePushes())
        if (!m_holding.contains(cPush.Key))
            Own(cPush.Key, aTick, "pushes", aOwned);
    for (const auto& cHit : m_engine.TakeHits())
        if (!m_holding.contains(cHit.Key))
            Own(cHit.Key, aTick, "hits", aOwned);

    // Let go: what it held and holds no more, from its last frame.
    const auto cHeld = m_engine.ReadHeld(CopySessions());
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

void GuestHands::OwnKnocked(uint64_t aTick, const std::set<BodyKey>& acPlayed, const std::vector<BodyFrame>& acAwake, std::vector<BodyKey>& aOwned) noexcept
{
    // Until none is added: a knocked body knocks the next.
    for (bool added = true; added;)
    {
        added = false;
        for (const BodyFrame& cFrame : acAwake)
        {
            if (cFrame.Key.Actor || m_own.contains(cFrame.Key) || acPlayed.contains(cFrame.Key))
                continue;
            const bool cNear = std::ranges::any_of(m_own, [&](const auto& acOwn) {
                return !acOwn.first.Actor && acOwn.second.Last && glm::distance(acOwn.second.Last->Position, cFrame.Position) <= kKnockReach;
            });
            if (!cNear)
                continue;
            Own(cFrame.Key, aTick, "knocks", aOwned);
            m_own.at(cFrame.Key).Last = BodyPose{.Key = cFrame.Key, .Position = cFrame.Position, .Rotation = cFrame.Rotation};
            added = true;
        }
    }
}

namespace
{
// Off where it lay: past a settle in place (kBodySameDistance, kBodySameAngle).
bool LeftWhereItLay(const BodyPose& acNow, const BodyPose& acLay) noexcept
{
    const auto cApart = BodyPrediction::Between({.Position = acNow.Position, .Rotation = acNow.Rotation},
                                                {.Position = acLay.Position, .Rotation = acLay.Rotation});
    return cApart.Distance >= kBodySameDistance || cApart.Angle >= kBodySameAngle;
}
} // namespace

BodyPose GuestHands::LayAt(const BodyPose& acNow) const noexcept
{
    const auto cRest = m_hostRests.find(acNow.Key);
    return cRest != m_hostRests.end() && glm::distance(cRest->second.Position, acNow.Position) <= BodySendLimits{}.Distance ? cRest->second : acNow;
}

void GuestHands::OwnNudged() noexcept
{
    auto& bridge = m_world.ctx().at<IBodyBridge&>();
    for (auto& [cKey, owned] : m_own)
    {
        if (cKey.Actor || owned.Moving || !owned.From || m_holding.contains(cKey))
            continue;
        const auto cNow = bridge.ReadPose(cKey);
        if (!cNow || !LeftWhereItLay(*cNow, *owned.From))
            continue;
        owned.Moving = true;
        owned.Last = cNow;
        m_engine.FollowOwn(cKey);
    }
}

void GuestHands::OwnIsland(uint64_t aTick, const std::set<BodyKey>& acPlayed, std::vector<BodyKey>& aOwned) noexcept
{
    std::vector<BodyKey> own;
    for (const auto& [cKey, cOwned] : m_own)
        if (!cKey.Actor)
            own.push_back(cKey);
    std::map<BodyKey, BodyPose> pile;
    m_islands.clear();
    for (const auto& [cMate, cIsland] : own.empty() ? std::vector<GuestEngine::IslandBody>{} : m_engine.ReadIslands(own))
    {
        if (m_own.contains(cMate.Key))
            m_islands[cMate.Key] = cIsland;
        if (acPlayed.contains(cMate.Key) || m_own.contains(cMate.Key))
            continue;
        const auto cLay = m_pile.find(cMate.Key);
        const BodyPose cFrom = cLay != m_pile.end() ? cLay->second : LayAt(cMate);
        // Beside one of its own, as a knock's reach: Havok also merges small islands that never touch (61522).
        const bool cBeside = std::ranges::any_of(m_own, [&](const auto& acOwn) {
            const auto& cAt = acOwn.second.Last ? acOwn.second.Last : acOwn.second.From;
            return !acOwn.first.Actor && cAt && glm::distance(cAt->Position, cMate.Position) <= kKnockReach;
        });
        if (!cBeside)
            continue;
        if (!LeftWhereItLay(cMate, cFrom))
        {
            pile[cMate.Key] = cFrom;
            continue;
        }
        // Moved by its own bodies: its frames and rest go out from here, whatever PhysicsBodies had seen of it.
        Own(cMate.Key, aTick, "lands on", aOwned);
        auto& owned = m_own.at(cMate.Key);
        owned.Moving = true;
        owned.Last = cMate;
        m_engine.FollowOwn(cMate.Key);
        m_islands[cMate.Key] = cIsland;
    }
    m_pile = std::move(pile);
}

void GuestHands::SendOwn(uint64_t aTick, GuestEngine::OwnBodies& aOwn, std::vector<BodyFrame>& aFrames) noexcept
{
    std::vector<BodyFrame> free;
    for (const BodyFrame& cFrame : aOwn.Objects)
        if (const auto it = m_own.find(cFrame.Key); it != m_own.end())
        {
            it->second.Moving = true;
            it->second.Seen = aTick;
            it->second.Last = BodyPose{.Key = cFrame.Key, .Position = cFrame.Position, .Rotation = cFrame.Rotation};
            it->second.Rest.reset();
            it->second.RestSince = 0;
            if (!m_holding.contains(cFrame.Key))
                free.push_back(cFrame);
        }
    // Free fall marked, so the others' guess (and the send rule) curves as it falls.
    m_motions.Mark(free, aTick);
    aFrames.insert(aFrames.end(), free.begin(), free.end());
    BodyRestsRequest rests;
    rests.Tick = aTick;
    for (const BodyPose& cRest : aOwn.ObjectsRested)
    {
        const auto it = m_own.find(cRest.Key);
        if (it == m_own.end() || m_holding.contains(cRest.Key) || !it->second.Moving)
            continue;
        it->second.Rest = it->second.Last = cRest;
        if (!it->second.RestSince)
            it->second.RestSince = aTick;
    }
    // A pile's rests go together: sent one by one the host put each among neighbours this game still moved, and a
    // body nudged again was claimed and rested many times a second (carry_into_pile, duo 2026-11-22).
    std::set<uint32_t> moving;
    for (const auto& [cKey, cOwned] : m_own)
        if (const auto cIsland = m_islands.find(cKey);
            cIsland != m_islands.end() && !m_holding.contains(cKey) && !cOwned.Rest && cOwned.Moving && cOwned.Seen == aTick)
            moving.insert(cIsland->second);
    for (auto it = m_own.begin(); it != m_own.end();)
    {
        // Its island still moves, or its player's capsule leans on it.
        const auto cIsland = m_islands.find(it->first);
        const bool cWaits = (cIsland != m_islands.end() && moving.contains(cIsland->second)) || aTick - it->second.Touched < kLeanMs;
        if (!it->second.Rest || (cWaits && aTick - it->second.RestSince < kPileRestMs))
        {
            ++it;
            continue;
        }
        // Where it lies now: pressed on a little while it waited, PhysicsBodies read nothing new of it.
        BodyPose cRest = *it->second.Rest;
        if (const auto cNow = cRest.Key.Actor ? std::nullopt : m_world.ctx().at<IBodyBridge&>().ReadPose(cRest.Key))
            cRest = {.Key = cRest.Key, .Position = cNow->Position, .Rotation = cNow->Rotation};
        spdlog::info("GuestHands: the host's {} rests where this game's player put it, at ({:.0f}, {:.0f}, {:.0f})",
                     WorldStateLog::DescribeBody(cRest.Key), cRest.Position.x, cRest.Position.y, cRest.Position.z);
        it = m_own.erase(it);
        m_world.ctx().at<IBodyBridge&>().SetMoving(cRest.Key, false);
        Rested(cRest, aTick, rests);
    }
    if (!rests.Rests.empty())
        m_world.GetTransport().Send(rests);
}

void GuestHands::Rested(const BodyPose& acRest, uint64_t aTick, BodyRestsRequest& aRests) noexcept
{
    // The host's frames and rests from before this rest come late: not played.
    m_world.ctx().at<BodyService>().Forget(acRest.Key, aTick);
    m_sends.Forget(acRest.Key);
    m_motions.Forget(acRest.Key);
    m_hostRests[acRest.Key] = acRest;
    m_ownRests[acRest.Key] = acRest;
    aRests.Rests.push_back(acRest);
}

GuestEngine::SessionOf_t GuestHands::CopySessions() const noexcept
{
    return [this](uint32_t aFormId) -> uint32_t {
        for (const auto cEntity : m_world.view<RemoteComponent, FormIdComponent>())
            if (m_world.get<FormIdComponent>(cEntity).Id == aFormId)
                return m_world.get<RemoteComponent>(cEntity).Id;
        return 0;
    };
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
    {
        m_world.ctx().at<BodyService>().DeclareRagdolls(aTick, aOwn.RagdollRested, aOwn.Ragdoll);
        return;
    }
    for (BodyPose& rest : aOwn.RagdollRested)
    {
        rest.Key.Actor = cPlayer;
        m_sends.Forget(rest.Key);
        m_motions.Forget(rest.Key);
    }
    spdlog::info("GuestHands: this game's player's ragdoll rests ({} bodies, {} updates moving)", aOwn.RagdollRested.size(), std::exchange(m_ragdollFrames, 0));
    m_world.ctx().at<BodyService>().DeclareRagdolls(aTick, aOwn.RagdollRested, aOwn.Ragdoll);
}

void GuestHands::TrackMoved(uint64_t aTick, const std::set<BodyKey>& acPlayed, const std::vector<BodyPose>& acMoved) noexcept
{
    for (auto& [cKey, moved] : m_moved)
        moved.Seen = false;
    for (const auto& cMove : acMoved)
    {
        if (m_own.contains(cMove.Key) || acPlayed.contains(cMove.Key) || m_pile.contains(cMove.Key))
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
        if (m_holding.contains(it->first) || it->second.Rest || aTick - std::max(it->second.Touched, it->second.Seen) < kOwnMs)
        {
            ++it;
            continue;
        }
        // Not awake here since: where it was last is its rest (a let go that never fell); never moved, the host's.
        if (it->second.Moving && it->second.Last)
            Rested(*it->second.Last, aTick, rests);
        else
            spdlog::info("GuestHands: this game's player touched the host's {}, which never moved here", WorldStateLog::DescribeBody(it->first));
        m_world.ctx().at<IBodyBridge&>().SetMoving(it->first, false);
        it = m_own.erase(it);
    }
    if (!rests.Rests.empty())
        m_world.GetTransport().Send(rests);
    // One due, every far one is asked with it: a pile's bodies moved one after another, and the host's rests one at a
    // time each landed among the others still off theirs (six bodies asked in turn every second, for good).
    const bool cDue = std::ranges::any_of(m_moved, [&](const auto& acMoved) { return acMoved.second.Far && aTick - acMoved.second.Since >= kOwnMs; });
    for (auto it = m_moved.begin(); it != m_moved.end();)
    {
        if (!it->second.Far || !cDue)
        {
            ++it;
            continue;
        }
        if (uint8_t& asks = m_asked[it->first]; asks >= kAsksAtMost)
        {
            if (asks++ == kAsksAtMost)
                spdlog::info("GuestHands: the host's {} does not stay at the host's rest in this game: left where it lies",
                             WorldStateLog::DescribeBody(it->first));
            asks = std::min<uint8_t>(asks, kAsksAtMost + 1);
        }
        else
        {
            ++asks;
            spdlog::info("GuestHands: this game moved the host's {} by itself", WorldStateLog::DescribeBody(it->first));
            asked.Rests.push_back({.Key = it->first});
        }
        it = m_moved.erase(it);
    }
    if (!asked.Rests.empty())
        m_world.GetTransport().Send(asked);
}
