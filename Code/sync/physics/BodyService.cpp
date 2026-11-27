#include <physics/BodyService.h>

#include <physics/IBodyBridge.h>
#include <physics/BodyPrediction.h>
#include <physics/BodyTolerances.h>
#include <core/FormIdComponent.h>
#include <core/LocalComponent.h>
#include <core/RemoteComponent.h>
#include <core/DisconnectedEvent.h>
#include <core/UpdateEvent.h>
#include <Messages/Relayed.h>
#include <core/CopyRecords.h>
#include <core/TransportService.h>
#include <Structs/RagdollPart.h>
#include <physics/BodyLog.h>
#include <movement/InterpolationSystem.h>
#include <core/World.h>
#include <core/Profile.h>

BodyService::BodyService(World& aWorld, entt::dispatcher& aDispatcher, IBodyBridge& aBridge) noexcept
    : m_world(aWorld)
    , m_bridge(aBridge)
{
    Listen<NotifyBodyStates, &BodyService::OnStates>(m_connections, aDispatcher, this);
    Listen<UpdateEvent, &BodyService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &BodyService::OnDisconnected>(m_connections, aDispatcher, this);
    // Its owner's ragdoll rest, also for a copy made after it (docs/design/one-simulator.md, piece 10).
    m_world.ctx().at<CopyRecords>().Register(RecordKind::kRagdoll, [this](uint32_t aId, const std::string& acBytes) {
        RagdollPart part;
        if (!CopyOf(aId) || !FromPartBytes(acBytes, part))
            return std::vector<uint32_t>{};
        // Its record's actor alone (the host checked who declared it): a body naming another is dropped.
        std::erase_if(part.Rests, [aId](const BodyPose& acRest) { return acRest.Key.Actor != aId || acRest.Key.Object != GameId{}; });
        if (part.Rests.empty())
            EndRagdoll(aId, UINT64_MAX); // its owner's ragdoll woke: the rest it had is gone
        else
            m_ragdolls.Rest(part.Tick, part.Rests);
        return std::vector<uint32_t>{};
    });
}

void BodyService::DeclareRagdolls(uint64_t aTick, std::vector<BodyPose>& aRests, const std::vector<BodyFrame>& acAwake) noexcept
{
    auto& records = m_world.ctx().at<CopyRecords>();
    // Awake again: the rest it had is gone (a spawn meanwhile lies it where its frames take it).
    for (const BodyFrame& cFrame : acAwake)
        if (cFrame.Key.Actor && m_declaredRests.erase(cFrame.Key.Actor))
            records.Declare(cFrame.Key.Actor, RecordKind::kRagdoll, ToPartBytes(RagdollPart{.Tick = aTick}));
    std::map<uint32_t, RagdollPart> parts;
    std::erase_if(aRests, [&parts, aTick](const BodyPose& acRest) {
        if (!acRest.Key.Actor)
            return false;
        auto& part = parts[acRest.Key.Actor];
        part.Tick = aTick;
        part.Rests.push_back(acRest);
        return true;
    });
    for (const auto& [cId, cPart] : parts)
    {
        records.Declare(cId, RecordKind::kRagdoll, ToPartBytes(cPart));
        m_declaredRests.insert(cId);
        // Its own physics has its rest now: a rest another game's mover put on it here is played no more.
        EndRagdoll(cId, UINT64_MAX);
    }
}

void BodyService::Forget(const BodyKey& acKey, uint64_t aUntil) noexcept
{
    if (aUntil)
        PlaybackOf(acKey).ForgetUntil(acKey, aUntil);
    else
        PlaybackOf(acKey).Forget(acKey);
    m_ragdollRests.erase(acKey);
    m_rests.erase(acKey);
    m_placed.erase(acKey);
    m_following.erase(acKey);
    Hold(acKey, false);
    m_bridge.Forget(acKey);
}

bool BodyService::Shows(const GameId& acObject) const noexcept
{
    const auto cOf = [&acObject](const auto& acEntry) { return !acEntry.first.Actor && acEntry.first.Object == acObject; };
    return std::ranges::any_of(m_following, [&acObject](const BodyKey& acKey) { return !acKey.Actor && acKey.Object == acObject; }) ||
           std::ranges::any_of(m_rests, cOf) || m_playback.RestComing(acObject);
}

void BodyService::Hold(const BodyKey& acKey, bool aHeld) noexcept
{
    if (aHeld && !m_held.contains(acKey))
    {
        if (const uint32_t cFormId = m_bridge.ReferenceFormId(acKey))
        {
            m_held[acKey] = cFormId;
            m_heldElsewhere.insert(cFormId);
        }
    }
    else if (!aHeld)
        if (const auto it = m_held.find(acKey); it != m_held.end())
        {
            m_heldElsewhere.erase(it->second);
            m_held.erase(it);
        }
}

void BodyService::EndRagdoll(uint32_t aSessionId, uint64_t aTick) noexcept
{
    std::erase_if(m_ragdollRests, [aSessionId, aTick](const auto& acRest) {
        return acRest.first.Actor == aSessionId && acRest.second.first <= aTick;
    });
}

void BodyService::KnockDown(uint32_t aCopyFormId) noexcept
{
    (void)m_bridge.KnockDown(aCopyFormId);
}

void BodyService::OnStates(const NotifyBodyStates& acMessage) noexcept
{
    std::vector<BodyFrame> frames, ragdolls;
    std::vector<BodyPose> rests, ragdollRests;
    for (const BodyState& cState : acMessage.States)
    {
        if (m_pKeeper && !m_pKeeper->Hears(cState))
            continue;
        const BodyKey& cKey = cState.Frame.Key;
        if (cState.AtRest)
        {
            if (!cKey.Actor)
                Hold(cKey, false);
            (cKey.Actor ? ragdollRests : rests).push_back({.Key = cKey, .Position = cState.Frame.Position, .Rotation = cState.Frame.Rotation,
                                                           .Knocked = cState.Frame.Knocked});
            if (cState.Placed)
                m_placed.insert(cKey);
            else
                m_placed.erase(cKey);
            continue;
        }
        if (!cKey.Actor)
        {
            Hold(cKey, cState.Held);
            if (const auto it = m_rests.find(cKey); it != m_rests.end() && acMessage.Tick > it->second.Tick)
                m_rests.erase(it); // moving again: a new rest will come
        }
        (cKey.Actor ? ragdolls : frames).push_back(cState.Frame);
    }
    m_playback.Add(acMessage.Tick, frames);
    m_ragdolls.Add(acMessage.Tick, ragdolls);
    m_playback.Rest(acMessage.Tick, rests);
    m_ragdolls.Rest(acMessage.Tick, ragdollRests);
    if (!ragdolls.empty())
        m_world.GetCopyClock().OnSnapshot(acMessage.Tick, m_world.GetTransport().GetTick());
}

void BodyService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_playback = {};
    m_ragdolls = {};
    m_declaredRests.clear();
    m_following.clear();
    m_held.clear();
    m_heldElsewhere.clear();
    m_ragdollRests.clear();
    m_rests.clear();
    m_placed.clear();
    m_missing.clear();
    m_bridge.ForgetAll();
}

entt::entity BodyService::CopyEntity(uint32_t aSessionId) const noexcept
{
    return m_world.GetActorIndex().BySession<RemoteComponent>(aSessionId);
}

uint32_t BodyService::CopyOf(uint32_t aSessionId) const noexcept
{
    const auto cEntity = CopyEntity(aSessionId);
    const auto* pFormId = cEntity != entt::null ? m_world.try_get<FormIdComponent>(cEntity) : nullptr;
    return pFormId ? pFormId->Id : 0;
}

uint32_t BodyService::OwnActorOf(uint32_t aSessionId) const noexcept
{
    const auto cEntity = m_world.GetActorIndex().BySession<LocalComponent, FormIdComponent>(aSessionId);
    return cEntity == entt::null ? 0 : m_world.get<FormIdComponent>(cEntity).Id;
}

uint32_t BodyService::ActorOf(uint32_t aSessionId) const noexcept
{
    const uint32_t cCopy = CopyOf(aSessionId);
    return cCopy ? cCopy : OwnActorOf(aSessionId);
}

void BodyService::OnUpdate(const UpdateEvent&) noexcept
{
    if (std::exchange(m_updateQueued, true))
        return;
    m_mainTasks.Queue([this] {
        m_updateQueued = false;
        Apply();
    });
}

void BodyService::Apply() noexcept
{
    PROFILE_ZONE("BodyService::Apply");
    std::vector<uint32_t> copies;
    for (const auto cEntity : m_world.view<RemoteComponent, FormIdComponent>())
        if (const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id)
            copies.push_back(cFormId);
    m_bridge.SetCopies(copies);
    // The frame clock, steady by the engine's frames: the session's time as read lags unevenly (S10, copy-clock.md).
    const uint64_t cTick = m_world.GetFrameTick();
    // What this game took is its own now: the playback of it stops, its older states too.
    if (m_pKeeper)
        for (const BodyKey& cKey : m_pKeeper->Update(cTick))
        {
            Forget(cKey, cTick);
            m_bridge.SetMoving(cKey, true); // this game's now: it meets what other games move
        }
    Show(m_playback, [cAt = PlaybackTick(cTick)](const BodyKey&) { return cAt; }, cTick);
    // A copy's ragdoll at its copy's time; this game's own corpse another game's player moves, at the references'.
    Show(m_ragdolls, [this, cAt = PlaybackTick(cTick)](const BodyKey& acKey) {
        const uint64_t cCopyTick = CopyTickOf(acKey.Actor);
        return cCopyTick ? cCopyTick : (OwnActorOf(acKey.Actor) ? cAt : 0);
    }, cTick);
    PlaceRests(cTick);
    for (auto it = m_ragdollRests.begin(); it != m_ragdollRests.end();)
    {
        const auto& cRest = it->second.second;
        // This game moves that corpse now: its own fall decides until it rests.
        if (m_pKeeper && m_pKeeper->Simulates(cRest.Key))
        {
            it = m_ragdollRests.erase(it);
            continue;
        }
        const uint32_t cCopy = ActorOf(cRest.Key.Actor);
        const BodyFrame cFrame{.Key = cRest.Key, .Position = cRest.Position, .Rotation = cRest.Rotation, .Knocked = cRest.Knocked};
        if (!cCopy || !m_bridge.RestRagdoll(cRest, cCopy) || !m_bridge.Drive(cFrame, cCopy))
            it = m_ragdollRests.erase(it);
        else
            ++it;
    }
}

uint64_t BodyService::PlaybackTick(uint64_t aNow) const noexcept
{
    // kDelayMs behind, or as far as the line's lateness needs (CopyClock) when that is more.
    const uint64_t cBehind = aNow > kDelayMs ? aNow - kDelayMs : 0;
    const uint64_t cClock = m_world.GetCopyClock().Now();
    return cClock ? std::min(cBehind, cClock) : cBehind;
}

uint64_t BodyService::CopyTickOf(uint32_t aSessionId) noexcept
{
    const auto cEntity = CopyEntity(aSessionId);
    return cEntity != entt::null ? InterpolationSystem::PlaybackTick(m_world, cEntity) : 0;
}

void BodyService::Show(BodyPlayback& aPlayback, const BodyPlayback::TickOf& acTickOf, uint64_t aNow) noexcept
{
    if (!aPlayback.Count())
        return;
    for (const auto& cPose : aPlayback.At(acTickOf))
    {
        const BodyKey& cKey = cPose.Body.Key;
        const BodyPose cAt{.Key = cKey, .Position = cPose.Body.Position, .Rotation = cPose.Body.Rotation, .Knocked = cPose.Body.Knocked};
        if (cPose.Rest)
        {
            m_following.erase(cKey);
            if (cKey.Actor)
            {
                if (!m_pKeeper || !m_pKeeper->Simulates(cKey))
                    m_ragdollRests[cKey] = {cPose.Tick, cAt};
                continue;
            }
            // The same rest again (the stream resends resting bodies): its placer goes on.
            Resting& resting = m_rests[cKey];
            const auto cApart = BodyPrediction::Between({.Position = resting.Pose.Position, .Rotation = resting.Pose.Rotation}, cPose.Body);
            if (resting.Pose.Key != cKey || cApart.Distance >= BodyTolerances::kSameDistance || cApart.Angle >= BodyTolerances::kStillAngle)
                resting = {.Pose = cAt, .Tick = cPose.Tick, .Placer = RestPlacer(aNow), .Placed = m_placed.contains(cKey), .Frames = cPose.Frames};
            continue;
        }
        if (cKey.Actor)
            m_ragdollRests.erase(cKey);
        const uint32_t cCopy = cKey.Actor ? ActorOf(cKey.Actor) : 0;
        // A held body: its holder's alone, so keyframed here (this game's grab refuses it), as its holder shows it.
        if (!m_bridge.Drive(cPose.Body, cCopy, m_held.contains(cKey)))
        {
            // Not loaded here (its cell, or its 3D): nothing to show; its cell's load gets the record's rest.
            aPlayback.Forget(cKey);
            if (m_missing.insert(cKey).second)
                spdlog::warn("BodyService: {} is not in this game", BodyLog::Describe(cKey));
            continue;
        }
        m_missing.erase(cKey);
        if (m_following.insert(cKey).second)
            spdlog::info("BodyService: follows the owner's {}", BodyLog::Describe(cKey));
    }
    for (const auto& cLost : aPlayback.TakeLost())
    {
        m_following.erase(cLost.Key);
        m_bridge.Forget(cLost.Key);
        spdlog::warn("BodyService: lost {}: no frame for {} ms and no rest, after {} frames", BodyLog::Describe(cLost.Key),
                     BodyPlayback::kLostMs, cLost.Frames);
    }
}

void BodyService::PlaceRests(uint64_t aNow) noexcept
{
    using Step = RestPlacer::Step;
    for (auto it = m_rests.begin(); it != m_rests.end();)
    {
        const BodyKey& cKey = it->first;
        Resting& resting = it->second;
        const auto cRead = m_bridge.Read(cKey);
        if (!cRead)
        {
            if (m_missing.insert(cKey).second)
                spdlog::warn("BodyService: {} is not in this game", BodyLog::Describe(cKey));
            // A guest's: put once its body is here (the stream's repeats are no newer than it).
            it = m_pKeeper ? std::next(it) : m_rests.erase(it);
            continue;
        }
        m_missing.erase(cKey);
        const auto cApart = BodyPrediction::Between({.Position = cRead->Position, .Rotation = cRead->Rotation},
                                                    {.Position = resting.Pose.Position, .Rotation = resting.Pose.Rotation});
        const bool cNear = cApart.Distance < BodyTolerances::kSameDistance && cApart.Angle < BodyTolerances::kStillAngle;
        const bool cFar = cApart.Distance > BodyTolerances::kPlaceFar;
        const bool cStill = BodyPrediction::StillToSleep(cRead->Dynamic, glm::length(cRead->LinearVelocity), glm::length(cRead->AngularVelocity));
        const bool cAsleep = !cRead->Active;
        // The host's game put it there (a rack's weapon, the console): held there asleep, as the engine left it.
        if (resting.Placed)
            resting.Placer.Hold(aNow);
        switch (resting.Placer.Next(cNear, cFar, cStill, cAsleep, cRead->Active && !cNear, aNow))
        {
        case Step::kGlide: m_bridge.Glide(resting.Pose); break;
        case Step::kPlace: m_bridge.Place(resting.Pose); break;
        case Step::kSleep: resting.Placer.Slept(m_bridge.Sleep(cKey), aNow); break;
        case Step::kHold:
            resting.Placer.Hold(aNow);
            m_bridge.HoldAt(resting.Pose);
            break;
        case Step::kKeep: m_bridge.Glide(resting.Pose); break;
        case Step::kLeave: break;
        }
        if (resting.Placer.Done && !std::exchange(resting.Logged, true))
            spdlog::info("BodyService: placed the owner's rest of {} at ({:.0f}, {:.0f}, {:.0f}) after {} frames", BodyLog::Describe(cKey),
                         resting.Pose.Position.x, resting.Pose.Position.y, resting.Pose.Position.z, resting.Frames);
        // Put: on the host's game (no keeper) the body is its own again, whatever moves it next; a guest's keeps the
        // rest to keep it there against its own physics' drift (RestPlacer::kKeep).
        if (resting.Placer.Done && !m_pKeeper)
        {
            it = m_rests.erase(it);
            continue;
        }
        ++it;
    }
}
