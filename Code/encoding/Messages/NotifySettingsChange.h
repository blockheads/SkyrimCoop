#pragma once

#include "Message.h"

struct NotifySettingsChange final : MessageOf<NotifySettingsChange, kNotifySettingsChange>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Difficulty); }
    bool operator==(const NotifySettingsChange&) const noexcept = default;

    uint32_t Difficulty{};
};
