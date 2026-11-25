#include <BSTimer.h>
#include <Camera/PlayerCamera.h>
#include <Events/TimeScaleEvent.h>
#include <Games/ClockWishes.h>
#include <HookSwitch.h>
#include <World.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

using TSetGlobalTimeMultiplier = void(BSTimer*, float aMultiplier, bool aArg2);
static TSetGlobalTimeMultiplier* RealSetGlobalTimeMultiplier = nullptr;

namespace
{
ClockWishes s_wishes;
} // namespace

BSTimer* BSTimer::Get() noexcept
{
    POINTER_SKYRIMSE(BSTimer, s_instance, 410196);
    return s_instance.Get();
}

void BSTimer::SetSharedMultiplier(float aMultiplier) noexcept
{
    s_wishes.Shared = aMultiplier;
    RealSetGlobalTimeMultiplier(Get(), s_wishes.Clock(), false);
}

// A kill cam (its camera shot's globalTimeMult), Slow Time, a bow zoom, `sgtm`: every game slows with it. The down
// camera's 0.8 (50714, its exit 50715, BeginGetUp 40150) is asked with the camera down: this game's alone.
void HookSetGlobalTimeMultiplier(BSTimer* apThis, float aMultiplier, bool aArg2)
{
    const PlayerCamera* pCamera = PlayerCamera::Get();
    if (s_wishes.Ask(aMultiplier, pCamera && pCamera->IsDown()) && World::Get().GetTransport().IsConnected())
        World::Get().GetRunner().Trigger(TimeScaleEvent{aMultiplier});
    RealSetGlobalTimeMultiplier(apThis, s_wishes.Clock(), aArg2);
}

static Hooks::Initializer s_timerHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(68245, &RealSetGlobalTimeMultiplier, HookSetGlobalTimeMultiplier); // BSTimer::SetGlobalTimeMultiplier
    });
