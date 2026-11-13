#include <Havok/SwingCast.h>

#include <Actor.h>
#include <Games/TES.h>
#include <Havok/bhkCollision.h>
#include <Havok/bhkPickData.h>
#include <VersionDb.h>

#include <cstring>

namespace
{
// hkpRootCdPoint (Havok 2010): the contact, then both root collidables; the collector sorts them nearest first (60336).
struct hkpRootCdPoint
{
    float position[4];                    // 00, Havok units
    float separatingNormal[4];            // 10
    const hkpCollidable* rootCollidableA; // 20: the cast's phantom
    uint32_t shapeKeyA;                   // 28
    uint8_t pad2C[4];
    const hkpCollidable* rootCollidableB; // 30: what it met (38628 reads +0x30)
    uint32_t shapeKeyB;                   // 38
    uint8_t pad3C[4];
};
static_assert(sizeof(hkpRootCdPoint) == 0x40);
static_assert(offsetof(hkpRootCdPoint, rootCollidableB) == 0x30);

// hkpAllCdPointCollector, the engine's shared swing collector (GetAllCdPointCollector 25925): its hits, and the group
// its phantom ignores.
struct SwingCollector
{
    uint8_t pad0[0x10];
    hkpRootCdPoint* hits; // 10
    int32_t hitCount;     // 18
    uint8_t pad1C[0x22C - 0x1C];
    uint32_t filterGroup; // 22C: set by 25932
};
static_assert(offsetof(SwingCollector, hitCount) == 0x18);
static_assert(offsetof(SwingCollector, filterGroup) == 0x22C);

float FloatSetting(uint32_t aId) noexcept
{
    VersionDbPtr<Setting> setting(aId);
    float value{};
    std::memcpy(&value, &setting.Get()->data, sizeof(value));
    return value;
}
} // namespace

namespace GameSwingCast
{
std::vector<Hit> HitsOf(const void* apCollector) noexcept
{
    using TFindRef = TESObjectREFR*(const hkpCollidable*);
    POINTER_SKYRIMSE(TFindRef, s_findCollidableRef, 26003); // TESHavokUtilities::FindCollidableRef

    std::vector<Hit> hits;
    const auto* pCollector = static_cast<const SwingCollector*>(apCollector);
    for (int32_t i = 0; pCollector && i < pCollector->hitCount; ++i)
    {
        const hkpRootCdPoint& cPoint = pCollector->hits[i];
        Hit hit;
        if (cPoint.rootCollidableB)
        {
            if (const TESObjectREFR* pRef = s_findCollidableRef(cPoint.rootCollidableB))
                hit.Reference = pRef->formID;
            hit.Layer = cPoint.rootCollidableB->filterInfo & 0x7F;
        }
        for (int axis = 0; axis < 3; ++axis)
            hit.Point[axis] = cPoint.position[axis] * cHavokToGame;
        hits.push_back(hit);
    }
    return hits;
}

uint32_t CollectorGroup(const void* apCollector) noexcept
{
    return apCollector ? static_cast<const SwingCollector*>(apCollector)->filterGroup : 0;
}

uint32_t GroupOf(Actor& aActor) noexcept
{
    using TFilterInfo = uint32_t*(Actor*, uint32_t*);
    POINTER_SKYRIMSE(TFilterInfo, s_filterInfo, 37560); // Actor::GetCollisionFilterInfo
    uint32_t filterInfo{};
    s_filterInfo(&aActor, &filterInfo);
    return filterInfo >> 16;
}

Cast CastFor(Actor& aActor, Reach aReach) noexcept
{
    using TGetWorld = void*(TESObjectCELL*);
    using TGetCollector = SwingCollector*(int, bool);
    using TSetGroup = void(SwingCollector*, uint32_t);
    using TLinearCast = bool(SwingCollector*, void*, const float*, const float*, float);
    POINTER_SKYRIMSE(TGetWorld, s_getWorld, 18995);         // TESObjectCELL::GetbhkWorld
    POINTER_SKYRIMSE(TGetCollector, s_getCollector, 25925); // GetAllCdPointCollector
    POINTER_SKYRIMSE(TSetGroup, s_setGroup, 25932);         // the phantom's filter group
    POINTER_SKYRIMSE(TLinearCast, s_linearCast, 25931);     // the phantom swept from origin along direction

    Cast cast;
    void* pWorld = aActor.parentCell ? s_getWorld(aActor.parentCell) : nullptr;
    SwingCollector* pCollector = s_getCollector(0, true);
    if (!pWorld || !pCollector)
        return cast;

    NiPoint3 eye{}, direction{};
    aActor.GetEyeVector(eye, direction, false);
    // As 38628: the phantom ignores the swinger's own group.
    if (const uint32_t cGroup = GroupOf(aActor); pCollector->filterGroup != cGroup)
        s_setGroup(pCollector, cGroup);

    constexpr uint32_t cReachIds[]{375309, 375315, 375312}; // fObjectHitWeaponReach, ...TwoHandReach, ...H2HReach
    cast.Reach = FloatSetting(cReachIds[static_cast<int>(aReach)]);
    cast.Radius = FloatSetting(370082); // fHitCasterSizeSmall: the phantom's sphere (25925 -> 25930)
    cast.Eye[0] = eye.x, cast.Eye[1] = eye.y, cast.Eye[2] = eye.z;
    cast.Direction[0] = direction.x, cast.Direction[1] = direction.y, cast.Direction[2] = direction.z;
    cast.Cast = true;
    if (s_linearCast(pCollector, pWorld, &eye.x, &direction.x, cast.Reach))
        cast.Hits = HitsOf(pCollector);
    return cast;
}
} // namespace GameSwingCast
