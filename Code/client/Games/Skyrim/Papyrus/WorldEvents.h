#pragma once

// The world's script events on this game: off on a guest, whose world reacts on the host's game
// (docs/design/world-state-sync.md, "Only the host's game decides").
namespace WorldEvents
{
// Off: only the events this game's own player or UI caused run its scripts (its body, SKSE's input, menus and
// mod events, its load), and what those scripts start or register; the rest run on the host's game.
void SetDelivered(bool aOn) noexcept;
} // namespace WorldEvents
