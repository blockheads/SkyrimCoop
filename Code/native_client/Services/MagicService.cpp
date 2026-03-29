#include "MagicService.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "json_protocol.h"

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"

MagicService::MagicService(GameReader& aReader, TcpClient& aTcp)
    : m_reader(aReader), m_tcp(aTcp)
{
}

void MagicService::OnHookEvent(const nlohmann::json& aMsg)
{
    const auto& op = aMsg[JsonField::kOp].get_ref<const std::string&>();

    if (op == JsonOp::kSpellCast) {
        uint32_t casterFormId = aMsg.value(JsonField::kCasterFormId, 0u);
        uint32_t spellFormId = aMsg.value(JsonField::kSpellFormId, 0u);
        uint64_t spellPtr = aMsg.value(JsonField::kSpellPtr, 0ull);

        if (casterFormId == 0 || spellFormId == 0) {
            spdlog::warn("spell_cast: formIds are zero (caster={:#x}, spell={:#x})",
                          casterFormId, spellFormId);
            return;
        }

        // TODO(09.1): move spell casting type read to DLL
        // Read spell casting type to determine if this is a concentration spell
        uint32_t castingType = 0;
        if (spellPtr != 0) {
            castingType = m_reader.Read<uint32_t>(spellPtr, kSpellCastingTypeOffset);
        }

        // Track as active cast
        ActiveCast cast{};
        cast.casterFormId = casterFormId;
        cast.spellFormId = spellFormId;
        cast.elapsedTime = 0.0f;
        m_activeCasts.push_back(cast);

        spdlog::debug("Spell cast: caster={:#x} spell={:#x} castType={}", casterFormId, spellFormId, castingType);
        // TODO: Send spell cast sync message to embedded server / network layer

    } else if (op == JsonOp::kInterruptCast) {
        uint32_t casterFormId = aMsg.value(JsonField::kCasterFormId, 0u);
        bool refund = aMsg.value(JsonField::kRefund, false);

        if (casterFormId == 0) {
            spdlog::warn("interrupt_cast: caster formId is 0");
            return;
        }

        // Remove from active casts
        RemoveActiveCast(casterFormId);

        spdlog::debug("Interrupt cast: caster={:#x} refund={}", casterFormId, refund);
        // TODO: Send interrupt cast message to embedded server / network layer

    } else if (op == JsonOp::kRemoveSpell) {
        uint32_t actorFormId = aMsg.value(JsonField::kActorFormId, 0u);
        uint32_t spellFormId = aMsg.value(JsonField::kSpellFormId, 0u);

        if (actorFormId == 0 || spellFormId == 0) {
            spdlog::warn("remove_spell: formIds are zero (actor={:#x}, spell={:#x})",
                          actorFormId, spellFormId);
            return;
        }

        spdlog::debug("Remove spell: actor={:#x} spell={:#x}", actorFormId, spellFormId);
        // TODO: Send spell removal message to embedded server / network layer

    } else if (op == JsonOp::kApplyEffect) {
        uint32_t actorFormId = aMsg.value(JsonField::kActorFormId, 0u);
        uint32_t casterFormId = aMsg.value(JsonField::kCasterFormId, 0u);
        uint64_t effectPtr = aMsg.value(JsonField::kEffectPtr, 0ull);

        if (actorFormId == 0 || casterFormId == 0) {
            spdlog::warn("apply_effect: formIds are zero (actor={:#x}, caster={:#x})",
                          actorFormId, casterFormId);
            return;
        }

        // TODO(09.1): move effect magnitude/duration reads to DLL
        // Read effect magnitude and duration from the MagicEffect struct
        float magnitude = 0.0f;
        float duration = 0.0f;
        if (effectPtr != 0) {
            magnitude = m_reader.Read<float>(effectPtr, kEffectMagnitudeOffset);
            duration = m_reader.Read<float>(effectPtr, kEffectDurationOffset);
        }

        spdlog::debug("Apply effect: actor={:#x} caster={:#x} mag={:.2f} dur={:.2f}",
                      actorFormId, casterFormId, magnitude, duration);
        // TODO: Send effect application message to embedded server / network layer
    }
}

void MagicService::Update(float aDeltaTime)
{
    // Age active casts and remove expired ones
    for (auto& cast : m_activeCasts) {
        cast.elapsedTime += aDeltaTime;
    }

    // Remove timed-out casts (concentration spells that were never interrupted)
    auto it = std::remove_if(m_activeCasts.begin(), m_activeCasts.end(),
        [](const ActiveCast& c) { return c.elapsedTime > kActiveCastTimeout; });

    if (it != m_activeCasts.end()) {
        for (auto expired = it; expired != m_activeCasts.end(); ++expired) {
            spdlog::debug("Active cast timed out: caster={:#x} spell={:#x}",
                          expired->casterFormId, expired->spellFormId);
        }
        m_activeCasts.erase(it, m_activeCasts.end());
    }
}

// --- Remote spell application (sending JSON commands to DLL) ---

void MagicService::RemoveActiveCast(uint32_t aCasterFormId)
{
    auto it = std::remove_if(m_activeCasts.begin(), m_activeCasts.end(),
        [aCasterFormId](const ActiveCast& c) { return c.casterFormId == aCasterFormId; });
    m_activeCasts.erase(it, m_activeCasts.end());
}

void MagicService::ApplyRemoteCastSpell(uint32_t aCasterFormId, uint32_t aSpellFormId, uint32_t aTargetFormId)
{
    uint64_t casterPtr = m_reader.LookupPointer(aCasterFormId);
    if (casterPtr == 0) {
        spdlog::warn("ApplyRemoteCastSpell: could not resolve caster ptr for formId={:#x}", aCasterFormId);
        return;
    }

    // cmd_cast_spell args: caster ptr, spell formId (not ptr -- DLL resolves), target ptr
    uint64_t targetPtr = 0;
    if (aTargetFormId != 0) {
        targetPtr = m_reader.LookupPointer(aTargetFormId);
    }

    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdCastSpell},
        {JsonField::kCasterPtr, casterPtr},
        {JsonField::kSpellFormId, aSpellFormId},
        {JsonField::kTargetPtr, targetPtr}
    };
    m_tcp.SendJsonLine(cmd);
    spdlog::debug("Sent cmd_cast_spell: caster={:#x} spell={:#x} target={:#x}",
                  aCasterFormId, aSpellFormId, aTargetFormId);
}

void MagicService::ApplyRemoteAddSpell(uint32_t aActorFormId, uint32_t aSpellFormId)
{
    uint64_t actorPtr = m_reader.LookupPointer(aActorFormId);
    if (actorPtr == 0) {
        spdlog::warn("ApplyRemoteAddSpell: could not resolve actor ptr for formId={:#x}", aActorFormId);
        return;
    }

    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdAddSpell},
        {JsonField::kActorPtr, actorPtr},
        {JsonField::kSpellFormId, aSpellFormId}
    };
    m_tcp.SendJsonLine(cmd);
    spdlog::debug("Sent cmd_add_spell: actor={:#x} spell={:#x}", aActorFormId, aSpellFormId);
}

void MagicService::ApplyRemoteRemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId)
{
    uint64_t actorPtr = m_reader.LookupPointer(aActorFormId);
    if (actorPtr == 0) {
        spdlog::warn("ApplyRemoteRemoveSpell: could not resolve actor ptr for formId={:#x}", aActorFormId);
        return;
    }

    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdRemoveSpell},
        {JsonField::kActorPtr, actorPtr},
        {JsonField::kSpellFormId, aSpellFormId}
    };
    m_tcp.SendJsonLine(cmd);
    spdlog::debug("Sent cmd_remove_spell: actor={:#x} spell={:#x}", aActorFormId, aSpellFormId);
}
