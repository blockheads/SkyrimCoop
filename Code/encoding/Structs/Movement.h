#pragma once

#include <Structs/GameId.h>

#include <Structs/Vector3_NetQuantize.h>
#include <Structs/Rotator2_NetQuantize.h>
#include <Structs/AnimationVariables.h>
#include <Wire/Buffer.h>

using Wire::Buffer;

struct Movement
{
    Movement() = default;
    ~Movement() = default;

    bool operator==(const Movement& acRhs) const noexcept;
    bool operator!=(const Movement& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    GameId CellId{};
    GameId WorldSpaceId{};
    Vector3_NetQuantize Position{};
    Rotator2_NetQuantize Rotation{};
    AnimationVariables Variables{};
    float Direction{};
    // Put there at once (a script's MoveTo, the console's setpos): the copy jumps, never slides across.
    bool Teleported{};
    // Lying in its ragdoll, its place its ragdoll's (the engine puts it there, feet to pelvis): a copy lies by its own
    // ragdoll, never placed here.
    bool Lying{};
    // Alive and in knock states 1-4: ends resting-body correction on the owner's delayed timeline.
    bool Knocked{};
    // Its controller on the ground: only then is a copy stepped by its engine; in any other state placed on its path.
    bool OnGround{};
    // Its capsule collides: the engine turns that off in furniture and elsewhere, and its copy's matches it.
    bool CapsuleCollides{true};
    // The layer its own bodies are on in the physics world, 0 when out of it (seated: 33, 40063); its copy's match.
    uint8_t BodiesLayer{};
    // Its graphs step with the physics world (their ragdoll driver drives its bodies): detached in furniture (40063).
    bool GraphsStepPhysics{true};
};
