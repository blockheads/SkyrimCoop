#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct RequestRemoveWaypoint final : MessageOf<RequestRemoveWaypoint, kRequestRemoveWaypoint>
{
    template <class TArchive> void Fields(TArchive&) noexcept
    {
    }
    bool operator==(const RequestRemoveWaypoint&) const noexcept = default;
};
