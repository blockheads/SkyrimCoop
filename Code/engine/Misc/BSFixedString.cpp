#include <Misc/BSFixedString.h>
#include <VersionDb.h>

BSFixedString::BSFixedString()
    : data{nullptr}
{
}

BSFixedString::BSFixedString(const char* acpData)
{
    using TConstructor = void(BSFixedString*, const char*);

    POINTER_SKYRIMSE(TConstructor, s_constructor, 69161);

    s_constructor(this, acpData);
}

BSFixedString::~BSFixedString()
{
    // Release();
}

void BSFixedString::Set(const char* acpData)
{
    using TSet = void(BSFixedString*, const char*);

    POINTER_SKYRIMSE(TSet, s_set, 69165);

    s_set(this, acpData);
}

void BSFixedString::Release() noexcept
{
    using TRelease = void(BSFixedString*);

    POINTER_SKYRIMSE(TRelease, s_release, 69192);

    s_release(this);
}
