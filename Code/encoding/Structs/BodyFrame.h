#pragma once

#include <Structs/GameId.h>
#include <Wire/Buffer.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <compare>
#include <cstdint>

// A physics body, the same on every game (docs/design/physics-sync.md, "Body keys"): a reference's (Object, its
// GameId; a created one by ObjectChange::kCreatedModId) or an actor's ragdoll's (Actor, its session id), and its
// place there (the 3D's depth-first collision objects, or the ragdoll's bodies in order).
struct BodyKey
{
    GameId Object{};
    uint32_t Actor{};
    uint8_t Index{};

    bool operator==(const BodyKey& acRhs) const noexcept { return Object == acRhs.Object && Actor == acRhs.Actor && Index == acRhs.Index; }
    auto operator<=>(const BodyKey& acRhs) const noexcept
    {
        return std::tie(Object.ModId, Object.BaseId, Actor, Index) <=> std::tie(acRhs.Object.ModId, acRhs.Object.BaseId, acRhs.Actor, acRhs.Index);
    }
};

// Where a body is and how it moves, in game units (positions, units/s) and radians; Gravity: it is falling freely,
// so a guess adds the world's gravity (else it goes on as it moves).
struct BodyFrame
{
    BodyKey Key{};
    glm::vec3 Position{};
    glm::quat Rotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 LinearVelocity{};
    glm::vec3 AngularVelocity{};
    bool Gravity{};

    bool operator==(const BodyFrame& acRhs) const noexcept = default;

    // As the wire carries it: what the receiver gets (Quantized), and roughly how many bytes that is.
    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;
    [[nodiscard]] BodyFrame Quantized() const noexcept;
    static constexpr size_t kWireBytes = 32;
};

// Where a body is, with full precision: a rest (where it stays), or where a guest drives it.
struct BodyPose
{
    BodyKey Key{};
    glm::vec3 Position{};
    glm::quat Rotation{1.f, 0.f, 0.f, 0.f};

    bool operator==(const BodyPose& acRhs) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;
};

// An impulse a player's character gave a body (Havok's character proxy), in the game's units (mass x units/s) at a
// point in the world.
struct BodyPush
{
    BodyKey Key{};
    glm::vec3 Impulse{};
    glm::vec3 Point{};

    bool operator==(const BodyPush& acRhs) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;
};
