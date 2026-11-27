#pragma once

#include "Message.h"

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

struct NotifyPlayerLevel final : MessageOf<NotifyPlayerLevel, kNotifyPlayerLevel>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(PlayerId, NewLevel); }
    bool operator==(const NotifyPlayerLevel&) const noexcept = default;

    uint32_t PlayerId{};
    uint16_t NewLevel{};
};
