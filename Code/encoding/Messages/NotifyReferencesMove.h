#pragma once

#include "Message.h"
#include <Structs/ReferenceUpdate.h>
#include <Wire/Buffer.h>

struct NotifyReferencesMove final : MessageOf<NotifyReferencesMove, kNotifyReferencesMove>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Tick, Wire::List(Updates, ReferenceUpdate::kMaxPerMessage));
    }
    bool operator==(const NotifyReferencesMove&) const noexcept = default;

    uint64_t Tick{};
    std::map<uint32_t, ReferenceUpdate> Updates{};
};
