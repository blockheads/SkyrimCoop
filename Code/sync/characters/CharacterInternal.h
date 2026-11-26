#pragma once

#include <Structs/AnimationVariables.h>
#include <Structs/Tints.h>
#include <characters/FaceGenComponent.h>
#include <core/World.h>

// Shared by CharacterService's files (CharacterService*.cpp).
// A player's copy's face tints, kept for its head (painted by the engine once it is built).
inline void KeepFaceTints(World& aWorld, entt::entity aEntity, const Tints& acTints) noexcept
{
    if (!acTints.Entries.empty())
        aWorld.emplace_or_replace<FaceGenComponent>(aEntity).FaceTints = acTints;
}

// A snapshot of a streamed owner carries none (graph-input-stream.md).
inline bool HasVariables(const AnimationVariables& acVariables) noexcept
{
    return !acVariables.Booleans.empty() || !acVariables.Integers.empty() || !acVariables.Floats.empty();
}

// TESNPC change flags the player's copies are made from: race, sex and face (NPC_ChangeForm 0x2000000, 0x1000000, 0x800).
constexpr uint32_t kPlayerLooksFlags = 0x3000800;
