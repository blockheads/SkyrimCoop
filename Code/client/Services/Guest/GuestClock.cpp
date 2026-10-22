#include <Services/Guest/GuestClock.h>

#include <Bridges/GuestEngine.h>
#include <Bridges/IWorldBridge.h>
#include <Events/UpdateEvent.h>
#include <Messages/NotifyTimeSettings.h>
#include <World.h>

namespace
{
constexpr float kFadeSeconds = 5.f;
} // namespace

bool GuestClock::s_hostClock = false;

GuestClock::GuestClock(World& aWorld, entt::dispatcher& aDispatcher, GuestEngine& aEngine) noexcept
    : m_world(aWorld)
    , m_engine(aEngine)
{
    m_timeConnection = aDispatcher.sink<NotifyTimeSettings>().connect<&GuestClock::OnTimeSettings>(this);
}

GuestClock::~GuestClock() noexcept
{
    s_hostClock = false;
}

void GuestClock::OnTimeSettings(const NotifyTimeSettings& acMessage) noexcept
{
    m_shownTime = m_world.ctx().at<IWorldBridge&>().GetGameTime();
    s_hostClock = true;
    // The host's calendar as of now: hour, date and days passed.
    m_hostTime.m_timeModel = acMessage.timeModel;
    m_lastTick = m_world.GetTick();
}

void GuestClock::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!s_hostClock)
        return;

    const uint64_t cNow = m_world.GetTick();
    if (cNow < m_lastTick)
        return;
    m_hostTime.Update(cNow - m_lastTick);
    m_lastTick = cNow;

    TimeModel shown = m_hostTime.m_timeModel;
    if (m_fadeTimer < kFadeSeconds)
    {
        shown.Time = FadedHour();
        m_fadeTimer += static_cast<float>(acEvent.Delta);
    }
    m_engine.SetCalendar(shown);
}

float GuestClock::FadedHour() const noexcept
{
    const float cFrom = m_shownTime.Time;
    const float cTo = m_hostTime.m_timeModel.Time;
    // Forward through midnight when the host's hour is earlier.
    const float cAhead = cTo < cFrom ? cTo + 24.f - cFrom : cTo - cFrom;
    return std::fmod(cFrom + std::lerp(0.f, cAhead, m_fadeTimer / kFadeSeconds), 24.f);
}
