#include <Services/Host/HostClock.h>

#include <GameServer.h>
#include <Services/Host/HostBridge.h>

#include <Messages/NotifySetTimeResult.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/SetTimeCommandRequest.h>

#include <cmath>

namespace
{
// Game minutes from acFrom to acTo (whole days as integers: a float day count is too coarse).
double MinutesBetween(const TimeModel& acFrom, const TimeModel& acTo)
{
    auto days = [](const TimeModel& acTime) {
        int64_t count = static_cast<int64_t>(acTime.Year) * 365 + acTime.Day;
        for (uint32_t month = 0; month < acTime.Month; ++month)
            count += DateTime::GetNumberOfDaysByMonthIndex(month);
        return count;
    };
    return (static_cast<double>(days(acTo) - days(acFrom)) * 24.0 + (acTo.Time - acFrom.Time)) * 60.0;
}
} // namespace

HostClock::HostClock(HostBridge& aBridge, Server::GameServer& aServer, const HostGame& acGame) noexcept
    : m_server(aServer)
    , m_game(acGame)
{
    aBridge.On(*this, &HostClock::OnSetTimeCommand);
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
            projected.m_timeModel.TimeScale == cHostClock.m_timeModel.TimeScale)
        {
            m_peersClock = projected;
            m_peersClockTick = aTick;
            return;
        }
    }

    m_peersClock = cHostClock;
    m_peersClockTick = aTick;
    ServerTimeSettings time;
    time.timeModel = cHostClock.m_timeModel;
    m_server.SendToOthers(time, m_server.HostConnection(), TiltedPhoques::kReliable);
}

void HostClock::SendTime(ConnectionId_t aPlayer) const noexcept
{
    ServerTimeSettings time;
    time.timeModel = m_game.ReadTime();
    m_server.Send(aPlayer, time);
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
    m_server.Send(aSender, result);
}
