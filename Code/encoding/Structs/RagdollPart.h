#pragma once

#include <Wire/Archive.h>

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
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Tick, Wire::List(Rests, kMaxBodies)); }

    COOP_SERIALIZE_FIELDS

    uint64_t Tick{}; // the session's time it rested
    std::vector<BodyPose> Rests;
};
