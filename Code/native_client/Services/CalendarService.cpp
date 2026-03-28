#include "CalendarService.h"

#include <spdlog/spdlog.h>

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"
#include "protocol.h"

// TimeData global offsets (extracted from existing game headers)
// These are Skyrim global variables accessed via Address Library IDs.
// GameHour, GameDay, GameMonth, GameYear, TimeScale are TESGlobal forms
// whose float value is at offset 0x34 (TESGlobal::value).
// The pointer to each TESGlobal is stored in the Calendar singleton.
//
// Calendar singleton offsets (from TimeManager.h in existing codebase):
//   GameHour at offset 0x08
//   GameDay at offset 0x10
//   GameMonth at offset 0x18
//   GameYear at offset 0x20
//   TimeScale at offset 0x28
static constexpr uint64_t cCalendarGameHourOffset = 0x08;
static constexpr uint64_t cCalendarGameDayOffset = 0x10;
static constexpr uint64_t cCalendarGameMonthOffset = 0x18;
static constexpr uint64_t cCalendarGameYearOffset = 0x20;
static constexpr uint64_t cCalendarTimeScaleOffset = 0x28;
static constexpr uint64_t cTESGlobalValueOffset = 0x34;

CalendarService::CalendarService(GameReader& aReader, TcpClient& aClient)
    : m_reader(aReader)
    , m_client(aClient)
{
    spdlog::info("CalendarService initialized");
}

void CalendarService::OnHookEvent(const HookEventPacket& aPkt)
{
    switch (static_cast<HookOpcode>(aPkt.header.opcode)) {
    case HOOK_SIMULATE_TIME: {
        // Time simulation hook -- note event, poll handles actual reads
        spdlog::debug("CalendarService: simulate time hook received");
        break;
    }
    default:
        break;
    }
}

void CalendarService::Update(float aDeltaTime)
{
    m_pollTimer += aDeltaTime;
    if (m_pollTimer < cPollInterval)
        return;
    m_pollTimer = 0.0f;

    // Calendar singleton pointer -- uses sentinel formId 0xFFFF0002
    uint64_t calendarPtr = m_reader.LookupPointer(0xFFFF0002);
    if (calendarPtr == 0) {
        // Calendar pointer not yet resolved -- will be wired in Plan 06+
        return;
    }

    // Read each time component: Calendar->{field} is a TESGlobal*,
    // then TESGlobal->value (float at offset 0x34)
    float newHour = m_reader.ReadChain<float>(calendarPtr, cCalendarGameHourOffset, cTESGlobalValueOffset);
    float newDay = m_reader.ReadChain<float>(calendarPtr, cCalendarGameDayOffset, cTESGlobalValueOffset);
    float newMonth = m_reader.ReadChain<float>(calendarPtr, cCalendarGameMonthOffset, cTESGlobalValueOffset);
    float newYear = m_reader.ReadChain<float>(calendarPtr, cCalendarGameYearOffset, cTESGlobalValueOffset);
    float newTimeScale = m_reader.ReadChain<float>(calendarPtr, cCalendarTimeScaleOffset, cTESGlobalValueOffset);

    // Detect changes
    bool changed = (newHour != m_gameHour || newDay != m_gameDay);

    m_gameHour = newHour;
    m_gameDay = newDay;
    m_gameMonth = newMonth;
    m_gameYear = newYear;
    m_timeScale = newTimeScale;

    if (changed) {
        // TODO(Plan 07+): Send ServerTimeSettings via network transport
        spdlog::info("CalendarService: time={:.2f}h day={:.0f} month={:.0f} year={:.0f} scale={:.0f}",
                     m_gameHour, m_gameDay, m_gameMonth, m_gameYear, m_timeScale);
    }
}
