#pragma once

#include "Message.h"
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/GameId.h>
#include <Wire/Buffer.h>

struct NotifySetWaypoint final : MessageOf<NotifySetWaypoint, kNotifySetWaypoint>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Position, WorldSpaceFormID); }
    bool operator==(const NotifySetWaypoint&) const noexcept = default;

    Vector3_NetQuantize Position;
    GameId WorldSpaceFormID;
};
