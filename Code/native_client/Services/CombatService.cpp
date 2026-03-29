#include "CombatService.h"

#include <spdlog/spdlog.h>
#include <cstring>
#include <nlohmann/json.hpp>
#include "json_protocol.h"

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"
#include "protocol.h"

// Game struct offsets
static constexpr uint64_t cFormIdOffset = 0x14;

// Utility: extract float from uint64_t (reverse of FloatToU64 in DLL)
static float U64ToFloat(uint64_t aVal)
{
    float f = 0.0f;
    memcpy(&f, &aVal, sizeof(float));
    return f;
}

CombatService::CombatService(GameReader& aReader, TcpClient& aClient)
    : m_reader(aReader)
    , m_client(aClient)
{
    spdlog::info("CombatService initialized");
}

void CombatService::OnHookEvent(const nlohmann::json& aMsg)
{
    // TODO(Plan-04): Implement JSON dispatch for CombatService
    const auto& op = aMsg.value(JsonField::kOp, std::string{});
    spdlog::debug("[CombatService] stub OnHookEvent: op={}", op);
}

void CombatService::Update(float /*aDeltaTime*/)
{
    // Combat is event-driven via hooks, not polled.
    // Periodic logging for debugging:
    static uint32_t s_lastLoggedDamage = 0;
    if (m_damageEventCount > s_lastLoggedDamage + 10) {
        spdlog::debug("CombatService: {} damage events, {} projectile events total",
                      m_damageEventCount, m_projectileEventCount);
        s_lastLoggedDamage = m_damageEventCount;
    }
}
