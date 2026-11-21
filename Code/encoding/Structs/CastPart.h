#pragma once

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

#include <array>
#include <optional>

// A caster's held casts (concentration, ward, invisibility) by casting source: a copy made mid-cast starts them, a
// slot cleared stops it (docs/design/one-simulator.md, piece 9).
struct HeldCast
{
    bool operator==(const HeldCast&) const noexcept = default;

    GameId Spell{};
    bool Dual{};
    uint32_t Target{}; // the desired target's session id, 0 none
};

struct CastPart
{
    bool operator==(const CastPart&) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    std::array<std::optional<HeldCast>, 4> Sources{}; // left hand, right hand, voice, instant
};
