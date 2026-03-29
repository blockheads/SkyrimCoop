#include "replay_harness.h"
#include "dispatch_hook_event.h"
#include "Systems/InterpolationSystem.h"
#include "Systems/AnimationSystem.h"
#include "protocol.h"
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

        if (!msg.contains("op")) continue;

        // Skip control messages
        if (msg["op"].is_string()) {
            const auto& op = msg["op"].get_ref<const std::string&>();
            if (op.starts_with("ctrl_")) continue;
        }

        // Build HookEventPacket from JSON
        HookEventPacket hookPkt{};
        if (msg["op"].is_number()) {
            hookPkt.header.opcode = msg["op"].get<uint16_t>();
        } else {
            // String opcodes not yet supported -- skip
            spdlog::warn("ReplayHarness: skipping string opcode at line {}", count + 1);
            continue;
        }

        if (msg.contains("args") && msg["args"].is_array()) {
            const auto& args = msg["args"];
            hookPkt.argCount = static_cast<uint8_t>(std::min(args.size(), size_t(8)));
            for (uint8_t i = 0; i < hookPkt.argCount; i++) {
                hookPkt.args[i] = args[i].get<uint64_t>();
            }
        }

        // Set header length to match what the binary protocol expects
        hookPkt.header.length = static_cast<uint16_t>(
            sizeof(hookPkt.argCount) + sizeof(hookPkt._pad) +
            hookPkt.argCount * sizeof(uint64_t));

        DispatchHookEvent(hookPkt, m_gameReader, m_pointerTable,
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
