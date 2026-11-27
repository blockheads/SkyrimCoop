#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct NotifyRemoveWaypoint final : MessageOf<NotifyRemoveWaypoint, kNotifyRemoveWaypoint>
{
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    template <class TArchive> void Fields(TArchive&) noexcept
    {
    }
    bool operator==(const NotifyRemoveWaypoint&) const noexcept = default;
};
