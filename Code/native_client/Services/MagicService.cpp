#include "MagicService.h"
#include "game_bridge/game_reader.h"
#include "game_bridge/tcp_client.h"
#include "protocol.h"

#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>

MagicService::MagicService(GameReader& aReader, TcpClient& aTcp)
    : m_reader(aReader), m_tcp(aTcp)
{
}

void MagicService::OnHookEvent(const HookEventPacket& aPkt)
{
    const uint8_t argCount = aPkt.argCount;
    const uint64_t* pArgs = aPkt.args;

    switch (static_cast<HookOpcode>(aPkt.header.opcode)) {
    case HOOK_SPELL_CAST:
        HandleSpellCast(pArgs, argCount);
        break;
    case HOOK_INTERRUPT_CAST:
        HandleInterruptCast(pArgs, argCount);
        break;
    case HOOK_ADD_TARGET:
        HandleAddTarget(pArgs, argCount);
        break;
    case HOOK_APPLY_EFFECT:
        HandleApplyEffect(pArgs, argCount);
        break;
    case HOOK_REMOVE_SPELL:
        HandleRemoveSpell(pArgs, argCount);
        break;
    default:
        break;
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

// --- Hook handlers ---

void MagicService::HandleSpellCast(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] caster ptr, [1] spell ptr
    if (aArgCount < 2) {
        spdlog::warn("HOOK_SPELL_CAST: insufficient args ({})", aArgCount);
        return;
    }

    uint32_t casterFormId = m_reader.ReadFormId(apArgs[0]);
    uint32_t spellFormId = m_reader.ReadFormId(apArgs[1]);

    if (casterFormId == 0 || spellFormId == 0) {
        spdlog::warn("HOOK_SPELL_CAST: failed to read formIds (caster={:#x}, spell={:#x})",
                      casterFormId, spellFormId);
        return;
    }

    // Read spell casting type to determine if this is a concentration spell
    uint32_t castingType = m_reader.Read<uint32_t>(apArgs[1], kSpellCastingTypeOffset);

    // Track as active cast
    ActiveCast cast{};
    cast.casterFormId = casterFormId;
    cast.spellFormId = spellFormId;
    cast.elapsedTime = 0.0f;
    m_activeCasts.push_back(cast);

    spdlog::debug("Spell cast: caster={:#x} spell={:#x} castType={}", casterFormId, spellFormId, castingType);
    // TODO: Send spell cast sync message to embedded server / network layer
}

void MagicService::HandleInterruptCast(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] caster ptr
    if (aArgCount < 1) {
        spdlog::warn("HOOK_INTERRUPT_CAST: insufficient args ({})", aArgCount);
        return;
    }

    uint32_t casterFormId = m_reader.ReadFormId(apArgs[0]);
    if (casterFormId == 0) {
        spdlog::warn("HOOK_INTERRUPT_CAST: failed to read caster formId at ptr={:#x}", apArgs[0]);
        return;
    }

    // Remove from active casts
    RemoveActiveCast(casterFormId);

    spdlog::debug("Interrupt cast: caster={:#x}", casterFormId);
    // TODO: Send interrupt cast message to embedded server / network layer
}

void MagicService::HandleAddTarget(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] target ptr, [1] target data ptr (MagicTarget::AddTargetData)
    if (aArgCount < 2) {
        spdlog::warn("HOOK_ADD_TARGET: insufficient args ({})", aArgCount);
        return;
    }

    uint32_t targetFormId = m_reader.ReadFormId(apArgs[0]);
    if (targetFormId == 0) {
        spdlog::warn("HOOK_ADD_TARGET: failed to read target formId at ptr={:#x}", apArgs[0]);
        return;
    }

    // Read magic effect data from the target data struct via memory
    // AddTargetData layout includes spell ptr, effect item ptr, magnitude, etc.
    // Read spell formId from the spell pointer within the target data
    uint64_t spellPtr = m_reader.Read<uint64_t>(apArgs[1], 0x08); // pSpell offset in AddTargetData
    uint32_t spellFormId = 0;
    if (spellPtr != 0) {
        spellFormId = m_reader.ReadFormId(spellPtr);
    }

    // Read effect item pointer and magnitude
    uint64_t effectPtr = m_reader.Read<uint64_t>(apArgs[1], 0x10); // pEffectItem offset
    float magnitude = m_reader.Read<float>(apArgs[1], 0x18); // fMagnitude offset

    spdlog::debug("Add target: target={:#x} spell={:#x} effect={:#x} magnitude={:.2f}",
                  targetFormId, spellFormId, effectPtr, magnitude);
    // TODO: Send add target message to embedded server / network layer
}

void MagicService::HandleApplyEffect(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] actor ptr, [1] magic item ptr, [2] effect ptr
    if (aArgCount < 3) {
        spdlog::warn("HOOK_APPLY_EFFECT: insufficient args ({})", aArgCount);
        return;
    }

    uint32_t actorFormId = m_reader.ReadFormId(apArgs[0]);
    uint32_t magicItemFormId = m_reader.ReadFormId(apArgs[1]);

    if (actorFormId == 0 || magicItemFormId == 0) {
        spdlog::warn("HOOK_APPLY_EFFECT: failed to read formIds (actor={:#x}, magicItem={:#x})",
                      actorFormId, magicItemFormId);
        return;
    }

    // Read effect magnitude and duration from the MagicEffect struct
    float magnitude = m_reader.Read<float>(apArgs[2], kEffectMagnitudeOffset);
    float duration = m_reader.Read<float>(apArgs[2], kEffectDurationOffset);

    spdlog::debug("Apply effect: actor={:#x} magicItem={:#x} mag={:.2f} dur={:.2f}",
                  actorFormId, magicItemFormId, magnitude, duration);
    // TODO: Send effect application message to embedded server / network layer
}

void MagicService::HandleRemoveSpell(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] actor ptr, [1] spell ptr
    if (aArgCount < 2) {
        spdlog::warn("HOOK_REMOVE_SPELL: insufficient args ({})", aArgCount);
        return;
    }

    uint32_t actorFormId = m_reader.ReadFormId(apArgs[0]);
    uint32_t spellFormId = m_reader.ReadFormId(apArgs[1]);

    if (actorFormId == 0 || spellFormId == 0) {
        spdlog::warn("HOOK_REMOVE_SPELL: failed to read formIds (actor={:#x}, spell={:#x})",
                      actorFormId, spellFormId);
        return;
    }

    spdlog::debug("Remove spell: actor={:#x} spell={:#x}", actorFormId, spellFormId);
    // TODO: Send spell removal message to embedded server / network layer
}

// --- Remote spell application (sending commands to DLL) ---

void MagicService::SendCommand(uint16_t aOpcode, const uint64_t* apArgs, uint8_t aArgCount)
{
    CommandPacket cmd{};
    cmd.header.opcode = aOpcode;
    cmd.header.length = sizeof(CommandPacket) - sizeof(PacketHeader);
    cmd.argCount = aArgCount;
    for (uint8_t i = 0; i < aArgCount && i < 8; ++i)
        cmd.args[i] = apArgs[i];

    m_tcp.Send(&cmd, sizeof(cmd));
}

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

    // CMD_CAST_SPELL args: caster ptr, spell formId (not ptr -- DLL resolves), target ptr
    uint64_t targetPtr = 0;
    if (aTargetFormId != 0) {
        targetPtr = m_reader.LookupPointer(aTargetFormId);
    }

    uint64_t args[3] = {casterPtr, static_cast<uint64_t>(aSpellFormId), targetPtr};
    SendCommand(CMD_CAST_SPELL, args, 3);
    spdlog::debug("Sent CMD_CAST_SPELL: caster={:#x} spell={:#x} target={:#x}",
                  aCasterFormId, aSpellFormId, aTargetFormId);
}

void MagicService::ApplyRemoteAddSpell(uint32_t aActorFormId, uint32_t aSpellFormId)
{
    uint64_t actorPtr = m_reader.LookupPointer(aActorFormId);
    if (actorPtr == 0) {
        spdlog::warn("ApplyRemoteAddSpell: could not resolve actor ptr for formId={:#x}", aActorFormId);
        return;
    }

    uint64_t args[2] = {actorPtr, static_cast<uint64_t>(aSpellFormId)};
    SendCommand(CMD_ADD_SPELL, args, 2);
    spdlog::debug("Sent CMD_ADD_SPELL: actor={:#x} spell={:#x}", aActorFormId, aSpellFormId);
}

void MagicService::ApplyRemoteRemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId)
{
    uint64_t actorPtr = m_reader.LookupPointer(aActorFormId);
    if (actorPtr == 0) {
        spdlog::warn("ApplyRemoteRemoveSpell: could not resolve actor ptr for formId={:#x}", aActorFormId);
        return;
    }

    uint64_t args[2] = {actorPtr, static_cast<uint64_t>(aSpellFormId)};
    SendCommand(CMD_REMOVE_SPELL, args, 2);
    spdlog::debug("Sent CMD_REMOVE_SPELL: actor={:#x} spell={:#x}", aActorFormId, aSpellFormId);
}
