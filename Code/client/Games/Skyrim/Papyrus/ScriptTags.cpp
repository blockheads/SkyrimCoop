#include <Papyrus/ScriptTags.h>

#include <Games/EngineNames.h>

#include <algorithm>

namespace GuestScripts
{
namespace
{
constexpr TimerNative cTimerNatives[] = {
    {"RegisterForSingleUpdate", {"OnUpdate"}, Timer::kOnce},
    {"RegisterForUpdate", {"OnUpdate"}, Timer::kRepeat},
    {"UnregisterForUpdate", {"OnUpdate"}, Timer::kStop},
    {"RegisterForSingleUpdateGameTime", {"OnUpdateGameTime"}, Timer::kOnce},
    {"RegisterForUpdateGameTime", {"OnUpdateGameTime"}, Timer::kRepeat},
    {"UnregisterForUpdateGameTime", {"OnUpdateGameTime"}, Timer::kStop},
    {"RegisterForAnimationEvent", {"OnAnimationEvent", "OnAnimationEventUnregistered"}, Timer::kRepeat},
    {"UnregisterForAnimationEvent", {"OnAnimationEvent"}, Timer::kStop},
    {"RegisterForSleep", {"OnSleepStart", "OnSleepStop"}, Timer::kRepeat},
    {"UnregisterForSleep", {"OnSleepStart", "OnSleepStop"}, Timer::kStop},
    {"RegisterForLOS", {"OnGainLOS", "OnLostLOS"}, Timer::kRepeat},
    {"RegisterForSingleLOSGain", {"OnGainLOS"}, Timer::kOnce},
    {"RegisterForSingleLOSLost", {"OnLostLOS"}, Timer::kOnce},
    {"UnregisterForLOS", {"OnGainLOS", "OnLostLOS"}, Timer::kStop},
    {"RegisterForTrackedStatsEvent", {"OnTrackedStatsEvent"}, Timer::kRepeat},
    {"UnregisterForTrackedStatsEvent", {"OnTrackedStatsEvent"}, Timer::kStop},
};
} // namespace

const TimerNative* FindTimerNative(std::string_view aName) noexcept
{
    const auto it =
        std::ranges::find_if(cTimerNatives, [aName](const auto& acTimer) { return SameName(acTimer.Name, aName); });
    return it != std::end(cTimerNatives) ? it : nullptr;
}

std::string_view TimerEvent(std::string_view aEvent) noexcept
{
    for (const auto& cTimer : cTimerNatives)
        for (const std::string_view cEvent : cTimer.Events)
            if (!cEvent.empty() && SameName(cEvent, aEvent))
                return cEvent;
    return {};
}

void Tags::TagCall(const void* apCall, const Cause& acCause) noexcept
{
    std::scoped_lock _{m_lock};
    if (acCause.Player)
    {
        m_calls[apCall] = acCause;
        m_any = true;
    }
    else
        m_calls.erase(apCall);
}

void Tags::MoveToStack(const void* apCall, uint32_t aStackId) noexcept
{
    std::scoped_lock _{m_lock};
    if (auto call = m_calls.extract(apCall))
        m_stacks[aStackId] = call.mapped();
}

Cause Tags::OfStack(uint32_t aStackId) noexcept
{
    std::scoped_lock _{m_lock};
    const auto it = m_stacks.find(aStackId);
    return it != m_stacks.end() ? it->second : Cause{};
}

void Tags::EndStack(uint32_t aStackId) noexcept
{
    std::scoped_lock _{m_lock};
    m_stacks.erase(aStackId);
}

void Tags::SetTimer(uint64_t aObject, std::string_view aEvent, const Cause& acCause, Timer aKind) noexcept
{
    std::scoped_lock _{m_lock};
    const std::pair<uint64_t, std::string> cKey{aObject, aEvent};
    if (!acCause.Player || aKind == Timer::kStop)
        m_timers.erase(cKey);
    else
        m_timers[cKey] = {acCause, aKind == Timer::kRepeat};
}

bool Tags::HasTimers() noexcept
{
    std::scoped_lock _{m_lock};
    return !m_timers.empty();
}

Cause Tags::TakeTimer(uint64_t aObject, std::string_view aEvent) noexcept
{
    std::scoped_lock _{m_lock};
    const auto it = m_timers.find({aObject, std::string(aEvent)});
    if (it == m_timers.end())
        return {};
    const Cause cCause = it->second.For;
    if (!it->second.Repeats)
        m_timers.erase(it);
    return cCause;
}

uint32_t Tags::RegisteredBy(uint64_t aObject, std::string_view aEvent) noexcept
{
    if (!HasTimers())
        return 0;
    const auto cEvent = TimerEvent(aEvent);
    if (cEvent.empty())
        return 0;
    std::scoped_lock _{m_lock};
    const auto it = m_timers.find({aObject, std::string(cEvent)});
    return it != m_timers.end() ? it->second.For.Player : 0;
}

void Tags::TagFunctor(const void* apFunctor, uint32_t aGuest) noexcept
{
    std::scoped_lock _{m_lock};
    m_functors[apFunctor] = aGuest;
}

uint32_t Tags::TakeFunctor(const void* apFunctor) noexcept
{
    std::scoped_lock _{m_lock};
    auto functor = m_functors.extract(apFunctor);
    return functor ? functor.mapped() : 0;
}

void NativeCalled(Tags& aTags, std::string_view aName, uint64_t aSelf, const Cause& acCause) noexcept
{
    const TimerNative* pTimer = FindTimerNative(aName);
    if (!pTimer || !aSelf)
        return;
    for (const std::string_view cEvent : pTimer->Events)
        if (!cEvent.empty())
            aTags.SetTimer(aSelf, cEvent, acCause, pTimer->Kind);
}
} // namespace GuestScripts
