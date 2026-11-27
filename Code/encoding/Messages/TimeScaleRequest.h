#pragma once

#include "Message.h"

// How fast a player's game wants its clock to run (1 normal; a kill cam, Slow Time or a bow zoom slow it).
struct TimeScaleRequest final : MessageOf<TimeScaleRequest, kTimeScaleRequest>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Multiplier); }
    bool operator==(const TimeScaleRequest&) const noexcept = default;

    float Multiplier{1.f};
};
