#pragma once

// Actors stacked on one spot (the party's arrival: its players, a follower) pass through each other until each
// walks off it: the collision filter (bhkCollisionFilter 78553) lets no body of one touch a body of another.
// docs/design/host-authority-migration.md, "Arriving together".
namespace PlayerCollision
{
// The stacked actors (none: everyone collides). Main thread, every frame: an actor's collision group comes
// from its current controller.
void SetPassThrough(const std::vector<uint32_t>& acActorFormIds) noexcept;
} // namespace PlayerCollision
