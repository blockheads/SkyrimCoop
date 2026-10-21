#pragma once

// The world's script events on this game: off on a guest, whose world reacts on the host's game
// (docs/design/world-state-sync.md step 5).
namespace WorldEvents
{
// Off: activation and trigger events (OnActivate, OnTrigger*) reach the player's own scripts only.
void SetDelivered(bool aOn) noexcept;
} // namespace WorldEvents
