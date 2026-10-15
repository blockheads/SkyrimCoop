#pragma once

// Players stacked on one spot (the party's arrival) pass through each other until each walks off it: the
// collision filter (bhkCollisionFilter 78553) lets no body of one touch a body of another.
// docs/design/host-authority-migration.md, "Arriving together".
namespace PlayerCollision
{
// The stacked players (none: everyone collides). Main thread, every frame: an actor's collision group comes
// from its current controller.
void SetPassThrough(const TiltedPhoques::Vector<uint32_t>& acActorFormIds) noexcept;
} // namespace PlayerCollision
