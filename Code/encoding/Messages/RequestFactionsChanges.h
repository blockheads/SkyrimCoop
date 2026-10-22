#pragma once

#include "Message.h"
#include <Structs/Factions.h>

struct RequestFactionsChanges final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kRequestFactionsChanges;

    RequestFactionsChanges()
        : RequestMessage(Opcode)
    {
    }

    virtual ~RequestFactionsChanges() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestFactionsChanges& acRhs) const noexcept { return Changes == acRhs.Changes && GetOpcode() == acRhs.GetOpcode(); }

    TiltedPhoques::Map<uint32_t, Factions> Changes;
};
