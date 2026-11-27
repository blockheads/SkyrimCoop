#include <NetImmerse/NiAVObject.h>
#include <NetImmerse/NiUpdateData.h>
#include <VersionDb.h>

void NiAVObject::Update(uint32_t aFlags) noexcept
{
    using TUpdate = void(NiAVObject*, NiUpdateData*);
    POINTER_SKYRIMSE(TUpdate, s_update, 70251);
    NiUpdateData data{.flags = aFlags};
    s_update.Get()(this, &data);
}

bool NiAVObject::SetMotionType(uint32_t aMotionType, bool aRecurse, bool aForce, bool aAllowActivate) noexcept
{
    using TSetMotionType = bool(NiAVObject*, uint32_t, bool aRecurse, bool aForce, bool aAllowActivate);
    POINTER_SKYRIMSE(TSetMotionType, s_setMotionType, 77866);
    return s_setMotionType.Get()(this, aMotionType, aRecurse, aForce, aAllowActivate);
}
