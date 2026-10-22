#include <Services/Host/HostClock.h>

#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostAuthority.h>

#include <Messages/NotifySetTimeResult.h>
#include <Messages/NotifyTimeSettings.h>
#include <Messages/SetTimeCommandRequest.h>

#include <cmath>

namespace
{
// Game minutes from acFrom to acTo (whole days as integers: a float day count is too coarse).
double MinutesBetween(const TimeModel& acFrom, const TimeModel& acTo)
{
    const auto cDays = static_cast<int64_t>(acTo.DaysPassed) - static_cast<int64_t>(acFrom.DaysPassed);
    return (static_cast<double>(cDays) * 24.0 + (acTo.Time - acFrom.Time)) * 60.0;
}

bool SameDate(const TimeModel& acLeft, const TimeModel& acRight)
{
    return acLeft.Day == acRight.Day && acLeft.Month == acRight.Month && acLeft.Year == acRight.Year;
}
} // namespace

HostClock::HostClock(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostClock::OnSetTimeCommand);
}

void HostClock::Update(uint64_t aTick) noexcept
{
    const DateTime cHostClock(m_game.ReadTime());
    if (m_peersClock)
    {
        // Where peers' clocks are now, running on their own.
        DateTime projected = *m_peersClock;
        projected.Update(aTick - m_peersClockTick);
        constexpr double cTolerance = 1.0; // game minutes
        if (std::abs(MinutesBetween(projected.m_timeModel, cHostClock.m_timeModel)) < cTolerance &&
            SameDate(projected.m_timeModel, cHostClock.m_timeModel) && projected.m_timeModel.TimeScale == cHostClock.m_timeModel.TimeScale)
        {
            m_peersClock = projected;
            m_peersClockTick = aTick;
            return;
        }
    }

    m_peersClock = cHostClock;
    m_peersClockTick = aTick;
    NotifyTimeSettings time;
    time.timeModel = cHostClock.m_timeModel;
    m_players.SendToOthers(time, m_players.Host(), TiltedPhoques::kReliable);
}

void HostClock::SendTime(ConnectionId_t aPlayer) const noexcept
{
    NotifyTimeSettings time;
    time.timeModel = m_game.ReadTime();
    m_network.Send(aPlayer, time);
}

void HostClock::OnSetTimeCommand(const SetTimeCommandRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifySetTimeResult result;
    if (acRequest.Hours < 24 && acRequest.Minutes < 60)
    {
        m_game.SetHour(static_cast<float>(acRequest.Hours) + static_cast<float>(acRequest.Minutes) / 60.f);
        result.Result = NotifySetTimeResult::SetTimeResult::kSuccess;
    }
    else
        result.Result = NotifySetTimeResult::SetTimeResult::kNoPermission;
    m_network.Send(aSender, result);
}
