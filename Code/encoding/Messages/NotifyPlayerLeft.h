#pragma once

#include "Message.h"
#include <Wire/Buffer.h>

struct NotifyPlayerLeft final : MessageOf<NotifyPlayerLeft, kNotifyPlayerLeft>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(PlayerId, Username); }
    bool operator==(const NotifyPlayerLeft&) const noexcept = default;

    uint32_t PlayerId{};
    std::string Username;
};
