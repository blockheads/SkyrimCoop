#pragma once

#include <Wire/Archive.h>

#include "Faction.h"
#include <Wire/Buffer.h>

struct Factions
{
    Factions() = default;
    ~Factions() = default;

    static constexpr uint64_t kMaxFactions = 0x1FF;
    bool operator==(const Factions&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Wire::List(NpcFactions, kMaxFactions), Wire::List(ExtraFactions, kMaxFactions)); }

    COOP_SERIALIZE_FIELDS

    std::vector<Faction> NpcFactions;
    std::vector<Faction> ExtraFactions;
};
