#pragma once

#include "Message.h"
#include <Structs/ReferenceUpdate.h>
#include <Wire/Buffer.h>

struct ReferencesMoveRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kReferencesMoveRequest;

    ReferencesMoveRequest()
        : RequestMessage(Opcode)
    {
    }

    virtual ~ReferencesMoveRequest() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const ReferencesMoveRequest& acRhs) const noexcept { return Updates == acRhs.Updates && GetOpcode() == acRhs.GetOpcode(); }

    uint64_t Tick{};
    std::map<uint32_t, ReferenceUpdate> Updates{};
};
