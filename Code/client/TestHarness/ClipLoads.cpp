// The animation file manager's queued clip loads, and a hold on its job (63980) so a load stays queued: whose lock each
// names, and whether that actor is gone (docs/re/systems/animation-file-loads.md).
#include "ClipLoads.h"
#include "Commands.h"

#include <AI/AIProcess.h>
#include <Actor.h>
#include <Games/Skyrim/AnimationFileManager.h>
#include <Games/TES.h>
#include <Misc/MiddleProcess.h>
#include <MinHook.h>
#include <VersionDb.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace TestHarness::ClipLoads
{
namespace
{
using TUpdate = uint64_t(AnimationFileManager*);
TUpdate* RealUpdate{};
std::atomic<bool> s_installed{};
std::atomic<bool> s_held{};
std::mutex s_ownersLock;
std::unordered_map<const void*, uint32_t> s_owners; // a process's animation lock to its actor, as last seen

// Job_Animation_files' update: skipped while held, so nothing queued is loaded or called back.
uint64_t HookUpdate(AnimationFileManager* apManager)
{
    return s_held.load(std::memory_order_acquire) ? 0 : RealUpdate(apManager);
}

bool Install()
{
    if (s_installed)
        return true;
    VersionDbPtr<void> update(63980);
    if (MH_CreateHook(update.GetPtr(), reinterpret_cast<void*>(&HookUpdate), reinterpret_cast<void**>(&RealUpdate)) != MH_OK ||
        MH_QueueEnableHook(update.GetPtr()) != MH_OK || MH_ApplyQueued() != MH_OK)
        return false;
    s_installed = true;
    return true;
}

// The actor whose middle process holds this lock, and whether that is still so.
nlohmann::json Owner(const BSRecursiveLock* apLock)
{
    uint32_t owner = 0;
    {
        std::scoped_lock _{s_ownersLock};
        if (const auto it = s_owners.find(apLock); it != s_owners.end())
            owner = it->second;
    }
    if (!owner)
        return {{"owner", 0}, {"owner_gone", false}};
    const auto* pActor = Cast<Actor>(TESForm::GetById(owner));
    const auto* pMiddle = pActor && pActor->currentProcess ? pActor->currentProcess->middleProcess : nullptr;
    const bool cGone = !pActor || pActor->IsDeleted() || !pMiddle || &pMiddle->animationLock != apLock;
    return {{"owner", owner}, {"owner_gone", cGone}};
}

nlohmann::json LoadJson(const AnimationFileLoad& acLoad, const char* acpStage)
{
    nlohmann::json load = Owner(acLoad.lock);
    load["stage"] = acpStage;
    load["kind"] = acLoad.kind;
    load["live"] = acLoad.live;
    return load;
}

// {"cmd": "clip_loads", "hold": true|false}: the queued clip loads (pending, loading, finished) with their lock's
// actor; `hold` stops or restarts the manager's job in this game.
nlohmann::json Command(Harness&, const nlohmann::json& acRequest)
{
    if (!Install())
        return Error("could not hook the animation file manager's update (63980)");
    if (acRequest.contains("hold"))
        s_held = acRequest["hold"].get<bool>();
    Poll();
    auto loads = nlohmann::json::array();
    if (AnimationFileManager* pManager = AnimationFileManager::Get())
    {
        BSScopedLock<BSRecursiveLock> _{pManager->lock};
        for (uint32_t i = 0; pManager->pending.data && i < pManager->pending.length; ++i)
            if (const auto* pLoad = pManager->pending.data[i].load)
                loads.push_back(LoadJson(*pLoad, "pending"));
        if (pManager->loading)
            loads.push_back(LoadJson(*pManager->loading, "loading"));
        for (uint32_t i = 0; pManager->finished.data && i < pManager->finished.length; ++i)
            if (const auto* pLoad = pManager->finished.data[i])
                loads.push_back(LoadJson(*pLoad, "finished"));
    }
    return {{"ok", true}, {"held", s_held.load()}, {"loads", loads}};
}

constexpr TestHarness::Command cCommands[] = {{"clip_loads", true, &Command}};
} // namespace

bool Watching() noexcept
{
    return s_installed.load(std::memory_order_relaxed);
}

void Poll()
{
    ProcessLists* pLists = ProcessLists::Get();
    if (!pLists)
        return;
    std::scoped_lock _{s_ownersLock};
    if (s_owners.size() > 4096)
        s_owners.clear();
    for (const auto* pHandles : {&pLists->highActorHandleArray, &pLists->middleHighActorHandleArray})
        for (uint32_t i = 0; pHandles->data && i < pHandles->length; ++i)
        {
            const auto* pActor = Cast<Actor>(TESObjectREFR::GetByHandle(pHandles->data[i]));
            if (const auto* pMiddle = pActor && pActor->currentProcess ? pActor->currentProcess->middleProcess : nullptr)
                s_owners[&pMiddle->animationLock] = pActor->formID;
        }
}

std::span<const TestHarness::Command> Commands()
{
    return cCommands;
}
} // namespace TestHarness::ClipLoads
