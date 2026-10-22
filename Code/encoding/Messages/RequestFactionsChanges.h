#pragma once

#include "Message.h"
#include <Structs/Factions.h>
#include <Wire/Buffer.h>

struct RequestFactionsChanges final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kRequestFactionsChanges;

    RequestFactionsChanges()
        : RequestMessage(Opcode)
    {
    }

    virtual ~RequestFactionsChanges() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestFactionsChanges& acRhs) const noexcept { return Changes == acRhs.Changes && GetOpcode() == acRhs.GetOpcode(); }

    std::map<uint32_t, Factions> Changes;
};
