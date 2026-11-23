#include "Sky.h"
#include <Games/TES.h>
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

void Sky::FadeTo(TESWeather* apWeather, bool aOverride) noexcept
{
    RealSetWeather(this, apWeather, aOverride, false);
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

// The player's weather region, picked on each cell event (PlayerRegionState::Handle 41264).
struct PlayerRegionState
{
    uint8_t unk0[0x48];
    TESRegion* pLastKnownWeatherRegion; // 48
};
static_assert(offsetof(PlayerRegionState, pLastKnownWeatherRegion) == 0x48);

TESRegion* Sky::PlayerWeatherRegion() noexcept
{
    // An interior without a sky region falls back to the player's, as 26231 does.
    if (const TES* pTes = TES::Get(); pTes && pTes->interiorCell)
    {
        using TGetSkyRegion = TESRegion*(TESObjectCELL*);
        POINTER_SKYRIMSE(TGetSkyRegion, getSkyRegion, 19007);
        if (TESRegion* pRegion = getSkyRegion(pTes->interiorCell))
            return pRegion;
    }
    POINTER_SKYRIMSE(PlayerRegionState*, regionState, 400477);
    const PlayerRegionState* pState = *regionState.Get();
    return pState ? pState->pLastKnownWeatherRegion : nullptr;
}

bool TESRegion::HasWeather(const TESWeather* apWeather) const noexcept
{
    using THasWeather = bool(const TESRegion*, const TESWeather*);
    POINTER_SKYRIMSE(THasWeather, hasWeather, 16450);
    return hasWeather(this, apWeather);
}

void TESRegion::SetCurrentWeather(TESWeather* apShown) noexcept
{
    using TSetCurrentWeather = void(TESRegion*, TESWeather*);
    POINTER_SKYRIMSE(TSetCurrentWeather, setCurrentWeather, 16448);
    setCurrentWeather(this, apShown);
}

void HookSetWeather(Sky* apThis, TESWeather* apWeather, bool abOverride, bool abAccelerate)
{
    if (!abOverride)
        Sky::s_pSetWeather = apWeather;
    RealSetWeather(apThis, apWeather, abOverride, abAccelerate);
}

void HookForceWeather(Sky* apThis, TESWeather* apWeather, bool abOverride)
{
    if (!abOverride)
        Sky::s_pSetWeather = apWeather;
    RealForceWeather(apThis, apWeather, abOverride);
}

using TUpdateWeather = void(Sky*);
static TUpdateWeather* RealUpdateWeather = nullptr;

void HookUpdateWeather(Sky* apThis)
{
    // A held override never ages out (26231 drops one after the climate's interval, or at once when the hour went
    // back: a guest's clock set to the host's), once its fade is done.
    if (Sky::s_pHeldWeather && apThis->pOverrideWeather == Sky::s_pHeldWeather && !apThis->pLastWeather)
        apThis->lastWeatherUpdate = apThis->currentGameHour;
    RealUpdateWeather(apThis);
    if (Sky::s_pSetWeather && Sky::PlayerWeatherRegion())
        Sky::s_pSetWeather = nullptr;
}

static Hooks::Initializer s_skyHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(26241, &RealSetWeather, HookSetWeather);
        HOOK_ID(26243, &RealForceWeather, HookForceWeather);
        HOOK_ID(26231, &RealUpdateWeather, HookUpdateWeather);
    });
