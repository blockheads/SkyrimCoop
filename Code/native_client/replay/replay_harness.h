#pragma once
#include <string>
#include <cstddef>
#include <entt/entt.hpp>
#include "game_bridge/proc_memory.h"
#include "game_bridge/pointer_table.h"
#include "game_bridge/game_reader.h"
#include "game_bridge/tcp_client.h"
#include "Services/WeatherService.h"
#include "Services/CalendarService.h"
#include "Services/QuestService.h"
#include "Services/CombatService.h"
#include "Services/InventoryService.h"
#include "Services/ActorValueService.h"
#include "Services/MagicService.h"
#include "Services/CharacterService.h"

// ReplayHarness: replays NDJSON hook event recordings through the full
// service stack without needing a live Skyrim process or TCP connection.
//
// NDJSON format (one JSON object per line):
//   {"op": 1, "args": [12345678]}        -- numeric opcode + uint64 args
//   {"op": "HOOK_ACTOR_ADDED", "args": [12345678]}  -- string opcode (future)
//
// Lines with "op" starting with "ctrl_" are skipped (control messages).
// Empty lines and malformed JSON are skipped with a warning.
class ReplayHarness {
public:
    ReplayHarness();

    // Replay all events from an NDJSON file. Returns event count.
    // Fast-forward: no delays between events (per D-02).
    size_t ReplayFile(const std::string& aPath);

    // Access for assertions
    entt::registry& GetRegistry() { return m_registry; }
    CharacterService& GetCharacterService() { return m_character; }
    PointerTable& GetPointerTable() { return m_pointerTable; }
    WeatherService& GetWeatherService() { return m_weather; }
    CalendarService& GetCalendarService() { return m_calendar; }
    QuestService& GetQuestService() { return m_quest; }
    CombatService& GetCombatService() { return m_combat; }
    InventoryService& GetInventoryService() { return m_inventory; }
    ActorValueService& GetActorValueService() { return m_actorValue; }
    MagicService& GetMagicService() { return m_magic; }

private:
    // Member order matters: stubs before services that depend on them
    ProcMemReader m_memReader;       // Never opened, fd=-1, reads return 0
    PointerTable m_pointerTable;     // Pure data, no I/O
    GameReader m_gameReader;         // Wraps m_memReader + m_pointerTable
    TcpClient m_tcpClient;           // Never connected, m_socket=-1, sends are no-op

    entt::registry m_registry;

    // All 8 services
    WeatherService m_weather;
    CalendarService m_calendar;
    QuestService m_quest;
    CombatService m_combat;
    InventoryService m_inventory;
    ActorValueService m_actorValue;
    MagicService m_magic;
    CharacterService m_character;

    static constexpr float kFixedDeltaTime = 1.0f / 60.0f;
};
