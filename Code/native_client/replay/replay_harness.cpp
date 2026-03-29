#include "replay_harness.h"
#include "dispatch_hook_event.h"
#include "Systems/InterpolationSystem.h"
#include "Systems/AnimationSystem.h"
#include "json_protocol.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>

ReplayHarness::ReplayHarness()
    : m_gameReader(m_memReader, m_pointerTable)
    , m_weather(m_gameReader, m_tcpClient)
    , m_calendar(m_gameReader, m_tcpClient)
    , m_quest(m_gameReader, m_tcpClient)
    , m_combat(m_gameReader, m_tcpClient)
    , m_inventory(m_gameReader, m_tcpClient)
    , m_actorValue(m_gameReader, m_tcpClient)
    , m_magic(m_gameReader, m_tcpClient)
    , m_character(m_gameReader, m_tcpClient, m_registry)
{}

size_t ReplayHarness::ReplayFile(const std::string& aPath)
{
    std::ifstream file(aPath);
    if (!file.is_open()) {
        spdlog::error("ReplayHarness: cannot open {}", aPath);
        return 0;
    }

    size_t count = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        auto msg = nlohmann::json::parse(line, nullptr, false);
        if (msg.is_discarded()) {
            spdlog::warn("ReplayHarness: skipping malformed line {}", count + 1);
            continue;
        }

        if (!msg.contains(JsonField::kOp)) continue;

        const auto& op = msg[JsonField::kOp].get_ref<const std::string&>();
        if (op.starts_with("ctrl_")) continue;

        DispatchHookEvent(msg, m_gameReader, m_pointerTable,
                          m_weather, m_calendar, m_quest, m_combat,
                          m_inventory, m_actorValue, m_magic, m_character);

        // Fixed deltaTime update after each event
        m_weather.Update(kFixedDeltaTime);
        m_calendar.Update(kFixedDeltaTime);
        m_quest.Update(kFixedDeltaTime);
        m_combat.Update(kFixedDeltaTime);
        m_inventory.Update(kFixedDeltaTime);
        m_actorValue.Update(kFixedDeltaTime);
        m_magic.Update(kFixedDeltaTime);
        m_character.Update(kFixedDeltaTime);

        InterpolationSystem::Update(m_registry, kFixedDeltaTime);
        AnimationSystem::Update(m_registry, m_tcpClient, kFixedDeltaTime);

        count++;
    }
    return count;
}
