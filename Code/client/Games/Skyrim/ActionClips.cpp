#include <Games/Skyrim/ActionClips.h>

#include <AI/AIProcess.h>
#include <Actor.h>
#include <Misc/BSFixedString.h>
#include <Misc/MiddleProcess.h>

namespace
{
// IAnimationSetCallbackFunctor: the engine holds it by its count and calls Loaded on the loader's side.
struct ClipsLoaded
{
    ClipsLoaded(AIProcess& aProcess, std::shared_ptr<std::atomic<bool>> aDone) noexcept
        : m_pProcess(&aProcess)
        , m_done(std::move(aDone))
    {
    }
    virtual ~ClipsLoaded() = default;
    virtual void Loaded(uint32_t aKind, AnimationClipSet* apSet);

    volatile LONG m_refCount{0}; // 08
    AIProcess* m_pProcess;
    std::shared_ptr<std::atomic<bool>> m_done;
};

void ClipsLoaded::Loaded(uint32_t aKind, AnimationClipSet* apSet)
{
    using TKeepSet = void(AIProcess*, uint32_t, AnimationClipSet*);
    using TReleaseSet = void(AnimationClipSet*, bool);
    POINTER_SKYRIMSE(TKeepSet, KeepSet, 40055);
    POINTER_SKYRIMSE(TReleaseSet, ReleaseSet, 38275);

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
    {
        BSScopedLock<BSRecursiveLock> _{pMiddle->animationLock};
        void** ppCallback = aSet == kInstant ? &pMiddle->instantClipsLoaded : &pHigh->actionClipsLoaded;
        const auto cAssign = aSet == kInstant ? AssignMiddleCallback.Get() : AssignHighCallback.Get();
        cAssign(ppCallback, new ClipsLoaded(*pProcess, done));
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
