#include <Misc/StoryEvents.h>
#include <HookSwitch.h>

#include <Actor.h>
#include <Events/StoryEvents.h>
#include <FormIds.h>
#include <Games/ActorExtension.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Misc/PartyArrivals.h>
#include <Misc/StoryEventRules.h>
#include <SessionIds.h>
#include <VersionDb.h>
#include <World.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

BGSStoryEventManager* BGSStoryEventManager::Get() noexcept
{
    using TGet = BGSStoryEventManager*();
    POINTER_SKYRIMSE(TGet, s_get, 22790);
    return s_get.Get()();
}

namespace StoryEvents
{
namespace
{
using StoryEventRules::MemberType;

constexpr uint32_t cPlayerId = 0x14;

std::mutex s_lock;
std::unordered_map<uint32_t, uint32_t> s_judgedFor; // an event raised for a character: id -> its form id
PartyArrivals s_arrivals;
thread_local bool s_raising = false;
std::atomic<bool> s_sends{false};

// The event types' registered indexes (QuestEvents.h).
uint32_t KillIndex()
{
    POINTER_SKYRIMSE(uint32_t, s_index, 380067);
    return *s_index.Get();
}

// TESQuest::EnsureQuestStarted's (25003) queued start: the engine's, not a player's event.
uint32_t QuestStartIndex()
{
    POINTER_SKYRIMSE(uint32_t, s_index, 380097);
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

// A registered type's members' types (none: no such index here).
std::optional<std::vector<MemberType>> TypesOf(uint32_t aIndex)
{
    const BGSStoryEventManager* pManager = BGSStoryEventManager::Get();
    if (!pManager || aIndex >= pManager->registeredEvents.length)
        return std::nullopt;
    std::vector<MemberType> types;
    if (const auto* pMembers = pManager->registeredEvents.data[aIndex].members)
        for (uint32_t i = 0; i < pMembers->length && i < StoryEvent::kMaxMembers; ++i)
            types.push_back(static_cast<MemberType>(pMembers->data[i].type));
    return types;
}

std::optional<uint32_t> IndexOf(uint32_t aType)
{
    const BGSStoryEventManager* pManager = BGSStoryEventManager::Get();
    for (uint32_t i = 0; pManager && i < pManager->registeredEvents.length; ++i)
        if (pManager->registeredEvents.data[i].uniqueID == aType)
            return i;
    return std::nullopt;
}

uint32_t Add(uint32_t aIndex, const void* apEvent, const Actor& acJudged)
{
    using TAddEvent = uint32_t(BGSStoryEventManager*, uint32_t, const void*);
    POINTER_SKYRIMSE(TAddEvent, s_addEvent, 32359); // BGSStoryEventManager::AddEvent_Impl
    s_raising = true;
    const uint32_t cId = s_addEvent(BGSStoryEventManager::Get(), aIndex, apEvent);
    s_raising = false;
    std::scoped_lock _{s_lock};
    // Events leave the queue within frames: a stale entry is one whose event never ran.
    if (s_judgedFor.size() > 256)
        s_judgedFor.clear();
    s_judgedFor[cId] = acJudged.formID;
    return cId;
}

// BGSStoryEvent from its raw event (32330), inside AddEvent_Impl (its one caller): a guest's own events, for the host.
using TBuildEvent = BGSStoryEvent*(BGSStoryEvent*, uint32_t, const void*);
TBuildEvent* RealBuildEvent = nullptr;

BGSStoryEvent* HookBuildEvent(BGSStoryEvent* apThis, uint32_t aIndex, const void* apRaw)
{
    BGSStoryEvent* pEvent = RealBuildEvent(apThis, aIndex, apRaw);
    if (pEvent && s_sends.load(std::memory_order_relaxed) && !s_raising && aIndex != QuestStartIndex())
    {
        OwnStoryEvent own{aIndex};
        std::copy(std::begin(pEvent->members), std::end(pEvent->members), own.Members.begin());
        World::Get().GetRunner().Trigger(own);
    }
    return pEvent;
}

Hooks::Initializer s_storyEventHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        HOOK_ID(32330, &RealBuildEvent, HookBuildEvent);
    });
} // namespace

uint32_t LocationChangeIndex() noexcept
{
    POINTER_SKYRIMSE(uint32_t, s_index, 380069);
    return *s_index.Get();
}

void SetSends(bool aSends) noexcept
{
    s_sends.store(aSends, std::memory_order_relaxed);
}

std::optional<StoryEvent> ToSend(const OwnStoryEvent& acEvent) noexcept
{
    const auto cTypes = TypesOf(acEvent.Index);
    if (!cTypes)
        return std::nullopt;
    using StoryEventRules::Named;
    const auto cReference = [](uint32_t aHandle) -> Named {
        TESObjectREFR* pRef = TESObjectREFR::GetByHandle(aHandle);
        if (!pRef)
            return {};
        if (pRef->formID == cPlayerId)
            return {Named::kThisPlayer};
        if (Cast<Actor>(pRef))
            return {Named::kActor};
        const auto cId = ToSession(pRef->formID);
        return cId ? Named{Named::kObject, *cId} : Named{};
    };
    const auto cForm = [](uint64_t aForm) { return ToSession(FormIdOf(reinterpret_cast<const TESForm*>(aForm))); };
    auto members = StoryEventRules::ToSend(*cTypes, acEvent.Members, cReference, cForm);
    if (!members)
        return std::nullopt;
    StoryEvent event;
    event.Type = BGSStoryEventManager::Get()->registeredEvents.data[acEvent.Index].uniqueID;
    event.Members = std::move(*members);
    return event;
}

uint32_t Raise(const StoryEvent& acEvent, Actor& aCharacter) noexcept
{
    const auto cIndex = IndexOf(acEvent.Type);
    const auto cTypes = cIndex ? TypesOf(*cIndex) : std::nullopt;
    if (!cTypes || acEvent.Members.size() > cTypes->size())
        return 0;
    std::vector<uint64_t> values;
    for (size_t i = 0; i < acEvent.Members.size(); ++i)
    {
        const StoryEvent::Member& cMember = acEvent.Members[i];
        if (!StoryEventRules::Fits((*cTypes)[i], cMember.Type))
            return 0;
        uint64_t& value = values.emplace_back();
        switch (cMember.Type)
        {
        case StoryEvent::Member::kCharacter: value = aCharacter.GetHandle().handle.iBits; break;
        case StoryEvent::Member::kReference:
            if (auto* pRef = Cast<TESObjectREFR>(TESForm::GetById(ToGame(cMember.Form))))
                value = pRef->GetHandle().handle.iBits;
            break;
        case StoryEvent::Member::kForm: value = reinterpret_cast<uint64_t>(TESForm::GetById(ToGame(cMember.Form))); break;
        case StoryEvent::Member::kInteger: value = cMember.Value; break;
        default: break;
        }
    }
    const auto cRaw = StoryEventRules::Layout(*cTypes, values);
    return cRaw ? Add(*cIndex, cRaw->data(), aCharacter) : 0;
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
} // namespace StoryEvents
