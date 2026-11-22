#pragma once

#include <Wire/Buffer.h>

#include <cstdint>

// A player riding along on a horse another player drives (one-simulator.md, piece 5): seated at once on every game.
struct RidePart
{
    bool operator==(const RidePart&) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    uint64_t Tick{};  // the owner's frame it sat at, on the session's clock: its copy sits when its stream does
    uint32_t Mount{}; // the horse's session id, 0 none
};
