#pragma once

#include <cstdint>
#include <string>

#include <TiltedCore/Buffer.hpp>

// The Skyrim runtime and SKSE a game runs, packed as SKSE packs them (major << 24 | minor << 16 |
// build << 4 | sub): the same in every game of a session (docs/design/join-flow.md, stage 2).
struct GameVersions
{
    uint32_t Runtime{};
    uint32_t Skse{};

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    bool operator==(const GameVersions&) const noexcept = default;

    // "1.6.1170", from a packed version.
    [[nodiscard]] static std::string Format(uint32_t aPacked) noexcept;
    [[nodiscard]] static constexpr uint32_t Pack(uint32_t aMajor, uint32_t aMinor, uint32_t aBuild) noexcept
    {
        return ((aMajor & 0xFF) << 24) | ((aMinor & 0xFF) << 16) | ((aBuild & 0xFFF) << 4);
    }
};
