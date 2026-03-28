#include "ActorValueService.h"
#include "game_bridge/game_reader.h"
#include "game_bridge/tcp_client.h"
#include "protocol.h"

#include <spdlog/spdlog.h>
#include <cstring>
#include <cmath>

ActorValueService::ActorValueService(GameReader& aReader, TcpClient& aTcp)
    : m_reader(aReader), m_tcp(aTcp)
{
}

void ActorValueService::OnHookEvent(const HookEventPacket& aPkt)
{
    if (static_cast<HookOpcode>(aPkt.header.opcode) == HOOK_REGEN_ATTR) {
        HandleRegenAttr(aPkt.args, aPkt.argCount);
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

void ActorValueService::HandleRegenAttr(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] actor ptr, [1] attr index, [2] regen amount (as uint64_t-encoded float)
    if (aArgCount < 3) {
        spdlog::warn("HOOK_REGEN_ATTR: insufficient args ({})", aArgCount);
        return;
    }

    uint64_t actorPtr = apArgs[0];
    uint32_t attrIndex = static_cast<uint32_t>(apArgs[1]);

    // Decode regen amount from uint64_t bit pattern
    float regenAmount = 0.0f;
    uint32_t regenBits = static_cast<uint32_t>(apArgs[2]);
    std::memcpy(&regenAmount, &regenBits, sizeof(float));

    uint32_t actorFormId = m_reader.ReadFormId(actorPtr);
    if (actorFormId == 0) {
        spdlog::warn("HOOK_REGEN_ATTR: failed to read actor formId at ptr={:#x}", actorPtr);
        return;
    }

    // Update cached value if we have one
    auto it = m_cachedValues.find(actorFormId);
    if (it != m_cachedValues.end()) {
        switch (attrIndex) {
        case kHealth:  it->second.health += regenAmount; break;
        case kMagicka: it->second.magicka += regenAmount; break;
        case kStamina: it->second.stamina += regenAmount; break;
        default: break;
        }
    }

    spdlog::debug("Regen attr: actor={:#x} attr={} amount={:.2f}", actorFormId, attrIndex, regenAmount);
    // TODO: Send actor value update message to embedded server / network layer
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
    // Encode float value as uint64_t bit pattern for command args
    uint64_t valueBits = 0;
    uint32_t floatBits = 0;
    std::memcpy(&floatBits, &aValue, sizeof(float));
    valueBits = static_cast<uint64_t>(floatBits);

    CommandPacket cmd{};
    cmd.header.opcode = CMD_SET_ACTOR_VALUE;
    cmd.header.length = sizeof(CommandPacket) - sizeof(PacketHeader);
    cmd.argCount = 3;
    cmd.args[0] = aActorPtr;
    cmd.args[1] = static_cast<uint64_t>(aAvIndex);
    cmd.args[2] = valueBits;

    m_tcp.Send(&cmd, sizeof(cmd));
    spdlog::debug("Sent CMD_SET_ACTOR_VALUE: ptr={:#x} av={} val={:.2f}", aActorPtr, aAvIndex, aValue);
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
