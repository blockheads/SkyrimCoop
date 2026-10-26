// GuestEngine's half for the host's bodies (docs/design/physics-sync.md): played, rested, pushed and held here.
#include <Bridges/GuestEngine.h>

#include <Bridges/ReferenceBodies.h>
#include <Games/ReferenceStates.h>
#include <Games/Skyrim/PlayerHands.h>
#include <Havok/bhkCollision.h>
#include <Havok/Ragdoll.h>
#include <NetImmerse/NiNode.h>
#include <Actor.h>
#include <FormIds.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>

#include <algorithm>
#include <RTTI.h>

bool GuestEngine::PlaceAtHostRest(TESObjectREFR& aObject, const ObjectChange& acMove) noexcept
{
    if (!ReferenceStates::Apply(aObject, acMove))
        return false;
    // This game's own flight of it ends there: held, so the engine puts its bodies at its node (FreePlaced frees them).
    NiNode* p3D = aObject.GetNiNode();
    if (!p3D)
        return true;
    const auto cCollisions = CollisionObjectsOf(*p3D);
    for (size_t i = 0; i < cCollisions.size(); ++i)
    {
        const BodyKey cKey{.Object = acMove.Object, .Index = static_cast<uint8_t>(i)};
        RememberMotion(cKey, {.pBody = cCollisions[i]->body, .pCollision = cCollisions[i]});
        if (m_motions.contains(cKey) && std::ranges::find(m_placed, cKey, &std::pair<BodyKey, uint32_t>::first) == m_placed.end())
            m_placed.emplace_back(cKey, 0);
    }
    HoldBodies(*p3D);
    return true;
}

void GuestEngine::FreePlaced() noexcept
{
    for (auto it = m_placed.begin(); it != m_placed.end();)
    {
        // The node update after the hold moves the keyframed body there (bhkNiCollisionObject::UpdateWorldData 78378).
        if (++it->second < 2)
        {
            ++it;
            continue;
        }
        FreeBody(it->first, FindHostBody(it->first, 0));
        it = m_placed.erase(it);
    }
}

std::optional<BodyPose> GuestEngine::ReadPose(const BodyKey& acKey) noexcept
{
    BodyRead read;
    if (const HostBody cFound = acKey.Actor ? HostBody{} : FindHostBody(acKey, 0); cFound.pBody && ReadBody(*cFound.pBody, read))
        return BodyPose{.Key = acKey, .Position = read.Position, .Rotation = read.Rotation};
    return std::nullopt;
}

GuestEngine::HostBody GuestEngine::FindHostBody(const BodyKey& acKey, uint32_t aCopyFormId) noexcept
{
    if (acKey.Actor)
    {
        const auto* pCopy = Cast<Actor>(TESForm::GetById(aCopyFormId));
        const auto cBodies = pCopy && !pCopy->IsDeleted() ? RagdollBodiesOf(*pCopy) : std::vector<bhkWorldObject*>{};
        bhkWorldObject* pBody = acKey.Index < cBodies.size() ? cBodies[acKey.Index] : nullptr;
        if (!pBody || !pBody->referencedObject)
            return {};
        if (!m_ragdolls.contains(acKey))
        {
            // Still its animation's until the copy dies by its owner's action here (a creature's powered ragdoll is
            // dynamic while it lives): left until its own ragdoll has it.
            if (!pCopy->IsDead() || !pBody->referencedObject->IsDynamic())
                return {.Waiting = true};
            m_ragdolls.insert(acKey);
        }
        return {.pBody = pBody};
    }
    const auto cFound = ReferenceBodies::At(FindReference(acKey.Object), acKey.Index);
    return {.pBody = cFound.pBody, .pCollision = cFound.pCollision};
}

void GuestEngine::RememberMotion(const BodyKey& acKey, const HostBody& acBody) noexcept
{
    if (acBody.pCollision && acBody.pBody->referencedObject && acBody.pBody->referencedObject->IsDynamic())
        m_motions.try_emplace(acKey, acBody.pBody->referencedObject->motionType);
}

bool GuestEngine::DriveBody(const BodyFrame& acFrame, uint32_t aCopyFormId) noexcept
{
    const HostBody cFound = FindHostBody(acFrame.Key, aCopyFormId);
    if (cFound.Waiting)
        return true;
    if (!cFound.pBody)
        return false;
    RememberMotion(acFrame.Key, cFound);
    if (!PlaceBody(*cFound.pBody, acFrame.Position, acFrame.Rotation, acFrame.LinearVelocity, acFrame.AngularVelocity))
        return false;
    // A keyframed body leaves its node behind: the node shows the frame.
    if (cFound.pCollision)
        FollowBody(*cFound.pCollision);
    return true;
}

bool GuestEngine::RestBody(const BodyPose& acPose, uint32_t aCopyFormId) noexcept
{
    const HostBody cFound = FindHostBody(acPose.Key, aCopyFormId);
    if (cFound.Waiting)
        return true;
    if (!cFound.pBody)
        return false;
    // A reference's body not held and already there (the host's settle in place): moving it would record a move
    // here the host's game has not.
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
        FreeBody(acPose.Key, cFound);
    }
    return true;
}

bool GuestEngine::FreeBody(const BodyKey& acKey, const HostBody& acBody) noexcept
{
    const auto it = m_motions.find(acKey);
    if (it == m_motions.end() || !acBody.pCollision)
        return false;
    const auto cMotion = static_cast<hkpRigidBody::MotionType>(it->second);
    m_motions.erase(it);
    // Asleep: a rest at a teetering pose would roll off again.
    return acBody.pCollision && ::FreeBody(*acBody.pCollision, cMotion, {}, {}, true);
}

std::optional<GameId> GuestEngine::HostIdOf(const TESObjectREFR& acObject) const noexcept
{
    if (!IsCreatedForm(acObject.formID))
        return ToSession(acObject.formID);
    for (const auto& [cHostId, cHandle] : m_created)
        if (TESObjectREFR::GetByHandle(cHandle) == &acObject)
            return GameId(ObjectChange::kCreatedModId, cHostId);
    return std::nullopt;
}

std::vector<BodyPush> GuestEngine::TakePushes() noexcept
{
    std::vector<BodyPush> pushes;
    for (const auto& cPush : PlayerHands::Take())
    {
        if (cPush.Impulse == glm::vec3(0.f))
            continue;
        auto* pObject = Cast<TESObjectREFR>(TESForm::GetById(cPush.Object));
        const auto cId = pObject ? HostIdOf(*pObject) : std::nullopt;
        const auto cIndex = cId ? ReferenceBodies::IndexOf(*pObject, cPush.pBody) : std::nullopt;
        if (!cIndex)
            continue;
        const BodyKey cKey{.Object = *cId, .Index = *cIndex};
        // One per body: the frame's impulses summed, at the last one's point.
        if (auto it = std::ranges::find(pushes, cKey, &BodyPush::Key); it != pushes.end())
        {
            it->Impulse += cPush.Impulse;
            it->Point = cPush.Point;
        }
        else
            pushes.push_back({cKey, cPush.Impulse, cPush.Point});
    }
    return pushes;
}

std::vector<BodyPose> GuestEngine::TakeOwnMoves() noexcept
{
    std::vector<BodyPose> moves;
    for (const auto& cAwake : m_moving.Take().Awake)
    {
        const auto* pObject = cAwake.Actor ? nullptr : Cast<TESObjectREFR>(TESForm::GetById(cAwake.FormId));
        if (const auto cId = pObject ? HostIdOf(*pObject) : std::nullopt)
            for (const auto& cRead : cAwake.Read)
                moves.push_back({.Key = {.Object = *cId, .Index = cRead.Key.Index}, .Position = cRead.Position, .Rotation = cRead.Rotation});
    }
    (void)m_moving.TakeRested();
    return moves;
}

std::vector<BodyFrame> GuestEngine::ReadHeld() const noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESObjectREFR* pObject = pPlayer && pPlayer->grabbedObject ? TESObjectREFR::GetByHandle(pPlayer->grabbedObject) : nullptr;
    const auto cId = pObject ? HostIdOf(*pObject) : std::nullopt;
    if (!cId)
        return {};
    auto frames = ReferenceBodies::ReadFrames(*pObject);
    for (BodyFrame& frame : frames)
        frame.Key.Object = *cId;
    return frames;
}
