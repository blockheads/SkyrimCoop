#pragma once
#include <cstdint>
#include <vector>

struct HookEventPacket;
class GameReader;
class TcpClient;

// Native Linux InventoryService: handles inventory hook events from DLL relay
// and sends commands back for remote inventory application.
// All game state reads use GameReader (/proc/pid/mem), no direct pointer dereference.
class InventoryService {
public:
    InventoryService(GameReader& aReader, TcpClient& aTcp);

    // Process a hook event from the DLL relay
    void OnHookEvent(const HookEventPacket& aPkt);

    // Periodic update (weapon draw state, etc.)
    void Update(float aDeltaTime);

    // Apply remote inventory change (from another player via network)
    void ApplyRemoteInventoryAdd(uint32_t aActorFormId, uint32_t aItemFormId, int32_t aCount);
    void ApplyRemoteInventoryRemove(uint32_t aActorFormId, uint32_t aItemFormId, int32_t aCount);
    void ApplyRemoteEquip(uint32_t aActorFormId, uint32_t aItemFormId);
    void ApplyRemoteUnequip(uint32_t aActorFormId, uint32_t aItemFormId);

private:
    void HandleAddInventory(const uint64_t* apArgs, uint8_t aArgCount);
    void HandleEquip(const uint64_t* apArgs, uint8_t aArgCount);
    void HandleUnequip(const uint64_t* apArgs, uint8_t aArgCount);
    void HandlePickUp(const uint64_t* apArgs, uint8_t aArgCount);
    void HandleDropObject(const uint64_t* apArgs, uint8_t aArgCount);
    void HandleRemoveInvItem(const uint64_t* apArgs, uint8_t aArgCount);

    // Send a command packet to the DLL relay
    void SendCommand(uint16_t aOpcode, const uint64_t* apArgs, uint8_t aArgCount);

    GameReader& m_reader;
    TcpClient& m_tcp;
};
