#include <Services/Guest/GuestClock.h>
#include <HookSwitch.h>
#include <TimeManager.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>
#include <Events/FastTravelEvent.h>
#include <PlayerCharacter.h>
#include <TESObjectREFR.h>
#include <World.h>

#include <utility>

TimeData* TimeData::Get() noexcept
{
    POINTER_SKYRIMSE(TimeData*, s_instance, 400447);
    return *(s_instance.Get());
}

using TSimulateTime = void(TimeData*, float);
static TSimulateTime* RealSimulateTime;
static thread_local bool s_fastTravel = false;
using TFastTravel = uintptr_t(PlayerCharacter*, TESObjectREFR*, bool);
static TFastTravel* RealFastTravel;

static uintptr_t HookFastTravel(PlayerCharacter* apPlayer, TESObjectREFR* apMarker, bool aAutoSave)
{
    const bool cPrevious = std::exchange(s_fastTravel, true);
    const auto cResult = RealFastTravel(apPlayer, apMarker, aAutoSave);
    s_fastTravel = cPrevious;
    return cResult;
}

void TimeData::Advance(float aGameHours) noexcept
{
    if (TimeScale && TimeScale->f > 0.f)
        RealSimulateTime(this, aGameHours * 3600.f / TimeScale->f);
}

// in SP mode we let the client handle its own time simulation
void HookSimulateTime(TimeData* apData, float aMultiplier)
{
    if (s_fastTravel && !GuestClock::GameClockRuns() && apData->TimeScale)
        World::Get().GetRunner().Trigger(FastTravelEvent{aMultiplier * apData->TimeScale->f / 3600.f});
    if (GuestClock::GameClockRuns())
    {
        RealSimulateTime(apData, aMultiplier);
    }
}

static Hooks::Initializer s_loadingScreenHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(40445, &RealFastTravel, HookFastTravel);
        HOOK_ID(36291, &RealSimulateTime, HookSimulateTime);
    });
