#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct CancelAssignmentRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kCancelAssignmentRequest;

    CancelAssignmentRequest()
        : RequestMessage(Opcode)
    {
    }

    virtual ~CancelAssignmentRequest() = default;

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const CancelAssignmentRequest& achRhs) const noexcept { return Cookie == achRhs.Cookie && GetOpcode() == achRhs.GetOpcode(); }

    uint32_t Cookie;
};
