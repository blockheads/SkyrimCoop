#pragma once
#include <cstdint>
#include <unordered_map>

struct HookEventPacket;
class GameReader;
class TcpClient;

// Native Linux ActorValueService: periodically polls actor health/magicka/stamina
// via /proc/pid/mem and syncs changes. Handles HOOK_REGEN_ATTR events.
// All game state reads use GameReader, no direct pointer dereference.
class ActorValueService {
public:
    ActorValueService(GameReader& aReader, TcpClient& aTcp);

    // Process a hook event from the DLL relay
    void OnHookEvent(const HookEventPacket& aPkt);

    // Periodic update -- polls actor values at kSyncInterval
    void Update(float aDeltaTime);

    // Apply remote actor value change (from another player via network)
    void ApplyRemoteSetActorValue(uint32_t aActorFormId, uint32_t aAvIndex, float aValue);

private:
    // Skyrim actor value indices for the three primary attributes
    static constexpr uint32_t kHealth = 24;
    static constexpr uint32_t kMagicka = 25;
    static constexpr uint32_t kStamina = 26;

    // Actor value array offset within Actor struct
    // Actor inherits MagicTarget -> ActorValueOwner
    // The cached actor values are stored at Actor + 0x9F0 (actorValueBase array)
    // Current values: base + offset per AV index
    static constexpr uint64_t kActorValueBaseOffset = 0x9F0;
    static constexpr uint64_t kActorValueStride = 4; // float per entry

    // Sync interval for periodic polling
    static constexpr float kSyncInterval = 0.25f; // 250ms

    struct CachedActorValues {
        float health = 0.0f;
        float magicka = 0.0f;
        float stamina = 0.0f;
    };

    void HandleRegenAttr(const uint64_t* apArgs, uint8_t aArgCount);
    void PollActorValues();
    float ReadActorValue(uint64_t aActorPtr, uint32_t aValueIndex);

    // Send a command to the DLL relay
    void SendSetActorValueCommand(uint64_t aActorPtr, uint32_t aAvIndex, float aValue);

    GameReader& m_reader;
    TcpClient& m_tcp;
    float m_timeSinceLastSync = 0.0f;

    // Cached values per actor formId to detect changes
    std::unordered_map<uint32_t, CachedActorValues> m_cachedValues;
};
