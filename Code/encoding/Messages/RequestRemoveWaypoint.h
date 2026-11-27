#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct RequestRemoveWaypoint final : MessageOf<RequestRemoveWaypoint, kRequestRemoveWaypoint>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive&) noexcept
    {
    }
    bool operator==(const RequestRemoveWaypoint&) const noexcept = default;
};
