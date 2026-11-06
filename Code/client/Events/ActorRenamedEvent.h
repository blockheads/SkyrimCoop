#pragma once

#include <cstdint>

// An actor's display name changed: a quest alias set or cleared it (BGSRefAlias 24011, 24012).
struct ActorRenamedEvent
{
    uint32_t FormId{};
};
