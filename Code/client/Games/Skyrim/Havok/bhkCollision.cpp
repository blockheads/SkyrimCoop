#include <TiltedOnlinePCH.h>

#include <Havok/bhkCollision.h>

#include <NetImmerse/NiAVObject.h>
#include <NetImmerse/NiNode.h>

void HoldBodies(NiAVObject& aRoot) noexcept
{
    constexpr uint32_t cKeyframed = 4; // hkpMotion::MotionType
    TP_THIS_FUNCTION(TSetVelocity, void, bhkWorldObject, const float*);
    POINTER_SKYRIMSE(TSetVelocity, s_setLinearVelocity, 78089);
    POINTER_SKYRIMSE(TSetVelocity, s_setAngularVelocity, 78090);
    alignas(16) const float cZero[4]{};
    TiltedPhoques::Vector<NiAVObject*> queue{&aRoot};
    for (size_t i = 0; i < queue.size(); ++i)
    {
        NiAVObject* pObject = queue[i];
        if (bhkNiCollisionObject* pCollision = pObject->collisionObject; pCollision && pCollision->body && Cast<bhkRigidBody>(pCollision->body))
        {
            TiltedPhoques::ThisCall(s_setLinearVelocity, pCollision->body, cZero);
            TiltedPhoques::ThisCall(s_setAngularVelocity, pCollision->body, cZero);
        }
        if (auto* pNode = Cast<NiNode>(pObject))
            for (uint16_t child = 0; child < pNode->children.length; ++child)
                if (NiAVObject* pChild = pNode->children.data[child])
                    queue.push_back(pChild);
    }
    using TSetMotionType = bool(NiAVObject*, uint32_t, bool aRecurse, bool aForce, bool aAllowActivate);
    POINTER_SKYRIMSE(TSetMotionType, s_setMotionType, 77866);
    s_setMotionType.Get()(&aRoot, cKeyframed, true, false, false);
}
