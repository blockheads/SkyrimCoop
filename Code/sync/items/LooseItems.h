#pragma once

#include <Messages/WorldState.h>
#include <Structs/Inventory.h>

#include <optional>

struct TESObjectREFR;

// Loose items in the world (docs/design/dropped-items.md), HostEngine's and GuestEngine's.

// The reference as an item: its base, count (ExtraCount, else 1) and extra data; none for an actor or no base.
[[nodiscard]] std::optional<Inventory::Entry> ReadLooseItem(TESObjectREFR& aObject) noexcept;
// Where it is: its cell, or outdoors its worldspace, position and angle; none outside any cell.
[[nodiscard]] std::optional<ObjectChange::Placement> ReadPlacement(const TESObjectREFR& acObject) noexcept;
// Makes acBase's reference at the placement (CreateReferenceAtLocation 13723, past our watch of it), with apItem's
// count and extra data; none when the base, the cell or the worldspace is not in this game.
[[nodiscard]] TESObjectREFR* MakeLooseItem(const GameId& acBase, const Inventory::Entry* apItem,
                                           const ObjectChange::Placement& acPlacement) noexcept;
// Whether the placement's cell is loaded here (the interior, or the exterior cell at its position attached).
[[nodiscard]] bool IsPlacementLoaded(const ObjectChange::Placement& acPlacement) noexcept;
