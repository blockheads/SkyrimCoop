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
