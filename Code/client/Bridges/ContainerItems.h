#pragma once

#include <Messages/WorldState.h>

#include <optional>


// A container's count of the item now (not an actor's), as a kItems change in session ids; none for an actor, a
// reference the game created (0xFF ids) or no container. HostEngine and GuestEngine.
[[nodiscard]] std::optional<ObjectChange> ReadContainerItems(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept;

struct TESObjectREFR;

// Everything a world container holds, as a kContents change; none for an actor, a created reference or no container.
// Items of a mod the session lacks, or created ones, are left out.
[[nodiscard]] std::optional<ObjectChange> ReadContainerContents(const TESObjectREFR& acObject) noexcept;
// Makes the container hold exactly those (anything else it holds goes), raising nothing (SetContainerCount).
bool ApplyContainerContents(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept;
