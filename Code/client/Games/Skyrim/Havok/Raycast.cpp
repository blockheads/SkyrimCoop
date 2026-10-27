#include <Havok/Raycast.h>
#include <Havok/bhkPickData.h>
#include <Forms/TESObjectCELL.h>
#include <PlayerCharacter.h>
#include <VersionDb.h>
#include <glm/glm.hpp>

namespace GameRaycast
{
namespace
{
std::optional<RayHit> CastRayOnce(const float acFrom[3], const float acTo[3], uint32_t aLayer) noexcept;
} // namespace

std::optional<RayHit> CastRay(const float acFrom[3], const float acTo[3], uint32_t aLayer, uint32_t aThrough) noexcept
{
    // Past the camera's own collision (kCameraSphere 36, kCameraPick 39) and the player's: they are not the world.
    const glm::vec3 cStart(acFrom[0], acFrom[1], acFrom[2]);
    const glm::vec3 cEnd(acTo[0], acTo[1], acTo[2]);
    const glm::vec3 cDirection = glm::length(cEnd - cStart) > 0.f ? glm::normalize(cEnd - cStart) : glm::vec3(0.f);
    glm::vec3 from = cStart;
    for (int skip = 0; skip < 8; ++skip)
    {
        const float cFrom[3]{from.x, from.y, from.z};
        auto hit = CastRayOnce(cFrom, acTo, aLayer);
        if (!hit || !hit->Hit)
        {
            if (hit)
                hit->Distance = glm::distance(cStart, cEnd);
            return hit;
        }
        if (hit->Layer != 36 && hit->Layer != 39 && hit->Reference != 0x14 && (!aThrough || hit->Reference != aThrough))
        {
            hit->Distance = glm::distance(cStart, glm::vec3(hit->Point[0], hit->Point[1], hit->Point[2]));
            return hit;
        }
        from = glm::vec3(hit->Point[0], hit->Point[1], hit->Point[2]) + cDirection;
        if (glm::dot(cEnd - from, cDirection) <= 0.f)
            return RayHit{.Distance = glm::distance(cStart, cEnd)};
    }
    return std::nullopt;
}

namespace
{
std::optional<RayHit> CastRayOnce(const float acFrom[3], const float acTo[3], uint32_t aLayer) noexcept
{
    const PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer || !pPlayer->parentCell)
        return std::nullopt;
    using TGetWorld = void*(TESObjectCELL*);
    POINTER_SKYRIMSE(TGetWorld, s_getWorld, 18995); // TESObjectCELL::GetbhkWorld
    void* pWorld = s_getWorld(pPlayer->parentCell);
    if (!pWorld)
        return std::nullopt;

    bhkPickData pick{};
    for (int axis = 0; axis < 3; ++axis)
    {
        pick.From[axis] = acFrom[axis] * 0.0142875f;
        pick.To[axis] = acTo[axis] * 0.0142875f;
    }
    pick.FilterInfo = aLayer & 0x7F;
    pick.HitFraction = 1.f;
    // bhkWorld::PickObject, vtable 0x33 (77860; bhkWorldM's for exteriors).
    using TPick = bool(void*, bhkPickData*);
    auto** pVtable = *reinterpret_cast<TPick***>(pWorld);
    const bool cHit = pVtable[0x33](pWorld, &pick);
    if (pick.PickFailed)
        return std::nullopt;

    RayHit hit;
    const glm::vec3 cFrom(acFrom[0], acFrom[1], acFrom[2]);
    const glm::vec3 cTo(acTo[0], acTo[1], acTo[2]);
    hit.Hit = cHit && pick.RootCollidable && pick.HitFraction < 1.f;
    const float cFraction = hit.Hit ? pick.HitFraction : 1.f;
    const glm::vec3 cPoint = cFrom + (cTo - cFrom) * cFraction;
    hit.Distance = glm::distance(cFrom, cPoint);
    for (int axis = 0; axis < 3; ++axis)
    {
        hit.Point[axis] = cPoint[axis];
        hit.Normal[axis] = pick.Normal[axis];
    }
    if (hit.Hit)
    {
        using TFindRef = TESObjectREFR*(const hkpCollidable*);
        POINTER_SKYRIMSE(TFindRef, s_findCollidableRef, 26003); // TESHavokUtilities::FindCollidableRef
        if (const TESObjectREFR* pRef = s_findCollidableRef(pick.RootCollidable))
            hit.Reference = pRef->formID;
        hit.Layer = pick.RootCollidable->filterInfo & 0x7F;
    }
    return hit;
}
} // namespace

} // namespace GameRaycast
