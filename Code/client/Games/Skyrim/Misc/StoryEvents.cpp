#include <Misc/StoryEvents.h>
#include <HookSwitch.h>

#include <Actor.h>
#include <Events/EventDispatcher.h>
#include <Events/StoryEvents.h>
#include <Games/ActorExtension.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Misc/PartyArrivals.h>
#include <VersionDb.h>
#include <World.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace StoryEvents
{
namespace
{
constexpr uint32_t cPlayerId = 0x14;

std::mutex s_lock;
std::unordered_map<uint32_t, uint32_t> s_judgedFor; // an event raised for a character: id -> its form id
PartyArrivals s_arrivals;
thread_local bool s_raising = false;
std::atomic<uint32_t> s_users{0};

// The event types' registered indexes (QuestEvents.h).
uint32_t KillIndex()
{
    POINTER_SKYRIMSE(uint32_t, s_index, 380067);
    return *s_index.Get();
}

uint32_t CraftIndex()
{
    POINTER_SKYRIMSE(uint32_t, s_index, 380076);
    return *s_index.Get();
}

uint32_t FormIdOf(const TESForm* apForm)
{
    return apForm ? apForm->formID : 0;
}

Actor* ActorOfHandle(uint64_t aMember)
{
    return Cast<Actor>(TESObjectREFR::GetByHandle(static_cast<uint32_t>(aMember)));
}

// This game's player, or another player's copy here.
bool IsPlayer(uint32_t aFormId)
{
    if (aFormId == cPlayerId)
        return true;
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    return pActor && pActor->GetExtension()->IsOtherPlayer();
}

uint32_t Add(uint32_t aIndex, const void* apEvent, const Actor& acJudged)
{
    using TGetManager = void*();
    using TAddEvent = uint32_t(void*, uint32_t, const void*);
    POINTER_SKYRIMSE(TGetManager, s_getManager, 22790); // BGSStoryEventManager::GetSingleton
    POINTER_SKYRIMSE(TAddEvent, s_addEvent, 32359);     // BGSStoryEventManager::AddEvent_Impl
    s_raising = true;
    const uint32_t cId = s_addEvent(s_getManager.Get()(), aIndex, apEvent);
    s_raising = false;
    std::scoped_lock _{s_lock};
    // Events leave the queue within frames: a stale entry is one whose event never ran.
    if (s_judgedFor.size() > 256)
        s_judgedFor.clear();
    s_judgedFor[cId] = acJudged.formID;
    return cId;
}

// BGSStoryEvent from its raw event (32330), inside AddEvent_Impl: this game's player's crafts, for a guest's game to
// send to the host (GuestWorldState).
using TBuildEvent = BGSStoryEvent*(BGSStoryEvent*, uint32_t, const void*);
TBuildEvent* RealBuildEvent = nullptr;

BGSStoryEvent* HookBuildEvent(BGSStoryEvent* apThis, uint32_t aIndex, const void* apRaw)
{
    BGSStoryEvent* pEvent = RealBuildEvent(apThis, aIndex, apRaw);
    if (!s_raising && apRaw && aIndex == CraftIndex())
    {
        const auto* pCraft = static_cast<const CraftItem*>(apRaw);
        World::Get().GetRunner().Trigger(PlayerCraftEvent{FormIdOf(TESObjectREFR::GetByHandle(pCraft->WorkbenchHandle)),
                                                          FormIdOf(pCraft->pBenchLocation), FormIdOf(pCraft->pCreated)});
    }
    return pEvent;
}

Hooks::Initializer s_storyEventHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(32330, &RealBuildEvent, HookBuildEvent);
    });

struct LocationSink final : BSTEventSink<TESActorLocationChangeEvent>
{
    BSTEventResult OnEvent(const TESActorLocationChangeEvent* apEvent, const EventDispatcher<TESActorLocationChangeEvent>*) override
    {
        auto* pActor = apEvent ? Cast<Actor>(apEvent->actor) : nullptr;
        if (pActor && pActor->GetExtension()->IsOtherPlayer())
            World::Get().GetRunner().Trigger(
                CopyLocationChangeEvent{pActor->formID, FormIdOf(apEvent->oldLocation), FormIdOf(apEvent->newLocation)});
        return BSTEventResult::kOk;
    }
};

LocationSink s_locations;
} // namespace

uint32_t LocationChangeIndex() noexcept
{
    POINTER_SKYRIMSE(uint32_t, s_index, 380069);
    return *s_index.Get();
}

uint32_t RaiseLocationChange(Actor& aCharacter, TESForm* apOldLocation, TESForm* apNewLocation) noexcept
{
    using TBuild = LocationChange*(LocationChange*, TESObjectREFR*, TESForm*, TESForm*);
    POINTER_SKYRIMSE(TBuild, s_build, 32288);
    LocationChange event{};
    s_build(&event, &aCharacter, apOldLocation, apNewLocation);
    return Add(LocationChangeIndex(), &event, aCharacter);
}

uint32_t RaiseCraft(Actor& aCharacter, TESObjectREFR* apWorkbench, TESForm* apLocation, TESForm* apCreated) noexcept
{
    CraftItem event{};
    event.WorkbenchHandle = apWorkbench ? apWorkbench->GetHandle().handle.iBits : 0;
    event.pBenchLocation = apLocation;
    event.pCreated = apCreated;
    return Add(CraftIndex(), &event, aCharacter);
}

Verdict Judge(const BGSStoryEvent& acEvent) noexcept
{
    Verdict verdict;
    uint32_t judgedId = 0;
    {
        std::scoped_lock _{s_lock};
        if (const auto it = s_judgedFor.find(acEvent.id); it != s_judgedFor.end())
            judgedId = it->second;
    }
    if (judgedId)
    {
        verdict.pJudged = Cast<Actor>(TESForm::GetById(judgedId));
        if (!verdict.pJudged)
            return {.Process = false};
    }
    // A kill by a guest's character, on the host (the engine raises it for the copy).
    else if (acEvent.index == KillIndex())
    {
        auto* pKiller = ActorOfHandle(acEvent.members[1]);
        if (pKiller && pKiller->GetExtension()->IsOtherPlayer())
            verdict.pJudged = pKiller;
    }
    if (acEvent.index == LocationChangeIndex())
    {
        const Actor* pMover = verdict.pJudged ? verdict.pJudged : ActorOfHandle(acEvent.members[0]);
        const auto* pLocation = reinterpret_cast<const TESForm*>(acEvent.members[2]);
        if (pMover && IsPlayer(pMover->formID))
        {
            std::scoped_lock _{s_lock};
            verdict.Process = s_arrivals.Arrive(pMover->formID, FormIdOf(pLocation), &IsPlayer);
        }
    }
    return verdict;
}

void Done(uint32_t aId) noexcept
{
    std::scoped_lock _{s_lock};
    s_judgedFor.erase(aId);
}

void Start() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (!pEvents || s_users++ != 0)
        return;
    pEvents->actorLocationChangeEvent.RegisterSink(&s_locations);
}

void Stop() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (!pEvents || !s_users || --s_users != 0)
        return;
    pEvents->actorLocationChangeEvent.UnRegisterSink(&s_locations);
}
} // namespace StoryEvents
