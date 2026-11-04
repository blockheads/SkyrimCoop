#include <Games/Skyrim/ActionClips.h>

#include <AI/AIProcess.h>
#include <Actor.h>
#include <Misc/BSFixedString.h>
#include <Misc/MiddleProcess.h>

namespace
{
// The action slot of a process's animation sets (RunActionAnimationLoadedCallback's kind).
constexpr uint32_t cActionSet = 3;

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
    m_pProcess->middleProcess->animationSets->actionLoad = nullptr;
    m_done->store(true, std::memory_order_release);
}
} // namespace

namespace ActionClips
{
std::shared_ptr<std::atomic<bool>> Request(Actor& aActor, const char* acEvent) noexcept
{
    using TCancel = void(AIProcess*);
    using TAssign = void*(void**, ClipsLoaded*);
    using TLoad = void*(BSFixedString*, Actor*, uint32_t, uint32_t, ClipsLoaded*, BSRecursiveLock*, bool);
    POINTER_SKYRIMSE(TCancel, CancelLoad, 39258);
    POINTER_SKYRIMSE(TAssign, AssignCallback, 39732);
    POINTER_SKYRIMSE(TLoad, LoadEventClips, 43621);

    AIProcess* pProcess = aActor.currentProcess;
    if (!pProcess || !pProcess->middleProcess || !pProcess->highProcess || !pProcess->middleProcess->animationSets || !acEvent || !*acEvent)
        return nullptr;

    CancelLoad(pProcess);
    auto done = std::make_shared<std::atomic<bool>>(false);
    auto* pMiddle = pProcess->middleProcess;
    auto* pHigh = pProcess->highProcess;
    BSFixedString event(acEvent);
    void* pLoad = nullptr;
    {
        BSScopedLock<BSRecursiveLock> _{pMiddle->animationLock};
        AssignCallback(&pHigh->actionClipsLoaded, new ClipsLoaded(*pProcess, done));
        pLoad = LoadEventClips(&event, &aActor, cActionSet, 1, static_cast<ClipsLoaded*>(pHigh->actionClipsLoaded), &pMiddle->animationLock, true);
        pMiddle->animationSets->actionLoad = pLoad;
        if (!pLoad)
            AssignCallback(&pHigh->actionClipsLoaded, nullptr);
    }
    event.Release();
    return pLoad ? done : nullptr;
}
} // namespace ActionClips
