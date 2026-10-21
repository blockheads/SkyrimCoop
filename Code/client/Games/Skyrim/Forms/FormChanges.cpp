#include <Forms/FormChanges.h>

#include <Forms/TESForm.h>
#include <HookSwitch.h>

#include <array>
#include <atomic>

namespace FormChanges
{
namespace
{
std::array<std::atomic<Watcher>, 4> s_watchers{};

void Notify(const TESForm* apForm, uint32_t aFlags, bool aForced, bool aRecorded) noexcept
{
    if (!apForm)
        return;
    for (const auto& cSlot : s_watchers)
        if (const Watcher cWatcher = cSlot.load(std::memory_order_acquire))
            cWatcher(*apForm, aFlags, aForced, aRecorded);
}

// Both return whether the funnel recorded the change (the forced one in its low byte).
TP_THIS_FUNCTION(TAddChange, bool, TESForm, uint32_t);
TP_THIS_FUNCTION(TAddChangeForced, uint64_t, TESForm, uint32_t);
TAddChange* RealAddChange = nullptr;
TAddChangeForced* RealAddChangeForced = nullptr;

bool TP_MAKE_THISCALL(HookAddChange, TESForm, uint32_t aFlags)
{
    const bool cRecorded = TiltedPhoques::ThisCall(RealAddChange, apThis, aFlags);
    Notify(apThis, aFlags, false, cRecorded);
    return cRecorded;
}

uint64_t TP_MAKE_THISCALL(HookAddChangeForced, TESForm, uint32_t aFlags)
{
    const uint64_t cResult = TiltedPhoques::ThisCall(RealAddChangeForced, apThis, aFlags);
    Notify(apThis, aFlags, true, (cResult & 0xFF) != 0);
    return cResult;
}

TiltedPhoques::Initializer s_formChangesHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TAddChange, s_addChange, 14607);
        POINTER_SKYRIMSE(TAddChangeForced, s_addChangeForced, 14608);
        RealAddChange = s_addChange.Get();
        RealAddChangeForced = s_addChangeForced.Get();
        TP_HOOK(&RealAddChange, HookAddChange);
        TP_HOOK(&RealAddChangeForced, HookAddChangeForced);
    });
} // namespace

bool Watch(Watcher aWatcher) noexcept
{
    for (auto& slot : s_watchers)
    {
        Watcher expected = nullptr;
        if (slot.compare_exchange_strong(expected, aWatcher, std::memory_order_acq_rel))
            return true;
    }
    return false;
}

void Unwatch(Watcher aWatcher) noexcept
{
    for (auto& slot : s_watchers)
    {
        Watcher expected = aWatcher;
        slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }
}
} // namespace FormChanges
