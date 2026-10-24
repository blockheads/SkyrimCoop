#include <GlobalEvents.h>

#include <DefaultObjectManager.h>
#include <Forms/Script.h>
#include <Forms/TESGlobal.h>
#include <Games/ScriptAssignments.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>

#include <atomic>
#include <mutex>
#include <utility>

struct StatsMenu;

namespace GlobalEvents
{
namespace
{
std::atomic<bool> s_active = false;
std::mutex s_lock;
std::set<uint32_t> s_changed;

void Raise(const TESGlobal* apGlobal)
{
    if (!s_active || !apGlobal || apGlobal->IsConstant())
        return;
    std::scoped_lock lock(s_lock);
    s_changed.insert(apGlobal->formID);
}

// po3-Tweaks Papyrus_SetGlobal (55923); Mod and the Value property call this native through SetValue.
using TSetValue = void(void*, uint32_t, TESGlobal*, float);
TSetValue* RealSetValue = nullptr;

void HookSetValue(void* apVm, uint32_t aStack, TESGlobal* apGlobal, float aValue)
{
    RealSetValue(apVm, aStack, apGlobal, aValue);
    Raise(apGlobal);
}

TESGlobal* Assignment(Script* apScript, uint32_t aOpcode, uint32_t aOffset, bool aSkip, bool aScan)
{
    if (!s_active || !apScript || !apScript->data)
        return nullptr;
    const auto cIndex =
        ScriptAssignments::GlobalTarget({apScript->data, apScript->dataSize}, aOpcode, aOffset, aSkip, aScan);
    if (!cIndex || *cIndex > apScript->referenceCount)
        return nullptr;
    uint32_t index = 0;
    for (const auto* pRef : apScript->references)
        if (++index == *cIndex)
            return pRef ? Cast<TESGlobal>(pRef->form) : nullptr;
    return nullptr;
}

// Interpreter operation 23329: the console and compiled scripts share its 'set' global store.
using TOperation = bool(void*, Script*, uint32_t, TESObjectREFR*, uint32_t*, uint32_t, uint32_t*, uint32_t, bool, bool);
TOperation* RealOperation = nullptr;

bool HookOperation(void* apContext, Script* apScript, uint32_t aOpcode, TESObjectREFR* apTarget, uint32_t* apLength,
                   uint32_t aOffset, uint32_t* apDepth, uint32_t aLine, bool aSkip, bool aScan)
{
    const TESGlobal* pGlobal = Assignment(apScript, aOpcode, aOffset, aSkip, aScan);
    const bool cResult =
        RealOperation(apContext, apScript, aOpcode, apTarget, apLength, aOffset, apDepth, aLine, aSkip, aScan);
    Raise(pGlobal);
    return cResult;
}

// StatsMenu::ModifyPerkPoints (52538) writes the beast perk global directly, without Papyrus or a script.
using TPerkPoints = void(StatsMenu*, int32_t);
TPerkPoints* RealPerkPoints = nullptr;

void HookPerkPoints(StatsMenu* apMenu, int32_t aDelta)
{
    RealPerkPoints(apMenu, aDelta);
    Raise(Cast<TESGlobal>(DefaultObjectManager::GetObject(DefaultObjectManager::kWerewolfAvailablePerks)));
    Raise(Cast<TESGlobal>(DefaultObjectManager::GetObject(DefaultObjectManager::kVampireAvailablePerks)));
}

Hooks::Initializer s_hooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(55923, &RealSetValue, HookSetValue);
    HOOK_ID(23329, &RealOperation, HookOperation);
    HOOK_ID(52538, &RealPerkPoints, HookPerkPoints);
});
} // namespace

void Start() noexcept
{
    Take();
    s_active = true;
}

void Stop() noexcept
{
    s_active = false;
    Take();
}

std::set<uint32_t> Take() noexcept
{
    std::scoped_lock lock(s_lock);
    return std::exchange(s_changed, {});
}
} // namespace GlobalEvents
