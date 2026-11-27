#pragma once

#include <Wire/Archive.h>

#include <Wire/Buffer.h>

struct ActorValues
{
    ActorValues() = default;
    ~ActorValues() = default;

    // Keyed by actor value index: the engine has 164.
    static constexpr uint64_t kMaxActorValues = 256;
    bool operator==(const ActorValues&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Wire::List(ActorValuesList, kMaxActorValues), Wire::List(ActorMaxValuesList, kMaxActorValues),
                 Wire::List(ActorTemporaryValuesList, kMaxActorValues));
    }

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    std::map<uint32_t, float> ActorValuesList{};
    std::map<uint32_t, float> ActorMaxValuesList{};
    std::map<uint32_t, float> ActorTemporaryValuesList{};
};
