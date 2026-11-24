#include <Services/Guest/GuestBodies.h>

#include <Bridges/IBodyBridge.h>
#include <Physics/BodyPrediction.h>
#include <Physics/BodyTolerances.h>

#include <Components.h>
#include <Messages/Relayed.h>
#include <Services/TransportService.h>
#include <Services/WorldStateLog.h>
#include <World.h>

#include <algorithm>

namespace
{
// States per message: about 1 KB, under a datagram's MTU (an unreliable fragment lost loses the packet).
constexpr size_t kStatesPerMessage = 24;
} // namespace

GuestBodies::GuestBodies(World& aWorld, GuestEngine& aEngine) noexcept
    : m_world(aWorld)
    , m_engine(aEngine)
{
    m_world.ctx().at<BodyService>().SetKeeper(this);
}

GuestBodies::~GuestBodies() noexcept
{
    m_world.ctx().at<BodyService>().SetKeeper(nullptr);
}

void GuestBodies::Took(const BodyKey& acKey, const char* acpHow, std::vector<BodyKey>& aTaken) noexcept
{
    m_rests.erase(acKey);
    m_confirmed.erase(acKey);
    if (std::ranges::find(aTaken, acKey) == aTaken.end())
        aTaken.push_back(acKey);
    m_engine.FollowOwn(acKey);
    spdlog::info("GuestBodies: this game's player {} the host's {}", acpHow, WorldStateLog::DescribeBody(acKey));
}

void GuestBodies::Drop(const BodyKey& acKey) noexcept
{
    m_world.ctx().at<IBodyBridge&>().SetMoving(acKey, false);
    m_stream.Forget(acKey);
    m_motions.Forget(acKey);
    m_rests.erase(acKey);
    m_held.erase(acKey);
    m_last.erase(acKey);
}

bool GuestBodies::HostsOnly(const BodyKey& acKey) const noexcept
{
    return m_world.ctx().at<IBodyBridge&>().IsTrap(acKey);
}

bool GuestBodies::Elsewhere(const BodyKey& acKey) const noexcept
{
    const auto it = m_newest.find(acKey);
    if (acKey.Actor || it == m_newest.end() || m_authority.Is(acKey, kMe))
        return false;
    const auto cRead = m_world.ctx().at<IBodyBridge&>().Read(acKey);
    return cRead && glm::distance(cRead->Position, it->second) > BodyTolerances::kPlaceFar;
}

void GuestBodies::TakeHands(uint64_t aTick, std::map<BodyKey, BodyFrame>& aHeld, std::vector<BodyKey>& aTaken) noexcept
{
    const auto cTake = [&](const BodyKey& acKey, const char* acpHow) {
        const bool cWasMine = m_authority.Is(acKey, kMe) && !m_authority.Of(acKey).AtRest;
        if (Elsewhere(acKey) || HostsOnly(acKey))
            return;
        if (m_authority.Touch(acKey, kMe, aTick) && !cWasMine)
            Took(acKey, acpHow, aTaken);
    };
    // Its player's drop falls as this game's physics drops it.
    for (const BodyKey& cKey : m_engine.TakeAdoptedDrops())
        cTake(cKey, "dropped");
    for (const BodyKey& cKey : m_engine.TakePushes())
        cTake(cKey, "pushes");
    for (const BodyKey& cKey : m_engine.TakeHits())
        cTake(cKey, "hits");

    for (const BodyFrame& cFrame : m_engine.ReadHeld(CopySessions()))
        aHeld[cFrame.Key] = cFrame;
    for (auto it = m_held.begin(); it != m_held.end();)
    {
        if (aHeld.contains(*it))
        {
            ++it;
            continue;
        }
        spdlog::info("GuestBodies: this game's player let go of the host's {}", WorldStateLog::DescribeBody(*it));
        m_authority.Release(*it, kMe);
        it = m_held.erase(it);
    }
    for (const auto& [cKey, cFrame] : aHeld)
    {
        const bool cWasMine = m_authority.Is(cKey, kMe) && !m_authority.Of(cKey).AtRest;
        if (m_held.contains(cKey) || HostsOnly(cKey) || !m_authority.Grab(cKey, kMe, aTick))
            continue;
        m_held.insert(cKey);
        if (!cWasMine)
            Took(cKey, "holds", aTaken);
    }
}

std::vector<BodyKey> GuestBodies::Update(uint64_t aTick) noexcept
{
    auto& bridge = m_world.ctx().at<IBodyBridge&>();
    std::vector<BodyKey> taken;
    std::map<BodyKey, BodyFrame> held;
    auto own = m_engine.TakeOwnBodies(aTick, CopySessions());
    TakeHands(aTick, held, taken);

    // What its bodies touch, recursively (Havok's collision entries: 64648).
    std::vector<BodyKey> mine;
    for (const BodyKey& cKey : m_authority.Bodies(kMe))
        if (!cKey.Actor)
            mine.push_back(cKey);
    const auto cContacts = mine.empty() ? std::vector<std::pair<BodyKey, BodyKey>>{} : bridge.ReadTouching(mine);
    std::vector<std::pair<BodyKey, std::optional<BodyPose>>> paired;
    for (const auto& [cMine, cOther] : cContacts)
        if (const auto cRead = bridge.Read(cOther))
            paired.emplace_back(cOther, BodyPose{.Key = cOther, .Position = cRead->Position, .Rotation = cRead->Rotation});
    m_paired.Update(paired, [this](const BodyKey& acKey) -> std::optional<BodyPose> {
        const auto it = m_hostRests.find(acKey);
        return it != m_hostRests.end() ? std::optional(it->second) : std::nullopt;
    });
    for (const BodyKey& cKey : m_authority.Spread(kMe, cContacts, aTick, [this](const BodyKey& acKey) { return m_paired.Moved(acKey) && !Elsewhere(acKey) && !HostsOnly(acKey); }))
        Took(cKey, "knocks", taken);

    // Its bodies not awake here are still; a pile rests as one.
    std::set<BodyKey> awake;
    for (const BodyFrame& cFrame : own.Objects)
    {
        awake.insert(cFrame.Key);
        m_last[cFrame.Key] = {.Key = cFrame.Key, .Position = cFrame.Position, .Rotation = cFrame.Rotation};
    }
    for (const BodyPose& cRested : own.ObjectsRested)
        m_last[cRested.Key] = cRested;
    // Its player's own ragdoll rests by its record part (Ragdoll), not here.
    const uint32_t cPlayer = PlayerId();
    std::set<BodyKey> still;
    for (const BodyKey& cKey : m_authority.Bodies(kMe))
        if ((!cKey.Actor || cKey.Actor != cPlayer) && !awake.contains(cKey) && !held.contains(cKey))
            still.insert(cKey);
    for (const BodyKey& cKey : m_authority.Rest(kMe, still, cContacts, aTick))
    {
        const auto cRead = cKey.Actor ? std::nullopt : bridge.Read(cKey);
        const auto cLast = m_last.find(cKey);
        if (!cRead && cLast == m_last.end())
            continue;
        BodyPose rest = cRead ? BodyPose{.Key = cKey, .Position = cRead->Position, .Rotation = cRead->Rotation} : cLast->second;
        m_rests[cKey] = rest;
        bridge.SetMoving(cKey, false);
        spdlog::info("GuestBodies: the host's {} rests where this game's player put it, at ({:.0f}, {:.0f}, {:.0f})", WorldStateLog::DescribeBody(cKey),
                     rest.Position.x, rest.Position.y, rest.Position.z);
    }

    // Its states: held, moving (free fall marked), and its rests until confirmed.
    std::vector<BodyFrame> moving;
    for (const BodyFrame& cFrame : own.Objects)
        if (m_authority.Is(cFrame.Key, kMe) && !held.contains(cFrame.Key) && !m_authority.Of(cFrame.Key).AtRest)
            moving.push_back(cFrame);
    // Its bodies lying still (a capsule leans on one, its pile waits): read where they are, so the stream keeps the
    // host hearing from this game (BodyTolerances::kSilentMs).
    for (const BodyKey& cKey : m_authority.Bodies(kMe))
    {
        const auto cClaim = m_authority.Of(cKey);
        if (cClaim.AtRest || awake.contains(cKey) || held.contains(cKey) || (cKey.Actor && cKey.Actor == cPlayer))
            continue;
        if (const auto cRead = cKey.Actor ? std::nullopt : bridge.Read(cKey))
            moving.push_back({.Key = cKey, .Position = cRead->Position, .Rotation = cRead->Rotation, .LinearVelocity = cRead->LinearVelocity,
                              .AngularVelocity = cRead->AngularVelocity});
        else if (const auto cLast = m_last.find(cKey); cLast != m_last.end())
            moving.push_back({.Key = cKey, .Position = cLast->second.Position, .Rotation = cLast->second.Rotation});
    }
    m_motions.Mark(moving, aTick);
    std::vector<BodyState> states;
    for (const BodyFrame& cFrame : moving)
        states.push_back(m_authority.Stamp(cFrame, kMe));
    for (const auto& [cKey, cFrame] : held)
        if (m_authority.Is(cKey, kMe))
            states.push_back(m_authority.Stamp(cFrame, kMe));
    for (const auto& [cKey, cRest] : m_rests)
        states.push_back(m_authority.Stamp({.Key = cKey, .Position = cRest.Position, .Rotation = cRest.Rotation}, kMe));
    Ragdoll(aTick, own, states);

    BodyStatesRequest request;
    request.Tick = aTick;
    request.States = m_stream.Due(states, aTick);
    // What this game's physics moved alone that the host never sent: the host is told, its state comes back.
    for (const BodyPose& cMoved : own.Moved)
        if (!m_authority.Is(cMoved.Key, kMe) && !m_heard.contains(cMoved.Key) && m_hinted.try_emplace(cMoved.Key, 0, cMoved).second)
            spdlog::info("GuestBodies: this game moved the host's {} by itself", WorldStateLog::DescribeBody(cMoved.Key));
    // Unreliable: again until the host's state comes.
    for (auto& [cKey, hint] : m_hinted)
        if (!hint.first || aTick >= hint.first + kHintMs)
        {
            hint.first = aTick;
            request.States.push_back({.Frame = {.Key = cKey, .Position = hint.second.Position, .Rotation = hint.second.Rotation}, .AtRest = true});
        }
    for (size_t first = 0; first < request.States.size(); first += kStatesPerMessage)
    {
        BodyStatesRequest part;
        part.Tick = aTick;
        part.States.assign(request.States.begin() + first, request.States.begin() + std::min(request.States.size(), first + kStatesPerMessage));
        m_world.GetTransport().Send(part);
    }
    return taken;
}

bool GuestBodies::Hears(const BodyState& acState) noexcept
{
    const BodyKey& cKey = acState.Frame.Key;
    m_heard.insert(cKey);
    m_newest[cKey] = acState.Frame.Position;
    if (acState.AtRest)
        m_hostRests[cKey] = {.Key = cKey, .Position = acState.Frame.Position, .Rotation = acState.Frame.Rotation};
    m_hinted.erase(cKey);
    // Its own rest come back on the stream (repeats, refreshes): it lies there already.
    if (const auto it = m_confirmed.find(cKey); it != m_confirmed.end())
    {
        const auto cApart = BodyPrediction::Between(it->second.Frame, acState.Frame);
        if (acState.AtRest && acState.Author == BodyAuthor::kNone && acState.AuthSeq == it->second.AuthSeq &&
            cApart.Distance < BodyTolerances::kSameDistance && cApart.Angle < BodyTolerances::kStillAngle)
            return false;
        m_confirmed.erase(it);
    }
    const bool cWasMine = m_authority.Is(cKey, kMe);
    const auto cHeard = m_authority.Hear(acState, kMe);
    if (cWasMine && !m_authority.Is(cKey, kMe))
    {
        if (cHeard == BodyAuthority::Heard::kConfirmed)
        {
            // Its numbers can meet the host's taking it back (its rest lost): only its own pose confirms it.
            const auto cRest = m_rests.find(cKey);
            const bool cOwn = cRest != m_rests.end() &&
                              BodyPrediction::Between({.Position = cRest->second.Position, .Rotation = cRest->second.Rotation}, acState.Frame).Distance <
                                  BodyTolerances::kSameDistance;
            spdlog::info("GuestBodies: the host {} this game's rest of {}", cOwn ? "confirmed" : "took back", WorldStateLog::DescribeBody(cKey));
            Drop(cKey);
            if (!cOwn)
            {
                m_world.ctx().at<BodyService>().Reopen(cKey);
                return true;
            }
            m_confirmed[cKey] = acState;
        }
        else
        {
            spdlog::info("GuestBodies: the host's {} is another's: this game yields it", WorldStateLog::DescribeBody(cKey));
            Drop(cKey);
            m_world.ctx().at<BodyService>().Reopen(cKey);
        }
    }
    return cHeard == BodyAuthority::Heard::kApply;
}

void GuestBodies::Ragdoll(uint64_t aTick, GuestEngine::OwnBodies& aOwn, std::vector<BodyState>& aStates) noexcept
{
    if (aOwn.Ragdoll.empty() && aOwn.RagdollRested.empty())
        return;
    const uint32_t cPlayer = PlayerId();
    if (!cPlayer)
        return;
    for (BodyFrame& frame : aOwn.Ragdoll)
    {
        frame.Key.Actor = cPlayer;
        m_authority.Touch(frame.Key, kMe, aTick);
    }
    m_motions.Mark(aOwn.Ragdoll, aTick);
    if (!aOwn.Ragdoll.empty() && m_ragdollFrames++ == 0)
        spdlog::info("GuestBodies: this game's player's ragdoll moves ({} bodies awake)", aOwn.Ragdoll.size());
    for (const BodyFrame& cFrame : aOwn.Ragdoll)
        aStates.push_back(m_authority.Stamp(cFrame, kMe));
    for (BodyPose& rest : aOwn.RagdollRested)
    {
        rest.Key.Actor = cPlayer;
        m_authority.Forget(rest.Key);
        m_stream.Forget(rest.Key);
        m_motions.Forget(rest.Key);
    }
    if (!aOwn.RagdollRested.empty())
        spdlog::info("GuestBodies: this game's player's ragdoll rests ({} bodies, {} updates moving)", aOwn.RagdollRested.size(),
                     std::exchange(m_ragdollFrames, 0));
    // Its rest is its record part: every other game, and every later spawn, gets it.
    m_world.ctx().at<BodyService>().DeclareRagdolls(aTick, aOwn.RagdollRested, aOwn.Ragdoll);
}

GuestEngine::SessionOf_t GuestBodies::CopySessions() const noexcept
{
    return [this](uint32_t aFormId) -> uint32_t {
        for (const auto cEntity : m_world.view<RemoteComponent, FormIdComponent>())
            if (m_world.get<FormIdComponent>(cEntity).Id == aFormId)
                return m_world.get<RemoteComponent>(cEntity).Id;
        return 0;
    };
}

uint32_t GuestBodies::PlayerId() const noexcept
{
    for (const auto cEntity : m_world.view<LocalComponent, FormIdComponent>())
        if (m_world.get<FormIdComponent>(cEntity).Id == 0x14)
            return m_world.get<LocalComponent>(cEntity).Id;
    return 0;
}
