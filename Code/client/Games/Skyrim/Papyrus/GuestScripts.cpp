#include <Papyrus/GuestScripts.h>

#include <Actor.h>
#include <Forms/TESForm.h>
#include <Games/EngineNames.h>
#include <Games/ActorExtension.h>
#include <HookSwitch.h>
#include <Misc/BSScript.h>
#include <Services/HostService.h>
#include <World.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

// On the host, scripts treat a guest's character as the player:
//   Rule 1: `x == Game.GetPlayer()` is true when x is a guest's character.
//   Rule 2: Game.GetPlayer() returns the guest whose action started the script, so every running script is
//   tagged with the guest it runs for. Evidence: docs/re/systems/papyrus-guest-scripts.md.
namespace GuestScripts
{
namespace
{
// --- What we hook ---

// Rule 1: a script compares two values (Internal::CodeTasklet, 105220).
TP_THIS_FUNCTION(TCompare, int64_t, BSScript::CodeTasklet, BSScript::Variable*, BSScript::Variable*);
TCompare* RealCompare = nullptr;
int64_t TP_MAKE_THISCALL(HookCompare, BSScript::CodeTasklet, BSScript::Variable* apFirst, BSScript::Variable* apSecond);

// Rule 2: Game.GetPlayer() returns (NativeFunction0<StaticFunctionTag, Actor*>::MarshallAndDispatch, 55618).
TP_THIS_FUNCTION(TGetPlayer, bool, BSScript::IFunction, BSScript::Variable*, BSScript::IVirtualMachine*, uint32_t,
                 BSScript::Variable*, BSScript::StackFrame*);
TGetPlayer* RealGetPlayer = nullptr;
bool TP_MAKE_THISCALL(HookGetPlayer, BSScript::IFunction, BSScript::Variable* apBase, BSScript::IVirtualMachine* apVm,
                      uint32_t aStackId, BSScript::Variable* apResult, BSScript::StackFrame* apFrame);

// Tagging: an event or call is queued on a script object (Internal::RawFuncCallQuery's constructor, 104943).
TP_THIS_FUNCTION(TCallQueued, void*, void, int32_t, void*, const BSFixedString*, const BSScript::Variable*,
                 const BSScript::Arguments*);
TCallQueued* RealCallQueued = nullptr;
void* TP_MAKE_THISCALL(HookCallQueued, void, int32_t aCallType, void* apObjectType, const BSFixedString* apEvent,
                       const BSScript::Variable* apSelf, const BSScript::Arguments* apArguments);

// Tagging: the call gets the stack that will run it (VirtualMachine, 104848).
TP_THIS_FUNCTION(TStackCreated, bool, void, BSScript::Stack*, void**);
TStackCreated* RealStackCreated = nullptr;
bool TP_MAKE_THISCALL(HookStackCreated, void, BSScript::Stack* apStack, void** appCall);

// Tagging: a script calls a native (NativeFunctionBase::Call, 104651).
TP_THIS_FUNCTION(TNativeCalled, BSScript::CallResult, BSScript::IFunction, BSScript::Stack**, void*,
                 BSScript::IVirtualMachine*, bool);
TNativeCalled* RealNativeCalled = nullptr;
BSScript::CallResult TP_MAKE_THISCALL(HookNativeCalled, BSScript::IFunction, BSScript::Stack** appStack, void* apLogger,
                                      BSScript::IVirtualMachine* apVm, bool aArg4);

// Tagging: a script finished (Stack::~Stack, 104480).
TP_THIS_FUNCTION(TStackEnded, void*, BSScript::Stack);
TStackEnded* RealStackEnded = nullptr;
void* TP_MAKE_THISCALL(HookStackEnded, BSScript::Stack);

TiltedPhoques::Initializer s_hooks([]() {
    const HookGate cGate(__FILE__);
    POINTER_SKYRIMSE(TCompare, s_compare, 105220);
    POINTER_SKYRIMSE(TGetPlayer, s_getPlayer, 55618);
    POINTER_SKYRIMSE(TCallQueued, s_callQueued, 104943);
    POINTER_SKYRIMSE(TStackCreated, s_stackCreated, 104848);
    POINTER_SKYRIMSE(TNativeCalled, s_nativeCalled, 104651);
    POINTER_SKYRIMSE(TStackEnded, s_stackEnded, 104480);
    RealCompare = s_compare.Get();
    RealGetPlayer = s_getPlayer.Get();
    RealCallQueued = s_callQueued.Get();
    RealStackCreated = s_stackCreated.Get();
    RealNativeCalled = s_nativeCalled.Get();
    RealStackEnded = s_stackEnded.Get();
    TP_HOOK(&RealCompare, HookCompare);
    TP_HOOK(&RealGetPlayer, HookGetPlayer);
    TP_HOOK(&RealCallQueued, HookCallQueued);
    TP_HOOK(&RealStackCreated, HookStackCreated);
    TP_HOOK(&RealNativeCalled, HookNativeCalled);
    TP_HOOK(&RealStackEnded, HookStackEnded);
});

constexpr uint32_t cPlayerFormId = 0x14;

bool IsHosting() noexcept
{
    return World::Get().GetHostService().IsHosting();
}

uint32_t FormIdOf(const BSScript::Variable& acValue) noexcept
{
    auto* pObject = acValue.GetObject();
    return pObject ? BSScript::FormIdOfHandle(pObject->GetHandle()) : 0;
}

// --- The guests' characters (SetGuest) ---

// By form id (0: free), read without engine calls: HookCallQueued runs with engine locks held.
std::array<std::atomic<uint32_t>, 16> s_guests{};

bool IsGuest(uint32_t aFormId) noexcept
{
    const auto cHolds = [aFormId](const auto& acSlot) { return acSlot.load(std::memory_order_relaxed) == aFormId; };
    return aFormId && std::ranges::any_of(s_guests, cHolds);
}

// The guest's character, confirmed with the engine (form ids are reused). Script threads only.
// this makes sure something isn't re-using our ID to be a funny cow "🐮" <- see image to the left
// for what a cow looks like you fucking retard
Actor* GuestCharacter(uint32_t aFormId) noexcept
{
    auto* pActor = IsGuest(aFormId) ? Cast<Actor>(TESForm::GetById(aFormId)) : nullptr;
    return pActor && pActor->GetExtension() && pActor->GetExtension()->IsOtherPlayer() ? pActor : nullptr;
}

// The first argument that is a guest's character, or 0.
uint32_t GuestArgument(const BSScript::Arguments* apArguments) noexcept
{
    if (!apArguments)
        return 0;
    const auto cFormIds = std::span(apArguments->pData, apArguments->size) | std::views::transform(FormIdOf);
    const auto it = std::ranges::find_if(cFormIds, IsGuest);
    return it != cFormIds.end() ? *it : 0;
}

// --- Tags: which guest each call and stack runs for ---

enum class Timer : uint8_t
{
    kOnce,
    kRepeat,
    kStop,
};

// A script's timer: when it fires, the engine queues Event on the script that set it.
struct TimerNative
{
    std::string_view Name;
    std::string_view Event;
    Timer Kind;
};

// Form, Alias and ActiveMagicEffect share these names.
constexpr TimerNative cTimerNatives[] = {
    {"RegisterForSingleUpdate", "OnUpdate", Timer::kOnce},
    {"RegisterForUpdate", "OnUpdate", Timer::kRepeat},
    {"UnregisterForUpdate", "OnUpdate", Timer::kStop},
    {"RegisterForSingleUpdateGameTime", "OnUpdateGameTime", Timer::kOnce},
    {"RegisterForUpdateGameTime", "OnUpdateGameTime", Timer::kRepeat},
    {"UnregisterForUpdateGameTime", "OnUpdateGameTime", Timer::kStop},
};

const TimerNative* FindTimerNative(std::string_view aName) noexcept
{
    const auto it = std::ranges::find_if(cTimerNatives, [aName](const auto& acTimer) { return SameName(acTimer.Name, aName); });
    return it != std::end(cTimerNatives) ? it : nullptr;
}

// The timer event aEvent names, spelled as cTimerNatives spells it ("" if none).
std::string_view TimerEvent(std::string_view aEvent) noexcept
{
    const auto it = std::ranges::find_if(cTimerNatives, [aEvent](const auto& acTimer) { return SameName(acTimer.Event, aEvent); });
    return it != std::end(cTimerNatives) ? it->Event : std::string_view{};
}

// Its lock is a leaf: no engine call while it is held.
class Tags
{
  public:
    // False until a guest's call is first tagged: until then the hooks skip the lock.
    bool Any() const noexcept { return m_any.load(std::memory_order_relaxed); }

    // A queued call runs for aGuest (0: for no one, a new call where an old one was).
    void TagCall(const void* apCall, uint32_t aGuest) noexcept
    {
        std::scoped_lock _{m_lock};
        if (aGuest)
        {
            m_calls[apCall] = aGuest;
            m_any = true;
        }
        else
            m_calls.erase(apCall);
    }

    // The stack that runs the call takes its guest.
    void MoveToStack(const void* apCall, uint32_t aStackId) noexcept
    {
        std::scoped_lock _{m_lock};
        if (auto call = m_calls.extract(apCall))
            m_stacks[aStackId] = call.mapped();
    }

    uint32_t OfStack(uint32_t aStackId) noexcept
    {
        std::scoped_lock _{m_lock};
        const auto it = m_stacks.find(aStackId);
        return it != m_stacks.end() ? it->second : 0;
    }

    void EndStack(uint32_t aStackId) noexcept
    {
        std::scoped_lock _{m_lock};
        m_stacks.erase(aStackId);
    }

    // A script object's timer, set (or stopped) by a stack running for aGuest.
    void SetTimer(uint64_t aObject, std::string_view aEvent, uint32_t aGuest, Timer aKind) noexcept
    {
        std::scoped_lock _{m_lock};
        const std::pair<uint64_t, std::string> cKey{aObject, aEvent};
        if (!aGuest || aKind == Timer::kStop)
            m_timers.erase(cKey);
        else
            m_timers[cKey] = {aGuest, aKind == Timer::kRepeat};
    }

    bool HasTimers() noexcept
    {
        std::scoped_lock _{m_lock};
        return !m_timers.empty();
    }

    // The guest whose timer raised aEvent on aObject; a single timer is used up.
    uint32_t TakeTimer(uint64_t aObject, std::string_view aEvent) noexcept
    {
        std::scoped_lock _{m_lock};
        const auto it = m_timers.find({aObject, std::string(aEvent)});
        if (it == m_timers.end())
            return 0;
        const uint32_t cGuest = it->second.Guest;
        if (!it->second.Repeats)
            m_timers.erase(it);
        return cGuest;
    }

  private:
    struct TimerGuest
    {
        uint32_t Guest;
        bool Repeats;
    };

    std::mutex m_lock;
    std::atomic<bool> m_any{false};
    std::map<const void*, uint32_t> m_calls;                         // queued call -> guest
    std::map<uint32_t, uint32_t> m_stacks;                            // stack id -> guest
    std::map<std::pair<uint64_t, std::string>, TimerGuest> m_timers; // (script object, event) -> guest
};

Tags s_tags;

// The guest whose native is running on this thread: what the native queues runs for them too.
thread_local uint32_t t_nativeGuest = 0;

uint32_t GuestOf(const BSScript::Stack* apStack) noexcept
{
    return apStack && s_tags.Any() ? s_tags.OfStack(apStack->stackId) : 0;
}

// --- The rules ---

int64_t TP_MAKE_THISCALL(HookCompare, BSScript::CodeTasklet, BSScript::Variable* apFirst, BSScript::Variable* apSecond)
{
    if (IsHosting())
    {
        const uint32_t cFirst = FormIdOf(*apFirst);
        const uint32_t cSecond = FormIdOf(*apSecond);
        if ((cFirst == cPlayerFormId && GuestCharacter(cSecond)) || (cSecond == cPlayerFormId && GuestCharacter(cFirst)))
            return 0; // equal
    }
    return TiltedPhoques::ThisCall(RealCompare, apThis, apFirst, apSecond);
}

// 55618 serves every no-argument native returning an Actor: only GetPlayer is changed.
bool TP_MAKE_THISCALL(HookGetPlayer, BSScript::IFunction, BSScript::Variable* apBase, BSScript::IVirtualMachine* apVm,
                      uint32_t aStackId, BSScript::Variable* apResult, BSScript::StackFrame* apFrame)
{
    const bool cResult = TiltedPhoques::ThisCall(RealGetPlayer, apThis, apBase, apVm, aStackId, apResult, apFrame);
    const char* cpName = apThis->GetName().AsAscii();
    if (cResult && apFrame && apResult && cpName && SameName(cpName, "GetPlayer") && IsHosting())
        if (Actor* pGuest = GuestCharacter(GuestOf(apFrame->pParent)))
            apResult->SetActor(pGuest, apVm);
    return cResult;
}

// --- Tagging ---

// The call runs for the guest whose native queued it, whose character is an argument, or whose timer it answers.
void* TP_MAKE_THISCALL(HookCallQueued, void, int32_t aCallType, void* apObjectType, const BSFixedString* apEvent,
                       const BSScript::Variable* apSelf, const BSScript::Arguments* apArguments)
{
    void* pResult = TiltedPhoques::ThisCall(RealCallQueued, apThis, aCallType, apObjectType, apEvent, apSelf, apArguments);
    if (!IsHosting())
        return pResult;
    uint32_t guest = t_nativeGuest ? t_nativeGuest : GuestArgument(apArguments);
    auto* pSelf = apSelf ? apSelf->GetObject() : nullptr;
    if (!guest && pSelf && apEvent && apEvent->AsAscii() && s_tags.HasTimers())
        if (const auto cEvent = TimerEvent(apEvent->AsAscii()); !cEvent.empty())
            guest = s_tags.TakeTimer(pSelf->GetHandle(), cEvent);
    if (guest || s_tags.Any())
        s_tags.TagCall(apThis, guest);
    return pResult;
}

bool TP_MAKE_THISCALL(HookStackCreated, void, BSScript::Stack* apStack, void** appCall)
{
    if (apStack && appCall && *appCall && s_tags.Any())
        s_tags.MoveToStack(*appCall, apStack->stackId);
    return TiltedPhoques::ThisCall(RealStackCreated, apThis, apStack, appCall);
}

// While a native runs for a guest, what it queues runs for them too; a timer it sets fires for them.
BSScript::CallResult TP_MAKE_THISCALL(HookNativeCalled, BSScript::IFunction, BSScript::Stack** appStack, void* apLogger,
                                      BSScript::IVirtualMachine* apVm, bool aArg4)
{
    uint32_t guest = 0;
    if (appStack && *appStack && s_tags.Any())
    {
        guest = GuestOf(*appStack);
        const char* cpName = apThis->GetName().AsAscii();
        const BSScript::StackFrame* pTop = (*appStack)->pTop;
        if (const TimerNative* pTimer = cpName ? FindTimerNative(cpName) : nullptr)
            if (auto* pScript = pTop ? pTop->self.GetObject() : nullptr)
                s_tags.SetTimer(pScript->GetHandle(), pTimer->Event, guest, pTimer->Kind);
    }
    const uint32_t cOuter = t_nativeGuest;
    t_nativeGuest = guest;
    const auto cResult = TiltedPhoques::ThisCall(RealNativeCalled, apThis, appStack, apLogger, apVm, aArg4);
    t_nativeGuest = cOuter;
    return cResult;
}

void* TP_MAKE_THISCALL(HookStackEnded, BSScript::Stack)
{
    if (s_tags.Any())
        s_tags.EndStack(apThis->stackId);
    return TiltedPhoques::ThisCall(RealStackEnded, apThis);
}
} // namespace

void SetGuest(uint32_t aFormId, bool aIsGuest) noexcept
{
    aIsGuest = aIsGuest && IsHosting(); // only the host's scripts run for guests
    for (auto& slot : s_guests)
        if (slot.load() == aFormId)
        {
            if (aIsGuest)
                return;
            slot.store(0);
        }
    if (!aIsGuest)
        return;
    for (auto& slot : s_guests)
        if (uint32_t expected = 0; slot.compare_exchange_strong(expected, aFormId))
            return;
    spdlog::warn("GuestScripts: no slot for guest {:X}", aFormId);
}
} // namespace GuestScripts
