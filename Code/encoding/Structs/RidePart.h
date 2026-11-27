#pragma once

#include <Wire/Archive.h>

#include <Wire/Buffer.h>

#include <cstdint>

// A player riding along on a horse another player drives (one-simulator.md, piece 5): seated at once on every game.
struct RidePart
{
    bool operator==(const RidePart&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Tick, Mount); }

    COOP_SERIALIZE_FIELDS

    uint64_t Tick{};  // the owner's frame it sat at, on the session's clock: its copy sits when its stream does
    uint32_t Mount{}; // the horse's session id, 0 none
};
