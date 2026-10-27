#include <Bridges/BodyBridge.h>

#include <Bridges/ReferenceBodies.h>
#include <Havok/bhkCollision.h>
#include <Havok/Ragdoll.h>
#include <AI/AIProcess.h>
#include <Actor.h>
#include <FormIds.h>
#include <Messages/WorldState.h>
#include <Physics/BodyPrediction.h>
#include <TESObjectREFR.h>

#include <RTTI.h>

#include <cmath>

void BodyBridge::SetReferences(std::function<uint32_t(const GameId& acId)> aFormIdOf) noexcept
{
    m_formIdOf = std::move(aFormIdOf);
}

bool BodyBridge::KnockedDown(Actor& aCopy, bool aKnock) noexcept
{
    const uint8_t cState = aCopy.actorState.GetKnockState();
    // Getting up (its knock update, or its owner's get up replayed): its animation's again.
    if (cState >= 5)
        return false;
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
    if (!aKnock || !aCopy.currentProcess)
        return false;
    aCopy.currentProcess->KnockExplosion(&aCopy, &aCopy.position, 0.f);
    m_knocked.emplace(aCopy.formID, cState != 0);
    spdlog::info("BodyBridge: {:X} knocked down with its owner (knock state {}, ragdolls {})", aCopy.formID, cState, aCopy.knockRagdolls);
    return true;
}

BodyBridge::Found BodyBridge::Find(const BodyKey& acKey, uint32_t aCopyFormId, bool aKnock) noexcept
{
    if (acKey.Actor)
    {
        auto* pCopy = Cast<Actor>(TESForm::GetById(aCopyFormId));
        const auto cBodies = pCopy && !pCopy->IsDeleted() ? RagdollBodiesOf(*pCopy) : std::vector<bhkWorldObject*>{};
        bhkWorldObject* pBody = acKey.Index < cBodies.size() ? cBodies[acKey.Index] : nullptr;
        if (!pBody || !pBody->referencedObject)
            return {};
        // Its animation's while the copy stands (a creature's powered ragdoll is dynamic then, a revived one's again).
        if (!pCopy->IsDead() && !KnockedDown(*pCopy, aKnock))
        {
            m_ragdolls.erase(acKey);
            return {.Waiting = true};
        }
        // Dead by its owner's action here, or knocked down: left until its own ragdoll has it; kept then (a corpse
        // kicked again).
        if (!m_ragdolls.contains(acKey))
        {
            if (!pBody->referencedObject->IsDynamic())
                return {.Waiting = true};
            m_ragdolls.insert(acKey);
        }
        return {.pBody = pBody};
    }
    const uint32_t cFormId = m_formIdOf ? m_formIdOf(acKey.Object) : (acKey.Object.ModId == ObjectChange::kCreatedModId ? 0 : ToGame(acKey.Object));
    const auto cFound = ReferenceBodies::At(cFormId ? Cast<TESObjectREFR>(TESForm::GetById(cFormId)) : nullptr, acKey.Index);
    return {.pBody = cFound.pBody, .pCollision = cFound.pCollision};
}

void BodyBridge::RememberMotion(const BodyKey& acKey, const Found& acBody) noexcept
{
    if (acBody.pCollision && acBody.pBody->referencedObject && acBody.pBody->referencedObject->IsDynamic())
        m_motions.try_emplace(acKey, acBody.pBody->referencedObject->motionType);
}

std::optional<BodyPose> BodyBridge::ReadPose(const BodyKey& acKey) noexcept
{
    BodyRead read;
    if (const Found cFound = acKey.Actor ? Found{} : Find(acKey, 0, false); cFound.pBody && ReadBody(*cFound.pBody, read))
        return BodyPose{.Key = acKey, .Position = read.Position, .Rotation = read.Rotation};
    return std::nullopt;
}

bool BodyBridge::Drive(const BodyFrame& acFrame, uint32_t aCopyFormId) noexcept
{
    const Found cFound = Find(acFrame.Key, aCopyFormId, acFrame.Knocked);
    if (cFound.Waiting)
        return true;
    if (!cFound.pBody)
        return false;
    // A ragdoll's body steered onto the frame, not placed: its ragdoll driver makes a placed (keyframed) body simulated
    // again each update, so the two would fight (physics-sync.md, "A copy's ragdoll, steered"). Placed when far off.
    if (BodyRead read; acFrame.Key.Actor && ReadBody(*cFound.pBody, read) && glm::distance(read.Position, acFrame.Position) < kSteerDistance)
    {
        const auto cSteer = BodyPrediction::Steer(read.Position, read.Rotation, acFrame);
        return SteerBody(*cFound.pBody, cSteer.Linear, cSteer.Angular);
    }
    RememberMotion(acFrame.Key, cFound);
    if (!PlaceBody(*cFound.pBody, acFrame.Position, acFrame.Rotation, acFrame.LinearVelocity, acFrame.AngularVelocity))
        return false;
    // A keyframed body leaves its node behind: the node shows the frame.
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
    // A living copy's rest is its owner's getting up: its own ragdoll, steered there, gets up as the owner's does. Held
    // there instead, its bodies stay at the rest while it stands, and its next knock flings them from it.
    if (acPose.Key.Actor)
        if (const auto* pCopy = Cast<Actor>(TESForm::GetById(aCopyFormId)); pCopy && !pCopy->IsDead())
            return true;
    // A reference's body not held and already there (the owner's settle in place): moving it would record a move
    // here the owner's game has not.
    if (BodyRead read; cFound.pCollision && !m_motions.contains(acPose.Key) && ReadBody(*cFound.pBody, read) && read.Dynamic &&
                       glm::distance(read.Position, acPose.Position) < ReferenceBodies::kSameDistance &&
                       std::abs(glm::dot(read.Rotation, acPose.Rotation)) > std::cos(ReferenceBodies::kSameAngle))
        return true;
    RememberMotion(acPose.Key, cFound);
    if (!PlaceBody(*cFound.pBody, acPose.Position, acPose.Rotation))
        return false;
    if (cFound.pCollision)
    {
        FollowBody(*cFound.pCollision);
        SettleFound(acPose.Key, cFound);
    }
    return true;
}

bool BodyBridge::Free(const BodyFrame& acFrame) noexcept
{
    const Found cFound = Find(acFrame.Key, 0, false);
    const auto it = m_motions.find(acFrame.Key);
    if (!cFound.pCollision || it == m_motions.end())
        return false;
    const auto cMotion = static_cast<hkpRigidBody::MotionType>(it->second);
    m_motions.erase(it);
    if (!PlaceBody(*cFound.pBody, acFrame.Position, acFrame.Rotation))
        return false;
    FollowBody(*cFound.pCollision);
    return ::FreeBody(*cFound.pCollision, cMotion, acFrame.LinearVelocity, acFrame.AngularVelocity);
}

bool BodyBridge::Push(const BodyPush& acPush) noexcept
{
    const Found cFound = acPush.Key.Actor ? Found{} : Find(acPush.Key, 0, false);
    return cFound.pBody && ::PushBody(*cFound.pBody, acPush.Impulse, acPush.Point);
}

bool BodyBridge::KeepMotion(const BodyKey& acKey) noexcept
{
    RememberMotion(acKey, Find(acKey, 0, false));
    return m_motions.contains(acKey);
}

bool BodyBridge::Settle(const BodyKey& acKey) noexcept
{
    return SettleFound(acKey, Find(acKey, 0, false));
}

bool BodyBridge::SettleFound(const BodyKey& acKey, const Found& acBody) noexcept
{
    const auto it = m_motions.find(acKey);
    if (it == m_motions.end() || !acBody.pCollision)
        return false;
    const auto cMotion = static_cast<hkpRigidBody::MotionType>(it->second);
    m_motions.erase(it);
    // Asleep: a rest at a teetering pose would roll off again.
    return ::FreeBody(*acBody.pCollision, cMotion, {}, {}, true);
}
