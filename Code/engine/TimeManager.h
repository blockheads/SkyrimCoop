#pragma once

#include <Forms/TESGlobal.h>

// The game's calendar (CommonLibSSE-NG RE::Calendar, 400447); docs/re/systems/calendar.md.
struct TimeData
{
    static TimeData* Get() noexcept;
    // GameYear to TimeScale below: the engine looks them up by these ids (Calendar::Calendar, 36289).
    static bool IsCalendarGlobal(uint32_t aFormId) noexcept { return aFormId >= 0x35 && aFormId <= 0x3A; }

    // Runs the clock aGameHours on as a wait does (Calendar::Update 36291): date, days passed, Days Passed stat.
    void Advance(float aGameHours) noexcept;

    char pad0[0x8];            // 0x0000
    TESGlobal* GameYear;       // 0x0008
    TESGlobal* GameMonth;      // 0x0010
    TESGlobal* GameDay;        // 0x0018
    TESGlobal* GameHour;       // 0x0020
    TESGlobal* GameDaysPassed; // 0x0028
    TESGlobal* TimeScale;      // 0x0030
    uint32_t midnightsPassed;  // 0x0038, since the game started (not saved)
    float rawDaysPassed;       // 0x003C, whole days: GameDaysPassed = rawDaysPassed + GameHour / 24
};

static_assert(offsetof(TimeData, GameDaysPassed) == 0x28);
static_assert(offsetof(TimeData, midnightsPassed) == 0x38);
static_assert(offsetof(TimeData, rawDaysPassed) == 0x3C);
static_assert(sizeof(TimeData) == 0x40);
