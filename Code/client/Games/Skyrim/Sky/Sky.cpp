#include "Sky.h"
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

using TSetWeather = void(Sky*, TESWeather* apWeather, bool abOverride, bool abAccelerate);
using TForceWeather = void(Sky*, TESWeather* apWeather, bool abOverride);

static TSetWeather* RealSetWeather = nullptr;
static TForceWeather* RealForceWeather = nullptr;

Sky* Sky::Get() noexcept
{
    using SkyGet = Sky*();

    POINTER_SKYRIMSE(SkyGet, skyGet, 13878);

    return skyGet.Get()();
}

void Sky::SetWeather(TESWeather* apWeather) noexcept
{
    // TODO: verify the use of these bools
    RealSetWeather(this, apWeather, true, true);
}

void Sky::ForceWeather(TESWeather* apWeather) noexcept
{
    // TODO: verify the use of abOverride
    RealForceWeather(this, apWeather, true);
}

void Sky::ReleaseWeatherOverride() noexcept
{
    using TReleaseWeatherOverride = void(Sky*);
    POINTER_SKYRIMSE(TReleaseWeatherOverride, releaseWeatherOverride, 26244);

    releaseWeatherOverride(this);
}

void Sky::ResetWeather() noexcept
{
    using TResetWeather = void(Sky*);
    POINTER_SKYRIMSE(TResetWeather, resetWeather, 26242);

    resetWeather(this);
}

TESWeather* Sky::GetWeather() const noexcept
{
    return pCurrentWeather;
}

void HookSetWeather(Sky* apThis, TESWeather* apWeather, bool abOverride, bool abAccelerate)
{
    RealSetWeather(apThis, apWeather, abOverride, abAccelerate);
}

void HookForceWeather(Sky* apThis, TESWeather* apWeather, bool abOverride)
{
    RealForceWeather(apThis, apWeather, abOverride);
}

using TUpdateWeather = void(Sky*);
static TUpdateWeather* RealUpdateWeather = nullptr;

void HookUpdateWeather(Sky* apThis)
{
    RealUpdateWeather(apThis);
}

static Hooks::Initializer s_skyHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TSetWeather, setWeather, 26241);
        POINTER_SKYRIMSE(TForceWeather, forceWeather, 26243);
        POINTER_SKYRIMSE(TUpdateWeather, updateWeather, 26231);

        RealSetWeather = setWeather.Get();
        RealForceWeather = forceWeather.Get();
        RealUpdateWeather = updateWeather.Get();

        HOOK(&RealSetWeather, HookSetWeather);
        HOOK(&RealForceWeather, HookForceWeather);
        HOOK(&RealUpdateWeather, HookUpdateWeather);
    });
