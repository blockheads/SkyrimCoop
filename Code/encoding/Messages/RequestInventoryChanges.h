#pragma once

#include "Message.h"
#include <Structs/Inventory.h>
#include <Wire/Buffer.h>

struct RequestInventoryChanges final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kRequestInventoryChanges;

    RequestInventoryChanges()
        : RequestMessage(Opcode)
    {
    }

    virtual ~RequestInventoryChanges() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestInventoryChanges& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && SessionId == acRhs.SessionId && Item == acRhs.Item && Drop == acRhs.Drop && UpdateClients == acRhs.UpdateClients && NewPartyItem == acRhs.NewPartyItem && StoryGiven == acRhs.StoryGiven; }

    uint32_t SessionId{};
    Inventory::Entry Item{};
    bool Drop = false;
    bool UpdateClients = true;
    // A party item arriving (docs/design/party-items.md): its game took its own untagged copy back; the story gave it.
    bool NewPartyItem = false;
    bool StoryGiven = false;
};
