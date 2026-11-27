#pragma once

#include "Message.h"

#include <Structs/GameId.h>

struct NotifyWeatherChange final : MessageOf<NotifyWeatherChange, kNotifyWeatherChange>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Id); }
    bool operator==(const NotifyWeatherChange&) const noexcept = default;

    GameId Id{};
};
