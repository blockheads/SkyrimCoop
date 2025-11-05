#include <WorldBase.h>
#include <TiltedCore/Stl.hpp>

WorldBase& WorldBase::Get() noexcept
{
    BASE_ASSERT(s_pInstance != nullptr, "World instance is null!");
    return *s_pInstance;
}

void WorldBase::Set(WorldBase* apWorld) noexcept
{
    s_pInstance = apWorld;
}

void WorldBase::Destroy() noexcept
{
    if (s_pInstance)
    {
        delete s_pInstance;
        s_pInstance = nullptr;
    }
}
