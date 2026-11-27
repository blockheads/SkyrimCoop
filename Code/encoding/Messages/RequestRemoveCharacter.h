#pragma once

#include "Message.h"
#include <Structs/ActorWhereabouts.h>
#include <Wire/Buffer.h>

struct RequestRemoveCharacter final : MessageOf<RequestRemoveCharacter, kRequestRemoveCharacter>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(SessionId, Whereabouts); }
    bool operator==(const RequestRemoveCharacter&) const noexcept = default;

    uint32_t SessionId{};
    // Where the owner's actor is now; its copies follow.
    ActorWhereabouts Whereabouts{};
};
