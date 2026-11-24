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
#include <Physics/BodyTolerances.h>
#include <Games/Skyrim/PlayerHands.h>
#include <TESObjectREFR.h>

#include <RTTI.h>

#include <algorithm>
#include <cmath>

void BodyBridge::SetReferences(std::function<uint32_t(const GameId& acId)> aFormIdOf, std::function<std::optional<GameId>(uint32_t aFormId)> aIdOf) noexcept
{
    m_formIdOf = std::move(aFormIdOf);
    m_idOf = std::move(aIdOf);
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
    // Another game's rest being put here wakes it for a step: that is not this game moving it.
    if (aMoving && m_placing.contains(acKey))
        return;
    SetClass(acKey, Find(acKey, 0, false).pBody, aMoving ? BodyClass::kMoving : BodyClass::kNone);
}

void BodyBridge::Forget(const BodyKey& acKey) noexcept
{
    m_placing.erase(acKey);
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
    m_placing.clear();
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

bool BodyBridge::IsTrap(const BodyKey& acKey) noexcept
{
    if (acKey.Actor)
        return false;
    const uint32_t cFormId = m_formIdOf ? m_formIdOf(acKey.Object) : (acKey.Object.ModId == ObjectChange::kCreatedModId ? 0 : ToGame(acKey.Object));
    auto* pObject = cFormId ? Cast<TESObjectREFR>(TESForm::GetById(cFormId)) : nullptr;
    return pObject && ReferenceBodies::IsTrap(*pObject);
}

std::optional<BodyRead> BodyBridge::Read(const BodyKey& acKey) noexcept
{
    BodyRead read;
    if (const Found cFound = acKey.Actor ? Found{} : Find(acKey, 0, false); cFound.pBody && ReadBody(*cFound.pBody, read))
        return read;
    return std::nullopt;
}

std::vector<std::pair<BodyKey, BodyKey>> BodyBridge::ReadTouching(const std::vector<BodyKey>& acKeys) noexcept
{
    std::vector<std::pair<BodyKey, BodyKey>> pairs;
    for (const BodyKey& cKey : acKeys)
    {
        const Found cFound = cKey.Actor ? Found{} : Find(cKey, 0, false);
        if (!cFound.pBody)
            continue;
        for (const hkpRigidBody* pOther : TouchingOf(*cFound.pBody))
        {
            TESObjectREFR* pObject = ReferenceOf(*pOther);
            const auto cId = pObject && m_idOf && !Cast<Actor>(pObject) ? m_idOf(pObject->formID) : std::nullopt;
            if (const auto cIndex = cId ? ReferenceBodies::IndexOf(*pObject, pOther) : std::nullopt)
                pairs.emplace_back(cKey, BodyKey{.Object = *cId, .Index = *cIndex});
        }
    }
    return pairs;
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
    m_placing.erase(acFrame.Key);
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
        if (glm::distance(read.Position, acFrame.Position) <= BodyTolerances::kPlaceFar)
        {
            const auto cSteer = BodyPrediction::Follow(read.Position, read.Rotation, acFrame, m_frameSeconds);
            return SteerBody(*cFound.pBody, cSteer.Linear, cSteer.Angular);
        }
        if (!PlaceBody(*cFound.pBody, acFrame.Position, acFrame.Rotation, acFrame.LinearVelocity, acFrame.AngularVelocity, true))
            return false;
        FollowBody(*cFound.pCollision);
        return true;
    }
    RememberMotion(acFrame.Key, cFound);
    // Held in another game: keyframed, carried onto the frame by its velocity; put there only when far off.
    if (BodyRead read; cFound.pCollision && ReadBody(*cFound.pBody, read) && glm::distance(read.Position, acFrame.Position) <= BodyTolerances::kPlaceFar)
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

bool BodyBridge::Unhold(const BodyKey& acKey, const Found& acBody) noexcept
{
    const auto it = m_motions.find(acKey);
    if (it == m_motions.end() || !acBody.pCollision)
        return true;
    const auto cMotion = static_cast<hkpRigidBody::MotionType>(it->second);
    if (!::FreeBody(*acBody.pCollision, cMotion, {}, {}))
        return false;
    m_motions.erase(it);
    return true;
}

bool BodyBridge::Release(const BodyKey& acKey) noexcept
{
    const Found cFound = Find(acKey, 0, false);
    m_placing.erase(acKey);
    return cFound.pBody && Unhold(acKey, cFound);
}

bool BodyBridge::Glide(const BodyPose& acRest) noexcept
{
    const Found cFound = Find(acRest.Key, 0, false);
    BodyRead read;
    if (!cFound.pBody || !Unhold(acRest.Key, cFound) || !ReadBody(*cFound.pBody, read))
        return false;
    m_placing.insert(acRest.Key);
    SetClass(acRest.Key, cFound.pBody, BodyClass::kNone);
    const auto cSteer = BodyPrediction::Steer(read.Position, read.Rotation, {.Position = acRest.Position, .Rotation = acRest.Rotation},
                                              BodyPrediction::kRestGlideStep);
    return SteerBody(*cFound.pBody, cSteer.Linear, cSteer.Angular);
}

bool BodyBridge::Place(const BodyPose& acRest) noexcept
{
    const Found cFound = Find(acRest.Key, 0, false);
    if (!cFound.pBody || !cFound.pCollision || !Unhold(acRest.Key, cFound))
        return false;
    m_placing.insert(acRest.Key);
    SetClass(acRest.Key, cFound.pBody, BodyClass::kNone);
    // Awake and still for a step: the collide step finds its contacts (physics.md, "A rest needs its contacts").
    if (!PlaceBody(*cFound.pBody, acRest.Position, acRest.Rotation, {}, {}, true))
        return false;
    FollowBody(*cFound.pCollision);
    return true;
}

bool BodyBridge::Sleep(const BodyKey& acKey) noexcept
{
    const Found cFound = Find(acKey, 0, false);
    if (!cFound.pBody)
        return false;
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
    if (cAsleep)
        m_placing.erase(acKey);
    return cAsleep;
}

bool BodyBridge::HoldAt(const BodyPose& acRest) noexcept
{
    const Found cFound = Find(acRest.Key, 0, false);
    if (!cFound.pBody || !cFound.pCollision)
        return false;
    SetClass(acRest.Key, cFound.pBody, BodyClass::kNone);
    RememberMotion(acRest.Key, cFound);
    // Keyframed there (its node with it), then simulated again asleep: it does not stay there by itself.
    if (!PlaceBody(*cFound.pBody, acRest.Position, acRest.Rotation))
        return false;
    FollowBody(*cFound.pCollision);
    m_placing.erase(acRest.Key);
    return !m_motions.contains(acRest.Key) || SettleFound(acRest.Key, cFound);
}

bool BodyBridge::RestRagdoll(const BodyPose& acRest, uint32_t aCopyFormId) noexcept
{
    const Found cFound = Find(acRest.Key, aCopyFormId, false);
    if (cFound.Waiting)
        return true;
    if (!cFound.pBody)
        return false;
    // Its exact rest, simulated: the ragdoll driver's bone mapping needs its dynamic bodies.
    QueueCopyBodyFrame(aCopyFormId, {.Key = acRest.Key, .Position = acRest.Position, .Rotation = acRest.Rotation, .Knocked = acRest.Knocked}, true,
                       true);
    return true;
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
