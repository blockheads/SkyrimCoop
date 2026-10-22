#include <Forms/FormChanges.h>

#include <Forms/TESForm.h>
#include <HookSwitch.h>
#include <SaveLoad.h>

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
using TAddChange = bool(TESForm*, uint32_t);
using TAddChangeForced = uint64_t(TESForm*, uint32_t);
TAddChange* RealAddChange = nullptr;
TAddChangeForced* RealAddChangeForced = nullptr;

bool HookAddChange(TESForm* apThis, uint32_t aFlags)
{
    const bool cRecorded = RealAddChange(apThis, aFlags);
    Notify(apThis, aFlags, false, cRecorded);
    return cRecorded;
}

uint64_t HookAddChangeForced(TESForm* apThis, uint32_t aFlags)
{
    const uint64_t cResult = RealAddChangeForced(apThis, aFlags);
    Notify(apThis, aFlags, true, (cResult & 0xFF) != 0);
    return cResult;
}

// The change records' own remove (35511, (map, form id, flags)): TESForm::RemoveChange (14609) calls it, and so does
// StoreChangesOnUnload (35570) for the flags a reference it stores or demotes does not keep. Recorded: the record held
// a flag before and not after.
using TRemoveFlags = bool(void*, uint32_t, uint32_t);
TRemoveFlags* RealRemoveFlags = nullptr;

bool HookRemoveFlags(void* apThis, uint32_t aFormId, uint32_t aFlags)
{
    const BGSSaveLoadGame* pSaveLoad = BGSSaveLoadGame::Get();
    const TESForm* pForm = TESForm::GetById(aFormId);
    const bool cHeld = pForm && pSaveLoad && pSaveLoad->HasChange(*pForm, aFlags);
    const bool cResult = RealRemoveFlags(apThis, aFormId, aFlags);
    if (cHeld)
        Notify(pForm, aFlags, false, !pSaveLoad->HasChange(*pForm, aFlags));
    return cResult;
}

Hooks::Initializer s_formChangesHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TAddChange, s_addChange, 14607);
        POINTER_SKYRIMSE(TAddChangeForced, s_addChangeForced, 14608);
        POINTER_SKYRIMSE(TRemoveFlags, s_removeFlags, 35511);
        RealAddChange = s_addChange.Get();
        RealAddChangeForced = s_addChangeForced.Get();
        RealRemoveFlags = s_removeFlags.Get();
        HOOK(&RealAddChange, HookAddChange);
        HOOK(&RealAddChangeForced, HookAddChangeForced);
        HOOK(&RealRemoveFlags, HookRemoveFlags);
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
