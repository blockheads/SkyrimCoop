#include "InventoryService.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "json_protocol.h"

#include "../game_bridge/game_reader.h"
#include "../game_bridge/tcp_client.h"

InventoryService::InventoryService(GameReader& aReader, TcpClient& aTcp)
    : m_reader(aReader), m_tcp(aTcp)
{
}

void InventoryService::OnHookEvent(const nlohmann::json& aMsg)
{
    const auto& op = aMsg[JsonField::kOp].get_ref<const std::string&>();

    if (op == JsonOp::kAddInventory) {
        uint32_t actorFormId = aMsg.value(JsonField::kActorFormId, 0u);
        uint32_t itemFormId = aMsg.value(JsonField::kItemFormId, 0u);
        int32_t count = aMsg.value(JsonField::kCount, 0);

        if (actorFormId == 0 || itemFormId == 0) {
            spdlog::warn("add_inventory: formIds are zero (actor={:#x}, item={:#x})",
                          actorFormId, itemFormId);
            return;
        }

        spdlog::debug("Inventory add: actor={:#x} item={:#x} count={}", actorFormId, itemFormId, count);
        // TODO: Send inventory add message to embedded server / network layer

    } else if (op == JsonOp::kPickUp) {
        uint32_t actorFormId = aMsg.value(JsonField::kActorFormId, 0u);
        uint32_t objFormId = aMsg.value(JsonField::kObjFormId, 0u);
        int32_t count = aMsg.value(JsonField::kCount, 0);

        if (actorFormId == 0 || objFormId == 0) {
            spdlog::warn("pick_up: formIds are zero (actor={:#x}, obj={:#x})",
                          actorFormId, objFormId);
            return;
        }

        spdlog::debug("Pick up: actor={:#x} obj={:#x} count={}", actorFormId, objFormId, count);
        // TODO: Send pickup message to embedded server / network layer

    } else if (op == JsonOp::kDropObject) {
        uint32_t actorFormId = aMsg.value(JsonField::kActorFormId, 0u);
        uint32_t itemFormId = aMsg.value(JsonField::kItemFormId, 0u);
        int32_t count = aMsg.value(JsonField::kCount, 0);

        if (actorFormId == 0 || itemFormId == 0) {
            spdlog::warn("drop_object: formIds are zero (actor={:#x}, item={:#x})",
                          actorFormId, itemFormId);
            return;
        }

        spdlog::debug("Drop object: actor={:#x} item={:#x} count={}", actorFormId, itemFormId, count);
        // TODO: Send drop message to embedded server / network layer

    } else if (op == JsonOp::kEquip) {
        uint32_t actorFormId = aMsg.value(JsonField::kActorFormId, 0u);
        uint32_t itemFormId = aMsg.value(JsonField::kItemFormId, 0u);

        if (actorFormId == 0 || itemFormId == 0) {
            spdlog::warn("equip: formIds are zero (actor={:#x}, item={:#x})",
                          actorFormId, itemFormId);
            return;
        }

        spdlog::debug("Equip: actor={:#x} item={:#x}", actorFormId, itemFormId);
        // TODO: Send equip message to embedded server / network layer

    } else if (op == JsonOp::kUnequip) {
        uint32_t actorFormId = aMsg.value(JsonField::kActorFormId, 0u);
        uint32_t itemFormId = aMsg.value(JsonField::kItemFormId, 0u);

        if (actorFormId == 0 || itemFormId == 0) {
            spdlog::warn("unequip: formIds are zero (actor={:#x}, item={:#x})",
                          actorFormId, itemFormId);
            return;
        }

        spdlog::debug("Unequip: actor={:#x} item={:#x}", actorFormId, itemFormId);
        // TODO: Send unequip message to embedded server / network layer

    } else if (op == JsonOp::kRemoveInvItem) {
        uint32_t actorFormId = aMsg.value(JsonField::kActorFormId, 0u);
        uint32_t itemFormId = aMsg.value(JsonField::kItemFormId, 0u);
        int32_t count = aMsg.value(JsonField::kCount, 0);

        if (actorFormId == 0 || itemFormId == 0) {
            spdlog::warn("remove_inv_item: formIds are zero (actor={:#x}, item={:#x})",
                          actorFormId, itemFormId);
            return;
        }

        spdlog::debug("Remove inv item: actor={:#x} item={:#x} count={}", actorFormId, itemFormId, count);
        // TODO: Send remove message to embedded server / network layer

    } else if (op == JsonOp::kActivate) {
        uint32_t thisFormId = aMsg.value(JsonField::kThisFormId, 0u);
        uint32_t activatorFormId = aMsg.value(JsonField::kActivatorFormId, 0u);
        int32_t count = aMsg.value(JsonField::kCount, 0);

        spdlog::debug("Activate: this={:#x} activator={:#x} count={}", thisFormId, activatorFormId, count);
        // TODO: Send activate message to embedded server / network layer

    } else if (op == JsonOp::kLockChange) {
        uint32_t thisFormId = aMsg.value(JsonField::kThisFormId, 0u);
        bool locked = aMsg.value(JsonField::kLocked, false);

        spdlog::debug("Lock change: this={:#x} locked={}", thisFormId, locked);
        // TODO: Send lock change message to embedded server / network layer
    }
}

void InventoryService::Update(float /*aDeltaTime*/)
{
    // Periodic updates (weapon draw state polling, etc.) can be added here.
    // The original service polls weapon draw state at 500ms intervals.
    // In the native architecture, weapon draw state hooks arrive via DLL relay.
}

// --- Remote inventory application (sending JSON commands to DLL) ---

void InventoryService::ApplyRemoteInventoryAdd(uint32_t aActorFormId, uint32_t aItemFormId, int32_t aCount)
{
    uint64_t actorPtr = m_reader.LookupPointer(aActorFormId);
    uint64_t itemPtr = m_reader.LookupPointer(aItemFormId);
    if (actorPtr == 0 || itemPtr == 0) {
        spdlog::warn("ApplyRemoteInventoryAdd: could not resolve ptrs (actor={:#x}, item={:#x})",
                      aActorFormId, aItemFormId);
        return;
    }

    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdAddItem},
        {JsonField::kActorPtr, actorPtr},
        {JsonField::kItemPtr, itemPtr},
        {JsonField::kCount, aCount}
    };
    m_tcp.SendJsonLine(cmd);
    spdlog::debug("Sent cmd_add_item: actor={:#x} item={:#x} count={}", aActorFormId, aItemFormId, aCount);
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

    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdRemoveItem},
        {JsonField::kActorPtr, actorPtr},
        {JsonField::kItemPtr, itemPtr},
        {JsonField::kCount, aCount}
    };
    m_tcp.SendJsonLine(cmd);
    spdlog::debug("Sent cmd_remove_item: actor={:#x} item={:#x} count={}", aActorFormId, aItemFormId, aCount);
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

    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdEquipItem},
        {JsonField::kActorPtr, actorPtr},
        {JsonField::kItemPtr, itemPtr}
    };
    m_tcp.SendJsonLine(cmd);
    spdlog::debug("Sent cmd_equip_item: actor={:#x} item={:#x}", aActorFormId, aItemFormId);
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

    nlohmann::json cmd = {
        {JsonField::kOp, JsonOp::kCmdUnequipItem},
        {JsonField::kActorPtr, actorPtr},
        {JsonField::kItemPtr, itemPtr}
    };
    m_tcp.SendJsonLine(cmd);
    spdlog::debug("Sent cmd_unequip_item: actor={:#x} item={:#x}", aActorFormId, aItemFormId);
}
