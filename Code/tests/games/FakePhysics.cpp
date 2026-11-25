#include <Events/MenuEvent.h>
#include <Games/CheckedPlacement.h>
#include <Events/PlayerDeathEvent.h>
#include "FakeEngine.h"
#include <Physics/BodyPrediction.h>
#include <Systems/CopyStep.h>
#include "FakeDialogue.h"
#include <Events/PlayerTravelQueuedEvent.h>
#include <Events/StoryEvents.h>

#include <Events/ContainerChangeEvent.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/ActivateEvent.h>
#include <Events/AddTargetEvent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/EffectVisualsChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/HealthChangeEvent.h>
#include <Events/ActorValueChangeEvent.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Games/DeathActions.h>
#include <Events/HitEvent.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Events/SpellCastEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/LooseItemEvents.h>
#include <Events/CallEndedEvent.h>
#include <Events/RoutedCallEvent.h>
#include <Events/PlayerProgressEvent.h>
#include <Games/EngineNames.h>
#include <Events/RemoveWaypointEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/TimeScaleEvent.h>
#include <Structs/GridCellCoords.h>
#include <Papyrus/PartyNatives.h>
#include <Papyrus/SystemQuests.h>
#include <Events/SystemQuestLearnedEvent.h>
#include <ScriptValues.h>
#include <array>
#include <ranges>
#include <cstdio>
#include <Games/Overrides.h>
#include <Events/GameLoadedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/PlayerFormChangeEvent.h>
#include <Services/SaveService.h>
#include <Wire/Buffer.h>
#include <World.h>
#include <FormIds.h>

using namespace PartyRules;

float FakeEngine::HeadingOf(const glm::quat& acRotation) noexcept
{
    const glm::vec3 cForward = acRotation * glm::vec3(0.f, 1.f, 0.f);
    const float cHeading = std::atan2(cForward.x, cForward.y);
    return cHeading < 0.f ? cHeading + 6.28318531f : cHeading;
}

glm::vec3 FakeEngine::StandingPose(uint32_t aFormId, uint8_t aIndex) const
{
    const auto& cActor = Actors.at(aFormId);
    return {cActor.Position[0], cActor.Position[1], cActor.Position[2] + 40.f + 20.f * aIndex};
}

uint8_t FakeEngine::KnockStateOf(uint32_t aFormId) const
{
    const auto& cActor = Actors.at(aFormId);
    return cActor.KnockedDown ? 1 : cActor.Rising ? 6 : 0;
}

void FakeEngine::StandUp(uint32_t aFormId)
{
    auto& actor = Actors.at(aFormId);
    actor.KnockedDown = false;
    for (auto& [cKey, body] : Bodies)
        if (body.RagdollOf == aFormId && !body.OutOfWorld)
        {
            actor.Rising = kRisingFrames;
            body.Awake = true;
            body.Held = false;
        }
}

void FakeEngine::PlaceByGame(const BodyKey& acKey, const glm::vec3& acPosition)
{
    Body& body = Bodies.at(acKey);
    body.Flight.Body.Position = body.NodePosition = acPosition;
    body.Flight.Body.LinearVelocity = body.Flight.Body.AngularVelocity = {};
    body.Flight.Resting = true;
    body.Awake = false;
    body.PlacedByGame = true;
    const float cPose[6]{acPosition.x, acPosition.y, acPosition.z, 0.f, 0.f, 0.f};
    ObjectChange change{.Type = ObjectChange::Kind::kMove, .Object = acKey.Object};
    change.Data.assign(reinterpret_cast<const char*>(cPose), sizeof(cPose));
    Recorded.push_back(std::move(change));
    Placed.emplace_back(acKey.Object, acPosition);
}

void FakeEngine::DriveCopyRagdoll(uint32_t aCopy, const std::vector<RagdollPending::Drive>& acDrives) noexcept
{
    constexpr float cStep = 1.f / 60.f;
    constexpr float cSettle = 0.05f;          // units an awake, unsteered body is moved by its joints each frame
    constexpr uint16_t cSettleFrames = 30;     // then Havok sleeps it
    const uint64_t cNow = m_frameCount * 16;
    std::set<BodyKey> driven;
    struct Resting
    {
        BodyKey Key;
        Body* pBody;
        const BodyFrame* pRest;
    };
    std::vector<Resting> rests;
    std::vector<RagdollStart::Body> steered;
    std::vector<Body*> steeredBodies;
    for (const auto& cDrive : acDrives)
    {
        const auto it = Bodies.find(RagdollKey(cDrive.Frame.Key, aCopy));
        if (it == Bodies.end())
            continue;
        Body& body = it->second;
        body.RagdollOf = aCopy;
        body.Held = false; // its ragdoll driver keeps it simulated (motion 2/3)
        // A copy's: played (BodyBridge::Find), unless this game's player moves it now.
        if (Actors.contains(aCopy) && Actors.at(aCopy).Copy)
            Classes.try_emplace(it->first, BodyClass::kPlayed);
        if (cDrive.Rest)
        {
            rests.push_back({it->first, &body, &cDrive.Frame});
            continue;
        }
        steered.push_back({cDrive.Frame, body.Flight.Body.Position, body.Flight.Body.Rotation, cDrive.Place});
        steeredBodies.push_back(&body);
        driven.insert(it->first);
    }
    // Steered onto its owner's frames (78089/78090), which wakes it; a steer lands within the frame here. A ragdoll part
    // put there, part steered, its joints throw (duo 2026-11-23): a plan is for the whole ragdoll.
    const auto cPlan = steered.empty() ? RagdollStart::Plan{} : m_ragdollStarts[aCopy].Next(steered, cNow);
    for (size_t i = 0; i < steered.size(); ++i)
    {
        Body& body = *steeredBodies[i];
        const BodyFrame& cTarget = cPlan.Targets[i];
        if (!cPlan.Place)
            RagdollJump = std::max(RagdollJump, glm::distance(body.Flight.Body.Position, cTarget.Position));
        body.RestAt.reset();
        body.Flight.Body.Position = cTarget.Position;
        body.Flight.Body.Rotation = cTarget.Rotation;
        body.Flight.Body.LinearVelocity = cTarget.LinearVelocity;
        body.Awake = true;
        ++body.Drives;
    }
    if (!rests.empty() && steered.empty())
        m_ragdollStarts.erase(aCopy);
    if (!rests.empty())
    {
        auto& rest = m_copyRests[aCopy];
        bool fresh = false, allNear = true, anyFar = false, allStill = true, allAsleep = true, anyWoken = false;
        for (auto& [cKey, pBody, pRest] : rests)
        {
            fresh |= pBody->RestAt != pRest->Position || pBody->RestTurn != pRest->Rotation;
            pBody->RestAt = pRest->Position;
            pBody->RestTurn = pRest->Rotation;
            const bool cNear = glm::distance(pBody->Flight.Body.Position, pRest->Position) <= kRagdollNear;
            allNear &= cNear;
            anyFar |= glm::distance(pBody->Flight.Body.Position, pRest->Position) > BodyTolerances::kPlaceFar;
            allAsleep &= !pBody->Awake;
            allStill &= BodyPrediction::StillToSleep(true, glm::length(pBody->Flight.Body.LinearVelocity), 0.f);
            anyWoken |= pBody->Awake && !cNear;
        }
        if (fresh)
            rest = RestPlacer(cNow, false);
        switch (rest.Next(allNear, anyFar, allStill, allAsleep, anyWoken, cNow))
        {
        case RestPlacer::Step::kGlide:
        case RestPlacer::Step::kKeep:
            for (auto& [cKey, pBody, pRest] : rests)
            {
                driven.insert(cKey);
                const glm::vec3 cGap = pRest->Position - pBody->Flight.Body.Position;
                pBody->Flight.Body.LinearVelocity = cGap / BodyPrediction::kRestGlideStep;
                pBody->Flight.Body.Position += cGap * (cStep / BodyPrediction::kRestGlideStep);
                pBody->Flight.Body.Rotation = pRest->Rotation;
                pBody->Awake = true;
                ++pBody->Drives;
            }
            break;
        case RestPlacer::Step::kPlace:
            for (auto& [cKey, pBody, pRest] : rests)
            {
                driven.insert(cKey);
                pBody->Flight.Body.Position = pRest->Position;
                pBody->Flight.Body.Rotation = pRest->Rotation;
                pBody->Flight.Body.LinearVelocity = {};
                pBody->Awake = true;
                ++pBody->Drives;
            }
            break;
        case RestPlacer::Step::kSleep:
        {
            // 60850 sleeps the island (its joints make the ragdoll one); its own bodies' settling does not keep it awake
            // (CopyRagdollPhysics: duo 2026-11-21, they never stood still in 2 s in 1 of 3 runs after the place).
            const bool cAsleep = true;
            if (cAsleep)
                for (auto& [cKey, pBody, pRest] : rests)
                {
                    pBody->Awake = false;
                    pBody->Flight.Body.LinearVelocity = {};
                }
            rest.Slept(cAsleep, cNow);
            break;
        }
        case RestPlacer::Step::kHold:
        case RestPlacer::Step::kLeave: break;
        }
    }
    else
        m_copyRests.erase(aCopy);
    const bool cCopy = Actors.contains(aCopy) && Actors.at(aCopy).Copy;
    for (auto& [cKey, body] : Bodies)
    {
        if (!cCopy || body.RagdollOf != aCopy || !body.Awake || driven.contains(cKey))
        {
            body.SettleFrames = 0;
            continue;
        }
        if (++body.SettleFrames > cSettleFrames)
        {
            body.Awake = false;
            body.Flight.Body.LinearVelocity = {};
            continue;
        }
        body.Flight.Body.Position.x += cSettle;
        body.Flight.Body.LinearVelocity = {cSettle / cStep, 0.f, 0.f};
    }
}

void FakeEngine::Knock(const BodyKey& acKey, const glm::vec3& acVelocity, const glm::vec3& acSpin)
{
    Body& body = Bodies.at(acKey);
    body.OutOfWorld = false;
    // Its 3D where it was (it rested there): the engine moves it from the body as it flies.
    body.NodePosition = body.Flight.Body.Position;
    body.NodeRotation = body.Flight.Body.Rotation;
    if (body.RagdollOf && Actors.contains(body.RagdollOf) && !IsActorDead(body.RagdollOf))
        Actors.at(body.RagdollOf).KnockedDown = true;
    body.Flight.Resting = false;
    body.Flight.Body.LinearVelocity = acVelocity;
    body.Flight.Body.AngularVelocity = acSpin;
    body.Awake = true;
    body.Tick = 0;
    // Woken still (Wake) no more: the knock moves it.
    body.StillFor = body.StillUntil = 0;
}

void FakeEngine::Swing(const BodyKey& acKey, const glm::vec3& acImpulse)
{
    Body& body = Bodies.at(acKey);
    // A keyframed body (played on the host's frames) has no mass: 38628 scales its impulse to 0, nothing moves.
    if (body.Held)
        return;
    PlayerHits.push_back({acKey, acImpulse, body.Flight.Body.Position});
    Knock(acKey, body.Flight.Body.LinearVelocity + acImpulse, body.Flight.Body.AngularVelocity);
}

void FakeEngine::Wake(const BodyKey& acKey, uint64_t aMs)
{
    Body& body = Bodies.at(acKey);
    // Its 3D where it rests, as Skyrim keeps a sleeping body's.
    body.NodePosition = body.Flight.Body.Position;
    body.NodeRotation = body.Flight.Body.Rotation;
    body.Flight.Resting = false;
    body.Awake = true;
    body.StillFor = aMs;
    body.StillUntil = 0;
}

std::optional<BodyPose> FakeEngine::ReadPose(const BodyKey& acKey) const noexcept
{
    const auto it = acKey.Actor ? Bodies.end() : Bodies.find(acKey);
    if (it == Bodies.end())
        return std::nullopt;
    return BodyPose{.Key = acKey, .Position = it->second.Flight.Body.Position, .Rotation = it->second.Flight.Body.Rotation};
}

std::optional<BodyRead> FakeEngine::Read(const BodyKey& acKey) noexcept
{
    const auto it = acKey.Actor ? Bodies.end() : Bodies.find(acKey);
    if (it == Bodies.end())
        return std::nullopt;
    const Body& cBody = it->second;
    return BodyRead{.Position = cBody.Flight.Body.Position, .Rotation = cBody.Flight.Body.Rotation, .LinearVelocity = cBody.Flight.Body.LinearVelocity,
                    .AngularVelocity = cBody.Flight.Body.AngularVelocity, .Dynamic = !cBody.Held && !cBody.ScriptKeyframed, .Active = cBody.Awake};
}

std::vector<std::pair<BodyKey, BodyKey>> FakeEngine::ReadTouching(const std::vector<BodyKey>& acKeys) noexcept
{
    std::vector<std::pair<BodyKey, BodyKey>> pairs;
    for (const BodyKey& cKey : acKeys)
        for (const auto& cIsland : Islands)
            if (cIsland.contains(cKey))
                for (const BodyKey& cOther : cIsland)
                    if (cOther != cKey && Bodies.contains(cOther) && !Bodies.at(cOther).Held)
                        pairs.emplace_back(cKey, cOther);
    return pairs;
}

bool FakeEngine::KnockDown(uint32_t aCopyFormId) noexcept
{
    const auto it = Actors.find(aCopyFormId);
    if (it == Actors.end() || it->second.Dead)
        return false;
    // Queued, as KnockExplosion on the task queue.
    if (!it->second.KnockedDown && !it->second.KnockIn)
        it->second.KnockIn = 2;
    return true;
}

BodyKey FakeEngine::RagdollKey(const BodyKey& acKey, uint32_t aActor) const noexcept
{
    // A copy's ragdoll here by its session id; this game's own actor's by its form id (BodyBridge finds both by actor).
    if (!acKey.Actor || Bodies.contains(acKey))
        return acKey;
    return {.Actor = aActor, .Index = acKey.Index};
}

bool FakeEngine::Drive(const BodyFrame& acFrame, uint32_t aCopyFormId, bool aHeld) noexcept
{
    const auto it = Bodies.find(RagdollKey(acFrame.Key, aCopyFormId));
    // An actor's body needs its copy here (BodyBridge::Find): none yet, or gone, is not loaded here.
    if (it == Bodies.end() || (acFrame.Key.Actor && !Actors.contains(aCopyFormId)))
        return false;
    if (!acFrame.Key.Actor)
        Classes[acFrame.Key] = BodyClass::kPlayed;
    // A living copy's ragdoll body is its animation's: knocked down with its owner's, else left until its own death
    // (BodyBridge::Find).
    if (acFrame.Key.Actor && !IsActorDead(aCopyFormId))
    {
        auto& copy = Actors.at(aCopyFormId);
        // While its bodies are simulated its ragdoll is its owner's; standing, a lying owner's frame knocks it down
        // (BodyBridge::Find).
        if (const uint8_t cKnock = KnockStateOf(aCopyFormId); !KnockReplay::BodiesSimulated(cKnock))
        {
            if (acFrame.Knocked && !copy.KnockIn && cKnock == 0)
                copy.KnockIn = 2;
            return true;
        }
    }
    if (it->second.NeedsKnock && !Actors.at(aCopyFormId).KnockedDown)
        return true;
    if (acFrame.Key.Actor)
    {
        m_pendingRagdolls.Put(aCopyFormId, acFrame, it->second.Drives == 0); // its first (BodyBridge::Find)
        return true;
    }
    Body& body = it->second;
    // This game just placed it itself: left where that put it until the host has read the place (BodyBridge::Drive).
    if (body.PlacedByGame)
        return true;
    // Held in another game: keyframed (massless); else simulated, steered onto the frame (BodyBridge::Drive).
    body.Held = aHeld;
    body.Rested.reset();
    body.Awake = !aHeld;
    body.Flight.Resting = aHeld;
    body.Flight.Body.Position = acFrame.Position;
    body.Flight.Body.Rotation = acFrame.Rotation;
    body.Flight.Body.LinearVelocity = aHeld ? glm::vec3{} : acFrame.LinearVelocity;
    body.Flight.Body.AngularVelocity = aHeld ? glm::vec3{} : acFrame.AngularVelocity;
    body.SceneWritesDuringJobs += !AtMainLoop;
    body.NodePosition = acFrame.Position;
    body.NodeRotation = acFrame.Rotation;
    ++body.Drives;
    return true;
}

glm::vec3 FakeEngine::OutsideCapsules(const glm::vec3& acFrom, const glm::vec3& acTo) const noexcept
{
    glm::vec3 at = acTo;
    for (const auto& [cFormId, cActor] : Actors)
    {
        if ((cActor.Copy && !cActor.Stepped) || cActor.Dead || cActor.Disabled || !cActor.Loaded)
            continue;
        const glm::vec3 cFeet(cActor.Position[0], cActor.Position[1], cActor.Position[2]);
        const glm::vec2 cOff(at.x - cFeet.x, at.y - cFeet.y);
        if (glm::length(cOff) >= kCapsuleRadius || at.z < cFeet.z || at.z >= cFeet.z + kCapsuleTop)
            continue;
        // From above, it rests on top; else out to the side it came from.
        if (acFrom.z >= cFeet.z + kCapsuleTop)
            at.z = cFeet.z + kCapsuleTop;
        else
        {
            glm::vec2 side(acFrom.x - cFeet.x, acFrom.y - cFeet.y);
            if (glm::length(side) < 1e-3f)
                side = glm::length(cOff) > 1e-3f ? cOff : glm::vec2(1.f, 0.f);
            side = glm::normalize(side) * kCapsuleRadius;
            at.x = cFeet.x + side.x;
            at.y = cFeet.y + side.y;
        }
    }
    return at;
}

bool FakeEngine::TryGrab(const BodyKey& acKey) noexcept
{
    const auto it = Bodies.find(acKey);
    if (it == Bodies.end() || it->second.Held)
        return false;
    Grabbed = acKey;
    return true;
}

void FakeEngine::HoldTo(const BodyKey& acKey, const glm::vec3& acTo) noexcept
{
    auto& body = Bodies.at(acKey).Flight.Body;
    body.Position = OutsideCapsules(body.Position, acTo);
}

bool FakeEngine::RestRagdoll(const BodyPose& acRest, uint32_t aCopyFormId) noexcept
{
    const auto it = Bodies.find(RagdollKey(acRest.Key, aCopyFormId));
    if (it == Bodies.end() || !Actors.contains(aCopyFormId))
        return false;
    ++it->second.RestAttempts;
    if (it->second.RestBusy)
        return false;
    if (IsActorDead(aCopyFormId) || KnockReplay::BodiesSimulated(KnockStateOf(aCopyFormId)))
    {
        ++it->second.Rests;
        m_pendingRagdolls.Put(aCopyFormId, {.Key = acRest.Key, .Position = acRest.Position, .Rotation = acRest.Rotation, .Knocked = acRest.Knocked},
                              true, true);
    }
    return true;
}

// As BodyBridge's rest steps: a held one (keyframed for a hold) gets its own motion back first; another game's rest
// being put wakes it for a step (Placing), and a put body has no contact points until a physics step (Contacts).
bool FakeEngine::Glide(const BodyPose& acRest) noexcept
{
    const auto it = acRest.Key.Actor ? Bodies.end() : Bodies.find(acRest.Key);
    if (it == Bodies.end())
        return false;
    Body& body = it->second;
    ++body.RestAttempts;
    if (body.RestBusy)
        return false;
    Classes.erase(acRest.Key);
    body.Held = false;
    body.Placing = true;
    body.Rested.reset();
    body.Awake = body.RestStep = true;
    body.Flight.Resting = false;
    // Steered a sixth of the way each 16 ms frame (kRestGlideStep).
    body.Flight.Body.Position += (acRest.Position - body.Flight.Body.Position) * (0.016f / BodyPrediction::kRestGlideStep);
    body.Flight.Body.Rotation = glm::slerp(body.Flight.Body.Rotation, acRest.Rotation, 0.016f / BodyPrediction::kRestGlideStep);
    body.Flight.Body.LinearVelocity = (acRest.Position - body.Flight.Body.Position) / BodyPrediction::kRestGlideStep;
    return true;
}

bool FakeEngine::Place(const BodyPose& acRest) noexcept
{
    const auto it = acRest.Key.Actor ? Bodies.end() : Bodies.find(acRest.Key);
    if (it == Bodies.end())
        return false;
    Body& body = it->second;
    ++body.RestAttempts;
    if (body.RestBusy)
        return false;
    Classes.erase(acRest.Key);
    body.Held = false;
    body.Placing = true;
    body.Contacts = false;
    body.Awake = body.RestStep = true;
    body.Flight.Resting = false;
    body.Flight.Body.Position = acRest.Position;
    body.Flight.Body.Rotation = acRest.Rotation;
    body.Flight.Body.LinearVelocity = body.Flight.Body.AngularVelocity = {};
    body.SceneWritesDuringJobs += !AtMainLoop;
    body.NodePosition = acRest.Position;
    body.NodeRotation = acRest.Rotation;
    return true;
}

bool FakeEngine::Sleep(const BodyKey& acKey) noexcept
{
    const auto it = acKey.Actor ? Bodies.end() : Bodies.find(acKey);
    if (it == Bodies.end())
        return false;
    Body& body = it->second;
    ++body.RestAttempts;
    if (body.RestBusy)
        return false;
    // 60850 sleeps the island: refused while another body in it moves, or this game moves, plays or holds it.
    for (const auto& cIsland : Islands)
        if (cIsland.contains(acKey))
            for (const BodyKey& cOther : cIsland)
            {
                const auto cBody = Bodies.find(cOther);
                if (cOther == acKey || cBody == Bodies.end())
                    continue;
                const auto cClass = Classes.find(cOther);
                if ((cBody->second.Awake && !cBody->second.Flight.Resting) || cBody->second.Held || Grabbed == cOther ||
                    (cClass != Classes.end() && cClass->second != BodyClass::kNone))
                    return false;
            }
    body.Placing = false;
    if (body.OnlyAsleep)
    {
        // Asleep as far as 60850 goes; Havok's own last test wakes it again, and it falls.
        body.Flight.Resting = false;
        body.Awake = true;
        ++body.Rests;
        return true;
    }
    body.Awake = false;
    body.Flight.Resting = true;
    body.Flight.Body.LinearVelocity = body.Flight.Body.AngularVelocity = {};
    body.Rested = BodyPose{.Key = acKey, .Position = body.Flight.Body.Position, .Rotation = body.Flight.Body.Rotation};
    body.RestStep = false;
    ++body.Rests;
    return true;
}

bool FakeEngine::HoldAt(const BodyPose& acRest) noexcept
{
    const auto it = acRest.Key.Actor ? Bodies.end() : Bodies.find(acRest.Key);
    if (it == Bodies.end())
        return false;
    Body& body = it->second;
    ++body.RestAttempts;
    if (body.RestBusy)
        return false;
    Classes.erase(acRest.Key);
    // Keyframed there, then simulated again asleep (SettleFound): no contact points until a step.
    body.Contacts = false;
    body.Placing = false;
    body.Held = false;
    body.Awake = false;
    body.Flight.Resting = true;
    body.Flight.Body.Position = acRest.Position;
    body.Flight.Body.Rotation = acRest.Rotation;
    body.Flight.Body.LinearVelocity = body.Flight.Body.AngularVelocity = {};
    body.SceneWritesDuringJobs += !AtMainLoop;
    body.NodePosition = acRest.Position;
    body.NodeRotation = acRest.Rotation;
    // Its island's sleep is refused while another body in it moves (SettleFound's DeactivateIsland): awake, it falls.
    for (const auto& cIsland : Islands)
        if (cIsland.contains(acRest.Key))
            for (const BodyKey& cOther : cIsland)
                if (const auto cBody = Bodies.find(cOther); cOther != acRest.Key && cBody != Bodies.end() && cBody->second.Awake && !cBody->second.Flight.Resting)
                {
                    body.Awake = true;
                    body.Flight.Resting = false;
                }
    body.Rested = acRest;
    body.RestStep = false;
    ++body.Rests;
    return true;
}

bool FakeEngine::Release(const BodyKey& acKey) noexcept
{
    const auto it = acKey.Actor ? Bodies.end() : Bodies.find(acKey);
    if (it == Bodies.end())
        return false;
    it->second.Held = false;
    it->second.Placing = false;
    return true;
}

bool FakeEngine::Free(const BodyFrame& acFrame) noexcept
{
    const auto it = Bodies.find(acFrame.Key);
    if (it == Bodies.end())
        return false;
    Body& body = it->second;
    body.Held = false;
    body.Flight.Body = acFrame;
    body.Flight.Body.Key = {};
    Knock(acFrame.Key, acFrame.LinearVelocity, acFrame.AngularVelocity);
    return true;
}

namespace
{
constexpr uint64_t kStepMs = 16; // Havok's substeps (fTimePerSubStep)

// Its physics up to aTick: its flight steps (unless a holder's frames move it), and the engine writes its node from it
// unless it leaves the node behind. aByFrame: the whole frame in equal steps of at most kStepMs, as HavokStepFix
// steps Skyrim (physics.md, "Havok's step"); else fixed kStepMs substeps.
void StepBody(FakeEngine::Body& aBody, uint64_t aTick, bool aByFrame = false)
{
    if (!aBody.Tick)
        aBody.Tick = aTick;
    // Woken still (Wake): unmoved until Havok sleeps it.
    if (aBody.StillFor)
    {
        if (!aBody.StillUntil)
            aBody.StillUntil = aTick + aBody.StillFor;
        if (aTick < aBody.StillUntil)
            return;
        aBody.StillFor = aBody.StillUntil = 0;
        aBody.Flight.Resting = true;
    }
    if (aByFrame && aTick > aBody.Tick)
    {
        const uint64_t cElapsed = aTick - aBody.Tick;
        const uint64_t cSteps = (cElapsed + kStepMs) / (kStepMs + 1);
        for (uint64_t i = 0; i < std::max<uint64_t>(cSteps, 1) && !aBody.Flight.Resting; ++i)
            aBody.Flight.Step(static_cast<float>(cElapsed) / static_cast<float>(std::max<uint64_t>(cSteps, 1)) / 1000.f);
        aBody.Tick = aTick;
    }
    for (; !aByFrame && aBody.Tick + kStepMs <= aTick && !aBody.Flight.Resting; aBody.Tick += kStepMs)
        aBody.Flight.Step(kStepMs / 1000.f);
    if (!aBody.NodeLeftBehind)
    {
        aBody.NodePosition = aBody.Flight.Body.Position;
        aBody.NodeRotation = aBody.Flight.Body.Rotation;
    }
}
} // namespace

uint32_t FakeEngine::ReferenceOf(const BodyKey& acKey) noexcept
{
    return acKey.Actor ? acKey.Actor : ::FormIdOf(acKey.Object);
}

std::vector<uint32_t> FakeEngine::TakeMovedReferences() noexcept
{
    // A stepped copy's step sets its body's velocity, which wakes what shares its island (62456); a still one's is none
    // (CopyStep::Translation), as a still NPC's (79201).
    for (const auto& [cFormId, cActor] : Actors)
    {
        if (!cActor.Copy || !cActor.Stepped || cActor.Dead || cActor.Disabled || !cActor.Loaded)
            continue;
        const glm::vec3 cFeet(cActor.Position[0], cActor.Position[1], cActor.Position[2]);
        const glm::vec3 cGoal(cActor.Goal[0], cActor.Goal[1], cActor.Goal[2]);
        if (CopyStep::Translation(cGoal - cFeet, cActor.Rotation[2]) == glm::vec3(0.f))
            continue;
        for (auto& [cKey, body] : Bodies)
        {
            const glm::vec3& cAt = body.Flight.Body.Position;
            if (cKey.Actor || body.Held || glm::length(glm::vec2(cAt.x - cFeet.x, cAt.y - cFeet.y)) >= kIslandReach ||
                cAt.z < cFeet.z - kIslandReach || cAt.z >= cFeet.z + kCapsuleTop)
                continue;
            if (body.Flight.Resting)
                Wake(cKey, kStepMs);
        }
    }
    // Reported as its island wakes (the change funnel, 25857): in an island something keeps awake (KeptAwake), a later
    // nudge reports nothing (WarehouseTraps' rocks beside their ropes, 2026-11-24).
    std::vector<uint32_t> moved;
    for (auto& [cKey, body] : Bodies)
    {
        const bool cWoke = body.Awake && !body.Held && !(body.KeptAwake && body.ReportedAwake);
        body.ReportedAwake = body.Awake && !body.Held;
        if (cWoke && std::ranges::find(moved, ReferenceOf(cKey)) == moved.end())
            moved.push_back(ReferenceOf(cKey));
    }
    return moved;
}

void FakeEngine::StepDeathRagdolls()
{
    for (auto& [cId, actor] : Actors)
        if (actor.RagdollListed && !actor.RagdollInWorld)
            actor.RagdollInWorld = true; // 63562: every body not in the world added back
    for (const uint32_t cId : std::exchange(QueuedRagdollEnds, {}))
        if (Actors.contains(cId))
            Actors.at(cId).RagdollInWorld = false;
    for (auto& [cId, actor] : Actors)
    {
        if (cId == kPlayer && actor.Dead && !actor.RagdollForDeath)
        {
            actor.RagdollForDeath = actor.RagdollInWorld = true;
            actor.RagdollAt = {actor.Position[0], actor.Position[1], actor.Position[2]};
        }
        if (!actor.Dead)
            actor.RagdollForDeath = false;
        actor.RagdollListed = actor.RagdollInWorld;
    }
}

float FakeEngine::PelvisAboveFeet(uint32_t aFormId) const
{
    constexpr float cStanding = 71.f;
    const auto& cActor = Actors.at(aFormId);
    return cActor.RagdollInWorld && !cActor.Dead ? cActor.RagdollAt.z + cStanding - cActor.Position[2] : cStanding;
}

uint32_t FakeEngine::PlayerInRagdoll() noexcept
{
    const auto it = Actors.find(kPlayer);
    return it != Actors.end() && (IsActorDead(kPlayer) || it->second.KnockedDown) ? kPlayer : 0;
}

uint32_t FakeEngine::PlayerHolds() noexcept
{
    return Grabbed ? ReferenceOf(*Grabbed) : 0;
}

std::optional<IBodyReads::Reference> FakeEngine::ReadReference(uint32_t aFormId, bool aBodies) noexcept
{
    const auto cActor = Actors.find(aFormId);
    IBodyReads::Reference reference{.Actor = cActor != Actors.end(),
                        .Dead = cActor != Actors.end() && IsActorDead(aFormId),
                        .Up = cActor != Actors.end() && !IsActorDead(aFormId) && !KnockReplay::BodiesSimulated(KnockStateOf(aFormId)),
                        .Rising = cActor != Actors.end() && cActor->second.Rising != 0,
                        .Player = aFormId == kPlayer};
    const bool cRising = reference.Rising;
    bool any = reference.Actor;
    for (auto& [cKey, body] : Bodies)
    {
        if (ReferenceOf(cKey) != aFormId)
            continue;
        any = true;
        if (!reference.Actor && cKey.Index == 0)
            reference.Position = body.NodePosition;
        if (!aBodies || body.OutOfWorld)
            continue;
        // At an owner's rest awake for its step: read awake once, where this game's world pushed it.
        const bool cRestStep = std::exchange(body.RestStep, false);
        body.Contacts |= cRestStep;
        // This game's player's hold keeps it in its hands (the grab's spring), not falling. Getting up, its frame moved it.
        if (!cRestStep && body.Awake && !body.Held && Grabbed != cKey && !cRising)
            StepBody(body, PhysicsTick, FrameMs > 0.0);
        // Skyrim keeps a ragdolled actor on its ragdoll's root body, turned with it (Actor's BSTransformDeltaEvent sink,
        // 37996).
        if (body.RagdollOf && cKey.Index == 0 && cActor != Actors.end() && body.Awake && !cRising)
        {
            std::copy_n(&body.Flight.Body.Position.x, 3, cActor->second.Position);
            cActor->second.Rotation[2] = HeadingOf(body.Flight.Body.Rotation);
        }
        if (body.Flight.Resting && !cRising && !body.KeptAwake && !cRestStep)
            body.Awake = false;
        if (reference.Bodies.size() <= cKey.Index)
            reference.Bodies.resize(cKey.Index + 1);
        BodyRead read{.Position = body.Flight.Body.Position, .Rotation = body.Flight.Body.Rotation,
                      .LinearVelocity = body.Flight.Body.LinearVelocity, .AngularVelocity = body.Flight.Body.AngularVelocity,
                      .Dynamic = !body.Held && !body.ScriptKeyframed, .Active = body.Awake && (cRising || !body.Flight.Resting || body.KeptAwake || cRestStep)};
        reference.Bodies[cKey.Index] = IBodyReads::Body{.Read = read};
        if (!reference.Actor)
            reference.Bodies[cKey.Index]->Node = std::pair(body.NodePosition, body.NodeRotation);
    }
    if (!any)
        return std::nullopt;
    return reference;
}

void FakeEngine::WriteNode(uint32_t aFormId, uint8_t aIndex) noexcept
{
    for (auto& [cKey, body] : Bodies)
    {
        if (cKey.Actor || cKey.Index != aIndex || ReferenceOf(cKey) != aFormId)
            continue;
        ++body.NodeWrites;
        body.SceneWritesDuringJobs += !AtMainLoop;
        const auto cApart = BodyPrediction::Between({.Position = body.NodePosition, .Rotation = body.NodeRotation},
                                                    {.Position = body.Flight.Body.Position, .Rotation = body.Flight.Body.Rotation});
        if (cApart.Distance < 0.1f && cApart.Angle < 0.001f)
            ++body.Replaced;
        body.NodePosition = body.Flight.Body.Position;
        body.NodeRotation = body.Flight.Body.Rotation;
    }
}

HostBodyReads FakeEngine::TakeBodies(uint64_t aTick, const std::function<bool(const BodyKey&)>& acWhich)
{
    HostBodyReads bodies;
    for (auto& [cKey, body] : Bodies)
    {
        if (!body.Awake || (acWhich && !acWhich(cKey)))
            continue;
        const auto cOf = Actors.find(body.RagdollOf);
        // Getting up: its frame moved it (Frame), and it neither pins its actor nor rests.
        if (body.RagdollOf && cOf != Actors.end() && cOf->second.Rising)
        {
            BodyFrame frame = body.Flight.Body;
            frame.Key = cKey;
            bodies.Awake.push_back(frame);
            continue;
        }
        body.Contacts |= std::exchange(body.RestStep, false);
        StepBody(body, aTick, FrameMs > 0.0);
        BodyFrame frame = body.Flight.Body;
        frame.Key = cKey;
        // Skyrim keeps a ragdolled actor on its ragdoll's root body, turned with it (Actor's BSTransformDeltaEvent sink,
        // 37996).
        if (body.RagdollOf && cKey.Index == 0)
            if (const auto it = Actors.find(body.RagdollOf); it != Actors.end())
            {
                std::copy_n(&frame.Position.x, 3, it->second.Position);
                it->second.Rotation[2] = HeadingOf(frame.Rotation);
            }
        // A living actor's ragdoll simulated: knocked down (PhysicsBodies).
        frame.Knocked = body.RagdollOf && !IsActorDead(body.RagdollOf);
        if (body.Flight.Resting)
        {
            body.Awake = false;
            bodies.Rested.push_back({cKey, frame.Position, frame.Rotation, frame.Knocked});
        }
        else
            bodies.Awake.push_back(frame);
    }
    return bodies;
}

