#include <Havok/bhkCollision.h>

#include <BSCore/BSSpinLock.h>
#include <Physics/BodyPrediction.h>
#include <NetImmerse/NiAVObject.h>
#include <NetImmerse/NiNode.h>
#include <RTTI.h>
#include <VersionDb.h>
#include <TESObjectREFR.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>
#include <vector>

namespace
{
constexpr uint8_t kMaxBodies = 255;

bhkWorld* WorldOf(const hkpRigidBody& acBody) noexcept
{
    return acBody.world ? acBody.world->userData : nullptr;
}

// Havok's xyzw, its units: a float[4] each (hkVector4, hkQuaternion), 16-byte aligned.
struct alignas(16) HavokVector
{
    float Values[4]{};
};

// 60850 on aBody's island (world write lock held) if every other body is still and none kept awake; each body keeps
// its fastest speeds, not 60850's HK_REAL_MAX (physics.md, "When an island sleeps").
bool DeactivateIsland(hkpRigidBody& aBody, const std::function<bool(const bhkWorldObject*)>& acKeepsAwake,
                      const std::function<bool(const bhkWorldObject*)>& acOwnMotion = {}) noexcept
{
    using TDeactivate = void(hkpRigidBody*);
    POINTER_SKYRIMSE(TDeactivate, s_deactivate, 60850);
    const hkpSimulationIsland* pIsland = aBody.simulationIsland;
    if (!pIsland || !pIsland->IsActive())
        return true;
    const auto cVector = [](const float(&acValues)[4]) { return glm::vec3(acValues[0], acValues[1], acValues[2]); };
    std::vector<std::pair<hkpRigidBody*, std::array<float, 2>>> fastest;
    for (int32_t i = 0; i < pIsland->entityCount; ++i)
    {
        hkpRigidBody* pOther = pIsland->entities[i];
        if (!pOther)
            continue;
        if (pOther != &aBody &&
            ((acKeepsAwake && acKeepsAwake(pOther->userData)) ||
             (!(acOwnMotion && acOwnMotion(pOther->userData)) &&
              !BodyPrediction::StillToSleep(pOther->IsDynamic(), glm::length(cVector(pOther->linearVelocity)) * cHavokToGame,
                                           glm::length(cVector(pOther->angularVelocity))))))
            return false;
        // An own body keeps 60850's HK_REAL_MAX: Havok's last test (62264) then lets its settling motion sleep too.
        if (!(acOwnMotion && acOwnMotion(pOther->userData)))
            fastest.push_back({pOther, {pOther->deactivationReference[0][3], pOther->deactivationReference[1][3]}});
    }
    s_deactivate(&aBody);
    for (auto& [pOther, cFastest] : fastest)
    {
        pOther->deactivationReference[0][3] = cFastest[0];
        pOther->deactivationReference[1][3] = cFastest[1];
    }
    return true;
}

HavokVector ToHavok(const glm::vec3& acPosition) noexcept
{
    const glm::vec3 cHavok = acPosition / cHavokToGame;
    return {{cHavok.x, cHavok.y, cHavok.z, 0.f}};
}

HavokVector ToHavok(const glm::quat& acRotation) noexcept
{
    return {{acRotation.x, acRotation.y, acRotation.z, acRotation.w}};
}

// bhkRigidBody::SetLinearVelocity (78089) and SetAngularVelocity (78090), from the game's units (radians/s either way).
void SetVelocities(bhkWorldObject& aBody, const glm::vec3& acLinear, const glm::vec3& acAngular) noexcept
{
    using TSetVelocity = void(bhkWorldObject*, const HavokVector*);
    POINTER_SKYRIMSE(TSetVelocity, s_setLinearVelocity, 78089);
    POINTER_SKYRIMSE(TSetVelocity, s_setAngularVelocity, 78090);
    const HavokVector cLinear = ToHavok(acLinear);
    const HavokVector cAngular{{acAngular.x, acAngular.y, acAngular.z, 0.f}};
    s_setLinearVelocity(&aBody, &cLinear);
    s_setAngularVelocity(&aBody, &cAngular);
}

// bhkRigidBody::SetMotionType (78077, 1.6.1170: the body and the type; it takes the world's write lock and activates).
bool Keyframe(bhkWorldObject& aBody) noexcept
{
    if (aBody.referencedObject->motionType == hkpRigidBody::kKeyframed)
        return true;
    using TSetMotionType = bool(bhkWorldObject*, uint32_t);
    POINTER_SKYRIMSE(TSetMotionType, s_setMotionType, 78077);
    s_setMotionType(&aBody, hkpRigidBody::kKeyframed);
    return aBody.referencedObject->motionType == hkpRigidBody::kKeyframed;
}

// hkpRigidBody::setMotionType (60908) as bhkRigidBody::SetMotionType (78077) calls it (the world's write lock, then
// the engine's motion type lock 411545), but not woken: a woken island runs the engine's activation listener (25857),
// which takes its reference's extra data lock under the world's (docs/re/systems/physics.md, "Motion type and
// kSetLocal"). Then kSetLocal as bhkCollisionObject::SetMotionType (78182) sets it.
bool SetMotionTypeAsleep(bhkNiCollisionObject& aCollision, hkpRigidBody& aBody, hkpRigidBody::MotionType aMotion) noexcept
{
    constexpr int cDoNotActivate = 0;
    constexpr int cFilterUpdateFullCheck = 0;
    using TSetMotionType = void(hkpRigidBody*, uint32_t, int aActivation, int aFilterUpdate, int);
    POINTER_SKYRIMSE(TSetMotionType, s_setMotionType, 60908);
    VersionDbPtr<BSSpinLock> motionTypeLock(411545);
    TESObjectREFR* pReference = ReferenceOf(aBody);
    BSWriteLockGuard worldLock(WorldOf(aBody)->worldLock);
    // The callback needs extra data. A cell unload may hold it while waiting for this world: defer, never wait here.
    BSReadWriteLock* pExtra = pReference ? &pReference->extraData.lock : nullptr;
    if (pExtra && !pExtra->TryLockForWrite())
        return false;
    struct UnlockExtra
    {
        BSReadWriteLock* pLock;
        ~UnlockExtra() { if (pLock) pLock->UnlockForWrite(); }
    } extraLock{pExtra};
    {
        BSSpinLock& lock = *motionTypeLock.Get();
        lock.Lock(1);
        s_setMotionType(&aBody, aMotion, cDoNotActivate, cFilterUpdateFullCheck, -2);
        lock.Unlock();
    }
    if (aBody.IsDynamic())
        aCollision.flags |= bhkNiCollisionObject::kSetLocal;
    else
        aCollision.flags &= ~bhkNiCollisionObject::kSetLocal;
    return true;
}
} // namespace

void HoldBodies(NiAVObject& aRoot) noexcept
{
    for (bhkWorldObject* pBody : RigidBodiesOf(aRoot))
        SetVelocities(*pBody, {}, {});
    using TSetMotionType = bool(NiAVObject*, uint32_t, bool aRecurse, bool aForce, bool aAllowActivate);
    POINTER_SKYRIMSE(TSetMotionType, s_setMotionType, 77866);
    s_setMotionType.Get()(&aRoot, hkpRigidBody::kKeyframed, true, false, false);
}

std::vector<bhkNiCollisionObject*> CollisionObjectsOf(NiAVObject& aRoot) noexcept
{
    std::vector<bhkNiCollisionObject*> collisions;
    std::vector<NiAVObject*> queue{&aRoot};
    for (size_t i = 0; i < queue.size() && collisions.size() < kMaxBodies; ++i)
    {
        NiAVObject* pObject = queue[i];
        if (bhkNiCollisionObject* pCollision = pObject->collisionObject; pCollision && pCollision->body && Cast<bhkRigidBody>(pCollision->body))
            collisions.push_back(pCollision);
        if (auto* pNode = Cast<NiNode>(pObject))
            for (uint16_t child = 0; child < pNode->children.length; ++child)
                if (NiAVObject* pChild = pNode->children.data[child])
                    queue.push_back(pChild);
    }
    return collisions;
}

std::vector<bhkWorldObject*> RigidBodiesOf(NiAVObject& aRoot) noexcept
{
    std::vector<bhkWorldObject*> bodies;
    for (bhkNiCollisionObject* pCollision : CollisionObjectsOf(aRoot))
        bodies.push_back(pCollision->body);
    return bodies;
}

void FollowBody(bhkNiCollisionObject& aCollision) noexcept
{
    constexpr size_t cUpdateNodeFromCollision = 0x2B;
    using TUpdate = void(bhkNiCollisionObject*);
    auto** pVtable = *reinterpret_cast<TUpdate***>(&aCollision);
    // Without kSetLocal 78382 writes the world transform only, which the node's update below puts back.
    const uint16_t cFlags = aCollision.flags;
    aCollision.flags |= bhkNiCollisionObject::kSetLocal;
    pVtable[cUpdateNodeFromCollision](&aCollision);
    aCollision.flags = cFlags;
    // Its children and bounds follow (NiAVObject::Update, 70251); its keyframed body is put where it already is.
    if (NiAVObject* pNode = aCollision.sceneObject)
        pNode->Update();
}

bool ReadBody(const bhkWorldObject& acBody, BodyRead& aRead) noexcept
{
    const hkpRigidBody* pBody = acBody.referencedObject;
    bhkWorld* pWorld = pBody ? WorldOf(*pBody) : nullptr;
    if (!pWorld)
        return false;
    BSReadLockGuard _(pWorld->worldLock);
    const auto cVector = [](const float(&acValues)[4]) { return glm::vec3(acValues[0], acValues[1], acValues[2]); };
    aRead.Position = cVector(pBody->translation) * cHavokToGame;
    const auto& cRotation = pBody->rotations[1];
    aRead.Rotation = glm::normalize(glm::quat(cRotation[3], cRotation[0], cRotation[1], cRotation[2]));
    aRead.LinearVelocity = cVector(pBody->linearVelocity) * cHavokToGame;
    aRead.AngularVelocity = cVector(pBody->angularVelocity);
    aRead.Dynamic = pBody->IsDynamic();
    aRead.Active = pBody->IsActive();
    return true;
}

bool PlaceBody(bhkWorldObject& aBody, const glm::vec3& acPosition, const glm::quat& acRotation, const glm::vec3& acLinearVelocity,
               const glm::vec3& acAngularVelocity, bool aSimulated) noexcept
{
    hkpRigidBody* pBody = aBody.referencedObject;
    if (!pBody || !WorldOf(*pBody) || (!aSimulated && !Keyframe(aBody)))
        return false;
    using TSetPositionAndRotation = void(bhkWorldObject*, const HavokVector*, const HavokVector*);
    POINTER_SKYRIMSE(TSetPositionAndRotation, s_setPositionAndRotation, 78082);
    const HavokVector cPosition = ToHavok(acPosition);
    const HavokVector cRotation = ToHavok(acRotation);
    s_setPositionAndRotation(&aBody, &cPosition, &cRotation);
    SetVelocities(aBody, acLinearVelocity, acAngularVelocity);
    return true;
}

bool SteerBody(bhkWorldObject& aBody, const glm::vec3& acLinearVelocity, const glm::vec3& acAngularVelocity) noexcept
{
    if (!aBody.referencedObject || !WorldOf(*aBody.referencedObject))
        return false;
    SetVelocities(aBody, acLinearVelocity, acAngularVelocity);
    return true;
}

bool SteerKeyframed(bhkWorldObject& aBody, const glm::vec3& acLinearVelocity, const glm::vec3& acAngularVelocity) noexcept
{
    if (!aBody.referencedObject || !WorldOf(*aBody.referencedObject) || !Keyframe(aBody))
        return false;
    SetVelocities(aBody, acLinearVelocity, acAngularVelocity);
    return true;
}

bool SleepBody(bhkWorldObject& aBody, const std::function<bool(const bhkWorldObject*)>& acKeepsAwake,
               const std::function<bool(const bhkWorldObject*)>& acOwnMotion) noexcept
{
    hkpRigidBody* pBody = aBody.referencedObject;
    bhkWorld* pWorld = pBody ? WorldOf(*pBody) : nullptr;
    if (!pWorld || !pBody->IsDynamic())
        return false;
    BSWriteLockGuard _(pWorld->worldLock);
    return DeactivateIsland(*pBody, acKeepsAwake, acOwnMotion);
}

std::optional<std::vector<hkpRigidBody*>> IslandMatesOf(const bhkWorldObject& acBody) noexcept
{
    const hkpRigidBody* pBody = acBody.referencedObject;
    bhkWorld* pWorld = pBody ? WorldOf(*pBody) : nullptr;
    if (!pWorld)
        return std::nullopt;
    BSReadLockGuard _(pWorld->worldLock);
    const hkpSimulationIsland* pIsland = pBody->simulationIsland;
    if (!pIsland || !pIsland->IsActive() || !pBody->IsDynamic())
        return std::nullopt;
    std::vector<hkpRigidBody*> mates;
    for (int32_t i = 0; i < pIsland->entityCount; ++i)
        if (hkpRigidBody* pOther = pIsland->entities[i]; pOther && pOther != pBody && pOther->IsDynamic())
            mates.push_back(pOther);
    return mates;
}

std::optional<uint16_t> SetBodyGroup(bhkWorldObject& aBody, uint16_t aGroup) noexcept
{
    hkpRigidBody* pBody = aBody.referencedObject;
    bhkWorld* pWorld = pBody ? WorldOf(*pBody) : nullptr;
    if (!pWorld)
        return std::nullopt;
    const uint32_t cFilter = pBody->CollisionFilterInfo();
    const auto cWas = static_cast<uint16_t>(cFilter >> 16);
    if (cWas == aGroup)
        return cWas;
    const uint32_t cNew = (cFilter & 0xFFFF) | (static_cast<uint32_t>(aGroup) << 16);
    {
        BSWriteLockGuard _(pWorld->worldLock);
        std::memcpy(pBody->collidable + 0x2C, &cNew, sizeof(cNew));
    }
    using TUpdateCollisionFilter = void(bhkWorldObject*);
    POINTER_SKYRIMSE(TUpdateCollisionFilter, s_updateCollisionFilter, 77794);
    s_updateCollisionFilter(&aBody);
    return cWas;
}

bool FreeBody(bhkNiCollisionObject& aCollision, hkpRigidBody::MotionType aMotion, const glm::vec3& acLinearVelocity,
              const glm::vec3& acAngularVelocity, bool aAsleep) noexcept
{
    bhkWorldObject* pWorldObject = aCollision.body;
    hkpRigidBody* pBody = pWorldObject ? pWorldObject->referencedObject : nullptr;
    if (!pBody || !WorldOf(*pBody))
        return false;
    if (aAsleep)
    {
        // Still from its hold; a rest at a teetering pose woken would roll off again.
        if (!SetMotionTypeAsleep(aCollision, *pBody, aMotion))
            return false;
        if (!pBody->IsDynamic())
            return false;
        BSWriteLockGuard _(WorldOf(*pBody)->worldLock);
        // Still where it was put, for Havok's sleep test; written on the motion, as 78089 would wake it (lock order).
        std::fill(std::begin(pBody->linearVelocity), std::end(pBody->linearVelocity), 0.f);
        std::fill(std::begin(pBody->angularVelocity), std::end(pBody->angularVelocity), 0.f);
        // Beside a body still moving it is left awake, to settle as Havok has it.
        DeactivateIsland(*pBody, {});
        return true;
    }
    constexpr size_t cSetMotionType = 0x2E;
    using TSetMotionType = bool(bhkNiCollisionObject*, uint32_t aType, bhkWorldObject* apBody, bool aActivate);
    auto** pVtable = *reinterpret_cast<TSetMotionType***>(&aCollision);
    pVtable[cSetMotionType](&aCollision, aMotion, pWorldObject, true);
    if (!pBody->IsDynamic())
        return false;
    SetVelocities(*pWorldObject, acLinearVelocity, acAngularVelocity);
    return true;
}

TESObjectREFR* ReferenceOf(const hkpRigidBody& acBody) noexcept
{
    using TFindCollidableRef = TESObjectREFR*(const void*);
    POINTER_SKYRIMSE(TFindCollidableRef, s_findCollidableRef, 26003);
    return s_findCollidableRef(acBody.collidable);
}
