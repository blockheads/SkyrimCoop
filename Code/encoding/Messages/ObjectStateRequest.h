#pragma once

#include "Message.h"

#include <Messages/WorldState.h>

// A guest's own change to a world object's state (its player cut a web): a proposal, as the row it reads now. The host
// takes only the kinds a guest may propose (HostObjects::OnObjectState).
struct ObjectStateRequest final : MessageOf<ObjectStateRequest, kObjectStateRequest>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Change); }
    bool operator==(const ObjectStateRequest&) const noexcept = default;

    ObjectChange Change;
};
