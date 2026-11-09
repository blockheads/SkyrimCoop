#include <Games/Skyrim/ActionClips.h>

#include <AI/AIProcess.h>
#include <Actor.h>
#include <Misc/BSFixedString.h>
#include <Misc/MiddleProcess.h>
#include <Forms/TESForm.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace
{
using TReleaseSet = void(AnimationClipSet*, bool);
using TAssignCallback = void*(void**, void*);

// A copy's movement idle sets (kind 47): its owner's selector holds its own; a copy runs no movement AI.
struct HeldSet
{
    AnimationClipSet Set{};
    void* Callback = nullptr; // the load's, by its count (39731)
};
std::mutex s_heldLock; // never held across a load: the loader can call back at once, under the animation lock
std::unordered_map<uint32_t, std::unique_ptr<HeldSet>> s_held;

void Release(HeldSet& aHeld)
{
    POINTER_SKYRIMSE(TReleaseSet, ReleaseSet, 38275);
    POINTER_SKYRIMSE(TAssignCallback, AssignCallback, 39731);
    if (aHeld.Set.size)
        ReleaseSet(&aHeld.Set, true);
    AssignCallback(&aHeld.Callback, nullptr);
}

// IAnimationSetCallbackFunctor: the engine holds it by its count and calls Loaded on the loader's side.
struct ClipsLoaded
{
    ClipsLoaded(AIProcess& aProcess, uint32_t aFormId, std::shared_ptr<std::atomic<bool>> aDone) noexcept
        : m_pProcess(&aProcess)
        , m_formId(aFormId)
        , m_done(std::move(aDone))
    {
    }
    virtual ~ClipsLoaded() = default;
    virtual void Loaded(uint32_t aKind, AnimationClipSet* apSet);

    volatile LONG m_refCount{0}; // 08
    AIProcess* m_pProcess;
    uint32_t m_formId;
    std::shared_ptr<std::atomic<bool>> m_done;
};

void ClipsLoaded::Loaded(uint32_t aKind, AnimationClipSet* apSet)
{
    using TKeepSet = void(AIProcess*, uint32_t, AnimationClipSet*);
    POINTER_SKYRIMSE(TKeepSet, KeepSet, 40055);
    POINTER_SKYRIMSE(TReleaseSet, ReleaseSet, 38275);

    if (aKind == ActionClips::kMovement)
    {
        // Kept as 40055 keeps a process's: the set's contents swapped in, the old ones released.
        std::scoped_lock _{s_heldLock};
        auto& pHeld = s_held[m_formId];
        if (!pHeld)
            pHeld = std::make_unique<HeldSet>();
        auto& held = *pHeld;
        std::swap(held.Set.data, apSet->data);
        std::swap(held.Set.capacity, apSet->capacity);
        std::swap(held.Set.size, apSet->size);
        if (apSet->size)
            ReleaseSet(apSet, true);
        m_done->store(true, std::memory_order_release);
        return;
    }
    KeepSet(m_pProcess, aKind, apSet);
    if (apSet->size)
        ReleaseSet(apSet, true);
    auto* pSets = m_pProcess->middleProcess->animationSets;
    (aKind == ActionClips::kInstant ? pSets->instantLoad : pSets->actionLoad) = nullptr;
    m_done->store(true, std::memory_order_release);
}
} // namespace

namespace ActionClips
{
std::shared_ptr<std::atomic<bool>> Request(Actor& aActor, const char* acEvent, Set aSet) noexcept
{
    using TCancel = void(AIProcess*);
    using TAssign = void*(void**, ClipsLoaded*);
    using TLoad = void*(BSFixedString*, Actor*, uint32_t, uint32_t, ClipsLoaded*, BSRecursiveLock*, bool);
    POINTER_SKYRIMSE(TCancel, CancelLoad, 39258);
    POINTER_SKYRIMSE(TAssign, AssignHighCallback, 39732);
    POINTER_SKYRIMSE(TAssign, AssignMiddleCallback, 39731);
    POINTER_SKYRIMSE(TLoad, LoadEventClips, 43621);

    AIProcess* pProcess = aActor.currentProcess;
    if (!pProcess || !pProcess->middleProcess || !pProcess->highProcess || !pProcess->middleProcess->animationSets || !acEvent || !*acEvent)
        return nullptr;

    auto* pMiddle = pProcess->middleProcess;
    auto* pHigh = pProcess->highProcess;
    // The action's load replaces one under way (39625); an instant interaction's callback is replaced below.
    if (aSet == kAction)
        CancelLoad(pProcess);
    auto done = std::make_shared<std::atomic<bool>>(false);
    BSFixedString event(acEvent);
    void* pLoad = nullptr;
    if (aSet == kMovement)
    {
        HeldSet* pHeld = nullptr;
        {
            std::scoped_lock _{s_heldLock};
            std::erase_if(s_held, [](auto& aEntry) {
                if (TESForm::GetById(aEntry.first))
                    return false;
                Release(*aEntry.second); // its copy is gone
                return true;
            });
            auto& pEntry = s_held[aActor.formID];
            if (!pEntry)
                pEntry = std::make_unique<HeldSet>();
            pHeld = pEntry.get();
        }
        BSScopedLock<BSRecursiveLock> lock{pMiddle->animationLock};
        AssignMiddleCallback(&pHeld->Callback, new ClipsLoaded(*pProcess, aActor.formID, done));
        pLoad = LoadEventClips(&event, &aActor, aSet, 1, static_cast<ClipsLoaded*>(pHeld->Callback), &pMiddle->animationLock, true);
        if (!pLoad)
            AssignMiddleCallback(&pHeld->Callback, nullptr);
    }
    else
    {
        BSScopedLock<BSRecursiveLock> _{pMiddle->animationLock};
        void** ppCallback = aSet == kInstant ? &pMiddle->instantClipsLoaded : &pHigh->actionClipsLoaded;
        const auto cAssign = aSet == kInstant ? AssignMiddleCallback.Get() : AssignHighCallback.Get();
        cAssign(ppCallback, new ClipsLoaded(*pProcess, aActor.formID, done));
        // Its flags as the engine's own loads pass them: the action's 1 (2 for the player), an instant one's 5.
        pLoad = LoadEventClips(&event, &aActor, aSet, aSet == kInstant ? 5 : 1, static_cast<ClipsLoaded*>(*ppCallback), &pMiddle->animationLock, true);
        (aSet == kInstant ? pMiddle->animationSets->instantLoad : pMiddle->animationSets->actionLoad) = pLoad;
        if (!pLoad)
            cAssign(ppCallback, nullptr);
    }
    event.Release();
    return pLoad ? done : nullptr;
}
} // namespace ActionClips
