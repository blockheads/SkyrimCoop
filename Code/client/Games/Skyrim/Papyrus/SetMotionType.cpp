#include <Games/ScriptMotion.h>

#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <TESObjectREFR.h>

ScriptMotion::Store& ScriptMotion::Get() noexcept
{
    static Store s_store;
    return s_store;
}

namespace
{
// SkyrimScript::SetMotionTypeFunctor (56232 makes it, 56332 runs it after render).
struct SetMotionTypeFunctor
{
    void* vtable;
    uint32_t refCount;
    uint32_t stackId;
    uint32_t reference; // its handle
    uint32_t motionType;
    void* vm;
    bool allowActivate;
};
static_assert(offsetof(SetMotionTypeFunctor, reference) == 0x10);
static_assert(offsetof(SetMotionTypeFunctor, motionType) == 0x14);
static_assert(offsetof(SetMotionTypeFunctor, allowActivate) == 0x20);

using TRun = void*(SetMotionTypeFunctor*, void*);
TRun* RealRun = nullptr;

// SetMotionTypeFunctor::Run (56332): the script's motion type, remembered for the host's world row.
void* HookRun(SetMotionTypeFunctor* apThis, void* apResult)
{
    // Only a call that changes it: with no 3D, 56332 reports the error and changes nothing.
    if (TESObjectREFR* pObject = TESObjectREFR::GetByHandle(apThis->reference); pObject && pObject->GetNiNode())
        ScriptMotion::Get().Remember(pObject->formID, ScriptMotion::Pack(apThis->motionType, apThis->allowActivate));
    return RealRun(apThis, apResult);
}

Hooks::Initializer s_setMotionTypeHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(56332, &RealRun, HookRun);
});
} // namespace
