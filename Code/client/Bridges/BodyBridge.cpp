#include <Bridges/BodyBridge.h>

#include <Bridges/PlacedByGame.h>
#include <Bridges/ReferenceBodies.h>
#include <Physics/BodyGroups.h>
#include <Havok/bhkCollision.h>
#include <Havok/Ragdoll.h>
#include <AI/AIProcess.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <FormIds.h>
#include <Games/KnockReplay.h>
#include <Messages/WorldState.h>
#include <Physics/BodyPrediction.h>
#include <Games/Skyrim/PlayerHands.h>
#include <TESObjectREFR.h>

#include <RTTI.h>

#include <algorithm>
#include <cmath>

void BodyBridge::SetReferences(std::function<uint32_t(const GameId& acId)> aFormIdOf) noexcept
{
    m_formIdOf = std::move(aFormIdOf);
}

uint32_t BodyBridge::ReferenceFormId(const BodyKey& acKey) const noexcept
{
    return acKey.Actor || !m_formIdOf ? 0 : m_formIdOf(acKey.Object);
}

bool BodyBridge::KnockedDown(Actor& aCopy, bool aKnock) noexcept
{
    const uint8_t cState = aCopy.actorState.GetKnockState();
    if (const auto it = m_knocked.find(aCopy.formID); it != m_knocked.end())
    {
        // Down; or not yet, the knock queued (KnockExplosion may run on the task queue).
        if (cState != 0 || !it->second)
        {
            it->second |= cState != 0;
            return true;
        }
        m_knocked.erase(it);
        return false;
    }
    // Getting up without its owner's knock: its own, left alone.
    if (!aKnock || !aCopy.currentProcess || (cState != 0 && !KnockReplay::Lies(cState)))
        return false;
    aCopy.currentProcess->KnockExplosion(&aCopy, &aCopy.position, 0.f);
    m_knocked.emplace(aCopy.formID, cState != 0);
    spdlog::info("BodyBridge: {:X} knocked down with its owner (knock state {}, ragdolls {})", aCopy.formID, cState, aCopy.knockRagdolls);
    return true;
}

void BodyBridge::SetCopies(const std::vector<uint32_t>& acCopyFormIds) noexcept
{
    PlayerHands::SetCopies(acCopyFormIds);
}

void BodyBridge::SetClass(const BodyKey& acKey, bhkWorldObject* apBody, BodyClass aClass) noexcept
{
    const auto it = m_classes.find(acKey);
    if (acKey.Actor || (it == m_classes.end() ? aClass == BodyClass::kNone : it->second.Is == aClass))
        return;
    if (!apBody)
    {
        // Not here any more: nothing to give its group back to.
        if (aClass == BodyClass::kNone)
            m_classes.erase(acKey);
        return;
    }
    // This game's, and nothing played to meet: its group is left as it is until something is.
    if (aClass == BodyClass::kMoving && it == m_classes.end() && !BodyGroups::WritesMoving(AnyPlayed()))
    {
        if (apBody->referencedObject)
            m_classes[acKey] = {aClass, static_cast<uint16_t>(apBody->referencedObject->CollisionFilterInfo() >> 16), false};
        return;
    }
    if (aClass == BodyClass::kNone && !it->second.Applied)
    {
        m_classes.erase(it);
        return;
    }
    const uint16_t cGroup = aClass == BodyClass::kPlayed ? BodyGroups::kPlayed
                            : aClass == BodyClass::kMoving ? BodyGroups::kMoving
                            : it->second.Group;
    const auto cWas = SetBodyGroup(*apBody, cGroup);
    if (!cWas)
        return;
    const bool cFirstPlayed = aClass == BodyClass::kPlayed && !AnyPlayed();
    if (aClass == BodyClass::kNone)
        m_classes.erase(it);
    else if (it == m_classes.end())
        m_classes[acKey] = {aClass, *cWas};
    else
    {
        it->second.Is = aClass;
        it->second.Applied = true;
    }
    if (cFirstPlayed)
        WriteMoving();
}

bool BodyBridge::AnyPlayed() const noexcept
{
    return !m_ragdollGroups.empty() || std::ranges::any_of(m_classes, [](const auto& acClass) { return acClass.second.Is == BodyClass::kPlayed; });
}

void BodyBridge::WriteMoving() noexcept
{
    for (auto& [cKey, moving] : m_classes)
        if (moving.Is == BodyClass::kMoving && !moving.Applied)
            if (const Found cOther = Find(cKey, 0, false); cOther.pBody && SetBodyGroup(*cOther.pBody, BodyGroups::kMoving))
                moving.Applied = true;
}

void BodyBridge::SetMoving(const BodyKey& acKey, bool aMoving) noexcept
{
    // A copy's corpse this game's player took (its mover's until it rests): moving, then played again.
    if (acKey.Actor)
    {
        const auto cRagdoll = m_ragdolls.find(acKey);
        auto* pCopy = cRagdoll != m_ragdolls.end() ? Cast<Actor>(TESForm::GetById(cRagdoll->second.first)) : nullptr;
        if (const auto cBodies = pCopy && m_ragdollGroups.contains(acKey) ? RagdollBodiesOf(*pCopy) : std::vector<bhkWorldObject*>{};
            acKey.Index < cBodies.size() && cBodies[acKey.Index])
            SetBodyGroup(*cBodies[acKey.Index], aMoving ? BodyGroups::kMoving : BodyGroups::kPlayed);
        return;
    }
    const auto it = m_classes.find(acKey);
    // Played: its owner's frames decide until they end. Already so: nothing to look up.
    if (acKey.Actor || (it == m_classes.end() ? !aMoving : it->second.Is == BodyClass::kPlayed || aMoving))
        return;
    if (aMoving)
    {
        // Its owner's rest being put here wakes it for a step: that is not this game moving it (it began the rest again).
        if (const auto cRested = m_rested.find(acKey); cRested != m_rested.end() && !cRested->second.Done)
            return;
        m_rested.erase(acKey);
    }
    SetClass(acKey, Find(acKey, 0, false).pBody, aMoving ? BodyClass::kMoving : BodyClass::kNone);
}

void BodyBridge::Forget(const BodyKey& acKey) noexcept
{
    m_rested.erase(acKey);
    if (!acKey.Actor && m_classes.contains(acKey))
        SetClass(acKey, Find(acKey, 0, false).pBody, BodyClass::kNone);
}

void BodyBridge::ForgetAll() noexcept
{
    while (!m_classes.empty())
    {
        const BodyKey cKey = m_classes.begin()->first;
        SetClass(cKey, Find(cKey, 0, false).pBody, BodyClass::kNone);
        m_classes.erase(cKey); // its body in another world now: kept as it is
    }
    m_rested.clear();
}

bool BodyBridge::KnockDown(uint32_t aCopyFormId) noexcept
{
    auto* pCopy = Cast<Actor>(TESForm::GetById(aCopyFormId));
    return pCopy && !pCopy->IsDeleted() && !pCopy->IsDead() && KnockedDown(*pCopy, true);
}

void BodyBridge::StandRagdoll(Actor& aCopy, const BodyKey& acKey) noexcept
{
    if (acKey.Actor == 0 || !m_ragdolls.erase(acKey))
        return;
    const auto it = m_ragdollGroups.find(acKey);
    if (it == m_ragdollGroups.end())
        return;
    // Its bodies read again from the copy: a stale pointer from before its 3D was rebuilt is never written.
    if (const auto cBodies = RagdollBodiesOf(aCopy); acKey.Index < cBodies.size() && cBodies[acKey.Index])
        SetBodyGroup(*cBodies[acKey.Index], it->second);
    m_ragdollGroups.erase(it);
}

BodyBridge::Found BodyBridge::Find(const BodyKey& acKey, uint32_t aCopyFormId, bool aKnock) noexcept
{
    if (acKey.Actor)
    {
        auto* pCopy = Cast<Actor>(TESForm::GetById(aCopyFormId));
        if (!pCopy || pCopy->IsDeleted())
            return {};
        if (!pCopy->IsDead() && !m_knocked.contains(pCopy->formID))
            for (auto it = m_ragdolls.begin(); it != m_ragdolls.end();)
            {
                const BodyKey cKey = (it++)->first;
                if (cKey.Actor == acKey.Actor)
                    StandRagdoll(*pCopy, cKey);
            }
        // Its animation's while the copy stands (a creature's powered ragdoll is dynamic then, a revived one's again);
        // lying and getting up, its owner's (58291 only drives its simulated bodies toward the animation).
        if (!pCopy->IsDead() && !KnockedDown(*pCopy, aKnock))
        {
            StandRagdoll(*pCopy, acKey);
            return {.Waiting = true};
        }
        if (!pCopy->IsDead() && !KnockReplay::BodiesSimulated(pCopy->actorState.GetKnockState()))
            return {.Waiting = true};
        // The knock creates a new copy's physics wrappers; they need not exist before it is requested.
        const auto cBodies = RagdollBodiesOf(*pCopy);
        bhkWorldObject* pBody = acKey.Index < cBodies.size() ? cBodies[acKey.Index] : nullptr;
        if (!pBody || !pBody->referencedObject)
            return {.Waiting = true};
        // Dead by its owner's action here, or knocked down: left until its own ragdoll has it; kept then (a corpse
        // kicked again).
        bool first = false;
        const auto cGeneration = std::make_pair(aCopyFormId, pBody);
        if (const auto it = m_ragdolls.find(acKey); it == m_ragdolls.end() || it->second != cGeneration)
        {
            BodyRead read;
            if (!ReadBody(*pBody, read) || !read.Dynamic)
                return {.Waiting = true};
            m_ragdolls[acKey] = cGeneration;
            first = true;
            // A copy's: its owner's game settled what it lies among; it meets the world, characters and what this game
            // moves (this game's own corpse another game moves stays as the engine has it).
            if (pCopy->GetExtension()->IsCopy())
                if (const auto cOwn = SetBodyGroup(*pBody, BodyGroups::kPlayed); cOwn && *cOwn != BodyGroups::kPlayed)
                {
                    const bool cFirstPlayed = !AnyPlayed();
                    m_ragdollGroups[acKey] = *cOwn;
                    if (cFirstPlayed)
                        WriteMoving();
                }
        }
        return {.pBody = pBody, .First = first};
    }
    const uint32_t cFormId = m_formIdOf ? m_formIdOf(acKey.Object) : (acKey.Object.ModId == ObjectChange::kCreatedModId ? 0 : ToGame(acKey.Object));
    const auto cFound = ReferenceBodies::At(cFormId ? Cast<TESObjectREFR>(TESForm::GetById(cFormId)) : nullptr, acKey.Index);
    return {.pBody = cFound.pBody, .pCollision = cFound.pCollision};
}

void BodyBridge::RememberMotion(const BodyKey& acKey, const Found& acBody) noexcept
{
    if (acBody.pCollision && acBody.pBody && acBody.pBody->referencedObject && acBody.pBody->referencedObject->IsDynamic())
        m_motions.try_emplace(acKey, acBody.pBody->referencedObject->motionType);
}

std::optional<BodyPose> BodyBridge::ReadPose(const BodyKey& acKey) noexcept
{
    BodyRead read;
    if (const Found cFound = acKey.Actor ? Found{} : Find(acKey, 0, false); cFound.pBody && ReadBody(*cFound.pBody, read))
        return BodyPose{.Key = acKey, .Position = read.Position, .Rotation = read.Rotation};
    return std::nullopt;
}

bool BodyBridge::Drive(const BodyFrame& acFrame, uint32_t aCopyFormId, bool aHeld) noexcept
{
    const Found cFound = Find(acFrame.Key, aCopyFormId, acFrame.Knocked);
    if (cFound.Waiting)
        return true;
    if (!cFound.pBody)
        return false;
    if (acFrame.Key.Actor)
    {
        QueueCopyBodyFrame(aCopyFormId, acFrame, cFound.First);
        return true;
    }
    SetClass(acFrame.Key, cFound.pBody, BodyClass::kPlayed);
    m_rested.erase(acFrame.Key);
    // The host's game just placed it itself: left where that put it until the host has read the place.
    if (PlacedByGame::Is(ReferenceFormId(acFrame.Key)))
        return true;
    // Bodies driven within a couple of ms share a frame; Havok integrates the steer over the next one.
    const auto cNow = std::chrono::steady_clock::now();
    if (const float cSince = std::chrono::duration<float>(cNow - m_lastFrame).count(); cSince > 0.002f)
    {
        m_frameSeconds = std::min(cSince, 0.5f);
        m_lastFrame = cNow;
    }
    // Played, not held: a copy keeps its mass, so hits, pushes and grabs reach it (physics-sync.md, "One rule"); one
    // keyframed for a hold that ended gets its own motion back first.
    if (const auto it = m_motions.find(acFrame.Key); !aHeld && cFound.pCollision && it != m_motions.end())
    {
        if (!::FreeBody(*cFound.pCollision, static_cast<hkpRigidBody::MotionType>(it->second), acFrame.LinearVelocity, acFrame.AngularVelocity))
            return false;
        m_motions.erase(it);
    }
    if (BodyRead read; !aHeld && cFound.pCollision && !m_motions.contains(acFrame.Key) && ReadBody(*cFound.pBody, read) && read.Dynamic)
    {
        if (glm::distance(read.Position, acFrame.Position) <= kPlaceFar)
        {
            const auto cSteer = BodyPrediction::Steer(read.Position, read.Rotation, acFrame, BodyPrediction::SteerSeconds(acFrame, m_frameSeconds));
            return SteerBody(*cFound.pBody, cSteer.Linear, cSteer.Angular);
        }
        if (!PlaceBody(*cFound.pBody, acFrame.Position, acFrame.Rotation, acFrame.LinearVelocity, acFrame.AngularVelocity, true))
            return false;
        FollowBody(*cFound.pCollision);
        return true;
    }
    RememberMotion(acFrame.Key, cFound);
    // Held in another game: keyframed, carried onto the frame by its velocity; put there only when far off.
    if (BodyRead read; cFound.pCollision && ReadBody(*cFound.pBody, read) && glm::distance(read.Position, acFrame.Position) <= kPlaceFar)
    {
        const auto cSteer = BodyPrediction::Steer(read.Position, read.Rotation, acFrame, BodyPrediction::SteerSeconds(m_frameSeconds));
        if (!SteerKeyframed(*cFound.pBody, cSteer.Linear, cSteer.Angular))
            return false;
        // A keyframed body leaves its node behind: the node shows where the body is.
        FollowBody(*cFound.pCollision);
        return true;
    }
    if (!PlaceBody(*cFound.pBody, acFrame.Position, acFrame.Rotation, acFrame.LinearVelocity, acFrame.AngularVelocity))
        return false;
    if (cFound.pCollision)
        FollowBody(*cFound.pCollision);
    return true;
}

bool BodyBridge::Rest(const BodyPose& acPose, uint32_t aCopyFormId) noexcept
{
    const Found cFound = Find(acPose.Key, aCopyFormId, false);
    if (cFound.Waiting)
        return true;
    if (!cFound.pBody)
        return false;
    // Keep the exact rest without keyframing: the ragdoll driver's bone mapping needs its dynamic bodies.
    if (acPose.Key.Actor)
    {
        QueueCopyBodyFrame(aCopyFormId, {.Key = acPose.Key, .Position = acPose.Position, .Rotation = acPose.Rotation,
                                      .Knocked = acPose.Knocked}, true, true);
        return true;
    }
    const auto cSame = [](const BodyPose& acA, const BodyPose& acB) {
        return glm::distance(acA.Position, acB.Position) < ReferenceBodies::kSameDistance &&
               std::abs(glm::dot(acA.Rotation, acB.Rotation)) > std::cos(ReferenceBodies::kSameAngle);
    };
    const auto cNow = std::chrono::steady_clock::now();
    auto rested = m_rested.find(acPose.Key);
    if (rested == m_rested.end() || !cSame(rested->second.Pose, acPose))
        rested = m_rested.insert_or_assign(acPose.Key, Rested{.Pose = acPose, .Since = cNow}).first;
    const bool cHeld = m_motions.contains(acPose.Key);
    BodyRead read;
    const bool cSimulated = cFound.pCollision && !cHeld && ReadBody(*cFound.pBody, read) && read.Dynamic;
    const bool cNear = cSimulated && cSame({.Position = read.Position, .Rotation = read.Rotation}, acPose);
    const bool cPlaced = std::exchange(rested->second.Placed, true);
    // Simulated here (played, or never moved): a step awake at its rest gives Havok its contacts, then it sleeps
    // (BodyPrediction::PlaceRest); moving one already there would record a move the owner's game has not.
    if (cSimulated)
    {
        const auto cPlacement = BodyPrediction::PlaceRest(cNear, read.Active, cPlaced);
        // Off its rest: steered onto it first, for half a second at most (put there at once it snapped up to 16 units).
        if (cPlacement == BodyPrediction::RestPlacement::kPlace && !cNear &&
            BodyPrediction::GlidesToRest(glm::distance(read.Position, acPose.Position), std::chrono::duration<float>(cNow - rested->second.Since).count(), kPlaceFar))
        {
            rested->second.Placed = false;
            const auto cSteer = BodyPrediction::Steer(read.Position, read.Rotation, {.Position = acPose.Position, .Rotation = acPose.Rotation},
                                                      BodyPrediction::kRestGlideStep);
            SteerBody(*cFound.pBody, cSteer.Linear, cSteer.Angular);
            return false;
        }
        SetClass(acPose.Key, cFound.pBody, BodyClass::kNone);
        switch (cPlacement)
        {
        case BodyPrediction::RestPlacement::kLeave: return rested->second.Done = true;
        case BodyPrediction::RestPlacement::kPlace:
            if (PlaceBody(*cFound.pBody, acPose.Position, acPose.Rotation, {}, {}, true))
                FollowBody(*cFound.pCollision);
            return false; // asked again the next frame
        case BodyPrediction::RestPlacement::kSleep:
        {
            const auto cSince = rested->second.Sleeping.value_or(cNow);
            rested->second.Sleeping = cSince;
            // What this game moves, plays or holds keeps a pile awake (sleeping it froze the guest's carried object).
            std::set<const bhkWorldObject*> awake;
            for (const auto& [cKey, cClass] : m_classes)
                if (cClass.Is != BodyClass::kNone)
                    if (const Found cOther = Find(cKey, 0, false); cOther.pBody)
                        awake.insert(cOther.pBody);
            for (const auto& [cKey, cMotion] : m_motions)
                if (const Found cOther = Find(cKey, 0, false); cOther.pBody)
                    awake.insert(cOther.pBody);
            const bool cAsleep = SleepBody(*cFound.pBody, [&awake](const bhkWorldObject* apOther) { return awake.contains(apOther); });
            return rested->second.Done = BodyPrediction::RestSlept(cAsleep, std::chrono::duration<float>(cNow - cSince).count());
        }
        case BodyPrediction::RestPlacement::kHold: break;
        }
    }
    SetClass(acPose.Key, cFound.pBody, BodyClass::kNone);
    RememberMotion(acPose.Key, cFound);
    if (!PlaceBody(*cFound.pBody, acPose.Position, acPose.Rotation))
        return false;
    if (cFound.pCollision)
    {
        FollowBody(*cFound.pCollision);
        const auto it = m_motions.find(acPose.Key);
        if (it == m_motions.end())
            return rested->second.Done = true;
        // Keyframed for a hold: freed awake and still at its rest, then as a simulated one's the next frame.
        if (cHeld && !cPlaced)
        {
            const auto cMotion = static_cast<hkpRigidBody::MotionType>(it->second);
            m_motions.erase(it);
            ::FreeBody(*cFound.pCollision, cMotion, {}, {});
            return false;
        }
        if (!SettleFound(acPose.Key, cFound))
            return false;
    }
    return rested->second.Done = true;
}

bool BodyBridge::Free(const BodyFrame& acFrame) noexcept
{
    const Found cFound = Find(acFrame.Key, 0, false);
    // The reliable release may be the first frame heard of this hold.
    RememberMotion(acFrame.Key, cFound);
    const auto it = m_motions.find(acFrame.Key);
    if (!cFound.pCollision || it == m_motions.end())
        return false;
    const auto cMotion = static_cast<hkpRigidBody::MotionType>(it->second);
    m_motions.erase(it);
    // Its holder's frames show here kDelayMs behind, the let go at once: put at the let go's place only when far off
    // (near, that was a skip of up to 16 units); the mover's frames after it steer it on.
    if (BodyRead read; !ReadBody(*cFound.pBody, read) || glm::distance(read.Position, acFrame.Position) > kPlaceFar)
    {
        if (!PlaceBody(*cFound.pBody, acFrame.Position, acFrame.Rotation))
            return false;
        FollowBody(*cFound.pCollision);
    }
    return ::FreeBody(*cFound.pCollision, cMotion, acFrame.LinearVelocity, acFrame.AngularVelocity);
}

bool BodyBridge::KeepMotion(const BodyKey& acKey) noexcept
{
    RememberMotion(acKey, Find(acKey, 0, false));
    return m_motions.contains(acKey);
}

bool BodyBridge::Settle(const BodyKey& acKey) noexcept
{
    const Found cFound = Find(acKey, 0, false);
    if (!cFound.pCollision)
    {
        m_motions.erase(acKey);
        return true;
    }
    return !m_motions.contains(acKey) || SettleFound(acKey, cFound);
}

bool BodyBridge::SettleFound(const BodyKey& acKey, const Found& acBody) noexcept
{
    const auto it = m_motions.find(acKey);
    if (it == m_motions.end() || !acBody.pCollision)
        return false;
    const auto cMotion = static_cast<hkpRigidBody::MotionType>(it->second);
    // Asleep: a rest at a teetering pose would roll off again.
    if (!::FreeBody(*acBody.pCollision, cMotion, {}, {}, true))
        return false;
    m_motions.erase(it);
    return true;
}
