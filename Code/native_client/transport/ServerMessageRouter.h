#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <TiltedCore/Meta.hpp>

struct ServerMessageRouter
{
    TP_NOCOPYMOVE(ServerMessageRouter);

    ServerMessageRouter() = default;

    // Process raw server message bytes (from loopback callback)
    void OnServerMessage(const void* apData, uint32_t aSize);

    // Opcode-specific callbacks (set by native services)
    using AuthCallback = std::function<void(bool aAccepted, uint32_t aPlayerId)>;
    using MoveCallback = std::function<void(uint32_t aServerId, float aX, float aY, float aZ, float aRx, float aRy)>;
    using AssignCallback = std::function<void(uint32_t aServerId, uint32_t aFormId,
                                              float aX, float aY, float aZ,
                                              float aRx, float aRy, bool aOwner)>;

    // Phase 16: Extended SpawnCallback with appearance data for dual-path routing (D-02)
    using SpawnCallback = std::function<void(uint32_t aServerId, uint32_t aFormId,
                                             float aX, float aY, float aZ,
                                             float aRx, float aRy,
                                             bool aIsPlayer,
                                             const std::string& aAppearanceBuffer,
                                             uint32_t aChangeFlags,
                                             const std::string& aFaceTintsBuffer)>;
    using RemoveCallback = std::function<void(uint32_t aServerId)>;

    // Phase 16: WorldStateSync callback (D-09)
    struct WorldStateSyncEntry {
        uint32_t serverId;
        float x, y, z, rx, ry;
    };
    using WorldStateSyncCallback = std::function<void(const std::vector<WorldStateSyncEntry>& aEntries)>;

    void SetAuthCallback(AuthCallback aCb);
    void SetMoveCallback(MoveCallback aCb);
    void SetAssignCallback(AssignCallback aCb);
    void SetSpawnCallback(SpawnCallback aCb);
    void SetRemoveCallback(RemoveCallback aCb);
    void SetWorldStateSyncCallback(WorldStateSyncCallback aCb);

    [[nodiscard]] uint32_t GetLastOpcode() const { return m_lastOpcode; }
    [[nodiscard]] bool IsAuthenticated() const { return m_authenticated; }

private:
    AuthCallback m_onAuth;
    MoveCallback m_onMove;
    AssignCallback m_onAssign;
    SpawnCallback m_onSpawn;
    RemoveCallback m_onRemove;
    WorldStateSyncCallback m_onWorldStateSync;
    uint32_t m_lastOpcode{0};
    bool m_authenticated{false};
};
