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
// so a guess adds the world's gravity (else it goes on as it moves). Knocked: an actor's ragdoll whose owner lives,
// knocked down (its copies go down with it; a dead one's wait for its death).
struct BodyFrame
{
    BodyKey Key{};
    glm::vec3 Position{};
    glm::quat Rotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 LinearVelocity{};
    glm::vec3 AngularVelocity{};
    bool Gravity{};
    bool Knocked{};

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
    bool Knocked{}; // a living owner still down: keep steering here until its movement says it gets up

    bool operator==(const BodyPose& acRhs) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;
};

// Who simulates a body, as the game receiving its state hears it (docs/design/physics-authority-plan.md).
enum class BodyAuthor : uint8_t
{
    kNone,  // nobody's: the host's game simulates it
    kYou,   // the receiving game's player
    kOther, // another player
};

// Sequence numbers wrap at 16 bits: a is newer than b when it is less than half the range ahead.
[[nodiscard]] constexpr bool SeqAfter(uint16_t aA, uint16_t aB) noexcept
{
    return aA != aB && static_cast<uint16_t>(aA - aB) < 0x8000;
}

// A body's state on the stream (Fiedler's scheme): its authority and two sequence numbers, held or at rest, and its
// frame. At rest its pose goes exact and still; moving, quantized as a BodyFrame.
struct BodyState
{
    BodyFrame Frame{};
    BodyAuthor Author{};
    uint16_t AuthSeq{}; // +1 at each take of authority and at each rest
    uint16_t HoldSeq{}; // +1 at each grab; a newer one beats any AuthSeq
    bool Held{};
    bool AtRest{};
    bool Placed{}; // at rest where the host's game put it (a script, the console): held there asleep, never woken

    bool operator==(const BodyState& acRhs) const noexcept = default;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;
    // As the receiver gets it.
    [[nodiscard]] BodyState Quantized() const noexcept;
    [[nodiscard]] size_t WireBytes() const noexcept { return AtRest ? 40 : BodyFrame::kWireBytes + 5; }
};
