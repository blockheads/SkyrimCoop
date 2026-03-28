#include "InventoryService.h"
#include "game_bridge/game_reader.h"
#include "game_bridge/tcp_client.h"
#include "protocol.h"

#include <spdlog/spdlog.h>
#include <cstring>

InventoryService::InventoryService(GameReader& aReader, TcpClient& aTcp)
    : m_reader(aReader), m_tcp(aTcp)
{
}

void InventoryService::OnHookEvent(const HookEventPacket& aPkt)
{
    const uint8_t argCount = aPkt.argCount;
    const uint64_t* pArgs = aPkt.args;

    switch (static_cast<HookOpcode>(aPkt.header.opcode)) {
    case HOOK_ADD_INVENTORY:
        HandleAddInventory(pArgs, argCount);
        break;
    case HOOK_EQUIP:
        HandleEquip(pArgs, argCount);
        break;
    case HOOK_UNEQUIP:
        HandleUnequip(pArgs, argCount);
        break;
    case HOOK_PICK_UP:
        HandlePickUp(pArgs, argCount);
        break;
    case HOOK_DROP_OBJECT:
        HandleDropObject(pArgs, argCount);
        break;
    case HOOK_REMOVE_INV_ITEM:
        HandleRemoveInvItem(pArgs, argCount);
        break;
    default:
        break;
    }
}

void InventoryService::Update(float /*aDeltaTime*/)
{
    // Periodic updates (weapon draw state polling, etc.) can be added here.
    // The original service polls weapon draw state at 500ms intervals.
    // In the native architecture, weapon draw state hooks arrive via DLL relay.
}

// --- Hook handlers ---

void InventoryService::HandleAddInventory(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] actor ptr, [1] item ptr, [2] count
    if (aArgCount < 3) {
        spdlog::warn("HOOK_ADD_INVENTORY: insufficient args ({})", aArgCount);
        return;
    }

    uint32_t actorFormId = m_reader.ReadFormId(apArgs[0]);
    uint32_t itemFormId = m_reader.ReadFormId(apArgs[1]);
    int32_t count = static_cast<int32_t>(apArgs[2]);

    if (actorFormId == 0 || itemFormId == 0) {
        spdlog::warn("HOOK_ADD_INVENTORY: failed to read formIds (actor={:#x}, item={:#x})",
                      actorFormId, itemFormId);
        return;
    }

    spdlog::debug("Inventory add: actor={:#x} item={:#x} count={}", actorFormId, itemFormId, count);
    // TODO: Send inventory add message to embedded server / network layer
}

void InventoryService::HandleEquip(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] actor ptr, [1] item form ptr
    if (aArgCount < 2) {
        spdlog::warn("HOOK_EQUIP: insufficient args ({})", aArgCount);
        return;
    }

    uint32_t actorFormId = m_reader.ReadFormId(apArgs[0]);
    uint32_t itemFormId = m_reader.ReadFormId(apArgs[1]);

    if (actorFormId == 0 || itemFormId == 0) {
        spdlog::warn("HOOK_EQUIP: failed to read formIds (actor={:#x}, item={:#x})",
                      actorFormId, itemFormId);
        return;
    }

    spdlog::debug("Equip: actor={:#x} item={:#x}", actorFormId, itemFormId);
    // TODO: Send equip message to embedded server / network layer
}

void InventoryService::HandleUnequip(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] actor ptr, [1] item form ptr
    if (aArgCount < 2) {
        spdlog::warn("HOOK_UNEQUIP: insufficient args ({})", aArgCount);
        return;
    }

    uint32_t actorFormId = m_reader.ReadFormId(apArgs[0]);
    uint32_t itemFormId = m_reader.ReadFormId(apArgs[1]);

    if (actorFormId == 0 || itemFormId == 0) {
        spdlog::warn("HOOK_UNEQUIP: failed to read formIds (actor={:#x}, item={:#x})",
                      actorFormId, itemFormId);
        return;
    }

    spdlog::debug("Unequip: actor={:#x} item={:#x}", actorFormId, itemFormId);
    // TODO: Send unequip message to embedded server / network layer
}

void InventoryService::HandlePickUp(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] actor ptr, [1] ref ptr
    if (aArgCount < 2) {
        spdlog::warn("HOOK_PICK_UP: insufficient args ({})", aArgCount);
        return;
    }

    uint32_t actorFormId = m_reader.ReadFormId(apArgs[0]);
    uint32_t refFormId = m_reader.ReadFormId(apArgs[1]);

    if (actorFormId == 0 || refFormId == 0) {
        spdlog::warn("HOOK_PICK_UP: failed to read formIds (actor={:#x}, ref={:#x})",
                      actorFormId, refFormId);
        return;
    }

    spdlog::debug("Pick up: actor={:#x} ref={:#x}", actorFormId, refFormId);
    // TODO: Send pickup message to embedded server / network layer
}

void InventoryService::HandleDropObject(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] actor ptr, [1] ref ptr
    if (aArgCount < 2) {
        spdlog::warn("HOOK_DROP_OBJECT: insufficient args ({})", aArgCount);
        return;
    }

    uint32_t actorFormId = m_reader.ReadFormId(apArgs[0]);
    uint32_t refFormId = m_reader.ReadFormId(apArgs[1]);

    if (actorFormId == 0 || refFormId == 0) {
        spdlog::warn("HOOK_DROP_OBJECT: failed to read formIds (actor={:#x}, ref={:#x})",
                      actorFormId, refFormId);
        return;
    }

    spdlog::debug("Drop object: actor={:#x} ref={:#x}", actorFormId, refFormId);
    // TODO: Send drop message to embedded server / network layer
}

void InventoryService::HandleRemoveInvItem(const uint64_t* apArgs, uint8_t aArgCount)
{
    // args: [0] ref ptr, [1] item ptr, [2] count
    if (aArgCount < 3) {
        spdlog::warn("HOOK_REMOVE_INV_ITEM: insufficient args ({})", aArgCount);
        return;
    }

    uint32_t refFormId = m_reader.ReadFormId(apArgs[0]);
    uint32_t itemFormId = m_reader.ReadFormId(apArgs[1]);
    int32_t count = static_cast<int32_t>(apArgs[2]);

    if (refFormId == 0 || itemFormId == 0) {
        spdlog::warn("HOOK_REMOVE_INV_ITEM: failed to read formIds (ref={:#x}, item={:#x})",
                      refFormId, itemFormId);
        return;
    }

    spdlog::debug("Remove inv item: ref={:#x} item={:#x} count={}", refFormId, itemFormId, count);
    // TODO: Send remove message to embedded server / network layer
}

// --- Remote inventory application (sending commands to DLL) ---

void InventoryService::SendCommand(uint16_t aOpcode, const uint64_t* apArgs, uint8_t aArgCount)
{
    CommandPacket cmd{};
    cmd.header.opcode = aOpcode;
    cmd.header.length = sizeof(CommandPacket) - sizeof(PacketHeader);
    cmd.argCount = aArgCount;
    for (uint8_t i = 0; i < aArgCount && i < 8; ++i)
        cmd.args[i] = apArgs[i];

    m_tcp.Send(&cmd, sizeof(cmd));
}

void InventoryService::ApplyRemoteInventoryAdd(uint32_t aActorFormId, uint32_t aItemFormId, int32_t aCount)
{
    uint64_t actorPtr = m_reader.LookupPointer(aActorFormId);
    uint64_t itemPtr = m_reader.LookupPointer(aItemFormId);
    if (actorPtr == 0 || itemPtr == 0) {
        spdlog::warn("ApplyRemoteInventoryAdd: could not resolve ptrs (actor={:#x}, item={:#x})",
                      aActorFormId, aItemFormId);
        return;
    }

    uint64_t args[3] = {actorPtr, itemPtr, static_cast<uint64_t>(aCount)};
    SendCommand(CMD_ADD_ITEM, args, 3);
    spdlog::debug("Sent CMD_ADD_ITEM: actor={:#x} item={:#x} count={}", aActorFormId, aItemFormId, aCount);
}

void InventoryService::ApplyRemoteInventoryRemove(uint32_t aActorFormId, uint32_t aItemFormId, int32_t aCount)
{
    uint64_t actorPtr = m_reader.LookupPointer(aActorFormId);
    uint64_t itemPtr = m_reader.LookupPointer(aItemFormId);
    if (actorPtr == 0 || itemPtr == 0) {
        spdlog::warn("ApplyRemoteInventoryRemove: could not resolve ptrs (actor={:#x}, item={:#x})",
                      aActorFormId, aItemFormId);
        return;
    }

    uint64_t args[3] = {actorPtr, itemPtr, static_cast<uint64_t>(aCount)};
    SendCommand(CMD_REMOVE_ITEM, args, 3);
    spdlog::debug("Sent CMD_REMOVE_ITEM: actor={:#x} item={:#x} count={}", aActorFormId, aItemFormId, aCount);
}

void InventoryService::ApplyRemoteEquip(uint32_t aActorFormId, uint32_t aItemFormId)
{
    uint64_t actorPtr = m_reader.LookupPointer(aActorFormId);
    uint64_t itemPtr = m_reader.LookupPointer(aItemFormId);
    if (actorPtr == 0 || itemPtr == 0) {
        spdlog::warn("ApplyRemoteEquip: could not resolve ptrs (actor={:#x}, item={:#x})",
                      aActorFormId, aItemFormId);
        return;
    }

    uint64_t args[2] = {actorPtr, itemPtr};
    SendCommand(CMD_EQUIP_ITEM, args, 2);
    spdlog::debug("Sent CMD_EQUIP_ITEM: actor={:#x} item={:#x}", aActorFormId, aItemFormId);
}

void InventoryService::ApplyRemoteUnequip(uint32_t aActorFormId, uint32_t aItemFormId)
{
    uint64_t actorPtr = m_reader.LookupPointer(aActorFormId);
    uint64_t itemPtr = m_reader.LookupPointer(aItemFormId);
    if (actorPtr == 0 || itemPtr == 0) {
        spdlog::warn("ApplyRemoteUnequip: could not resolve ptrs (actor={:#x}, item={:#x})",
                      aActorFormId, aItemFormId);
        return;
    }

    uint64_t args[2] = {actorPtr, itemPtr};
    SendCommand(CMD_UNEQUIP_ITEM, args, 2);
    spdlog::debug("Sent CMD_UNEQUIP_ITEM: actor={:#x} item={:#x}", aActorFormId, aItemFormId);
}
