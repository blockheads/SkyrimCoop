#pragma once

#include <Havok/bhkCollision.h>
#include <Structs/BodyFrame.h>

#include <optional>
#include <vector>

struct TESObjectREFR;

// A reference's physics bodies as every game keys them (docs/design/physics-sync.md, "Body keys"): BodyKey::Index is
// their place among its 3D's collision objects (CollisionObjectsOf). None for an actor (its ragdoll's: Havok/Ragdoll.h),
// a deleted reference or one without 3D.
namespace ReferenceBodies
{
// A body this near a pose is there: a settle in place, not a move of its own.
constexpr float kSameDistance = 0.5f;  // units
constexpr float kSameAngle = 0.005f;   // radians

struct Body
{
    bhkWorldObject* pBody{};
    bhkNiCollisionObject* pCollision{}; // whose node shows it
};

[[nodiscard]] std::vector<bhkNiCollisionObject*> Of(TESObjectREFR& aObject) noexcept;
// Its body at that index; none for the world's own (fixed: a room's, a static's), which never moves.
[[nodiscard]] Body At(TESObjectREFR* apObject, uint8_t aIndex) noexcept;
// The index of that Havok body among its bodies.
[[nodiscard]] std::optional<uint8_t> IndexOf(TESObjectREFR& aObject, const hkpRigidBody* apBody) noexcept;
// A read as a frame of the body at that index (no object yet).
[[nodiscard]] BodyFrame FrameOf(const BodyRead& acRead, uint8_t aIndex) noexcept;
// Its dynamic bodies' frames, read now.
[[nodiscard]] std::vector<BodyFrame> ReadFrames(TESObjectREFR& aObject) noexcept;
} // namespace ReferenceBodies
