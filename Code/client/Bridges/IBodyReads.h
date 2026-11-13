#pragma once

#include <Physics/BodyRead.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <vector>

// What PhysicsBodies reads of this game's physics (docs/design/physics-sync.md): the engine's (BodyReads), or the fake
// engine's in GameTests.
class IBodyReads
{
public:
    struct Body
    {
        BodyRead Read;
        // Its node's world pose (a reference's): where the engine shows it. The engine writes it from the body each
        // step, but sometimes skips that (docs/re/systems/physics.md, "A flying body's node is sometimes left behind").
        std::optional<std::pair<glm::vec3, glm::quat>> Node;
    };
    struct Reference
    {
        bool Actor{};  // its bodies are its ragdoll's
        bool Dead{};
        bool Up{};     // a living actor standing (knock state 0): its animation alone poses it
        bool Rising{}; // getting up (knock states 5, 6): its ragdoll still simulated, driven toward its animation (58291)
        bool Player{}; // this game's player
        glm::vec3 Position{}; // the reference's own pose
        glm::vec3 Rotation{};
        std::vector<std::optional<Body>> Bodies; // by index; none where there is no body to read
    };

    virtual ~IBodyReads() = default;

    // The references physics moved since the last call (the change funnel's havok move, every frame while awake).
    [[nodiscard]] virtual std::vector<uint32_t> TakeMovedReferences() noexcept = 0;
    // This game's player's form id while its ragdoll simulates it (dead, knocked down), else 0.
    [[nodiscard]] virtual uint32_t PlayerInRagdoll() noexcept = 0;
    // The reference this game's player holds (PlayerCharacter::grabbedObject), else 0.
    [[nodiscard]] virtual uint32_t PlayerHolds() noexcept = 0;
    // That reference now; none if it is gone, deleted or has no 3D. aBodies false: its own fields only.
    [[nodiscard]] virtual std::optional<Reference> ReadReference(uint32_t aFormId, bool aBodies = true) noexcept = 0;
    // The engine's own body to node write for that body of a reference (FollowBody).
    virtual void WriteNode(uint32_t aFormId, uint8_t aIndex) noexcept = 0;
};
