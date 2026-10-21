#pragma once

#include <Messages/WorldState.h>

#include <optional>

struct ModSystem;

// A container's count of the item now (not an actor's), as a kItems change in session ids; none for an actor, a
// reference the game created (0xFF ids) or no container. HostEngine and GuestEngine.
[[nodiscard]] std::optional<ObjectChange> ReadContainerItems(const ModSystem& acMods, uint32_t aObjectFormId, uint32_t aItemFormId) noexcept;
