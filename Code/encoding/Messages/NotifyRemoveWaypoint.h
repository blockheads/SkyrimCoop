#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct NotifyRemoveWaypoint final : MessageOf<NotifyRemoveWaypoint, kNotifyRemoveWaypoint>
{
    template <class TArchive> void Fields(TArchive&) noexcept
    {
    }
    bool operator==(const NotifyRemoveWaypoint&) const noexcept = default;
};
