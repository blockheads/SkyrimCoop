#pragma once

#include <Structs/BodyFrame.h>
#include <Wire/Buffer.h>

#include <vector>

// An actor's ragdoll where it last came to rest, as its owner read it: every body, at that time (docs/design/one-
// simulator.md, piece 10). A copy made later lies there too.
struct RagdollPart
{
    // A ragdoll's bodies (a dragon's has the most, about 60).
    static constexpr size_t kMaxBodies = 128;

    bool operator==(const RagdollPart&) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    uint64_t Tick{}; // the session's time it rested
    std::vector<BodyPose> Rests;
};
