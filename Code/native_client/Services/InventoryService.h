#pragma once
#include <cstdint>
#include <vector>

#include <nlohmann/json_fwd.hpp>

class GameReader;
class TcpClient;

// Native Linux InventoryService: handles inventory hook events from DLL relay
// and sends commands back for remote inventory application.
// All game state reads use GameReader (/proc/pid/mem), no direct pointer dereference.
class InventoryService {
public:
    InventoryService(GameReader& aReader, TcpClient& aTcp);

    // Process a hook event from the DLL relay (JSON wire protocol)
    void OnHookEvent(const nlohmann::json& aMsg);

    // Periodic update (weapon draw state, etc.)
    void Update(float aDeltaTime);

    // Apply remote inventory change (from another player via network)
    void ApplyRemoteInventoryAdd(uint32_t aActorFormId, uint32_t aItemFormId, int32_t aCount);
    void ApplyRemoteInventoryRemove(uint32_t aActorFormId, uint32_t aItemFormId, int32_t aCount);
    void ApplyRemoteEquip(uint32_t aActorFormId, uint32_t aItemFormId);
    void ApplyRemoteUnequip(uint32_t aActorFormId, uint32_t aItemFormId);

    // Test accessor
    uint32_t GetEventCount() const { return m_eventCount; }

private:
    GameReader& m_reader;
    TcpClient& m_tcp;
    uint32_t m_eventCount = 0;
};
