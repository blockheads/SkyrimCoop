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

    // The head as the diagnostics show it: its face geometry, Facegen material and tint texture's name.
    struct HeadRead
    {
        bool Geometry = false;
        bool Material = false;
        std::string Tint; // "" for none
    };
    [[nodiscard]] static HeadRead Read(Actor* apActor) noexcept;
    static void Setup(World& aWorld, entt::entity aEntity, const Tints& acTints) noexcept;
};
