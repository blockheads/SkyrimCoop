#pragma once

#include "Message.h"
#include <Structs/ActorWhereabouts.h>
#include <Wire/Buffer.h>

struct NotifyRemoveCharacter final : MessageOf<NotifyRemoveCharacter, kNotifyRemoveCharacter>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(SessionId, Whereabouts); }
    bool operator==(const NotifyRemoveCharacter&) const noexcept = default;

    uint32_t SessionId{};
    // Where the owner's actor is now; its copies follow.
    ActorWhereabouts Whereabouts{};
};
