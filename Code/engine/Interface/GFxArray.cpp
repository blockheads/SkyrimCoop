#include <Interface/GFxArray.h>

#include <VersionDb.h>

// GFxValue::ObjectInterface: GetArraySize 82237, GetElement 82241, GetMember 82245, ObjectRelease 82270,
// RemoveElements 82280; each takes the interface, then the value's data (IDA 1.7.104, as 52890 and 52894 call them).

namespace GFxArray
{
void Release(GFxValue& aValue) noexcept
{
    using TRelease = void(void*, GFxValue*, void*);
    POINTER_SKYRIMSE(TRelease, s_release, 82270);
    if (aValue.type & GFxValue::kManaged)
        s_release(aValue.objectInterface, &aValue, aValue.value.object);
    aValue = {};
}

uint32_t Size(const GFxValue& acArray) noexcept
{
    using TSize = uint32_t(void*, void*);
    POINTER_SKYRIMSE(TSize, s_size, 82237);
    return acArray.objectInterface ? s_size(acArray.objectInterface, acArray.value.object) : 0;
}

std::optional<double> NumberOf(const GFxValue& acArray, uint32_t aIndex, const char* acpName) noexcept
{
    using TGetElement = bool(void*, void*, uint32_t, GFxValue*);
    using TGetMember = bool(void*, void*, const char*, GFxValue*, bool);
    POINTER_SKYRIMSE(TGetElement, s_element, 82241);
    POINTER_SKYRIMSE(TGetMember, s_member, 82245);
    if (!acArray.objectInterface)
        return std::nullopt;
    GFxValue element{};
    if (!s_element(acArray.objectInterface, acArray.value.object, aIndex, &element))
        return std::nullopt;
    GFxValue member{};
    std::optional<double> number;
    if (s_member(element.objectInterface, element.value.object, acpName, &member, (element.type & GFxValue::kTypeMask) == 8) &&
        (member.type & GFxValue::kTypeMask) == GFxValue::kNumber)
        number = member.value.number;
    Release(member);
    Release(element);
    return number;
}

void Remove(const GFxValue& acArray, uint32_t aIndex) noexcept
{
    using TRemove = bool(void*, void*, uint32_t, int32_t);
    POINTER_SKYRIMSE(TRemove, s_remove, 82280);
    if (acArray.objectInterface)
        s_remove(acArray.objectInterface, acArray.value.object, aIndex, 1);
}
} // namespace GFxArray
