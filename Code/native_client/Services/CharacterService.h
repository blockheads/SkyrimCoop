#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <entt/entt.hpp>

#include <nlohmann/json_fwd.hpp>

class GameReader;
class TcpClient;
class LoopbackTransport;

namespace TiltedPhoques { struct Client; }

// ECS components for actor tracking in the native client.
// These are self-contained -- no game API calls, just pure data.

struct LocalComponent {
    uint32_t formId = 0;
    uint64_t gamePtr = 0;
    bool isDead = false;
    bool isWeaponDrawn = false;
};

struct RemoteComponent {
    uint32_t formId = 0;
    uint64_t gamePtr = 0;    // in-game Actor* pointer (for DLL commands)
    uint32_t serverId = 0;   // server-assigned entity id
};

struct InterpolationComponent {
    glm::vec3 targetPos{0.0f};
    glm::vec3 currentPos{0.0f};
    glm::vec2 targetRot{0.0f};
    glm::vec2 currentRot{0.0f};
    bool initialized = false;
};

struct AnimationComponent {
    uint32_t lastActionId = 0;
    uint32_t lastTargetFormId = 0;
    float lastActionTimestamp = 0.0f;
    bool dirty = false;
};

// CharacterService: Core multiplayer sync service.
// Handles actor lifecycle, movement sync at 100ms, animation variable sync.
// All state reads via /proc/pid/mem (GameReader), all state writes via DLL commands.
class CharacterService {
public:
    CharacterService(GameReader& aReader, TcpClient& aClient, entt::registry& aRegistry);

    // Dispatch hook events from DLL (JSON wire protocol)
    void OnHookEvent(const nlohmann::json& aMsg);

    // Periodic update: movement sync, interpolation feed
    void Update(float aDeltaTime);

    // Set loopback transport for sending ClientMessages to embedded GameServer (host mode)
    void SetLoopback(LoopbackTransport* apLoopback) noexcept { m_pLoopback = apLoopback; }

    // Set enet6 client for sending ClientMessages to remote host (join mode)
    void SetJoinClient(TiltedPhoques::Client* apClient) noexcept { m_pJoinClient = apClient; }

    // Network message handlers (called when messages arrive from server)
    void OnAssignCharacter(uint32_t aServerId, uint32_t aFormId, const glm::vec3& aPosition, const glm::vec2& aRotation);
    void OnRemoveCharacter(uint32_t aServerId);
    void OnReferencesMoveRequest(uint32_t aServerId, const glm::vec3& aPosition, const glm::vec2& aRotation);
    void OnRemoteAnimationUpdate(uint32_t aServerId, uint32_t aActionId, uint32_t aTargetFormId);

private:
    // Actor lifecycle handlers (formId resolved DLL-side, passed in from packet args)
    void HandleActorAdded(uint64_t aActorPtr, uint32_t aFormId);
    void HandleCharacterCtor(uint64_t aActorPtr, uint32_t aFormId);
    void HandleSpawnActor(uint64_t aActorPtr, uint32_t aFormId);
    void HandleActorRemoved(uint64_t aActorPtr, uint32_t aFormId);
    void HandleDeathItems(uint64_t aActorPtr, uint32_t aFormId);

    // Action/animation handler
    void HandlePerformAction(uint32_t aActorFormId, uint32_t aActionFormId);

    // Periodic sync routines
    void SyncMovement();
    void ApplyRemotePositions();

    // Send a cmd_set_position JSON command to the DLL for a remote actor
    void SendSetPosition(uint64_t aActorPtr, const glm::vec3& aPos, const glm::vec2& aRot);

    // Send a cmd_play_animation JSON command to the DLL for a remote actor
    void SendPlayAnimation(uint64_t aActorPtr, uint32_t aActionId, uint32_t aTargetFormId);

    GameReader& m_reader;
    TcpClient& m_client;
    entt::registry& m_registry;
    LoopbackTransport* m_pLoopback{nullptr};
    TiltedPhoques::Client* m_pJoinClient{nullptr};

    // Movement sync timer
    float m_moveSyncTimer = 0.0f;
    static constexpr float kMoveSyncInterval = 0.1f;  // 100ms

    // Local player tracking
    uint64_t m_localPlayerPtr = 0;
    uint32_t m_localPlayerFormId = 0;

    // Cache: last sent positions to detect changes
    struct CachedPosition {
        glm::vec3 position{0.0f};
        glm::vec2 rotation{0.0f};
    };
    std::vector<std::pair<uint32_t, CachedPosition>> m_cachedLocalPositions;

    // Position+rotation cache: populated by HOOK_SET_POSITION events, consumed by SyncMovement
    struct CachedPosRot {
        glm::vec3 position{0.0f};
        glm::vec2 rotation{0.0f};  // x = pitch, y = yaw (rotZ)
    };
    std::unordered_map<uint32_t, CachedPosRot> m_positionCache;

    // Game memory offsets (Skyrim SE)
    static constexpr uint64_t kFormIdOffset = 0x14;
    static constexpr uint64_t kPositionOffset = 0x54;  // TESObjectREFR::pos (NiPoint3)
    static constexpr uint64_t kRotationXOffset = 0x60; // TESObjectREFR::rot.x
    static constexpr uint64_t kRotationZOffset = 0x68; // TESObjectREFR::rot.z
    // Player singleton formId
    static constexpr uint32_t kPlayerFormId = 0x14;
};
