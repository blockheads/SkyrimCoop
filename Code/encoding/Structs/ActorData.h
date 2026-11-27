#pragma once

#include <Wire/Archive.h>

#include <Structs/ActorValues.h>
#include <Wire/Buffer.h>

struct ActorData
{
    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const ActorData&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(InitialActorValues, IsDead); }

    ActorValues InitialActorValues{};
    bool IsDead{};
};
