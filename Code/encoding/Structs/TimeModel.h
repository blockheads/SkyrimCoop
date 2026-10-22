#pragma once

#include <cstdint>

#include <Wire/Buffer.h>

using Wire::Buffer;

struct TimeModel
{
    // Default time: 01/01/01 at 12:00
    float TimeScale = 20.f;
    float Time = 12.f;
    uint32_t Year = 1;
    uint32_t Month = 1;
    uint32_t Day = 1;
    // Whole days since the game started (the calendar's rawDaysPassed): GameDaysPassed is this + Time / 24.
    uint32_t DaysPassed = 0;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const TimeModel& achRhs) const noexcept { return TimeScale == achRhs.TimeScale && Time == achRhs.Time && Day == achRhs.Day && Month == achRhs.Month && Year == achRhs.Year && DaysPassed == achRhs.DaysPassed; }
    bool operator!=(const TimeModel& achRhs) const noexcept { return !this->operator==(achRhs); }
};
