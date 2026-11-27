#pragma once

#include "Message.h"

// The slowest clock the other players' games want: the receiving game runs no faster.
struct NotifyTimeScale final : MessageOf<NotifyTimeScale, kNotifyTimeScale>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Multiplier); }
    bool operator==(const NotifyTimeScale&) const noexcept = default;

    float Multiplier{1.f};
};
