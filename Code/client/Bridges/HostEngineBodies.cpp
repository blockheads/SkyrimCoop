// HostEngine's half for the host's bodies (docs/design/physics-sync.md): read for the guests, and a guest's hands on them.
#include <Bridges/HostEngine.h>

#include <Bridges/ReferenceBodies.h>
#include <Havok/bhkCollision.h>
#include <FormIds.h>
#include <TESObjectREFR.h>

#include <RTTI.h>

HostBodies HostEngine::TakeBodies(const std::function<uint32_t(uint32_t aFormId)>& aSessionOf) noexcept
{
    HostBodies bodies;
    const auto cOwner = [&](const PhysicsBodies::Bodies& acBodies) -> std::optional<BodyKey> {
        if (acBodies.Actor)
        {
            const uint32_t cSessionId = aSessionOf ? aSessionOf(acBodies.FormId) : 0;
            return cSessionId ? std::optional<BodyKey>(BodyKey{.Actor = cSessionId}) : std::nullopt;
        }
        const auto cId = IsShared(acBodies.FormId) ? ToSessionOrCreated(acBodies.FormId) : std::nullopt;
        return cId ? std::optional<BodyKey>(BodyKey{.Object = *cId}) : std::nullopt;
    };
    const auto cUpdate = m_bodies.Take();
    for (const auto& cAwake : cUpdate.Awake)
        if (const auto cKey = cOwner(cAwake))
            for (BodyFrame frame : cAwake.Read)
            {
                frame.Key = {cKey->Object, cKey->Actor, frame.Key.Index};
                bodies.Awake.push_back(frame);
            }
    for (const auto& cRested : cUpdate.Rested)
        if (const auto cKey = cOwner(cRested))
            for (const BodyFrame& cRead : cRested.Read)
                bodies.Rested.push_back({.Key = {cKey->Object, cKey->Actor, cRead.Key.Index}, .Position = cRead.Position, .Rotation = cRead.Rotation});
    return bodies;
}

ReferenceBodies::Body HostEngine::FindObjectBody(const BodyKey& acKey) const noexcept
{
    if (acKey.Actor)
        return {};
    const uint32_t cFormId = acKey.Object.ModId == ObjectChange::kCreatedModId ? 0xFF000000 | acKey.Object.BaseId : ToGame(acKey.Object);
    return ReferenceBodies::At(cFormId && IsShared(cFormId) ? Cast<TESObjectREFR>(TESForm::GetById(cFormId)) : nullptr, acKey.Index);
}

bool HostEngine::PushBody(const BodyPush& acPush) noexcept
{
    const auto cFound = FindObjectBody(acPush.Key);
    return cFound.pBody && ::PushBody(*cFound.pBody, acPush.Impulse, acPush.Point);
}

bool HostEngine::DriveBody(const BodyFrame& acFrame) noexcept
{
    const auto cFound = FindObjectBody(acFrame.Key);
    hkpRigidBody* pRigid = cFound.pBody ? cFound.pBody->referencedObject : nullptr;
    if (!pRigid)
        return false;
    if (pRigid->IsDynamic())
        m_motions.try_emplace(acFrame.Key, pRigid->motionType);
    if (!PlaceBody(*cFound.pBody, acFrame.Position, acFrame.Rotation, acFrame.LinearVelocity, acFrame.AngularVelocity))
        return false;
    // A keyframed body leaves its node behind.
    FollowBody(*cFound.pCollision);
    return true;
}

bool HostEngine::FreeBody(const BodyFrame& acFrame) noexcept
{
    const auto cFound = FindObjectBody(acFrame.Key);
    const auto it = m_motions.find(acFrame.Key);
    if (!cFound.pBody || it == m_motions.end())
        return false;
    const auto cMotion = static_cast<hkpRigidBody::MotionType>(it->second);
    m_motions.erase(it);
    if (!PlaceBody(*cFound.pBody, acFrame.Position, acFrame.Rotation))
        return false;
    FollowBody(*cFound.pCollision);
    return ::FreeBody(*cFound.pCollision, cMotion, acFrame.LinearVelocity, acFrame.AngularVelocity);
}

std::vector<BodyPose> HostEngine::ReadBodies(const std::vector<BodyKey>& acKeys) const noexcept
{
    std::vector<BodyPose> poses;
    for (const BodyKey& cKey : acKeys)
    {
        BodyRead read;
        if (const auto cFound = FindObjectBody(cKey); cFound.pBody && ReadBody(*cFound.pBody, read))
            poses.push_back({.Key = cKey, .Position = read.Position, .Rotation = read.Rotation});
    }
    return poses;
}
