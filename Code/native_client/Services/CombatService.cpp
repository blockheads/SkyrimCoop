#include "CombatService.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "json_protocol.h"

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"

CombatService::CombatService(GameReader& aReader, TcpClient& aClient)
    : m_reader(aReader)
    , m_client(aClient)
{
    spdlog::info("CombatService initialized");
}

void CombatService::OnHookEvent(const nlohmann::json& aMsg)
{
    const auto& op = aMsg[JsonField::kOp].get_ref<const std::string&>();

    if (op == JsonOp::kDamageActor) {
        uint32_t formId = aMsg.value(JsonField::kFormId, 0u);
        float damage = aMsg.value(JsonField::kDamage, 0.0f);

        if (formId == 0) {
            spdlog::debug("CombatService: damage_actor with formId=0 (combat controller update)");
        } else {
            spdlog::debug("CombatService: damage_actor formId={:#x} damage={:.2f}", formId, damage);
            // TODO: Send damage sync message to embedded server / network layer
        }
        m_damageEventCount++;
    } else if (op == JsonOp::kProjectile) {
        uint64_t dataPtr = aMsg.value(JsonField::kDataPtr, 0ull);
        spdlog::debug("CombatService: projectile launch dataPtr={:#x}", dataPtr);
        m_projectileEventCount++;
        // TODO: Send projectile sync message to embedded server / network layer
    } else if (op == JsonOp::kAddTarget) {
        uint32_t targetFormId = aMsg.value(JsonField::kTargetFormId, 0u);
        uint64_t hitDataPtr = aMsg.value(JsonField::kHitDataPtr, 0ull);
        spdlog::debug("CombatService: add_target targetFormId={:#x} hitDataPtr={:#x}",
                      targetFormId, hitDataPtr);
        // TODO: Send add target message to embedded server / network layer
    }
}

void CombatService::SendDamageActor(uint64_t aActorPtr, float aDamage)
{
    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdDamageActor},
        {JsonField::kActorPtr, aActorPtr},
        {JsonField::kDamage, aDamage}
    };
    m_client.SendJsonLine(cmd);
    spdlog::debug("CombatService: sent cmd_damage_actor ptr={:#x} damage={:.2f}", aActorPtr, aDamage);
}

void CombatService::SendLaunchProjectile(uint64_t aDataPtr)
{
    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdLaunchProjectile},
        {JsonField::kDataPtr, aDataPtr}
    };
    m_client.SendJsonLine(cmd);
    spdlog::debug("CombatService: sent cmd_launch_projectile dataPtr={:#x}", aDataPtr);
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
