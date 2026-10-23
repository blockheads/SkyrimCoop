#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

// GuestScripts' bookkeeping with no engine: which player each queued call, stack, timer and delayed native runs for,
// and the rules that give a call its cause (docs/design/cutscenes.md, "Whose moment"). Checked by ClientTests on
// Linux; GuestScripts.cpp's hooks gather the inputs.
namespace GuestScripts
{
// This game's player (its reference, 0x14) as a cause: what this game's own player did.
inline constexpr uint32_t kThisPlayer = 0x14;

// Who a call or stack runs for: the player whose action started it (a guest's character, or this game's player), and
// whether a stage that player's scripts set runs it (its fragment, and what that queues): the story, the party's.
struct Cause
{
    uint32_t Player = 0;
    bool Story = false;
    bool operator==(const Cause&) const = default;
};

enum class Timer : uint8_t
{
    kOnce,
    kRepeat,
    kStop,
};

// A script's timer or registration: when it fires, the engine queues one of Events on the script that set it.
struct TimerNative
{
    std::string_view Name;
    std::array<std::string_view, 2> Events;
    Timer Kind;
};

// The timer native named aName (Form, Alias and ActiveMagicEffect share the names), or none.
[[nodiscard]] const TimerNative* FindTimerNative(std::string_view aName) noexcept;
// The timer event aEvent names, spelled as the table spells it ("" if none).
[[nodiscard]] std::string_view TimerEvent(std::string_view aEvent) noexcept;

// Its lock is a leaf: no engine call while it is held.
class Tags
{
  public:
    // False until a call is first tagged: until then the hooks skip the lock.
    [[nodiscard]] bool Any() const noexcept
    {
        return m_any.load(std::memory_order_relaxed);
    }

    // A queued call runs for acCause (no player: for no one, a new call where an old one was).
    void TagCall(const void* apCall, const Cause& acCause) noexcept;
    // The stack that runs the call takes its cause.
    void MoveToStack(const void* apCall, uint32_t aStackId) noexcept;
    [[nodiscard]] Cause OfStack(uint32_t aStackId) noexcept;
    void EndStack(uint32_t aStackId) noexcept;

    // A script object's timer, set (or stopped) by a stack running for acCause.
    void SetTimer(uint64_t aObject, std::string_view aEvent, const Cause& acCause, Timer aKind) noexcept;
    [[nodiscard]] bool HasTimers() noexcept;
    // Who the timer that raised aEvent on aObject runs for; a single timer is used up.
    [[nodiscard]] Cause TakeTimer(uint64_t aObject, std::string_view aEvent) noexcept;
    // The player whose script set the timer or registration aEvent on aObject answers (any spelling), kept; or 0.
    [[nodiscard]] uint32_t RegisteredBy(uint64_t aObject, std::string_view aEvent) noexcept;

    // A delayed native (Spell.Cast's CastFunctor) queued by a guest's script, then run: the guest it runs for.
    void TagFunctor(const void* apFunctor, uint32_t aGuest) noexcept;
    [[nodiscard]] uint32_t TakeFunctor(const void* apFunctor) noexcept;

  private:
    struct TimerCause
    {
        Cause For;
        bool Repeats;
    };

    std::mutex m_lock;
    std::atomic<bool> m_any{false};
    std::map<const void*, Cause> m_calls;                            // queued call -> its cause
    std::map<uint32_t, Cause> m_stacks;                              // stack id -> its cause
    std::map<std::pair<uint64_t, std::string>, TimerCause> m_timers; // (script object, event) -> its cause
    std::map<const void*, uint32_t> m_functors;                      // queued delayed native -> guest
};

// A call being queued on a script (Internal::RawFuncCallQuery): what can give it a cause.
struct QueuedCall
{
    Cause Native;             // the native queuing it on this thread, if it runs for a player
    uint32_t NativeDepth = 0; // natives running on this thread
    bool InSession = false;   // this game is in a session (PartyNatives::InSession)
    uint64_t Self = 0;        // the script object's handle, 0 for none
    std::string_view Event;   // the event or function queued
};

// Who a queued call runs for, in order: the native queuing it (and its story); a guest's character among its
// arguments (aGuestArgument(), 0 for none: a guest's activation, hit, trigger); this game's player among them
// (aPlayerArgument()) for the engine's event of their action, in a session and with no native running; the timer or
// registration it answers (a single one used up). The arguments are read only if needed.
template <class TGuestArgument, class TPlayerArgument>
[[nodiscard]] Cause CauseOfCall(Tags& aTags, const QueuedCall& acCall, TGuestArgument&& aGuestArgument,
                                TPlayerArgument&& aPlayerArgument)
{
    Cause cause{acCall.Native.Player, acCall.Native.Player && acCall.Native.Story};
    if (!cause.Player)
        cause.Player = aGuestArgument();
    if (!cause.Player && !acCall.NativeDepth && acCall.InSession && aPlayerArgument())
        cause.Player = kThisPlayer;
    if (!cause.Player && acCall.Self && !acCall.Event.empty() && aTags.HasTimers())
        if (const auto cEvent = TimerEvent(acCall.Event); !cEvent.empty())
            cause = aTags.TakeTimer(acCall.Self, cEvent);
    return cause;
}

// A native aName called on aSelf in a stack running for acCause: a timer or registration it sets or stops answers
// to that cause (none: forgotten).
void NativeCalled(Tags& aTags, std::string_view aName, uint64_t aSelf, const Cause& acCause) noexcept;

// What a native running in a stack with acCause queues runs for: that player, and the story once the stack's player
// sets a stage (aSetsStage: the native's code calls TESQuest::SetStage).
[[nodiscard]] constexpr Cause Within(const Cause& acCause, bool aSetsStage) noexcept
{
    return {acCause.Player, acCause.Player && (acCause.Story || aSetsStage)};
}
} // namespace GuestScripts
