#include "ActorValueService.h"

#include <spdlog/spdlog.h>
#include <cmath>
#include <nlohmann/json.hpp>
#include "json_protocol.h"

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"

ActorValueService::ActorValueService(GameReader& aReader, TcpClient& aTcp)
    : m_reader(aReader), m_tcp(aTcp)
{
}

void ActorValueService::OnHookEvent(const nlohmann::json& aMsg)
{
    const auto& op = aMsg[JsonField::kOp].get_ref<const std::string&>();

    if (op == JsonOp::kRegenAttr) {
        uint32_t formId = aMsg.value(JsonField::kFormId, 0u);
        int32_t attrIndex = aMsg.value(JsonField::kType, 0);
        float regenAmount = aMsg.value(JsonField::kValue, 0.0f);

        if (formId == 0) {
            spdlog::warn("regen_attr: formId is 0");
            return;
        }

        // Update cached value (create entry if first regen event for this actor)
        auto& cached = m_cachedValues[formId];
        switch (static_cast<uint32_t>(attrIndex)) {
        case kHealth:  cached.health += regenAmount; break;
        case kMagicka: cached.magicka += regenAmount; break;
        case kStamina: cached.stamina += regenAmount; break;
        default: break;
        }

        spdlog::debug("Regen attr: actor={:#x} attr={} amount={:.2f}", formId, attrIndex, regenAmount);
        // TODO: Send actor value update message to embedded server / network layer
    }
}

void ActorValueService::Update(float aDeltaTime)
{
    m_timeSinceLastSync += aDeltaTime;
    if (m_timeSinceLastSync < kSyncInterval)
        return;

    m_timeSinceLastSync = 0.0f;
    PollActorValues();
}

void ActorValueService::PollActorValues()
{
    // Iterate all actors in the pointer table and read their primary actor values
    m_reader.GetPointers().ForEach([this](uint32_t formId, uint64_t actorPtr) {
        float health = ReadActorValue(actorPtr, kHealth);
        float magicka = ReadActorValue(actorPtr, kMagicka);
        float stamina = ReadActorValue(actorPtr, kStamina);

        auto& cached = m_cachedValues[formId];

        bool changed = false;

        // Use small epsilon for float comparison to avoid noise
        constexpr float kEpsilon = 0.01f;

        if (std::fabs(health - cached.health) > kEpsilon) {
            cached.health = health;
            changed = true;
        }
        if (std::fabs(magicka - cached.magicka) > kEpsilon) {
            cached.magicka = magicka;
            changed = true;
        }
        if (std::fabs(stamina - cached.stamina) > kEpsilon) {
            cached.stamina = stamina;
            changed = true;
        }

        if (changed) {
            spdlog::debug("Actor value change: formId={:#x} hp={:.1f} mp={:.1f} sp={:.1f}",
                          formId, health, magicka, stamina);
            // TODO: Send delta actor value changes to embedded server / network layer
        }
    });
}

float ActorValueService::ReadActorValue(uint64_t aActorPtr, uint32_t aValueIndex)
{
    // Read from the actor's cached value array at known offset
    // Actor + kActorValueBaseOffset + (index * sizeof(float))
    // This offset corresponds to the base actor values array in Skyrim SE
    uint64_t offset = kActorValueBaseOffset + (static_cast<uint64_t>(aValueIndex) * kActorValueStride);
    return m_reader.Read<float>(aActorPtr, offset);
}

void ActorValueService::SendSetActorValueCommand(uint64_t aActorPtr, uint32_t aAvIndex, float aValue)
{
    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdSetActorValue},
        {JsonField::kActorPtr, aActorPtr},
        {JsonField::kAvIndex, aAvIndex},
        {JsonField::kValue, aValue}
    };
    m_tcp.SendJsonLine(cmd);
    spdlog::debug("Sent cmd_set_actor_value: ptr={:#x} av={} val={:.2f}", aActorPtr, aAvIndex, aValue);
}

void ActorValueService::ApplyRemoteSetActorValue(uint32_t aActorFormId, uint32_t aAvIndex, float aValue)
{
    uint64_t actorPtr = m_reader.LookupPointer(aActorFormId);
    if (actorPtr == 0) {
        spdlog::warn("ApplyRemoteSetActorValue: could not resolve ptr for formId={:#x}", aActorFormId);
        return;
    }

    SendSetActorValueCommand(actorPtr, aAvIndex, aValue);
}
