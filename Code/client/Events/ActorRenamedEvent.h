#pragma once

#include <cstdint>
#include <string>

// An actor's display name changed: a quest alias set or cleared it (BGSRefAlias 24011, 24012). Name: as it reads after
// the call (the event runs later, when a copy can show its owner's name again).
struct ActorRenamedEvent
{
    uint32_t FormId{};
    std::string Name;
};
