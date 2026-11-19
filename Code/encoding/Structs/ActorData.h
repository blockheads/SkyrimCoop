#pragma once

#include <Structs/ActorValues.h>
#include <Wire/Buffer.h>

struct ActorData
{
    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    bool operator==(const ActorData& acRhs) const noexcept
    {
        return InitialActorValues == acRhs.InitialActorValues && IsDead == acRhs.IsDead;
    }

    ActorValues InitialActorValues{};
    bool IsDead{};
};
