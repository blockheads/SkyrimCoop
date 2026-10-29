#include "UpdateTrace.h"
#include "TestHarness.h"

#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Misc/GameVM.h>
#include <Plugin.h>
#include <SaveLoad.h>

#include <atomic>
#include <bit>
#include <spdlog/spdlog.h>

namespace TestHarness::UpdateTrace
{
namespace
{
std::atomic<uint64_t> s_updates{}, s_frozen{}, s_saving{}, s_loading{}, s_pollOverlaps{}, s_pollConcurrent{},
    s_updateOverlaps{};
std::atomic<uint32_t> s_updatesActive{}, s_pollsActive{}, s_pollThread{};
using TUpdate = void(Plugin*);
using TPoll = void();
TUpdate* RealUpdate = std::bit_cast<TUpdate*>(&Plugin::Update);
TPoll* RealPoll = &TestHarness::Poll;

void HookUpdate(Plugin* apPlugin)
{
    if (s_updatesActive.fetch_add(1))
        ++s_updateOverlaps;
    if (s_pollsActive.load())
    {
        ++s_pollOverlaps;
        if (s_pollThread.load() != GetCurrentThreadId())
            ++s_pollConcurrent;
    }
    ++s_updates;
    const SkyrimVM* pVM = SkyrimVM::Get();
    if (pVM && pVM->isFrozen)
        if (s_frozen.fetch_add(1) == 0)
            spdlog::warn("UpdateTrace: full plugin update while the VM is frozen (thread {}, active updates {})",
                         GetCurrentThreadId(), s_updatesActive.load());
    if (const BGSSaveLoadGame* pSave = BGSSaveLoadGame::Get())
    {
        if (pSave->globalFlags & BGSSaveLoadGame::kSaveGameSaving)
            ++s_saving;
        if (pSave->globalFlags & BGSSaveLoadGame::kSaveGameLoading)
            ++s_loading;
    }
    RealUpdate(apPlugin);
    --s_updatesActive;
}

void HookPoll()
{
    s_pollThread = GetCurrentThreadId();
    ++s_pollsActive;
    if (s_updatesActive.load())
        ++s_pollOverlaps;
    RealPoll();
    --s_pollsActive;
}

Hooks::Initializer s_updateTrace([] {
    auto& hooks = Hooks::FunctionHookManager::GetInstance();
    hooks.Add(&RealUpdate, &HookUpdate);
    hooks.Add(&RealPoll, &HookPoll);
});
} // namespace

Counts Read() noexcept
{
    return {s_updates.load(),      s_frozen.load(),         s_saving.load(),        s_loading.load(),
            s_pollOverlaps.load(), s_pollConcurrent.load(), s_updateOverlaps.load()};
}
} // namespace TestHarness::UpdateTrace
