#pragma once
#include <cstdint>
#include <vector>

struct HookEventPacket;
class GameReader;
class TcpClient;

// Native Linux MagicService: handles magic/spell hook events from DLL relay
// and sends commands back for remote spell application.
// All game state reads use GameReader (/proc/pid/mem), no direct pointer dereference.
class MagicService {
public:
    MagicService(GameReader& aReader, TcpClient& aTcp);

    // Process a hook event from the DLL relay
    void OnHookEvent(const HookEventPacket& aPkt);

    // Periodic update (process queued effects, timeout active casts)
    void Update(float aDeltaTime);

    // Apply remote spell operations (from another player via network)
    void ApplyRemoteCastSpell(uint32_t aCasterFormId, uint32_t aSpellFormId, uint32_t aTargetFormId);
    void ApplyRemoteAddSpell(uint32_t aActorFormId, uint32_t aSpellFormId);
    void ApplyRemoteRemoveSpell(uint32_t aActorFormId, uint32_t aSpellFormId);

private:
    // Track active spell casts for each caster
    struct ActiveCast {
        uint32_t casterFormId;
        uint32_t spellFormId;
        float elapsedTime;
    };

    // Memory offsets for spell data
    // SpellItem inherits MagicItem -> TESForm
    // SpellItem::eCastingType at offset 0xA8 (Skyrim SE)
    // MagicEffect magnitude/duration from EffectItem struct
    static constexpr uint64_t kSpellCastingTypeOffset = 0xA8;
    static constexpr uint64_t kEffectMagnitudeOffset = 0x10;
    static constexpr uint64_t kEffectDurationOffset = 0x14;

    // Active cast timeout (concentration spells that never get an interrupt)
    static constexpr float kActiveCastTimeout = 30.0f;

    void HandleSpellCast(const uint64_t* apArgs, uint8_t aArgCount);
    void HandleInterruptCast(const uint64_t* apArgs, uint8_t aArgCount);
    void HandleAddTarget(const uint64_t* apArgs, uint8_t aArgCount);
    void HandleApplyEffect(const uint64_t* apArgs, uint8_t aArgCount);
    void HandleRemoveSpell(const uint64_t* apArgs, uint8_t aArgCount);

    // Send a command packet to the DLL relay
    void SendCommand(uint16_t aOpcode, const uint64_t* apArgs, uint8_t aArgCount);

    // Remove an active cast by caster formId
    void RemoveActiveCast(uint32_t aCasterFormId);

    GameReader& m_reader;
    TcpClient& m_tcp;

    // Active concentration spell casts being tracked
    std::vector<ActiveCast> m_activeCasts;
};
