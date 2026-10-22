#pragma once

#include "Faction.h"

struct Factions
{
    Factions() = default;
    ~Factions() = default;

    bool operator==(const Factions& acRhs) const noexcept;
    bool operator!=(const Factions& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader);

    std::vector<Faction> NpcFactions;
    std::vector<Faction> ExtraFactions;
};
