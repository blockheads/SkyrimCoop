#pragma once

#include "Message.h"
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/GameId.h>
#include <Wire/Buffer.h>

struct RequestSetWaypoint final : MessageOf<RequestSetWaypoint, kRequestSetWaypoint>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Position, WorldSpaceFormID); }
    bool operator==(const RequestSetWaypoint&) const noexcept = default;

    Vector3_NetQuantize Position;
    GameId WorldSpaceFormID;
};
