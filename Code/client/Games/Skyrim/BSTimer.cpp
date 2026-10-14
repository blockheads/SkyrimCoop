#include <TiltedOnlinePCH.h>

#include <BSTimer.h>
#include <Events/TimeScaleEvent.h>
#include <HookSwitch.h>
#include <World.h>

TP_THIS_FUNCTION(TSetGlobalTimeMultiplier, void, BSTimer, float aMultiplier, bool aArg2);
static TSetGlobalTimeMultiplier* RealSetGlobalTimeMultiplier = nullptr;

namespace
{
float s_wanted = 1.f; // what this game's engine last asked for
float s_shared = 1.f; // the other players' slowest
} // namespace

BSTimer* BSTimer::Get() noexcept
{
    POINTER_SKYRIMSE(BSTimer, s_instance, 410196);
    return s_instance.Get();
}

void BSTimer::SetSharedMultiplier(float aMultiplier) noexcept
{
    s_shared = aMultiplier;
    TiltedPhoques::ThisCall(RealSetGlobalTimeMultiplier, Get(), std::min(s_wanted, s_shared), false);
}

// A kill cam (its camera shot's globalTimeMult), Slow Time, a bow zoom, `sgtm`: every game slows with it.
void TP_MAKE_THISCALL(HookSetGlobalTimeMultiplier, BSTimer, float aMultiplier, bool aArg2)
{
    if (aMultiplier != s_wanted)
    {
        s_wanted = aMultiplier;
        if (World::Get().GetTransport().IsConnected())
            World::Get().GetRunner().Trigger(TimeScaleEvent{aMultiplier});
    }
    TiltedPhoques::ThisCall(RealSetGlobalTimeMultiplier, apThis, std::min(s_wanted, s_shared), aArg2);
}

static TiltedPhoques::Initializer s_timerHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TSetGlobalTimeMultiplier, s_setGlobalTimeMultiplier, 68245); // BSTimer::SetGlobalTimeMultiplier

        RealSetGlobalTimeMultiplier = s_setGlobalTimeMultiplier.Get();

        TP_HOOK(&RealSetGlobalTimeMultiplier, HookSetGlobalTimeMultiplier);
    });
