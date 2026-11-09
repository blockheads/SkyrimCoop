#pragma once

struct World;
struct Actor;
struct Tints;

// Paints players' copies' heads with their owners' face tints.
struct FaceGenSystem
{
    // The head shows tints painted here; a head built again (TESNPC::BuildHead 24731) does not.
    [[nodiscard]] static bool IsPainted(Actor* apActor) noexcept;
    // Paints the tints on the head's Facegen material; true if painted.
    static bool Paint(Actor* apActor, const Tints& acTints) noexcept;
    static void Setup(World& aWorld, entt::entity aEntity, const Tints& acTints) noexcept;
};
