#pragma once

#include <Wire/Archive.h>

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

#include <array>
#include <optional>

// A caster's held casts (concentration, ward, invisibility) by casting source: a copy made mid-cast starts them, a
// slot cleared stops it (docs/design/one-simulator.md, piece 9).
struct HeldCast
{
    bool operator==(const HeldCast&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Spell, Dual, Target); }

    GameId Spell{};
    bool Dual{};
    uint32_t Target{}; // the desired target's session id, 0 none
};

struct CastPart
{
    bool operator==(const CastPart&) const noexcept = default;
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Sources); }

    COOP_SERIALIZE_FIELDS

    std::array<std::optional<HeldCast>, 4> Sources{}; // left hand, right hand, voice, instant
};
