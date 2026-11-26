#pragma once

#include <cstdint>

// A door's or container's open state changed while it had no 3D: its next 3D comes up part open unless posed
// (docs/re/systems/guest-world-events.md, "A lid after a disable").
struct LidUnposedEvent
{
    uint32_t FormId{};
};
