#include <Services/Guest/GuestClock.h>
#include <HookSwitch.h>
#include <TimeManager.h>

TimeData* TimeData::Get() noexcept
{
    POINTER_SKYRIMSE(TimeData*, s_instance, 400447);
    return *(s_instance.Get());
}

using TSimulateTime = void(TimeData*, float);
static TSimulateTime* RealSimulateTime;

void TimeData::Advance(float aGameHours) noexcept
{
    if (TimeScale && TimeScale->f > 0.f)
        RealSimulateTime(this, aGameHours * 3600.f / TimeScale->f);
}

// in SP mode we let the client handle its own time simulation
void HookSimulateTime(TimeData* apData, float aMultiplier)
{
    if (GuestClock::GameClockRuns())
    {
        RealSimulateTime(apData, aMultiplier);
    }
}

static Hooks::Initializer s_loadingScreenHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TSimulateTime, s_SimulateTime, 36291);
        RealSimulateTime = s_SimulateTime.Get();
        HOOK(&RealSimulateTime, HookSimulateTime);
    });
