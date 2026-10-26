#include <Bridges/ReferenceBodies.h>

#include <Actor.h>
#include <NetImmerse/NiNode.h>
#include <TESObjectREFR.h>

#include <algorithm>

namespace ReferenceBodies
{
std::vector<bhkNiCollisionObject*> Of(TESObjectREFR& aObject) noexcept
{
    NiNode* p3D = !aObject.IsDeleted() && !Cast<Actor>(&aObject) ? aObject.GetNiNode() : nullptr;
    return p3D ? CollisionObjectsOf(*p3D) : std::vector<bhkNiCollisionObject*>{};
}

Body At(TESObjectREFR* apObject, uint8_t aIndex) noexcept
{
    const auto cCollisions = apObject ? Of(*apObject) : std::vector<bhkNiCollisionObject*>{};
    bhkNiCollisionObject* pCollision = aIndex < cCollisions.size() ? cCollisions[aIndex] : nullptr;
    const hkpRigidBody* pRigid = pCollision ? pCollision->body->referencedObject : nullptr;
    if (!pRigid || pRigid->motionType == hkpRigidBody::kFixed)
        return {};
    return {pCollision->body, pCollision};
}

std::optional<uint8_t> IndexOf(TESObjectREFR& aObject, const hkpRigidBody* apBody) noexcept
{
    const auto cCollisions = Of(aObject);
    const auto cAt = std::ranges::find_if(cCollisions, [&](const bhkNiCollisionObject* apCollision) { return apCollision->body->referencedObject == apBody; });
    return cAt != cCollisions.end() ? std::optional(static_cast<uint8_t>(cAt - cCollisions.begin())) : std::nullopt;
}

BodyFrame FrameOf(const BodyRead& acRead, uint8_t aIndex) noexcept
{
    return {.Key = {.Index = aIndex}, .Position = acRead.Position, .Rotation = acRead.Rotation, .LinearVelocity = acRead.LinearVelocity,
            .AngularVelocity = acRead.AngularVelocity};
}

std::vector<BodyFrame> ReadFrames(TESObjectREFR& aObject) noexcept
{
    std::vector<BodyFrame> frames;
    const auto cCollisions = Of(aObject);
    for (size_t i = 0; i < cCollisions.size(); ++i)
        if (BodyRead read; ReadBody(*cCollisions[i]->body, read) && read.Dynamic)
            frames.push_back(FrameOf(read, static_cast<uint8_t>(i)));
    return frames;
}
} // namespace ReferenceBodies
