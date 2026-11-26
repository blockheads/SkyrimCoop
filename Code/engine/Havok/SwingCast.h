#pragma once

#include <cstdint>
#include <vector>

struct Actor;

// A melee swing's hit test, as Actor::CalculateCurrentHitTargetForWeaponSwing (38628) casts it
// (docs/re/systems/physics.md, "A swing's hit"): a sphere swept from the actor's eye along its look.
namespace GameSwingCast
{
struct Hit
{
    uint32_t Reference{}; // the hit collision's reference, 0 for none (terrain, the cell's own)
    uint32_t Layer{};     // its collision layer
    float Point[3]{};     // the contact, game units
};

struct Cast
{
    bool Cast{};    // false without a physics world or the engine's collector
    float Eye[3]{}; // GetEyeVector's origin and direction
    float Direction[3]{};
    float Reach{};         // the swing's length (the weapon type's fObjectHit*Reach)
    float Radius{};        // fHitCasterSizeSmall
    std::vector<Hit> Hits; // nearest first: the swing takes the first
};

enum class Reach
{
    kWeapon,  // one-handed: fObjectHitWeaponReach
    kTwoHand, // fObjectHitTwoHandReach
    kHandToHand,
};

// The engine's own cast for that actor's swing now, without swinging.
[[nodiscard]] Cast CastFor(Actor& aActor, Reach aReach) noexcept;
// A swing collector's (GetAllCdPointCollector 25925) hits after a cast, nearest first.
[[nodiscard]] std::vector<Hit> HitsOf(const void* apCollector) noexcept;
// The collision group its phantom ignores now (25932 sets it to the swinger's), and that actor's group.
[[nodiscard]] uint32_t CollectorGroup(const void* apCollector) noexcept;
[[nodiscard]] uint32_t GroupOf(Actor& aActor) noexcept;
} // namespace GameSwingCast
