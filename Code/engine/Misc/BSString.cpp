#include <Misc/BSString.h>
#include <VersionDb.h>

bool BSString::Set(const char* acpText, uint32_t aLength) noexcept
{
    using TSet = bool(BSString*, const char*, uint32_t);
    POINTER_SKYRIMSE(TSet, s_set, 439876);
    return s_set(this, acpText, aLength);
}
