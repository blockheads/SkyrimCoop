#pragma once

#include "Message.h"
#include <Wire/Buffer.h>
#include <Structs/Tints.h>

struct NotifyRespawn final : MessageOf<NotifyRespawn, kNotifyRespawn>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(ActorId, AppearanceBuffer);
        if (!AppearanceBuffer.empty())
            aArchive(ChangeFlags, FaceTints, Weight);
    }
    bool operator==(const NotifyRespawn&) const noexcept = default;

    uint32_t ActorId{};
    // With looks, update the existing copy; otherwise make it again from the owner's live state.
    std::string AppearanceBuffer;
    uint32_t ChangeFlags{};
    Tints FaceTints;
    float Weight{50.f};
};
