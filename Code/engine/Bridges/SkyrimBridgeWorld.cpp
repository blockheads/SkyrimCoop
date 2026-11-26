// SkyrimBridge: the calendar, the clock and the weather.
#include "SkyrimBridgeInternal.h"

uint32_t SkyrimBridge::GetCurrentWeatherId() const noexcept
{
    Sky* pSky = Sky::Get();
    if (!pSky)
        return 0;
    TESWeather* pWeather = pSky->GetWeather();
    return pWeather ? pWeather->formID : 0;
}

float SkyrimBridge::GetGameTimeHours() const noexcept
{
    if (TimeData* pTime = TimeData::Get())
        return pTime->GameHour ? pTime->GameHour->f : 0.f;
    return 0.f;
}

void SkyrimBridge::SetSharedTimeScale(float aMultiplier) noexcept
{
    BSTimer::SetSharedMultiplier(aMultiplier);
}

float SkyrimBridge::GetRealFrameSeconds() const noexcept
{
    const BSTimer* pTimer = BSTimer::Get();
    return pTimer ? pTimer->RealTimeDelta : 0.f;
}

float SkyrimBridge::GetGameTimeScale() const noexcept
{
    if (TimeData* pTime = TimeData::Get())
        return pTime->TimeScale ? pTime->TimeScale->f : 0.f;
    return 0.f;
}

float SkyrimBridge::GetGameYear() const noexcept
{
    if (TimeData* pTime = TimeData::Get())
        return pTime->GameYear ? pTime->GameYear->f : 0.f;
    return 0.f;
}

float SkyrimBridge::GetGameMonth() const noexcept
{
    if (TimeData* pTime = TimeData::Get())
        return pTime->GameMonth ? pTime->GameMonth->f : 0.f;
    return 0.f;
}

float SkyrimBridge::GetGameDay() const noexcept
{
    if (TimeData* pTime = TimeData::Get())
        return pTime->GameDay ? pTime->GameDay->f : 0.f;
    return 0.f;
}

TimeModel SkyrimBridge::GetGameTime() const noexcept
{
    TimeModel time;
    const TimeData* pTime = TimeData::Get();
    if (!pTime || !pTime->GameYear || !pTime->GameMonth || !pTime->GameDay || !pTime->GameHour || !pTime->TimeScale)
        return time;
    time.TimeScale = pTime->TimeScale->f;
    time.Time = pTime->GameHour->f;
    time.Day = static_cast<uint32_t>(pTime->GameDay->f);
    time.Month = static_cast<uint32_t>(pTime->GameMonth->f);
    time.Year = static_cast<uint32_t>(pTime->GameYear->f);
    time.DaysPassed = static_cast<uint32_t>(pTime->rawDaysPassed);
    return time;
}
