#include <Games/TES.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Physics/HavokStep.h>
#include <VersionDb.h>

// Engine bug fix: Havok steps by fMaxTime (1/60) whatever the frame rate; above 60 fps a step longer than the
// frame wakes resting clutter, which creeps. Each frame's step is the frame, as HIGGS and HavokFix do
// (S4, docs/re/systems/physics.md, "Havok's step").

namespace
{
using TUpdatePhysicsTimes = void(float aSeconds, bool aComplex, bool aVats);
TUpdatePhysicsTimes* RealUpdatePhysicsTimes = nullptr;

constexpr float cLongestStep = 1.f / 60.f; // the step Skyrim's physics is tuned to

float& SettingFloat(Setting& aSetting) noexcept
{
    return *reinterpret_cast<float*>(&aSetting.data);
}

// UpdateTimeManager's call (77850): sets this frame's step from fMaxTime(Complex) and the time to simulate.
void HookUpdatePhysicsTimes(float aSeconds, bool aComplex, bool aVats)
{
    POINTER_SKYRIMSE(Setting, s_maxTime, 389102);        // fMaxTime:HAVOK
    POINTER_SKYRIMSE(Setting, s_maxTimeComplex, 389144); // fMaxTimeComplex:HAVOK
    POINTER_SKYRIMSE(Setting, s_maxSteps, 389138);       // uMaxNumPhysicsStepsPerUpdate:HAVOK
    static const float s_complexRatio = SettingFloat(*s_maxTimeComplex.Get()) / SettingFloat(*s_maxTime.Get());

    if (aSeconds > 0.f)
    {
        const auto cMaxSteps = static_cast<uint32_t>(s_maxSteps.Get()->data);
        const float cStep = Physics::HavokStep(aSeconds, cLongestStep, cMaxSteps);
        SettingFloat(*s_maxTime.Get()) = cStep;
        SettingFloat(*s_maxTimeComplex.Get()) = cStep * s_complexRatio;
    }
    RealUpdatePhysicsTimes(aSeconds, aComplex, aVats);
}
} // namespace

static Hooks::Initializer s_havokStepHooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(77850, &RealUpdatePhysicsTimes, HookUpdatePhysicsTimes); // bhkWorld's per-frame physics times
});
