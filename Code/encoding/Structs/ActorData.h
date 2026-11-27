#pragma once

#include <Wire/Archive.h>

#include <Structs/ActorValues.h>
#include <Wire/Buffer.h>

struct ActorData
{
    COOP_SERIALIZE_FIELDS

    bool operator==(const ActorData&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(InitialActorValues, IsDead); }

    ActorValues InitialActorValues{};
    bool IsDead{};
};
