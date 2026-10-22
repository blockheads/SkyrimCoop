#pragma once

#include "Message.h"
#include <Structs/Inventory.h>

struct RequestInventoryChanges final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kRequestInventoryChanges;

    RequestInventoryChanges()
        : RequestMessage(Opcode)
    {
    }

    virtual ~RequestInventoryChanges() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestInventoryChanges& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && SessionId == acRhs.SessionId && Item == acRhs.Item && Drop == acRhs.Drop && UpdateClients == acRhs.UpdateClients; }

    uint32_t SessionId{};
    Inventory::Entry Item{};
    bool Drop = false;
    bool UpdateClients = true;
};
